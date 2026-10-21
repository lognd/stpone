#pragma once
// frob:waive PARSE002 reason="C++-only header kept as .h to match the include convention"
// The ONE place paper length is computed and formatted for the display and the
// result screen (DEC-006, DEC-008): integer math only, and NO printf: the AVR's
// avr-libc printf lacks `%*d` (it printed empty fields), so the AVR build and the
// host test (tests/unit/test_length.py) run the same plain character code.

#include <stddef.h>
#include <stdint.h>

namespace stpalpha {

    // pi in millionths: integer input for every length computation.
    constexpr uint64_t kPiMicro = 3141593ull;
    // One yard and one inch in micrometres.
    constexpr uint64_t kUmPerYd = 914400ull;
    constexpr uint64_t kUmPerIn = 25400ull;
    // Largest length shown, in tenths of a yard (9999.9 yd): the 4-digit field never overflows.
    constexpr uint32_t kMaxTenths = 99999;
    // Pulse counts above this are shown as kMaxTenths (keeps the 64-bit math in range).
    constexpr uint32_t kMaxDisplayPulses = 40000000;
    // An over/under difference of at least this many tenths of an inch (one yard) is shown in yards.
    constexpr uint32_t kYardInchTenths = 360;
    // Cells in the progress bar of the winding screen.
    constexpr uint8_t kBarCells = 13;

    // A bounded text builder: appends characters, never overruns `cap`, no printf.
    class Text {
      public:
        Text(char* out, size_t cap) : out_(out), cap_(cap) {}
        Text& c(char ch) {
            if (n_ + 1 < cap_) {
                out_[n_++] = ch;
            }
            return *this;
        }
        Text& s(const char* str) {
            while (*str != '\0') {
                c(*str++);
            }
            return *this;
        }
        // Decimal number, right-aligned in `width` columns (0 = as wide as needed).
        Text& u(uint32_t v, uint8_t width = 0) {
            char digits[10];
            uint8_t n = 0;
            do {
                digits[n++] = static_cast<char>('0' + v % 10u);
                v /= 10u;
            } while (v != 0 && n < sizeof(digits));
            for (uint8_t pad = n < width ? static_cast<uint8_t>(width - n) : 0; pad > 0; --pad) {
                c(' ');
            }
            while (n > 0) {
                c(digits[--n]);
            }
            return *this;
        }
        // "W.t" for a value in tenths, whole part right-aligned in `width` columns.
        Text& tenths(uint32_t t, uint8_t width = 0) {
            u(t / 10u, width);
            c('.');
            return u(t % 10u);
        }
        // Pad with spaces up to `n` characters.
        Text& pad_to(size_t n) {
            while (n_ < n && n_ + 1 < cap_) {
                c(' ');
            }
            return *this;
        }
        // Terminate the string (call once, last).
        void end() {
            if (cap_ > 0) {
                out_[n_] = '\0';
            }
        }

      private:
        char* out_;
        size_t cap_;
        size_t n_ = 0;
    };

    // Path length for a pulse count in micrometres times 1e6 (saturating pulse count).
    inline uint64_t scaled_length(uint32_t pulses, uint32_t wheel_um, uint32_t cpr) {
        if (pulses > kMaxDisplayPulses) {
            pulses = kMaxDisplayPulses;
        }
        uint64_t circ = kPiMicro * wheel_um; // circumference in um * 1e6
        // Whole revolutions plus the fractional one, so no product leaves 64 bits.
        return (pulses / cpr) * circ + (pulses % cpr) * circ / cpr;
    }

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0305
    // Length in tenths of a yard (rounded to nearest, saturating at kMaxTenths) for a pulse count.
    inline uint32_t pulses_to_tenths(uint32_t pulses, uint32_t wheel_um, uint32_t cpr) {
        if (cpr == 0 || pulses > kMaxDisplayPulses) {
            return kMaxTenths;
        }
        uint64_t unit = kUmPerYd * 1000000ull;
        uint64_t tenths = (scaled_length(pulses, wheel_um, cpr) * 10u + unit / 2) / unit;
        return tenths > kMaxTenths ? kMaxTenths : static_cast<uint32_t>(tenths);
    }

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0305
    // Length in tenths of an inch (rounded to nearest) for a pulse count; 0 for a zero cpr.
    inline uint32_t pulses_to_inch_tenths(uint32_t pulses, uint32_t wheel_um, uint32_t cpr) {
        if (cpr == 0) {
            return 0;
        }
        uint64_t unit = kUmPerIn * 1000000ull;
        uint64_t tenths = (scaled_length(pulses, wheel_um, cpr) * 10u + unit / 2) / unit;
        return tenths > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(tenths);
    }

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0305
    // Percent of the target reached, 0..100 (integer math, capped).
    inline uint8_t percent_done(uint32_t count, uint32_t target_pulses) {
        if (target_pulses == 0 || count >= target_pulses) {
            return 100;
        }
        return static_cast<uint8_t>(static_cast<uint64_t>(count) * 100u / target_pulses);
    }

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0305
    // Write "W.t" for a tenths-of-yards value, whole part right-aligned in `width` columns.
    inline void format_tenths(char* out, size_t cap, uint32_t tenths, uint8_t width) {
        Text t(out, cap);
        t.tenths(tenths, width);
        t.end();
    }

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0305
    // Fill a 20-column progress row: "<pct>% [####-------]" (`cap` >= 21).
    inline void format_progress(char* row, size_t cap, uint8_t pct) {
        if (pct > 100) {
            pct = 100;
        }
        const uint8_t filled = static_cast<uint8_t>(pct * kBarCells / 100u);
        Text t(row, cap);
        t.u(pct, 3).s("% [");
        for (uint8_t i = 0; i < kBarCells; ++i) {
            t.c(i < filled ? '#' : '-');
        }
        t.c(']').pad_to(20);
        t.end();
    }

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0305
    // A miss given in tenths of an inch: "W.t in" under one yard, else "W.t yd" (DEC-008).
    inline void put_miss(Text& t, uint32_t in_tenths) {
        if (in_tenths < kYardInchTenths) {
            t.tenths(in_tenths).s(" in");
        } else {
            t.tenths((in_tenths + kYardInchTenths / 20u) / (kYardInchTenths / 10u)).s(" yd");
        }
    }

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0305
    // Fill the two RESULT rows (each padded to 20 characters, `cap` >= 21): the final length
    // against the target in yards, and the miss in inches (under one yard) or yards
    // ("+7.2 in over", "-1.3 yd short"); "settling..." while the motor still coasts.
    inline void format_result(char* row1, char* row2, size_t cap, bool settling,
                              uint32_t final_pulses, uint32_t target_pulses, uint32_t wheel_um,
                              uint32_t cpr) {
        Text a(row1, cap);
        a.s(settling ? "NOW " : "DONE")
            .c(' ')
            .tenths(pulses_to_tenths(final_pulses, wheel_um, cpr))
            .c('/')
            .tenths(pulses_to_tenths(target_pulses, wheel_um, cpr))
            .s(" yd")
            .pad_to(20);
        a.end();

        Text b(row2, cap);
        const bool over = final_pulses > target_pulses;
        const uint32_t miss = over ? final_pulses - target_pulses : target_pulses - final_pulses;
        const uint32_t in_tenths = pulses_to_inch_tenths(miss, wheel_um, cpr);
        if (settling) {
            b.s("settling...");
        } else if (in_tenths == 0) {
            b.s("on target");
        } else {
            b.c(over ? '+' : '-');
            put_miss(b, in_tenths);
            b.s(over ? " over" : " short");
        }
        b.pad_to(20);
        b.end();
    }

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0305
    // The two rows of a FAILED wind (DEC-010): row1 names the miss against the goal in inches
    // (yards from one yard up) as "SHORT -x.x in" or "STOPPED -x.x in"; row2 the final length against
    // the target. A final count at or past the goal still reads "-0.0 in" (never a plus sign).
    inline void format_short(char* row1, char* row2, size_t cap, bool stopped,
                             uint32_t final_pulses, uint32_t target_pulses, uint32_t goal_pulses,
                             uint32_t wheel_um, uint32_t cpr) {
        Text a(row1, cap);
        const uint32_t miss = goal_pulses > final_pulses ? goal_pulses - final_pulses : 0;
        const uint32_t in_tenths = pulses_to_inch_tenths(miss, wheel_um, cpr);
        a.s(stopped ? "STOPPED -" : "SHORT -");
        put_miss(a, in_tenths);
        a.pad_to(20);
        a.end();

        Text b(row2, cap);
        b.s("LEN ")
            .tenths(pulses_to_tenths(final_pulses, wheel_um, cpr))
            .c('/')
            .tenths(pulses_to_tenths(target_pulses, wheel_um, cpr))
            .s(" yd")
            .pad_to(20);
        b.end();
    }

} // namespace stpalpha
