#pragma once
// frob:waive PARSE002 reason="C++-only header kept as .h to match the include convention"
// Host-link command interpreter (docs/hardware/serial-protocol.md). Pure: it
// turns one request line into one reply line and a set of side-effect
// requests, so tests/unit/test_hostlink.py runs the exact code the AVR runs.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stpalpha/controller.h>
#include <stpalpha/fmt.h>
#include <stpalpha/length.h>

namespace stpalpha {

    constexpr uint16_t kProtoVersion = 1;
    constexpr uint16_t kFwVersion = 6;

    // Nominal paper lengths in micrometres (1080 in = 30 yd, 3240 in = 90 yd): integer
    // inputs for Params::derive_targets (no float on the AVR; pi is kPiMicro in length.h).
    constexpr uint64_t kShortLengthUm = 1080ull * 25400ull;
    constexpr uint64_t kLongLengthUm = 3240ull * 25400ull;

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0304
    // Persisted parameters (the SET/SAVE/LOAD names of the protocol).
    struct Params {
        uint32_t wheel_um = 101638; // 4.0015 in
        uint32_t cpr = 2400;
        uint32_t short_pulses = kDefaultShortPulses;
        uint32_t long_pulses = kDefaultLongPulses;
        uint32_t coast = kDefaultCoastPulses;
        uint32_t margin = kDefaultMarginPulses; // goal = target + margin
        uint32_t bump = kDefaultBump ? 1 : 0;   // 1 = trim mode with the guarantee, 0 = legacy cut

        // Recompute the 30/90 yd targets from the wheel diameter and counts per
        // revolution (called when either is SET; a later SET short/long overrides).
        // Callers must have checked wheel_um > 0 and cpr > 0 (valid() does).
        void derive_targets() {
            uint64_t circ_um_micro = kPiMicro * wheel_um; // circumference in um * 1e6
            short_pulses = static_cast<uint32_t>(
                (kShortLengthUm * cpr * 1000000ull + circ_um_micro / 2) / circ_um_micro);
            long_pulses = static_cast<uint32_t>(
                (kLongLengthUm * cpr * 1000000ull + circ_um_micro / 2) / circ_um_micro);
        }

        // Sane-range check: a stored or entered set that fails is never used,
        // so the machine always has a working configuration.
        bool valid() const {
            uint32_t lo = short_pulses < long_pulses ? short_pulses : long_pulses;
            uint32_t hi = short_pulses < long_pulses ? long_pulses : short_pulses;
            return margin <= kMaxMarginPulses && bump <= 1 && hi + margin <= kMaxPulses &&
                   wheel_um >= 50000 && wheel_um <= 300000 && cpr >= 100 && cpr <= 100000 &&
                   short_pulses >= 1000 && short_pulses <= kMaxPulses && long_pulses >= 1000 &&
                   long_pulses <= kMaxPulses && coast < lo;
        }
    };

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0304
    // Live HAL values the controller does not own, sampled by the caller.
    struct Snapshot {
        int32_t count;
        uint32_t speed; // encoder pulses/s over the last 100 ms window (SpeedMeter)
        uint16_t errors;
        uint8_t btn;        // bit i = BTN(i+1) latched down
        uint8_t toggles[4]; // HAL debounced toggle counters
        uint8_t pending[4]; // toggles not yet consumed by the main loop
        int8_t enc_dir;
        uint32_t uptime_ms;
        uint16_t stack_free;  // never-used stack bytes (painted-stack high-water mark), DEC-009
        uint16_t pass_max_ms; // longest main-loop pass since boot / ZERO
        uint16_t isr_lat_max; // worst Timer1 entry latency in cycles since boot / ZERO
    };

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0304
    // Side effects the caller performs after handle().
    enum Request : uint8_t { REQ_NONE = 0, REQ_SAVE = 1, REQ_LOAD = 2, REQ_ZERO = 4 };

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0304
    class HostLink {
      public:
        Params params;
        uint8_t requests = REQ_NONE;
        bool stream = false;

        // Copy params into the controller (call after any SET/LOAD/DEFAULTS).
        void sync(Controller& c) const {
            c.short_pulses = params.short_pulses;
            c.long_pulses = params.long_pulses;
            c.coast = params.coast;
            c.margin = params.margin;
            c.bump = params.bump != 0;
        }

        // Interpret one line (no terminator). Writes one reply line into out.
        // Parsing is strict: whole-token decimal numbers, no trailing junk.
        void handle(Controller& c, const Snapshot& s, const char* line, char* out, size_t cap) {
            char cmd[12] = {0};
            char a[12] = {0};
            char b[12] = {0};
            char extra[2] = {0};
            int n = sscanf(line, "%11s %11s %11s %1s", cmd, a, b, extra);
            if (n <= 0 || n >= 4) {
                err(out, cap, "bad_command");
            } else if (!strcmp(cmd, "HELLO")) {
                STP_SNPRINTF(out, cap, STP_FMT("OK stpone proto=%u fw=%u"), kProtoVersion,
                             kFwVersion);
            } else if (!strcmp(cmd, "GET")) {
                STP_SNPRINTF(
                    out, cap,
                    STP_FMT("OK state=%s count=%ld speed=%lu errors=%u relay=%d btn=%s pass=%c"),
                    state_name(c), static_cast<long>(s.count), static_cast<unsigned long>(s.speed),
                    s.errors, c.relay() ? 1 : 0, bits(s.btn), pass_char(c));
            } else if (!strcmp(cmd, "PARAM")) {
                STP_SNPRINTF(
                    out, cap,
                    STP_FMT(
                        "OK wheel_um=%lu cpr=%lu short=%lu long=%lu coast=%lu margin=%lu bump=%lu"),
                    static_cast<unsigned long>(params.wheel_um),
                    static_cast<unsigned long>(params.cpr),
                    static_cast<unsigned long>(params.short_pulses),
                    static_cast<unsigned long>(params.long_pulses),
                    static_cast<unsigned long>(params.coast),
                    static_cast<unsigned long>(params.margin),
                    static_cast<unsigned long>(params.bump));
            } else if (!strcmp(cmd, "DUMP")) {
                dump(c, s, out, cap, "OK");
            } else if (!strcmp(cmd, "SET")) {
                set(c, a, n == 3 ? b : nullptr, out, cap);
            } else if (!strcmp(cmd, "SAVE")) {
                requests |= REQ_SAVE;
                ok(out, cap);
            } else if (!strcmp(cmd, "LOAD")) {
                // The reply is OK here; main replaces it with ERR no_data if the load fails.
                done(quiet(c), out, cap, REQ_LOAD);
            } else if (!strcmp(cmd, "DEFAULTS")) {
                if (quiet(c)) {
                    params = Params();
                    sync(c);
                    ok(out, cap);
                } else {
                    err(out, cap, "bad_state");
                }
            } else if (!strcmp(cmd, "DEBUG") && n == 2 && flag(a)) {
                if (a[0] == '1') {
                    done(c.enter_debug(), out, cap);
                } else {
                    stream = false;
                    done(c.leave_debug(), out, cap);
                }
            } else if (!strcmp(cmd, "RELAY") && n == 2 && flag(a)) {
                done(c.set_relay(a[0] == '1'), out, cap);
            } else if (!strcmp(cmd, "ZERO")) {
                done(quiet(c), out, cap, REQ_ZERO);
            } else if (!strcmp(cmd, "RUN") && n == 2) {
                uint32_t pulses = 0;
                if (!parse_u32(a, &pulses) || pulses == 0 || pulses > kMaxPulses ||
                    pulses <= c.coast) {
                    err(out, cap, "bad_arg");
                } else {
                    done(c.start_run(pulses), out, cap);
                }
            } else if (!strcmp(cmd, "ABORT")) {
                done(c.abort_run(), out, cap);
            } else if (!strcmp(cmd, "STREAM") && n == 2 && flag(a)) {
                if (c.debug()) {
                    stream = (a[0] == '1');
                    ok(out, cap);
                } else {
                    err(out, cap, "bad_state");
                }
            } else {
                err(out, cap, "bad_command");
            }
            if (strncmp(out, "ERR", 3) != 0) {
                c.host_activity(s.uptime_ms); // proof of a live host (debug watchdog)
            }
        }

        // The DUMP field list, prefixed by `head` ("OK" or "EVT state").
        void dump(const Controller& c, const Snapshot& s, char* out, size_t cap,
                  const char* head) const {
            STP_SNPRINTF(
                out, cap,
                STP_FMT(
                    "%s state=%s prev=%s count=%ld speed=%lu errors=%u relay=%d manual=%d "
                    "target=%lu "
                    "eff_target=%lu coast=%lu btn=%s toggles=%u,%u,%u,%u pending=%u,%u,%u,%u "
                    "enc_dir=%d uptime_ms=%lu run=%d stack_free=%u pass_max_ms=%u isr_lat_max=%u "
                    "margin=%lu bump=%lu bumps=%u trim=%d pass=%c"),
                head, state_name(c), "-", static_cast<long>(s.count),
                static_cast<unsigned long>(s.speed), s.errors, c.relay() ? 1 : 0,
                c.manual() ? 1 : 0, static_cast<unsigned long>(c.target()),
                static_cast<unsigned long>(c.eff_target()), static_cast<unsigned long>(c.coast),
                bits(s.btn), s.toggles[0], s.toggles[1], s.toggles[2], s.toggles[3], s.pending[0],
                s.pending[1], s.pending[2], s.pending[3], s.enc_dir,
                static_cast<unsigned long>(s.uptime_ms), c.running() ? 1 : 0, s.stack_free,
                s.pass_max_ms, s.isr_lat_max, static_cast<unsigned long>(params.margin),
                static_cast<unsigned long>(params.bump), static_cast<unsigned>(c.bumps()),
                c.trimming() ? 1 : 0, pass_char(c));
        }

      private:
        // Strict decimal parse of a whole token into a uint32 (no sign, no junk).
        static bool parse_u32(const char* t, uint32_t* v) {
            if (*t == '\0') {
                return false;
            }
            uint64_t acc = 0;
            for (; *t != '\0'; ++t) {
                if (*t < '0' || *t > '9') {
                    return false;
                }
                acc = acc * 10u + static_cast<uint64_t>(*t - '0');
                if (acc > 0xFFFFFFFFull) {
                    return false;
                }
            }
            *v = static_cast<uint32_t>(acc);
            return true;
        }
        static bool flag(const char* t) {
            return (t[0] == '0' || t[0] == '1') && t[1] == '\0';
        }
        // Nothing is running or being wound: the only time parameters may change.
        static bool quiet(const Controller& c) {
            return c.state() == MachineState::IDLE && !c.running();
        }

        void set(Controller& c, const char* name, const char* value, char* out, size_t cap) {
            uint32_t u = 0;
            if (!quiet(c)) {
                err(out, cap, "bad_state");
            } else if (value == nullptr || !parse_u32(value, &u)) {
                err(out, cap, "bad_arg");
            } else {
                // Apply to a copy; keep it only if the whole set stays valid.
                Params next = params;
                if (!strcmp(name, "wheel_um")) {
                    next.wheel_um = u;
                    if (next.valid()) { // derive only from a sane wheel/cpr (no divide by 0)
                        next.derive_targets();
                    }
                } else if (!strcmp(name, "cpr")) {
                    next.cpr = u;
                    if (next.valid()) {
                        next.derive_targets();
                    }
                } else if (!strcmp(name, "short")) {
                    next.short_pulses = u;
                } else if (!strcmp(name, "long")) {
                    next.long_pulses = u;
                } else if (!strcmp(name, "coast")) {
                    next.coast = u;
                } else if (!strcmp(name, "margin")) {
                    next.margin = u;
                } else if (!strcmp(name, "bump")) {
                    next.bump = u;
                } else {
                    err(out, cap, "bad_arg");
                    return;
                }
                if (!next.valid()) {
                    err(out, cap, "bad_arg");
                    return;
                }
                params = next;
                sync(c);
                ok(out, cap);
            }
        }
        static const char* state_name(const Controller& c) {
            if (c.debug()) {
                return "DEBUG";
            }
            return c.state() == MachineState::IDLE      ? "IDLE"
                   : c.state() == MachineState::WINDING ? "WINDING"
                   : c.state() == MachineState::TRIM    ? "TRIM"
                                                        : "PAUSED";
        }
        // The pass flag of the last panel wind: '1' PASS, '0' SHORT/STOPPED, '-' running/never/legacy.
        static char pass_char(const Controller& c) {
            return c.outcome() == Outcome::NONE ? '-' : c.outcome() == Outcome::PASS ? '1' : '0';
        }
        static const char* bits(uint8_t b) {
            static char buf[5];
            for (int i = 0; i < 4; ++i) {
                buf[i] = (b & (1 << (3 - i))) ? '1' : '0'; // BTN4..BTN1
            }
            buf[4] = '\0';
            return buf;
        }
        static void ok(char* out, size_t cap) {
            STP_SNPRINTF(out, cap, STP_FMT("OK"));
        }
        static void err(char* out, size_t cap, const char* code) {
            STP_SNPRINTF(out, cap, STP_FMT("ERR %s"), code);
        }
        void done(bool good, char* out, size_t cap, uint8_t request = REQ_NONE) {
            if (good) {
                requests |= request;
                ok(out, cap);
            } else {
                err(out, cap, "bad_state");
            }
        }
    };

} // namespace stpalpha
