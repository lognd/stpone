#include <stpalpha/hal/lcd.h>
#include <stpalpha/hal/setup.h>
#include <stpalpha/machine.h>

#include <avr/interrupt.h>
#include <stdio.h>
#include <string.h>

// ---- Physical constants ----

static constexpr float kWheelDiamIn = 4.0015f;                 // measured 4.001 / 4.002 in
static constexpr float kCircumIn = 3.14159265f * kWheelDiamIn; // ~12.571 in
static constexpr float kCountsPerRev = 2400.0f;                // 600 PPR * 4x
static constexpr float kShortLengthIn = 1080.0f;               // BTN1: 30 yd
static constexpr float kLongLengthIn = 3240.0f;                // BTN2: 90 yd
static constexpr uint16_t kShortTenthsYd = 300;                // 30.0 yd
static constexpr uint16_t kLongTenthsYd = 900;                 // 90.0 yd
static constexpr uint32_t kShortPulses =
    static_cast<uint32_t>(kShortLengthIn * kCountsPerRev / kCircumIn);
static constexpr uint32_t kLongPulses =
    static_cast<uint32_t>(kLongLengthIn * kCountsPerRev / kCircumIn);

// ---- Button bits and policy ----
// The panel buttons are LATCHING: each push toggles the level, so an action
// is an edge (btn_events), never a level. Slow travel can chatter for longer
// than the HAL debounce, so after an accepted event the button is ignored
// for kBtnLockoutMs (must stay under 256: timer0_ticks is 8-bit).

static constexpr uint8_t kBtnShort = 1 << 0; // BTN1: wind 30 yd; stop while running
static constexpr uint8_t kBtnLong = 1 << 1;  // BTN2: wind 90 yd; stop while running
static constexpr uint8_t kBtnPause = 1 << 2; // BTN3: pause / play mid cycle
static constexpr uint8_t kBtnJog = 1 << 3;   // BTN4: jog while latched down
static constexpr uint8_t kBtnLockoutMs = 200;

// ---- Beep sequence config ----

static constexpr uint8_t kBeepCount = 3;
static constexpr uint16_t kBeepMs = 300;   // duration of each beep
static constexpr uint8_t kBeepGapMs = 150; // silence between beeps

// Sentinel for "no count rendered yet", forcing the first winding redraw.
static constexpr uint32_t kNoCount = 0xFFFFFFFFu;

// ---- Atomics ----

static uint32_t read_enc(void) {
    cli();
    int32_t v = enc_count;
    sei();
    // Either mounting direction works: length is the magnitude of the count.
    return static_cast<uint32_t>(v < 0 ? -v : v);
}

static uint16_t read_enc_errors(void) {
    cli();
    uint16_t v = enc_errors;
    sei();
    return v;
}

static void reset_enc(void) {
    cli();
    enc_count = 0;
    enc_errors = 0;
    sei();
}

// Consume one button edge, ignoring chatter inside the lockout window.
static bool btn_edge(uint8_t bit) {
    static bool locked[4] = {false, false, false, false};
    static uint8_t since[4] = {0, 0, 0, 0};
    uint8_t idx = (bit == kBtnShort) ? 0 : (bit == kBtnLong) ? 1 : (bit == kBtnPause) ? 2 : 3;
    if (locked[idx] && static_cast<uint8_t>(timer0_ticks - since[idx]) >= kBtnLockoutMs) {
        locked[idx] = false;
    }
    if (!btn_consume(bit)) {
        return false;
    }
    if (locked[idx]) {
        return false;
    }
    locked[idx] = true;
    since[idx] = timer0_ticks;
    return true;
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

// Idle screen: the two start buttons and the jog hint; row 1 reports jog / encoder faults.
static void render_idle(LCDPacket& p, bool jogging, uint16_t errors) {
    init_packet(p);
    snprintf(p.rows[0], 21, " -= PAPER WINDER =- ");
    if (jogging) {
        snprintf(p.rows[1], 21, "      JOGGING       ");
    } else if (errors != 0) {
        snprintf(p.rows[1], 21, "!! ENC MISS:%-5u  !", errors);
    } else {
        snprintf(p.rows[1], 21, "                    ");
    }
    snprintf(p.rows[2], 21, "[1] 30yd    [2] 90yd");
    snprintf(p.rows[3], 21, "[4] JOG while down  ");
    flush_packet(p);
}

// Winding / paused screen: all fields have fixed column positions.
// Row 1: "CNT:XXXXXXXXXX   PP%"  (count left-justified in 10 chars, pct right 3+1)
// Row 2: "LEN LLLL.l/TTTT.t yd" (each number occupies exactly 4+1+1 chars)
static void render_winding(LCDPacket& p, uint32_t count, uint32_t target_pulses,
                           uint16_t target_tenths, bool paused, uint16_t errors) {
    float count_f = static_cast<float>(count);
    uint8_t pct = (count <= target_pulses)
                      ? static_cast<uint8_t>(count_f * 100.0f / static_cast<float>(target_pulses))
                      : 100u;
    float len_yd = count_f * kCircumIn / (kCountsPerRev * 36.0f);
    uint16_t len_10 = static_cast<uint16_t>(len_yd * 10.0f);
    const char* label = paused ? "PAUSED" : "WINDING";

    if (errors != 0) {
        snprintf(p.rows[0], 21, "%-11s ERR:%-4u", label, errors);
    } else if (paused) {
        snprintf(p.rows[0], 21, "      PAUSED        ");
    } else {
        snprintf(p.rows[0], 21, "     WINDING...     ");
    }
    // %-10lu: left-justify count in 10 chars so percent column is fixed.
    snprintf(p.rows[1], 21, "CNT:%-10lu  %3u%%", count, pct);
    // %4u.%01u: each value occupies exactly 6 chars (4 int + '.' + 1 frac).
    snprintf(p.rows[2], 21, "LEN %4u.%01u/%4u.%01u yd", len_10 / 10u, len_10 % 10u,
             target_tenths / 10u, target_tenths % 10u);
    snprintf(p.rows[3], 21, paused ? "[1/2]STOP   [3]PLAY " : "[1/2]STOP   [3]PAUSE");
    flush_packet(p);
}

// ---- State machine ----

static MachineState g_state = MachineState::IDLE;
static MachineState g_prev_state = MachineState::WINDING; // force first render
static LCDPacket g_lcd;
static uint32_t g_last_count = kNoCount;
static uint16_t g_last_errors = 0;
static uint32_t g_target_pulses = kShortPulses;
static uint16_t g_target_tenths = kShortTenthsYd;
// Jog runs only after BTN4 has been seen up in IDLE, so a button left latched
// down at power-up or after a wind never starts the motor by itself.
static bool g_jog_armed = false;
static bool g_jogging = false;

static void start_wind(uint32_t pulses, uint16_t tenths) {
    cancel_beep();
    g_target_pulses = pulses;
    g_target_tenths = tenths;
    g_state = MachineState::WINDING;
    g_last_count = kNoCount;
    g_jog_armed = false;
    g_jogging = false;
    reset_enc();
    relay_on();
    set_leds(0b1111);
}

static void stop_wind(void) {
    g_state = MachineState::IDLE;
    g_jog_armed = false;
    relay_off();
    set_leds(0b0000);
    start_beep_sequence();
}

static void run_idle(void) {
    bool on_entry = (g_prev_state != MachineState::IDLE);
    g_prev_state = MachineState::IDLE;

    tick_beep();

    bool down = (btn_state & kBtnJog) != 0;
    if (!down) {
        g_jog_armed = true;
    }
    bool jog = g_jog_armed && down;
    uint16_t errors = read_enc_errors();
    if (on_entry || jog != g_jogging || errors != g_last_errors) {
        g_jogging = jog;
        g_last_errors = errors;
        set_leds(jog ? 0b1000 : 0b0101);
        render_idle(g_lcd, jog, errors);
    }
    // Jog follows the latched level; the relay is the only thing it drives.
    if (jog) {
        relay_on();
    } else {
        relay_off();
    }
    (void)btn_consume(kBtnJog | kBtnPause); // jog is level-driven; pause is a no-op here

    if (btn_edge(kBtnShort)) {
        start_wind(kShortPulses, kShortTenthsYd);
    } else if (btn_edge(kBtnLong)) {
        start_wind(kLongPulses, kLongTenthsYd);
    }
}

static void run_winding(void) {
    bool paused = (g_state == MachineState::PAUSED);
    g_prev_state = g_state;

    uint32_t count = read_enc();
    uint16_t errors = read_enc_errors();

    if (count != g_last_count || errors != g_last_errors || g_last_count == kNoCount) {
        render_winding(g_lcd, count, g_target_pulses, g_target_tenths, paused, errors);
        g_last_count = count;
        g_last_errors = errors;
    }

    // 4-LED bargraph: each LED lights at 25% increments.
    static constexpr uint8_t kBargraph[5] = {0b0000, 0b0001, 0b0011, 0b0111, 0b1111};
    uint32_t capped = (count < g_target_pulses) ? count : g_target_pulses;
    uint8_t segment = static_cast<uint8_t>(capped * 4u / g_target_pulses);
    set_leds(kBargraph[segment]);

    bool stop_btn = btn_edge(kBtnShort) | btn_edge(kBtnLong);
    bool target_hit = (count >= g_target_pulses);
    (void)btn_consume(kBtnJog);

    if (stop_btn || target_hit) {
        stop_wind();
        return;
    }
    if (btn_edge(kBtnPause)) {
        g_last_count = kNoCount; // redraw with the new title
        if (paused) {
            g_state = MachineState::WINDING;
            relay_on();
        } else {
            g_state = MachineState::PAUSED;
            relay_off();
        }
    }
}

// ---- Entry point ----

// docs/runbooks/hardware-tests.md P1, simulator evidence tests/system/test_firmware.py"
// frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0302
int main(void) {
    lcd_init();
    lcd_clear();

    // Latching buttons hold their last physical state across power cycles.
    // Wait past the 16 ms debounce window (plus margin) so the ISR has
    // settled on every button, then discard the startup events.
    uint8_t settle_start = timer0_ticks;
    while (static_cast<uint8_t>(timer0_ticks - settle_start) < 40) {
    }
    cli();
    btn_events = 0;
    sei();

    for (;;) {
        switch (g_state) {
        case MachineState::IDLE:
            run_idle();
            break;
        case MachineState::WINDING:
        case MachineState::PAUSED:
            run_winding();
            break;
        }
    }
}
