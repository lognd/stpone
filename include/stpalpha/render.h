#pragma once
// frob:waive PARSE002 reason="C++-only header kept as .h to match the include convention"
// The four panel screens as pure functions of their inputs (no hardware, no flushing), so the
// host driver and the simavr image (tests/avr/test_render_fmt.cpp) execute the very snprintf
// calls the firmware does, on glibc and on avr-libc respectively (DEC-009).

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <stpalpha/fmt.h>
#include <stpalpha/length.h>
#include <stpalpha/machine.h>

namespace stpalpha {

    // Manual-mode display states: auto, or motor running by hand.
    constexpr uint8_t kManualOff = 0;
    constexpr uint8_t kManualOn = 1;

    // Blank packet: every row is 20 spaces and a NUL.
    inline void init_packet(LCDPacket& p) {
        memset(&p, ' ', sizeof(p));
        for (auto& row : p.rows)
            row[20] = '\0';
    }

    // Idle screen: the two start buttons and the manual hint; row 1 reports manual / encoder faults.
    inline void render_idle(LCDPacket& p, uint8_t manual, uint16_t errors) {
        init_packet(p);
        STP_SNPRINTF(p.rows[0], 21, STP_FMT(" -= PAPER WINDER =- "));
        if (manual == kManualOn) {
            STP_SNPRINTF(p.rows[1], 21, STP_FMT("MANUAL: motor ON    "));
        } else if (errors != 0) {
            STP_SNPRINTF(p.rows[1], 21, STP_FMT("!! ENC MISS:%-5u  !"), errors);
        } else {
            STP_SNPRINTF(p.rows[1], 21, STP_FMT("                    "));
        }
        STP_SNPRINTF(p.rows[2], 21, STP_FMT("[1] 30yd    [2] 90yd"));
        if (manual == kManualOn) {
            STP_SNPRINTF(p.rows[3], 21, STP_FMT("[4] click = auto    "));
        } else {
            STP_SNPRINTF(p.rows[3], 21, STP_FMT("[4] click = manual  "));
        }
    }

    // Debug screen: the host owns the machine; the panel only has the emergency stop.
    inline void render_debug(LCDPacket& p) {
        init_packet(p);
        STP_SNPRINTF(p.rows[0], 21, STP_FMT(" -= PAPER WINDER =- "));
        STP_SNPRINTF(p.rows[1], 21, STP_FMT("DEBUG: HOST CONTROL "));
        STP_SNPRINTF(p.rows[2], 21, STP_FMT("panel buttons locked"));
        STP_SNPRINTF(p.rows[3], 21, STP_FMT("[1/2] = STOP motor  "));
    }

    // Winding / paused screen (DEC-008): all fields have fixed column positions.
    // Row 1: " 45% [#####--------]"  (percent done and a bar; the raw count is not shown)
    // Row 2: "LEN LLLL.l/TTTT.t yd" (each number occupies exactly 4+1+1 chars)
    inline void render_winding(LCDPacket& p, uint32_t count, uint32_t target_pulses, bool paused,
                               uint8_t manual, uint16_t errors, uint32_t wheel_um, uint32_t cpr,
                               bool trimming = false) {
        init_packet(p);
        const uint8_t pct = percent_done(count, target_pulses);
        char len[12];
        char tgt[12];
        // Fixed 4-digit whole part; the length keeps counting past the target (saturates at 9999.9).
        // Built by length.h without printf: avr-libc's printf has no `%*d`.
        format_tenths(len, sizeof(len), pulses_to_tenths(count, wheel_um, cpr), 4);
        format_tenths(tgt, sizeof(tgt), pulses_to_tenths(target_pulses, wheel_um, cpr), 4);
        const char* label = paused ? "PAUSED" : "WINDING";

        if (errors != 0) {
            STP_SNPRINTF(p.rows[0], 21, STP_FMT("%-10s ERR:%-5u"), label, errors);
        } else if (paused && manual == kManualOn) {
            STP_SNPRINTF(p.rows[0], 21, STP_FMT("PAUSED - MANUAL RUN "));
        } else if (paused) {
            STP_SNPRINTF(p.rows[0], 21, STP_FMT("      PAUSED        "));
        } else if (trimming) {
            STP_SNPRINTF(p.rows[0], 21, STP_FMT("     TRIMMING...    "));
        } else {
            STP_SNPRINTF(p.rows[0], 21, STP_FMT("     WINDING...     "));
        }
        format_progress(p.rows[1], 21, pct);
        Text row2(p.rows[2], 21);
        row2.s("LEN ").s(len).c('/').s(tgt).s(" yd");
        row2.end();
        if (paused) {
            STP_SNPRINTF(p.rows[3], 21, STP_FMT("[1/2]STOP   [3]PLAY "));
        } else {
            STP_SNPRINTF(p.rows[3], 21, STP_FMT("[1/2]STOP   [3]PAUSE"));
        }
    }

    // Result screen (DEC-006): the final length against the target after a wind, or the live
    // length with a "settling" marker while the motor still coasts. Row 3 reports encoder faults.
    // A settled SHORT/STOPPED wind (DEC-010) replaces the title with the alarm banner and shows the
    // miss against `goal_pulses`; it never uses the DONE wording.
    inline void render_result(LCDPacket& p, uint32_t count, uint32_t target_pulses, bool settling,
                              uint16_t errors, uint32_t wheel_um, uint32_t cpr,
                              Outcome outcome = Outcome::NONE, uint32_t goal_pulses = 0) {
        init_packet(p);
        const bool failed = outcome == Outcome::SHORT || outcome == Outcome::STOPPED;
        if (failed && !settling) {
            STP_SNPRINTF(p.rows[0], 21, STP_FMT(" !! UNDER TARGET !! "));
            format_short(p.rows[1], p.rows[2], 21, outcome == Outcome::STOPPED, count,
                         target_pulses, goal_pulses, wheel_um, cpr);
        } else {
            STP_SNPRINTF(p.rows[0], 21, STP_FMT(" -= PAPER WINDER =- "));
            format_result(p.rows[1], p.rows[2], 21, settling, count, target_pulses, wheel_um, cpr);
        }
        if (errors != 0) {
            STP_SNPRINTF(p.rows[3], 21, STP_FMT("!! ENC MISS:%-5u  !"), errors);
        } else {
            STP_SNPRINTF(p.rows[3], 21, STP_FMT("[1] 30yd    [2] 90yd"));
        }
    }

} // namespace stpalpha
