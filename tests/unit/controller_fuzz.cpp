// Host fuzz driver for include/stpalpha/controller.h. Exit 0 = every invariant held.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <stpalpha/controller.h>

using namespace stpalpha;

static uint32_t rng_state;
static uint32_t rnd(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL seed=%u pass=%u: ", seed, pass);                                          \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(int argc, char** argv) {
    uint32_t seed = argc > 1 ? static_cast<uint32_t>(atoi(argv[1])) : 1;
    rng_state = seed;
    Controller c;
    c.coast = rnd() % 3 == 0 ? rnd() % 3000 : 0;
    c.margin = rnd() % 3 == 0 ? rnd() % 400 : 0;
    c.bump = false; // legacy single cut: the trim has its own driver (trim_sim.cpp)
    uint32_t queued[4] = {0, 0, 0, 0}; // toggles pushed but not yet taken
    uint32_t taken[4] = {0, 0, 0, 0};
    uint32_t pushed[4] = {0, 0, 0, 0};
    bool manual = false; // model of the software manual latch
    uint32_t count = 0;
    uint32_t pass = 0;

    for (pass = 0; pass < 20000; ++pass) {
        for (int b = 0; b < 4; ++b) {
            if (rnd() % 8 == 0) {
                ++queued[b];
                ++pushed[b];
            }
        }
        if (rnd() % 400 == 0) {
            queued[2] += 2; // fast double toggle inside one slow pass
            pushed[2] += 2; // two edges, two clicks: neither merged nor dropped
        }
        Inputs in{};
        for (int b = 0; b < 4; ++b) {
            if (queued[b] > 0) {
                in.pending |= static_cast<uint8_t>(1 << b);
                --queued[b];
                ++taken[b];
            }
        }
        count += rnd() % 700;
        in.count = count;
        in.now_ms = pass * 10u;
        MachineState before = c.state();
        bool b4 = (in.pending & kBtn4) != 0;
        uint32_t target = c.target();
        Outputs o = c.step(in);

        if (o.reset_count) {
            count = 0;
        }
        if (o.stopped) {
            CHECK(c.state() == MachineState::IDLE, "stop must land in IDLE");
            CHECK(!o.relay && !c.relay(), "relay on after stop");
            CHECK(!o.reset_count, "stop must keep the count for the result screen");
            CHECK(c.result() == Result::SETTLING, "stop must open the result screen");
            CHECK(o.beep == Beep::START, "stop must beep");
            manual = false;
        }
        if (before == MachineState::IDLE && c.state() != MachineState::IDLE) {
            manual = false;
            CHECK(in.pending & (kBtn1 | kBtn2), "wind started without a BTN1/BTN2 click");
            CHECK(o.reset_count && o.relay, "start must reset the count and run the relay");
        }
        if (before == MachineState::IDLE && c.state() == MachineState::IDLE && !c.debug()) {
            CHECK(!(in.pending & (kBtn1 | kBtn2)), "click must start a wind");
            if (b4) {
                manual = !manual;
            }
            CHECK(c.manual() == manual, "idle manual latch model");
            CHECK(o.relay == manual, "manual relay rule");
        }
        if (c.state() == MachineState::PAUSED) {
            if (before == MachineState::PAUSED && b4) {
                manual = !manual;
            } else if (before == MachineState::WINDING) {
                manual = false;
            }
            CHECK(c.manual() == manual, "paused manual latch model");
            CHECK(o.relay == manual, "paused relay is manual only");
        }
        if (c.state() == MachineState::WINDING) {
            manual = false;
            CHECK(!c.manual(), "manual must be off while winding");
            CHECK(o.relay, "relay off while winding");
        }
        if (c.result() != Result::NONE) {
            CHECK(c.state() == MachineState::IDLE && !c.debug(), "result outside plain IDLE");
            CHECK(!o.relay && !c.relay() && !c.manual(), "result screen with motor or manual on");
        }
        if (before != MachineState::IDLE && !o.stopped) {
            CHECK(in.count < c.eff_target(), "wind passed its stop point (count=%u eff=%u)", in.count,
                  c.eff_target());
            CHECK(c.target() == target, "target changed mid wind");
        }
        for (int b = 0; b < 4; ++b) {
            CHECK(pushed[b] == taken[b] + queued[b], "toggle lost or doubled on button %d", b);
        }
    }

    // Scenario: wind stops on target with the latching start button still down; popping it
    // back up is a click and starts the next wind (DEC-011: one push, one wind).
    Controller w;
    w.bump = false;
    w.margin = 0;
    w.coast = 0;
    Inputs wi{};
    wi.pending = kBtn1;
    CHECK(w.step(wi).relay && w.state() == MachineState::WINDING, "scenario: start");
    wi = Inputs{};
    wi.count = w.eff_target();
    CHECK(w.step(wi).stopped && w.state() == MachineState::IDLE, "scenario: stop on target");
    wi = Inputs{};
    wi.pending = kBtn1; // button popped up
    Outputs wo = w.step(wi);
    CHECK(w.state() == MachineState::WINDING && wo.relay && wo.reset_count,
          "scenario: release click must start the next wind");
    CHECK(w.result() == Result::NONE, "scenario: a new wind clears the result");
    CHECK(w.stop_at() == w.eff_target(), "a wind stops at eff_target");

    // Scenario: two clicks inside one pass interval are two toggles (net off);
    // BTN4 physically down at power-up is just a click away from manual.
    Controller m;
    Inputs mi{};
    mi.pending = kBtn4;
    CHECK(m.step(mi).relay && m.manual(), "scenario: click -> manual on");
    CHECK(!m.step(mi).relay && !m.manual(), "scenario: second click -> manual off");

    // Debug mode: buttons ignored, host owns the relay, RUN obeys coast.
    Controller d;
    d.coast = 100;
    CHECK(d.enter_debug(), "enter debug");
    Inputs in{};
    in.pending = kBtn1 | kBtn2 | kBtn3 | kBtn4;
    Outputs o = d.step(in);
    CHECK(d.state() == MachineState::IDLE && !o.relay && !o.stopped, "buttons must be ignored");
    CHECK(d.set_relay(true) && d.step(Inputs{}).relay, "host relay");
    CHECK(d.set_relay(false), "host relay off");
    CHECK(d.start_run(1000), "run");
    in = Inputs{};
    in.count = 899;
    CHECK(d.step(in).relay && d.running(), "must run below target-coast");
    in.count = 900;
    o = d.step(in);
    CHECK(o.run_stopped && !o.stopped && o.at_stop == 900 && !o.relay && !d.running(),
          "stop at target-coast");
    CHECK(!d.start_run(0) && !d.start_run(100) && !d.start_run(kMaxPulses + 1),
          "zero, <= coast and over-max runs rejected");
    CHECK(d.start_run(1000) && d.abort_run(), "abort");
    CHECK(d.step(Inputs{}).run_stopped, "abort stops");
    CHECK(d.leave_debug() && !d.debug() && !d.relay() && !d.manual(), "leave debug pristine");
    // Scenario: BTN1 while the host holds the motor stops it and leaves debug.
    CHECK(d.enter_debug() && d.set_relay(true), "re-enter debug");
    in = Inputs{};
    in.pending = kBtn1;
    o = d.step(in);
    CHECK(!d.debug() && !o.relay && o.debug_exit == DebugExit::PANEL && o.reset_count, "panel e-stop");
    // Scenario: a silent host is timed out, and the motor stops.
    d.host_activity(100);
    CHECK(d.enter_debug() && d.set_relay(true), "debug again");
    in = Inputs{};
    in.now_ms = 100 + kHostTimeoutMs;
    CHECK(d.step(in).relay && d.debug(), "alive at the limit");
    in.now_ms = 101 + kHostTimeoutMs;
    o = d.step(in);
    CHECK(!d.debug() && !o.relay && !d.relay() && o.debug_exit == DebugExit::TIMEOUT, "timeout");
    printf("OK seed=%u\n", seed);
    return 0;
}
