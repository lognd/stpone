#pragma once
// Shared case table for the panel/host-link text: tests/unit/render_driver.cpp runs it on the host
// (glibc printf) and tests/avr/test_render_fmt.cpp runs the very same code on the ATmega32U4 in
// simavr (avr-libc printf). tests/unit/test_render.py requires the two outputs to be identical,
// so an avr-libc-only formatting failure (`%*d`, a NULL `%s`, ...) cannot hide behind glibc.

#include <stdint.h>
#include <string.h>

#include <stpalpha/hostlink.h>
#include <stpalpha/render.h>

namespace render_cases {

    using namespace stpalpha;

    // emit(line) gets one NUL-terminated line per case, without a newline.
    template <class Emit>
    void run(Emit emit) {
        constexpr uint32_t kWheel = 101638;
        constexpr uint32_t kCpr = 2400;
        constexpr uint32_t kShort = 206187;
        constexpr uint32_t kLong = 618562;
        static const uint16_t kErrors[] = {0, 3, 65535};
        static const uint32_t kWindCounts[] = {0u, 1u, 51547u, 206186u, kShort, 400000u, 40000000u};
        static const uint32_t kResultCounts[] = {0u, kShort - 3000, kShort, kShort + 213,
                                                 kShort + 1900, kShort + 9000, 1000000u};
        static const char* const kCommands[] = {"HELLO", "GET", "PARAM", "DUMP", "SET coast 500",
                                                "DUMP", "SET short 5", "BOGUS", "DEBUG 1",
                                                "RUN 5000", "GET", "RELAY 1", "SET margin 0", "SET bump 0",
                                                "PARAM", "GET"};
        char line[112];
        LCDPacket p;
        auto rows = [&](const char* name) {
            size_t n = 0;
            for (const char* s = name; *s != '\0' && n < 8; ++s) {
                line[n++] = *s;
            }
            for (auto& row : p.rows) {
                line[n++] = '|';
                for (uint8_t c = 0; c < 20; ++c) {
                    line[n++] = row[c] == '\0' ? '~' : row[c]; // '~' marks a short row
                }
            }
            line[n] = '\0';
            emit(line);
        };
        for (uint8_t manual = 0; manual < 2; ++manual) {
            for (uint16_t errors : kErrors) {
                render_idle(p, manual, errors);
                rows(manual ? "idle-m" : "idle");
            }
        }
        render_debug(p);
        rows("debug");
        for (uint32_t count : kWindCounts) {
            for (uint8_t paused = 0; paused < 2; ++paused) {
                render_winding(p, count, kShort, paused != 0, 0, 0, kWheel, kCpr);
                rows(paused ? "pause" : "wind");
            }
        }
        render_winding(p, 1234, kLong, false, 0, 12, kWheel, kCpr);
        rows("wind-e");
        render_winding(p, 1234, kLong, true, 1, 0, kWheel, kCpr);
        rows("pause-m");
        render_winding(p, 1234, kLong, true, 1, 65535, kWheel, kCpr);
        rows("pause-me");
        for (uint32_t count : kResultCounts) {
            for (uint8_t settling = 0; settling < 2; ++settling) {
                render_result(p, count, kShort, settling != 0, 0, kWheel, kCpr);
                rows(settling ? "settle" : "done");
            }
        }
        render_result(p, kShort, kShort, false, 7, kWheel, kCpr);
        rows("done-e");
        // DEC-010: the trim screen and the failed-wind screens (goal = target + 191).
        render_winding(p, kShort - 300, kShort, false, 0, 0, kWheel, kCpr, true);
        rows("trim");
        render_winding(p, kShort - 300, kShort, false, 0, 5, kWheel, kCpr, true);
        rows("trim-e");
        for (uint32_t count : kResultCounts) {
            render_result(p, count, kShort, false, 0, kWheel, kCpr, Outcome::SHORT, kShort + 191);
            rows("short");
            render_result(p, count, kShort, false, 0, kWheel, kCpr, Outcome::STOPPED, kShort + 191);
            rows("stopped");
            render_result(p, count, kShort, true, 0, kWheel, kCpr, Outcome::SHORT, kShort + 191);
            rows("short-s");
            render_result(p, count, kShort, false, 3, kWheel, kCpr, Outcome::PASS, kShort + 191);
            rows("pass");
        }

        // Host-link replies (the same snprintf calls, fixed snapshot).
        Controller c;
        HostLink h;
        h.sync(c);
        Snapshot s{};
        s.count = -42;
        s.errors = 3;
        s.speed = 5170;
        s.btn = 0b1010;
        s.toggles[0] = 7;
        s.pending[2] = 1;
        s.enc_dir = -1;
        s.uptime_ms = 1234;
        s.stack_free = 321;
        s.pass_max_ms = 47;
        s.isr_lat_max = 33;
        char out[320];
        for (const char* cmd : kCommands) {
            h.handle(c, s, cmd, out, sizeof(out));
            size_t n = 0;
            for (const char* t = "reply|"; *t != '\0'; ++t) {
                line[n++] = *t;
            }
            for (const char* t = out; *t != '\0' && n < sizeof(line) - 1; ++t) {
                line[n++] = *t;
            }
            line[n] = '\0';
            emit(line);
            // DUMP is longer than the line buffer: emit the tail as a second line.
            if (strlen(out) > 100) {
                emit(out + 100);
            }
        }
    }

} // namespace render_cases
