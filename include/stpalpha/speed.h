#pragma once
// frob:waive PARSE002 reason="C++-only header kept as .h to match the include convention"
// Roll speed from the encoder count (DEC-006): pure and integer-only so the host
// test (tests/unit/test_hostlink.py) runs the code the AVR runs.

#include <stdint.h>

namespace stpalpha {

    // Sampling window of the speed meter, in milliseconds (the unit of resolution is
    // 1000/kSpeedWindowMs = 10 pulses/s).
    constexpr uint32_t kSpeedWindowMs = 100;
    // A delta above this in one window is a count jump, not motion (also bounds delta*1000).
    constexpr uint32_t kSpeedMaxDelta = 4000000;

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0306
    // Encoder pulses per second over the last completed >= 100 ms window, unsmoothed.
    class SpeedMeter {
      public:
        // Call every main-loop pass with the uptime and the SIGNED encoder count.
        void update(uint32_t now_ms, int32_t count) {
            uint32_t elapsed = now_ms - start_ms_;
            if (elapsed < kSpeedWindowMs) {
                return;
            }
            uint32_t delta = static_cast<uint32_t>(count) - static_cast<uint32_t>(last_);
            if (delta > 0x7FFFFFFFu) {
                delta = 0u - delta; // |count - last| across a sign change or reverse run
            }
            if (delta > kSpeedMaxDelta) {
                delta = 0; // a ZERO or jump, not motion
            }
            speed_ = delta * 1000u / elapsed;
            start_ms_ = now_ms;
            last_ = count;
        }

        // Restart the window at `count` (call when the count is zeroed so it is not read as motion).
        void rebase(uint32_t now_ms, int32_t count) {
            start_ms_ = now_ms;
            last_ = count;
        }

        // Last measured speed, pulses per second (0 until the first window completes).
        uint32_t pulses_per_s() const {
            return speed_;
        }

      private:
        uint32_t start_ms_ = 0;
        int32_t last_ = 0;
        uint32_t speed_ = 0;
    };

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0306
    // Longest main-loop pass seen, in ms (DUMP `pass_max_ms`): the button/stop latency budget is
    // ~60 ms, so a value above it names the pass that blocked (DEC-009). Saturates at 65535.
    class PassMeter {
      public:
        // Call once at the top of every pass with the latched uptime; the gap since the previous
        // call is the length of the pass that just ended.
        void tick(uint32_t now_ms) {
            if (started_) {
                uint32_t d = now_ms - last_ms_;
                if (d > max_) {
                    max_ = d > 0xFFFFu ? 0xFFFFu : static_cast<uint16_t>(d);
                }
            }
            started_ = true;
            last_ms_ = now_ms;
        }
        // Forget the maximum (ZERO); the pass in progress still counts from its own start.
        void clear() {
            max_ = 0;
        }
        uint16_t max_ms() const {
            return max_;
        }

      private:
        bool started_ = false;
        uint32_t last_ms_ = 0;
        uint16_t max_ = 0;
    };

} // namespace stpalpha
