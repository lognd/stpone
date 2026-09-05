#include <stpalpha/hal/lcd.h>
#include <stpalpha/hal/setup.h>
#include <stpalpha/machine.h>

#include <avr/interrupt.h>
#include <stdio.h>
#include <string.h>

// ---- Physical constants ----

static constexpr float kWheelDiamIn = 5.0f;
static constexpr float kCircumIn = 3.14159265f * kWheelDiamIn; // ~15.708 in
static constexpr float kCountsPerRev = 2400.0f;                // 600 PPR * 4x
static constexpr float kTargetLengthIn = 1080.0f;              // 30 yd = 1080 in
static constexpr float kTargetYd = kTargetLengthIn / 36.0f;    // 30.0 yd
static constexpr uint32_t kTargetPulses =
    static_cast<uint32_t>(kTargetLengthIn * kCountsPerRev / kCircumIn);

// ---- Beep sequence config ----

static constexpr uint8_t kBeepCount = 3;
static constexpr uint16_t kBeepMs = 300;   // duration of each beep
static constexpr uint8_t kBeepGapMs = 150; // silence between beeps

// ---- Atomics ----

static uint32_t read_enc(void) {
    cli();
    uint32_t v = enc_count;
    sei();
    return v;
}

static void reset_enc(void) {
    cli();
    enc_count = 0;
    sei();
}

// ---- Non-blocking beep sequence ----
// Uses timer0_ticks (8-bit, 1 kHz) for gap timing.
// call tick_beep() every main-loop iteration.

static uint8_t g_beep_remaining = 0;
static uint8_t g_beep_gap_start = 0;
static bool g_beep_in_gap = false;

static void start_beep_sequence(void) {
    g_beep_remaining = kBeepCount - 1; // first beep fires immediately below
    g_beep_in_gap = false;
    buzz(kBeepMs);
}

static void cancel_beep(void) {
    g_beep_remaining = 0;
    g_beep_in_gap = false;
    buzz(0);
}

static void tick_beep(void) {
    if (g_beep_in_gap) {
        uint8_t elapsed = static_cast<uint8_t>(timer0_ticks - g_beep_gap_start);
        if (elapsed >= kBeepGapMs) {
            g_beep_in_gap = false;
            if (g_beep_remaining > 0) {
                --g_beep_remaining;
                buzz(kBeepMs);
            }
        }
        return;
    }
    if (g_beep_remaining > 0 && buzz_ticks == 0) {
        g_beep_in_gap = true;
        g_beep_gap_start = timer0_ticks;
    }
}

// ---- LCD helpers ----

static void init_packet(LCDPacket& p) {
    memset(&p, ' ', sizeof(p));
    for (auto& row : p.rows)
        row[20] = '\0';
}

static void flush_packet(const LCDPacket& p) {
    for (uint8_t r = 0; r < 4; ++r) {
        lcd_set_cursor(0, r);
        lcd_write_str(p.rows[r]);
    }
}

// ---- Display renderers ----

// Fixed-width idle screen: target never moves.
static void render_idle(LCDPacket& p) {
    init_packet(p);
    // kTargetYd = 30.0 -> tenths = 300 -> "30.0"
    static constexpr uint16_t kTgtTenths = static_cast<uint16_t>(kTargetYd * 10.0f);
    snprintf(p.rows[0], 21, " -= PAPER WINDER =- ");
    snprintf(p.rows[1], 21, "                    ");
    snprintf(p.rows[2], 21, " TARGET: %2u.%01u yds   ", kTgtTenths / 10u, kTgtTenths % 10u);
    snprintf(p.rows[3], 21, "   [BTN1]  START    ");
    flush_packet(p);
}

// Fixed-width winding screen: all fields have fixed column positions.
// Row 1: "CNT:XXXXXXXXXX   PP%"  (count left-justified in 10 chars, pct right 3+1)
// Row 2: "LEN LLLL.l/TTTT.t yd" (each number occupies exactly 4+1+1 chars)
static void render_winding(LCDPacket& p, uint32_t count) {
    float count_f = static_cast<float>(count);
    uint8_t pct = (count <= kTargetPulses)
                      ? static_cast<uint8_t>(count_f * 100.0f / static_cast<float>(kTargetPulses))
                      : 100u;
    float len_yd = count_f * kCircumIn / (kCountsPerRev * 36.0f);
    uint16_t len_10 = static_cast<uint16_t>(len_yd * 10.0f);
    static constexpr uint16_t kTgt10 = static_cast<uint16_t>(kTargetYd * 10.0f); // 300

    snprintf(p.rows[0], 21, "     WINDING...     ");
    // %-10lu: left-justify count in 10 chars so percent column is fixed.
    snprintf(p.rows[1], 21, "CNT:%-10lu  %3u%%", count, pct);
    // %4u.%01u: each value occupies exactly 6 chars (4 int + '.' + 1 frac).
    snprintf(p.rows[2], 21, "LEN %4u.%01u/%4u.%01u yd", len_10 / 10u, len_10 % 10u, kTgt10 / 10u,
             kTgt10 % 10u);
    snprintf(p.rows[3], 21, "   [BTN1]  STOP     ");
    flush_packet(p);
}

// ---- State machine ----

static MachineState g_state = MachineState::IDLE;
static MachineState g_prev_state = MachineState::WINDING; // force first render
static LCDPacket g_lcd;
static uint32_t g_last_count = UINT32_MAX;

static void run_idle(void) {
    bool on_entry = (g_prev_state != MachineState::IDLE);
    g_prev_state = MachineState::IDLE;

    tick_beep();

    if (on_entry) {
        set_leds(0b0101);
        render_idle(g_lcd);
    }

    if (btn_consume(1 << 0)) {
        cancel_beep();
        g_state = MachineState::WINDING;
        g_last_count = UINT32_MAX;
        reset_enc();
        relay_on();
        set_leds(0b1111);
    }
}

static void run_winding(void) {
    g_prev_state = MachineState::WINDING;

    uint32_t count = read_enc();

    if (count != g_last_count || g_last_count == UINT32_MAX) {
        render_winding(g_lcd, count);
        g_last_count = count;
    }

    // 4-LED bargraph: each LED lights at 25% increments.
    static constexpr uint8_t kBargraph[5] = {0b0000, 0b0001, 0b0011, 0b0111, 0b1111};
    uint32_t capped = (count < kTargetPulses) ? count : kTargetPulses;
    uint8_t segment = static_cast<uint8_t>(capped * 4u / kTargetPulses);
    set_leds(kBargraph[segment]);

    bool stop_btn = btn_consume(1 << 0);
    bool target_hit = (count >= kTargetPulses);

    if (stop_btn || target_hit) {
        g_state = MachineState::IDLE;
        relay_off();
        set_leds(0b0000);
        start_beep_sequence();
    }
}

// ---- Entry point ----

// docs/runbooks/hardware-tests.md P1, simulator evidence tests/system/test_firmware.py"
// frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0302
int main(void) {
    lcd_init();
    lcd_clear();

    // Latching buttons hold their last physical state across power cycles.
    // Wait >8 ms for the debounce ISR to settle, then discard startup events.
    cli();
    btn_events = 0;
    sei();

    for (;;) {
        switch (g_state) {
        case MachineState::IDLE:
            run_idle();
            break;
        case MachineState::WINDING:
            run_winding();
            break;
        }
    }
}
