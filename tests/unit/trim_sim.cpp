// Physics model + driver for the default bump (trim) mode of include/stpalpha/controller.h
// (DEC-010). The REAL Controller is driven pass by pass against a simulated roll; the 40 kHz
// ISR cut is modelled by checking the armed stop count every 0.2 ms (the real ISR is 25 us).
//
// Model assumptions (stated, not proven for the real machine):
//  * the motor accelerates at a constant rate and reaches full speed (3600 pulses/s) after a ramp
//    of R pulses (R 300..1500); after the relay opens the roll decelerates at a constant rate so
//    that it coasts C pulses from full speed (Coulomb friction): a bump that reached speed v
//    coasts C * (v / vmax)^2, plus the distance covered during the relay opening lag (5..30 ms);
//  * C (the TRUE full-speed coast) is drawn in 0..2*2291 pulses -- also beyond the 12 in design
//    assumption -- and every coast event is jittered by +-J (0..0.3) on the friction;
//  * the motor starts moving a random 20..200 ms after the relay closes (start lag);
//  * the encoder counts the wheel exactly (the guarantee is conditional on that, DEC-010);
//  * the main loop passes are 5..60 ms apart (DEC-009: <= ~68 ms).
// Exit 0 = every invariant held; the last line is a STATS line the pytest wrapper bounds.
#include <math.h>
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
static double urand(void) { // [0, 1)
    return (rnd() & 0xFFFFFF) / 16777216.0;
}

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL seed=%u wind=%u: ", seed, wind);                                          \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum class Fault { NONE, PAUSES, JAM, STOP, CREEP, SLOW };

struct Roll {
    double pos = 0;
    double v = 0;
    double zero = 0;
    bool relay_on = false;
    bool cut = false;
    double t = 0;
    double powered_at = 0;
    double powered_until = -1;
    double vmax = 3600;
    double ramp = 800;   // R pulses
    double coast = 0;    // C pulses at full speed
    double jitter = 0;   // J
    double dn = 1e12;    // current deceleration (pulses/s^2)
    bool jammed = false;
    bool creep = false; // slipping: the roll advances 1 pulse per 450 ms whatever the motor does
    uint32_t stop_armed = Controller::kNoStop;

    uint32_t count() const {
        double c = pos - zero;
        return c < 0 ? 0 : static_cast<uint32_t>(c);
    }
    void relay(bool on) {
        if (on == relay_on) {
            return;
        }
        relay_on = on;
        if (on) {
            powered_at = t + 0.020 + 0.180 * urand();
        } else {
            powered_until = t + 0.005 + 0.025 * urand();
            double base = coast > 0 ? vmax * vmax / (2.0 * coast) : 1e12;
            dn = base / (1.0 + jitter * (2.0 * urand() - 1.0));
        }
    }
    bool powered() const {
        return relay_on ? t >= powered_at : t < powered_until;
    }
    // Advance 0.2 ms, with the ISR check at the end of the step.
    void step() {
        const double dt = 0.0002;
        t += dt;
        if (jammed) {
            v = 0;
        } else if (creep) {
            pos += dt / 0.45;
            v = 0;
        } else if (powered()) {
            v += vmax * vmax / (2.0 * ramp) * dt;
            if (v > vmax) {
                v = vmax;
            }
        } else {
            v -= dn * dt;
            if (v < 0) {
                v = 0;
            }
        }
        pos += v * dt;
        if (relay_on && count() >= stop_armed) {
            relay(false); // the ISR cut
            cut = true;
        }
    }
};

struct WindResult {
    Outcome outcome;
    uint32_t final_count;
    uint32_t goal;
    uint32_t target;
    uint8_t bumps;
    double trim_s;
    bool alarm_beep;
    bool cancel_beep;
    bool start_beep;
};

int main(int argc, char** argv) {
    uint32_t seed = argc > 1 ? static_cast<uint32_t>(atoi(argv[1])) : 1;
    uint32_t winds = argc > 2 ? static_cast<uint32_t>(atoi(argv[2])) : 40;
    rng_state = seed * 2654435761u + 7;
    uint32_t wind = 0;
    uint32_t n_pass = 0, n_short = 0, n_stopped = 0, n_legacy = 0, n_complete = 0;
    double max_over = 0, sum_over = 0;
    double max_over_assumed = 0, sum_over_assumed = 0;
    uint32_t n_over = 0, n_over_assumed = 0;
    uint8_t max_bumps = 0;
    double max_trim_s = 0;

    for (wind = 0; wind < winds; ++wind) {
        Controller c;
        c.short_pulses = 8000 + rnd() % 40000;
        c.long_pulses = c.short_pulses * 3;
        c.coast = kDefaultCoastPulses;
        c.margin = rnd() % 4 == 0 ? 0 : (rnd() % 4 == 0 ? kMaxMarginPulses : rnd() % 600);
        c.bump = true;
        Roll r;
        r.ramp = 300 + rnd() % 1200;
        r.coast = rnd() % 8 == 0 ? 0 : urand() * 2.0 * kDefaultCoastPulses;
        r.jitter = urand() * 0.3;
        Fault fault = static_cast<Fault>(rnd() % 8 < 5 ? 0 : 1 + rnd() % 5);
        const bool assumed = r.coast <= kDefaultCoastPulses;
        if (fault == Fault::SLOW) {
            r.vmax = 30 + rnd() % 200; // a weak motor: the 30 s limit decides
            r.coast = r.coast * r.vmax / 3600.0;
        }
        const double jam_at = urand() * 4.0; // seconds after the trim started (the main run has no stall rule)
        const uint32_t stop_pass = 5 + rnd() % 3000;
        uint32_t now = 1000;
        bool long_wind = rnd() % 3 == 0;
        const uint32_t target = long_wind ? c.long_pulses : c.short_pulses;
        const uint32_t goal = target + c.margin;

        Inputs in{};
        in.pending = long_wind ? kBtn2 : kBtn1;
        in.now_ms = now;
        Outputs o = c.step(in);
        CHECK(o.relay && o.reset_count && c.state() == MachineState::WINDING, "start");
        r.zero = r.pos;
        r.cut = false;
        bool driving = false;
        WindResult res{};
        bool ended = false;
        uint32_t trim_start = 0;
        uint32_t passes = 0;
        uint32_t relay_on_since = 0;
        bool relay_was_on = false;
        bool paused_trim = false;
        double trim_t0 = 1e18;
        uint32_t last_pause = 0;
        for (;;) {
            // ---- one main loop pass ----
            uint32_t dtms = 5 + rnd() % 56;
            for (uint32_t k = 0; k < dtms * 5; ++k) {
                r.step();
            }
            now += dtms;
            ++passes;
            if (fault == Fault::JAM && !r.jammed && c.trimming() && r.t - trim_t0 > jam_at) {
                r.jammed = true;
            }
            if (fault == Fault::CREEP && !r.creep && c.trimming() && r.t - trim_t0 > jam_at) {
                r.creep = true;
            }
            in = Inputs{};
            in.now_ms = now;
            in.count = r.count();
            in.cut = r.cut;
            if (fault == Fault::PAUSES && passes > last_pause && rnd() % 25 == 0) {
                in.pending |= kBtn3;
                last_pause = passes;
            }
            if (fault == Fault::PAUSES && paused_trim && rnd() % 6 == 0) {
                in.pending |= kBtn4; // a manual nudge while paused
            }
            if (fault == Fault::STOP && passes == stop_pass) {
                in.pending |= (rnd() & 1) ? kBtn1 : kBtn2;
            }
            MachineState before = c.state();
            o = c.step(in);
            if (o.reset_count) {
                r.zero = r.pos;
                r.cut = false;
            }
            // apply(): arm the ISR stop and drive the relay in one critical section
            if (o.relay && !driving) {
                r.cut = false;
            }
            driving = o.relay;
            uint32_t stop_at = c.stop_at();
            r.stop_armed = stop_at;
            if (o.relay && r.count() < stop_at && !r.cut) {
                r.relay(true);
            } else {
                r.relay(false);
            }
            // ---- per-pass invariants ----
            if (c.trimming()) {
                CHECK(c.stop_at() == Controller::kNoStop || c.stop_at() <= c.goal() + 0,
                      "trim stop_at beyond the goal: %u", c.stop_at());
                CHECK(c.bumps() <= kMaxBumps, "too many bumps");
                if (c.state() == MachineState::TRIM && !o.relay) {
                    CHECK(!r.relay_on, "relay on while the trim holds it off");
                }
                if (c.state() == MachineState::PAUSED && !c.manual()) {
                    CHECK(!o.relay && c.stop_at() == Controller::kNoStop, "paused trim drives");
                }
                if (c.state() == MachineState::PAUSED && c.manual()) {
                    CHECK(c.stop_at() == c.goal(), "paused manual cap");
                }
                if (c.state() == MachineState::TRIM && o.relay) {
                    CHECK(c.stop_at() != Controller::kNoStop && c.stop_at() > in.count,
                          "bump without an armed stop");
                }
            }
            paused_trim = c.trimming() && c.state() == MachineState::PAUSED;
            if (before == MachineState::WINDING && c.state() == MachineState::TRIM) {
                trim_start = now;
                trim_t0 = r.t;
            }
            // a relay-on bump never lasts longer than the stall rule allows
            if (c.state() == MachineState::TRIM && o.relay) {
                if (!relay_was_on) {
                    relay_on_since = now;
                }
                CHECK(fault == Fault::SLOW || now - relay_on_since <= kBumpMaxOnMs + kBumpStallMs + 200,
                      "bump relay on for %u ms", now - relay_on_since);
            }
            relay_was_on = c.state() == MachineState::TRIM && o.relay;
            if (c.state() == MachineState::TRIM || (c.trimming() && !c.relay())) {
                // pauses are not charged: only check the running-time limit loosely below
            }
            if (o.stopped) {
                ended = true;
                res.outcome = c.outcome();
                res.bumps = c.bumps();
                res.alarm_beep = o.beep == Beep::ALARM;
                res.cancel_beep = o.beep == Beep::CANCEL;
                res.start_beep = o.beep == Beep::START;
                CHECK(!o.relay && !c.relay() && c.stop_at() == Controller::kNoStop,
                      "relay/ISR armed after the end");
                res.trim_s = trim_start ? (now - trim_start) / 1000.0 : 0;
                break;
            }
            CHECK(passes < 400000, "wind did not end");
        }
        CHECK(ended, "no end");
        // let the roll stop and the result settle
        uint32_t quiet_passes = 0;
        for (int k = 0; k < 400; ++k) {
            for (uint32_t j = 0; j < 50 * 5; ++j) {
                r.step();
            }
            now += 50;
            in = Inputs{};
            in.now_ms = now;
            in.count = r.count();
            in.cut = r.cut;
            o = c.step(in);
            CHECK(!o.relay && !c.relay(), "relay on in the result");
            (void)quiet_passes;
        }
        res.final_count = r.count();
        res.goal = goal;
        res.target = target;
        CHECK(r.creep ? c.result() != Result::NONE : c.result() == Result::DONE,
              "result not DONE after settling");

        // ---- the guarantee and the failure rules ----
        const double over = static_cast<double>(res.final_count) - static_cast<double>(goal);
        if (res.outcome == Outcome::PASS) {
            CHECK(res.final_count >= goal, "PASS but final %u < goal %u", res.final_count, goal);
            CHECK(res.final_count >= target, "PASS below the target");
            CHECK(res.start_beep && !res.alarm_beep, "PASS beeps wrong");
            CHECK(!c.alarm(), "alarm on a PASS");
            ++n_pass;
            ++n_complete;
            sum_over += over;
            ++n_over;
            if (over > max_over) {
                max_over = over;
            }
            if (assumed) {
                sum_over_assumed += over;
                ++n_over_assumed;
                if (over > max_over_assumed) {
                    max_over_assumed = over;
                }
            }
            if (res.trim_s > max_trim_s) {
                max_trim_s = res.trim_s;
            }
            if (res.bumps > max_bumps) {
                max_bumps = res.bumps;
            }
        } else if (res.outcome == Outcome::SHORT) {
            CHECK(res.alarm_beep && !res.start_beep, "SHORT must sound the alarm");
            CHECK(fault == Fault::JAM || fault == Fault::CREEP || fault == Fault::SLOW,
                  "SHORT without a fault (final=%u goal=%u bumps=%u trim=%.1fs coast=%.0f)",
                  res.final_count, goal, res.bumps, res.trim_s, r.coast);
            CHECK(c.alarm(), "SHORT must hold the alarm");
            ++n_short;
        } else if (res.outcome == Outcome::STOPPED) {
            CHECK(fault == Fault::STOP, "STOPPED without an operator stop");
            CHECK(res.cancel_beep, "stop beep");
            ++n_stopped;
        } else {
            CHECK(false, "no outcome after a trim wind");
        }
        if (fault != Fault::PAUSES) {
            CHECK(res.trim_s <= kTrimMaxMs / 1000.0 + 0.1, "trim took %.1f s", res.trim_s);
        }
        if (fault == Fault::NONE || fault == Fault::PAUSES) {
            CHECK(res.outcome == Outcome::PASS,
                  "completed-wind fault-free run did not PASS (coast=%.0f ramp=%.0f margin=%u "
                  "target=%u final=%u bumps=%u)",
                  r.coast, r.ramp, c.margin, target, res.final_count, res.bumps);
        }
        if (res.outcome != Outcome::PASS) {
            // a failure is HELD: it survives far beyond the 60 s auto-clear and clears on a button
            CHECK(c.result() != Result::NONE, "failure not held");
            for (int k = 0; k < 3; ++k) {
                now += 50000;
                in = Inputs{};
                in.now_ms = now;
                in.count = r.count();
                (void)c.step(in);
                CHECK(c.result() != Result::NONE, "failure auto-cleared");
            }
            in = Inputs{};
            in.now_ms = now + 10;
            in.pending = kBtn3;
            in.count = r.count();
            (void)c.step(in);
            CHECK(c.result() == Result::NONE && c.outcome() != Outcome::PASS &&
                      c.outcome() != Outcome::NONE,
                  "a button press must clear the held failure but keep the pass flag");
        }
    }

    // Legacy mode (bump = 0): the single cut at goal - coast, no guarantee, outcome NONE.
    for (wind = 0; wind < 20; ++wind) {
        Controller c;
        c.short_pulses = 20000;
        c.coast = rnd() % 1500;
        c.margin = rnd() % 300;
        c.bump = false;
        Inputs in{};
        in.pending = kBtn1;
        Outputs o = c.step(in);
        CHECK(o.relay && c.stop_at() == 20000 + c.margin - c.coast, "legacy stop_at");
        in = Inputs{};
        in.count = 20000 + c.margin - c.coast - 1;
        CHECK(c.step(in).relay, "legacy still winding");
        in.count = 20000 + c.margin - c.coast;
        o = c.step(in);
        CHECK(o.stopped && c.state() == MachineState::IDLE && c.outcome() == Outcome::NONE &&
                  o.beep == Beep::START && c.result() == Result::SETTLING && !c.alarm(),
              "legacy end");
        ++n_legacy;
    }
    // Scenario: every bump advances only 20 pulses before the (spurious) cut: the bump budget
    // runs out after kMaxBumps and the wind ends SHORT with the alarm, the last bumps being final.
    {
        wind = 9999;
        Controller c;
        c.short_pulses = 50000;
        c.margin = 191;
        Inputs in{};
        in.pending = kBtn1;
        in.now_ms = 1000;
        Outputs o = c.step(in);
        uint32_t count = c.goal() - c.coast - 1;
        uint32_t now = 1000;
        bool pending_cut = false;
        uint32_t finals = 0;
        for (int k = 0; k < 200000 && !o.stopped; ++k) {
            now += 10;
            in = Inputs{};
            in.now_ms = now;
            if (c.state() == MachineState::TRIM && c.relay() && c.stop_at() != Controller::kNoStop) {
                count += 20;
                pending_cut = true;
                if (c.stop_at() == c.goal()) {
                    ++finals;
                }
            } else if (c.state() == MachineState::WINDING) {
                count = c.goal() - c.coast;
            }
            in.count = count;
            in.cut = pending_cut && c.relay();
            if (!c.relay()) {
                pending_cut = false;
            }
            o = c.step(in);
        }
        CHECK(o.stopped && c.outcome() == Outcome::SHORT && o.beep == Beep::ALARM &&
                  c.bumps() == kMaxBumps,
              "bump budget: outcome=%d bumps=%u", static_cast<int>(c.outcome()), c.bumps());
        CHECK(finals >= kFinalReserve * 1u, "the last bumps must be final-pass bumps (%u)", finals);
    }
    // Scenario (M1): a manual nudge carries a PAUSED wind across eff_target. The trim must start
    // PAUSED: manual stays capped at the goal, no bump runs on its own, play (BTN3) resumes it.
    {
        wind = 9998;
        Controller c;
        c.short_pulses = 20000;
        c.margin = 0;
        c.coast = 300;
        Inputs in{};
        in.pending = kBtn1;
        in.now_ms = 1000;
        CHECK(c.step(in).relay && c.state() == MachineState::WINDING, "m1: start");
        in = Inputs{};
        in.count = 10000;
        in.now_ms = 1010;
        in.pending = kBtn3;
        CHECK(!c.step(in).relay && c.state() == MachineState::PAUSED, "m1: pause");
        in.pending = kBtn4;
        in.now_ms = 1020;
        CHECK(c.step(in).relay && c.manual(), "m1: manual on while paused");
        in.pending = 0;
        in.count = c.eff_target() + 5; // the nudge crosses the stop point
        in.now_ms = 1030;
        Outputs o = c.step(in);
        CHECK(!o.stopped && c.state() == MachineState::PAUSED && c.trimming(),
              "m1: crossing eff_target while paused must give a PAUSED trim (state=%d)",
              static_cast<int>(c.state()));
        CHECK(o.relay == c.manual() && c.stop_at() == c.goal(), "m1: paused manual capped at goal");
        in.pending = kBtn4;
        in.now_ms = 1040;
        CHECK(!c.step(in).relay && !c.manual(), "m1: manual off");
        in.pending = 0;
        for (uint32_t t = 1050; t < 4000; t += 10) {
            in.now_ms = t;
            o = c.step(in);
            CHECK(!o.relay && c.state() == MachineState::PAUSED && c.bumps() == 0,
                  "m1: no bump without play (t=%u)", t);
        }
        in.pending = kBtn3;
        in.now_ms = 4000;
        (void)c.step(in);
        CHECK(c.state() == MachineState::TRIM, "m1: play resumes the trim");
        in.pending = 0;
        bool bumped = false;
        for (uint32_t t = 4010; t < 5000 && !bumped; t += 10) {
            in.now_ms = t;
            bumped = c.step(in).relay;
        }
        CHECK(bumped && c.bumps() >= 1, "m1: the trim bumps after play");
    }
    printf("STATS winds=%u pass=%u short=%u stopped=%u legacy=%u max_over=%.0f mean_over=%.1f "
           "max_over_assumed=%.0f mean_over_assumed=%.1f max_bumps=%u max_trim_s=%.1f\n",
           winds, n_pass, n_short, n_stopped, n_legacy, max_over,
           n_over ? sum_over / n_over : 0.0, max_over_assumed,
           n_over_assumed ? sum_over_assumed / n_over_assumed : 0.0, max_bumps, max_trim_s);
    (void)n_complete;
    return 0;
}
