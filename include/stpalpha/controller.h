#pragma once
// frob:waive PARSE002 reason="C++-only header kept as .h to match the include convention"
// Pure panel/host state machine: no hardware access, so the host fuzz test
// (tests/unit/test_controller.py) drives the exact code that runs on the AVR.

#include <stdint.h>

#include <stpalpha/machine.h>

namespace stpalpha {

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0303
    enum class Beep : uint8_t { NONE, START, CANCEL, ALARM };

    // Why debug mode ended on its own (DEC-005): the host went silent, or the
    // operator pushed BTN1/BTN2 while the host held the motor.
    enum class DebugExit : uint8_t { NONE, TIMEOUT, PANEL };

    // Compiled-in defaults (30 yd / 90 yd on the 4.0015 in wheel, 2400 counts/rev).
    constexpr uint32_t kDefaultShortPulses = 206187;
    constexpr uint32_t kDefaultLongPulses = 618562;
    // Largest pulse count any target or test run may have (Params::valid()).
    constexpr uint32_t kMaxPulses = 4000000;
    // The result screen (DEC-006): the count must hold still this long before it is final,
    // and the screen clears itself this long after the wind stopped.
    constexpr uint32_t kResultSettleMs = 500;
    constexpr uint32_t kResultHoldMs = 60000;
    // Follow-through assumed when nothing is calibrated (DEC-010): the wheel is assumed to stop within
    // 12 in of paper after the relay drops at full speed. 12 in = 304800 um; one revolution of the
    // 101638 um wheel is pi * 101638 = 319297.6 um = 2400 counts, so 304800 / 319297.6 * 2400 =
    // 2291.0 -> 2291 pulses.
    constexpr uint32_t kDefaultCoastPulses = 2291;
    // Safety allowance added to the target (goal = target + margin): 1.0 in = 25400 um =
    // 25400 / 319297.6 * 2400 = 190.9 -> 191 pulses. The encoder measures the wheel, not the paper,
    // so wheel wear or a wrong diameter makes the machine over-count; the margin absorbs that.
    constexpr uint32_t kDefaultMarginPulses = 191;
    // Largest margin a parameter set may carry.
    constexpr uint32_t kMaxMarginPulses = 2000;
    // Trim (bump) mode on by default: 0 restores the legacy single cut with no guarantee.
    constexpr bool kDefaultBump = true;
    // ---- trim algorithm constants (DEC-010) ----
    // Quiet time of |count| before a settled value is trusted (same as the result screen).
    constexpr uint32_t kTrimSettleMs = kResultSettleMs;
    // Most bumps one wind may use, and how many of the last ones are forced final-pass bumps.
    constexpr uint8_t kMaxBumps = 16;
    constexpr uint8_t kFinalReserve = 3;
    // Smallest conservative bump worth making (pulses); below it the final pass takes over.
    constexpr uint32_t kMinBump = 8;
    // Remaining gap (pulses) at or under which the final pass bump (relay on until the goal) is used:
    // 2.0 in = 2 * 190.9 = 381.8 -> 382 pulses.
    constexpr uint32_t kFinalWindow = 382;
    // The whole trim must end within this long (pauses excluded).
    constexpr uint32_t kTrimMaxMs = 30000;
    // A bump with the relay on longer than kBumpMaxOnMs and no count change for kBumpStallMs is a stall.
    constexpr uint32_t kBumpMaxOnMs = 1500;
    constexpr uint32_t kBumpStallMs = 300;
    // Two stalls end the trim.
    constexpr uint8_t kMaxStalls = 2;
    // Debug mode drops the relay and returns to IDLE after this long without a valid host line.
    constexpr uint32_t kHostTimeoutMs = 5000;

    // Bits of Inputs::pending and of the button masks (BTN1..BTN4).
    constexpr uint8_t kBtn1 = 1 << 0; // wind short; stop while running
    constexpr uint8_t kBtn2 = 1 << 1; // wind long; stop while running
    constexpr uint8_t kBtn3 = 1 << 2; // pause / play
    constexpr uint8_t kBtn4 = 1 << 3; // manual: every click toggles manual mode in software

    // What the IDLE display holds after a panel wind (DEC-006): nothing, the live count while the
    // motor still coasts, or the final length. Only the display depends on it; the machine is a
    // pristine IDLE (relay off, manual off) in every case.
    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0303
    enum class Result : uint8_t { NONE, SETTLING, DONE };

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0303
    // One loop pass of sampled inputs.
    struct Inputs {
        uint8_t
            pending;    // buttons clicked this pass: one debounced edge, either direction (DEC-011)
        uint32_t count; // |encoder count|
        uint32_t now_ms; // uptime: debug host-silence timeout and the trim timers
        bool cut;        // the ISR cut the relay in the current drive period (enc_cut_pending)
    };

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0303
    // What the caller must do after a pass.
    struct Outputs {
        bool relay;
        uint8_t leds;
        Beep beep;
        bool reset_count;     // zero count and enc_errors before the next sample
        bool changed;         // state/screen changed: force a redraw
        bool stopped;         // a panel wind just ended this pass
        bool run_stopped;     // a debug RUN just ended this pass (stop, abort, or debug exit)
        DebugExit debug_exit; // debug mode ended by itself this pass
        uint32_t
            at_stop; // main's count sample when the run/wind ended (valid if stopped/run_stopped)
    };

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0303
    class Controller {
      public:
        Controller() = default;

        // Parameters: pulses for BTN1/BTN2 and the motor follow-through.
        uint32_t short_pulses = kDefaultShortPulses;
        uint32_t long_pulses = kDefaultLongPulses;
        uint32_t coast = kDefaultCoastPulses;
        uint32_t margin = kDefaultMarginPulses; // goal = target + margin
        bool bump = kDefaultBump;               // trim mode (guarantee) on

        MachineState state() const {
            return state_;
        }
        bool debug() const {
            return debug_;
        }
        // Software manual mode: off at power-up whatever the switch position.
        bool manual() const {
            return manual_;
        }
        uint32_t target() const {
            return target_;
        }
        // Count a panel wind must reach to PASS: the target plus the safety margin.
        uint32_t goal() const {
            return target_ + margin;
        }
        // Count at which the main run's relay drops: the goal (a debug RUN: the target) minus the
        // follow-through.
        uint32_t eff_target() const {
            uint32_t base = (run_ || debug_) ? target_ : goal();
            return base > coast ? base - coast : 0;
        }
        // How the last panel wind ended (NONE while running, never run, or legacy mode).
        Outcome outcome() const {
            return outcome_;
        }
        // Bumps made in the current or last wind, and whether the trim phase is active (paused too).
        uint8_t bumps() const {
            return bumps_;
        }
        bool trimming() const {
            return trim_;
        }
        // The failure alarm should keep sounding: a held SHORT result is on the display.
        bool alarm() const {
            return hold_ && outcome_ == Outcome::SHORT && result_ != Result::NONE;
        }

        // What the IDLE screen shows after a panel wind.
        Result result() const {
            return result_;
        }
        // Drop the result screen; the next step() zeroes the held count and redraws.
        void clear_result() {
            hold_ = false;
            if (result_ != Result::NONE) {
                result_ = Result::NONE;
                zero_pending_ = true;
            }
        }

        // Advance one pass. Panel buttons are ignored in debug mode.
        Outputs step(const Inputs& in) {
            Outputs o = idle_out(false);
            if (debug_) {
                o = run_debug(in, o);
            } else if (state_ == MachineState::IDLE) {
                o = step_idle(in, o);
            } else {
                o = step_wind(in, o);
            }
            if (zero_pending_) {
                zero_pending_ = false;
                o.reset_count = true;
                o.changed = true;
            }
            if (left_pending_) {
                // The host left debug since the last pass: report an interrupted RUN
                // and hand back a pristine IDLE (count and miss counter cleared).
                left_pending_ = false;
                o.run_stopped = o.run_stopped || left_run_;
                if (left_run_) {
                    o.at_stop = in.count;
                }
                left_run_ = false;
                o.reset_count = true;
                o.changed = true;
            }
            return o;
        }

        // Record a valid host line (any command answered without ERR) at `now_ms`.
        void host_activity(uint32_t now_ms) {
            last_host_ = now_ms;
        }

        // ---- host (debug) interface; each returns false if the state forbids it ----

        bool enter_debug() {
            if (state_ != MachineState::IDLE) {
                return false;
            }
            debug_ = true;
            clear_result(); // debug starts from a zero count with no result on the display
            manual_ = false;
            run_ = false;
            abort_ = false;
            relay_ = false;
            return true;
        }
        // Leaving always lands in a pristine IDLE (relay off, manual off); the next
        // step() also clears the count and miss counter and reports an interrupted RUN.
        bool leave_debug() {
            if (!debug_) {
                return false;
            }
            left_pending_ = true;
            left_run_ = left_run_ || run_;
            drop_debug();
            return true;
        }
        bool set_relay(bool on) {
            if (!debug_ || run_) {
                return false;
            }
            relay_ = on;
            return true;
        }
        bool start_run(uint32_t pulses) {
            if (!debug_ || run_ || pulses == 0 || pulses > kMaxPulses || pulses <= coast) {
                return false;
            }
            target_ = pulses;
            run_ = true;
            relay_ = true;
            return true;
        }
        bool abort_run() {
            if (!run_) {
                return false;
            }
            abort_ = true;
            return true;
        }
        bool running() const {
            return run_;
        }
        // Count at which hardware must drop the relay now; kNoStop when none applies.
        static constexpr uint32_t kNoStop = 0xFFFFFFFFu;
        uint32_t stop_at() const {
            if (run_) {
                return eff_target();
            }
            if (debug_ || state_ == MachineState::IDLE) {
                return kNoStop;
            }
            if (!trim_) {
                return eff_target();
            }
            if (state_ == MachineState::TRIM) {
                return phase_ == TrimPhase::BUMP ? bump_stop_ : kNoStop;
            }
            return manual_ ? goal()
                           : kNoStop; // a paused trim: manual may nudge, never past the goal
        }
        bool relay() const {
            return relay_;
        }

        // 4-LED bargraph: each LED lights at 25% increments of the stop point.
        static uint8_t bargraph(uint32_t count, uint32_t stop_at) {
            static constexpr uint8_t kBar[5] = {0b0000, 0b0001, 0b0011, 0b0111, 0b1111};
            if (stop_at == 0) {
                return kBar[4];
            }
            uint32_t capped = (count < stop_at) ? count : stop_at;
            return kBar[static_cast<uint8_t>(capped * 4u / stop_at)];
        }

      private:
        Outputs idle_out(bool changed) const {
            Outputs o{};
            o.relay = relay_;
            o.leds = 0b0101;
            o.beep = Beep::NONE;
            o.changed = changed;
            return o;
        }

        // Common tail of every way out of debug mode: relay off, manual off, IDLE.
        void drop_debug() {
            debug_ = false;
            run_ = false;
            abort_ = false;
            relay_ = false;
            manual_ = false;
        }

        // Debug: the host owns the relay; RUN stops by the same rule as a wind. The
        // panel cannot start anything, but BTN1/BTN2 stop the motor and a silent host
        // (crash, unplug, sleep) is timed out, so no failure leaves the motor running.
        Outputs run_debug(const Inputs& in, Outputs o) {
            bool silent = static_cast<uint32_t>(in.now_ms - last_host_) > kHostTimeoutMs;
            bool panel = (in.pending & (kBtn1 | kBtn2)) != 0 && (relay_ || run_);
            if (silent || panel) {
                o.run_stopped = run_;
                o.at_stop = in.count;
                o.debug_exit = silent ? DebugExit::TIMEOUT : DebugExit::PANEL;
                o.reset_count = true;
                o.changed = true;
                drop_debug();
                o.relay = false;
                o.leds = 0b0101;
                return o;
            }
            if (run_ && (abort_ || in.count >= eff_target())) {
                o.run_stopped = true;
                o.at_stop = in.count;
                o.changed = true;
                run_ = false;
                relay_ = false;
                abort_ = false;
            }
            o.relay = relay_;
            o.leds = 0b1010;
            return o;
        }

        // Result bookkeeping for one IDLE pass: SETTLING until the count has held still for
        // kResultSettleMs (any movement restarts it), then DONE; gone after kResultHoldMs.
        void track_result(const Inputs& in, Outputs& o) {
            if (result_ == Result::NONE) {
                return;
            }
            if (!hold_ && static_cast<uint32_t>(in.now_ms - result_at_) >= kResultHoldMs) {
                clear_result();
                return;
            }
            Result next = result_;
            if (in.count != result_count_) {
                result_count_ = in.count;
                result_since_ = in.now_ms;
                next = Result::SETTLING;
            } else if (static_cast<uint32_t>(in.now_ms - result_since_) >= kResultSettleMs) {
                next = Result::DONE;
            }
            if (next != result_) {
                result_ = next;
                o.changed = true;
            }
        }

        Outputs step_idle(const Inputs& in, Outputs o) {
            if (hold_ && in.pending != 0) {
                clear_result(); // a failure stays on the display until a button is pressed
            }
            track_result(in, o);
            // Every click starts a wind, whichever way it left the latching button (DEC-011,
            // superseding DEC-003's down-only rule): one push, one wind.
            uint8_t start = in.pending;
            if (start & kBtn1) {
                return begin(short_pulses, o);
            }
            if (start & kBtn2) {
                return begin(long_pulses, o);
            }
            if (in.pending & kBtn4) {
                manual_ = !manual_;
                o.changed = true;
                if (manual_) {
                    clear_result(); // the motor is about to move the count again
                }
            }
            relay_ = manual_;
            o.relay = relay_;
            o.leds = relay_ ? 0b1000 : 0b0101;
            return o;
        }

        Outputs begin(uint32_t pulses, Outputs o) {
            target_ = pulses;
            result_ = Result::NONE; // the reset below zeroes the count itself
            hold_ = false;
            outcome_ = Outcome::NONE;
            trim_ = false;
            bumps_ = 0;
            state_ = MachineState::WINDING;
            manual_ = false;
            relay_ = true;
            o.relay = true;
            o.leds = 0b0000;
            o.beep = Beep::CANCEL;
            o.reset_count = true;
            o.changed = true;
            return o;
        }

        // Leave the wind for IDLE with the relay off. The count is kept (not reset) so the display can
        // show where the paper really ended once the motor has coasted to a halt (DEC-006).
        Outputs end_wind(const Inputs& in, Outputs o, Beep beep, Outcome outcome, bool settled) {
            o.stopped = true;
            o.at_stop = in.count;
            state_ = MachineState::IDLE;
            trim_ = false;
            manual_ = false;
            relay_ = false;
            o.relay = false;
            o.leds = 0b0000;
            o.beep = beep;
            outcome_ = outcome;
            hold_ = outcome == Outcome::SHORT || outcome == Outcome::STOPPED;
            result_ = settled ? Result::DONE : Result::SETTLING;
            result_count_ = in.count;
            result_since_ = in.now_ms;
            result_at_ = in.now_ms;
            o.changed = true;
            return o;
        }

        // The main run is over: trim (bump mode) or finish (legacy single cut).
        Outputs enter_trim(const Inputs& in, Outputs o) {
            trim_ = true;
            state_ = MachineState::TRIM;
            manual_ = false;
            relay_ = false;
            o.relay = false;
            init_trim(in, o);
            return o;
        }

        // A manual nudge carried a PAUSED wind across eff_target: become a PAUSED trim, so the
        // bumps wait for play (BTN3) and manual stays capped at the goal by stop_at(). This
        // pass's clicks are then handled by step_trim, so none is lost.
        Outputs enter_paused_trim(const Inputs& in, Outputs o) {
            trim_ = true;
            paused_at_ = in.now_ms;
            init_trim(in, o);
            return step_trim(in, o);
        }

        // Shared trim bookkeeping for enter_trim and enter_paused_trim.
        void init_trim(const Inputs& in, Outputs& o) {
            phase_ = TrimPhase::SETTLE;
            settle_count_ = in.count;
            settle_since_ = in.now_ms;
            trim_start_ = in.now_ms;
            bumps_ = 0;
            stalls_ = 0;
            over_est_ = 0;
            learned_ = false;
            learn_ = false;
            o.leds = bargraph(in.count, target_);
            o.changed = true;
        }

        Outputs step_wind(const Inputs& in, Outputs o) {
            if (trim_) {
                return step_trim(in, o);
            }
            if (in.pending & (kBtn1 | kBtn2)) {
                return bump ? end_wind(in, o, Beep::CANCEL, Outcome::STOPPED, false)
                            : end_wind(in, o, Beep::START, Outcome::NONE, false);
            }
            if (in.count >= eff_target()) {
                if (!bump) {
                    return end_wind(in, o, Beep::START, Outcome::NONE, false);
                }
                return state_ == MachineState::PAUSED ? enter_paused_trim(in, o)
                                                      : enter_trim(in, o);
            }
            bool was_winding = (state_ == MachineState::WINDING);
            if (in.pending & kBtn3) {
                state_ = (state_ == MachineState::WINDING) ? MachineState::PAUSED
                                                           : MachineState::WINDING;
                o.changed = true;
            }
            // Manual works only while PAUSED (it nudges the paper); clicks while
            // winding are ignored and resuming always leaves manual mode. The
            // stop rule above still applies, so manual can never overrun.
            if (was_winding || state_ == MachineState::WINDING) {
                manual_ = false; // a click in the pass that pauses is ignored
            } else if (in.pending & kBtn4) {
                manual_ = !manual_;
                o.changed = true;
            }
            relay_ = (state_ == MachineState::WINDING) || manual_;
            o.relay = relay_;
            o.leds = bargraph(in.count, eff_target());
            return o;
        }

        // Start the next bump from a settled count below the goal: a conservative one that is
        // expected to land at the goal, or the final pass (relay on until the ISR sees the goal).
        void start_bump(const Inputs& in, Outputs& o) {
            const uint32_t rem = goal() - in.count;
            bool final_pass = rem <= kFinalWindow || bumps_ + kFinalReserve >= kMaxBumps;
            uint32_t b = 0;
            if (!final_pass) {
                b = learned_ ? (rem > over_est_ ? rem - over_est_ : 0) : rem / 2;
                final_pass = b < kMinBump;
            }
            if (final_pass) {
                b = rem;
            }
            bump_from_ = in.count;
            bump_cmd_ = b;
            bump_stop_ = in.count + b;
            ++bumps_;
            phase_ = TrimPhase::BUMP;
            bump_start_ = in.now_ms;
            last_adv_ = in.now_ms;
            last_count_ = in.count;
            relay_ = true;
            o.relay = true;
        }

        void begin_settle(const Inputs& in, bool learn) {
            phase_ = TrimPhase::SETTLE;
            settle_count_ = in.count;
            settle_since_ = in.now_ms;
            learn_ = learn;
            relay_ = false;
        }

        // The trim gives up (limit, stalls, operator): the wind is SHORT or STOPPED, loudly.
        Outputs fail_trim(const Inputs& in, Outputs o, bool operator_stop, bool settled) {
            return operator_stop ? end_wind(in, o, Beep::CANCEL, Outcome::STOPPED, settled)
                                 : end_wind(in, o, Beep::ALARM, Outcome::SHORT, settled);
        }

        // The relay is off and the count quiet: learn from the last bump, then pass, give up or bump.
        Outputs trim_settled(const Inputs& in, Outputs o) {
            if (learn_) {
                const uint32_t move = in.count > bump_from_ ? in.count - bump_from_ : 0;
                const uint32_t over = move > bump_cmd_ ? move - bump_cmd_ : 0;
                over_est_ = over > over_est_ ? over : over_est_;
                learned_ = true;
                learn_ = false;
            }
            if (in.count >= goal()) {
                return end_wind(in, o, Beep::START, Outcome::PASS, true);
            }
            if (bumps_ >= kMaxBumps) {
                return fail_trim(in, o, false, true);
            }
            start_bump(in, o);
            return o;
        }

        Outputs step_trim(const Inputs& in, Outputs o) {
            const uint32_t now = in.now_ms;
            if (in.pending & (kBtn1 | kBtn2)) {
                return fail_trim(in, o, true, false);
            }
            const bool was_running = (state_ == MachineState::TRIM);
            if (in.pending & kBtn3) {
                if (was_running) {
                    state_ = MachineState::PAUSED;
                    paused_at_ = now;
                    learn_ = false; // an interrupted bump teaches nothing
                } else {
                    state_ = MachineState::TRIM;
                    trim_start_ += static_cast<uint32_t>(now - paused_at_); // pauses are free
                    begin_settle(in, false);
                }
                o.changed = true;
            }
            if (was_running || state_ == MachineState::TRIM) {
                manual_ = false; // a click in the pass that pauses is ignored
            } else if (in.pending & kBtn4) {
                manual_ = !manual_;
                o.changed = true;
            }
            o.leds = bargraph(in.count, target_);
            if (state_ == MachineState::PAUSED) {
                relay_ = manual_;
                o.relay = relay_;
                return o;
            }
            if (static_cast<uint32_t>(now - trim_start_) > kTrimMaxMs) {
                return fail_trim(in, o, false, phase_ == TrimPhase::SETTLE);
            }
            if (phase_ == TrimPhase::SETTLE) {
                relay_ = false;
                o.relay = false;
                if (in.count != settle_count_) {
                    settle_count_ = in.count;
                    settle_since_ = now;
                } else if (static_cast<uint32_t>(now - settle_since_) >= kTrimSettleMs) {
                    return trim_settled(in, o);
                }
                return o;
            }
            // BUMP: the ISR cuts the relay at bump_stop_; main only notices and settles.
            if (in.count != last_count_) {
                last_count_ = in.count;
                last_adv_ = now;
            }
            if (in.count >= bump_stop_ || in.cut) {
                begin_settle(in, true);
                o.relay = false;
            } else if (static_cast<uint32_t>(now - bump_start_) > kBumpMaxOnMs &&
                       static_cast<uint32_t>(now - last_adv_) > kBumpStallMs) {
                ++stalls_;
                if (stalls_ >= kMaxStalls) {
                    return fail_trim(in, o, false, false);
                }
                begin_settle(in, false);
                o.relay = false;
            } else {
                o.relay = true;
            }
            return o;
        }

        enum class TrimPhase : uint8_t { SETTLE, BUMP };
        TrimPhase phase_ = TrimPhase::SETTLE;
        bool trim_ = false;    // the trim phase is active (running or paused)
        bool hold_ = false;    // a failure result is held until a button press
        bool learned_ = false; // over_est_ comes from a completed bump
        bool learn_ = false;   // the next settle follows a completed bump
        Outcome outcome_ = Outcome::NONE;
        uint8_t bumps_ = 0;
        uint8_t stalls_ = 0;
        uint32_t over_est_ = 0;   // largest overshoot-after-cut seen this wind (pulses)
        uint32_t bump_from_ = 0;  // |count| when the current bump started
        uint32_t bump_cmd_ = 0;   // its commanded length
        uint32_t bump_stop_ = 0;  // |count| at which the ISR cuts it
        uint32_t bump_start_ = 0; // ms
        uint32_t last_adv_ = 0;   // ms of the last count change during the bump
        uint32_t last_count_ = 0;
        uint32_t settle_count_ = 0;
        uint32_t settle_since_ = 0;
        uint32_t trim_start_ = 0;
        uint32_t paused_at_ = 0;
        MachineState state_ = MachineState::IDLE;
        bool debug_ = false;
        bool manual_ = false;
        bool relay_ = false;
        bool run_ = false;
        bool abort_ = false;
        bool left_pending_ = false; // host left debug; report it on the next pass
        bool left_run_ = false;     // ... and it interrupted a RUN
        bool zero_pending_ = false; // the result was cleared; zero the held count next pass
        Result result_ = Result::NONE;
        uint32_t result_count_ = 0; // last count seen while the result is shown
        uint32_t result_since_ = 0; // when that count last changed
        uint32_t result_at_ = 0;    // when the wind stopped
        uint32_t last_host_ = 0;
        uint32_t target_ = kDefaultShortPulses;
    };

} // namespace stpalpha
