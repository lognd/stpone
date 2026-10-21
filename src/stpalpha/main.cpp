#include <stpalpha/controller.h>
#include <stpalpha/fmt.h>
#include <stpalpha/hal/lcd.h>
#include <stpalpha/hal/setup.h>
#include <stpalpha/hal/uart.h>
#include <stpalpha/hostlink.h>
#include <stpalpha/length.h>
#include <stpalpha/machine.h>
#include <stpalpha/panel.h>
#include <stpalpha/params_image.h>
#include <stpalpha/render.h>
#include <stpalpha/speed.h>

#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <stdio.h>
#include <string.h>

// ---- Physical constants ----
// The nominal length model of SPEC-001 (pinned by tests/unit/test_length_model.py). The
// live wheel diameter and counts/rev are g_link.params (SET wheel_um / cpr, EEPROM); the
// defaults in Params reproduce these numbers.

static constexpr float kWheelDiamIn = 4.0015f;                 // measured 4.001 / 4.002 in
static constexpr float kCircumIn = 3.14159265f * kWheelDiamIn; // ~12.571 in
static constexpr float kCountsPerRev = 2400.0f;                // 600 PPR * 4x
static constexpr float kShortLengthIn = 1080.0f;               // BTN1: 30 yd
static constexpr float kLongLengthIn = 3240.0f;                // BTN2: 90 yd
static constexpr uint16_t kShortTenthsYd = 300;                // 30.0 yd
static constexpr uint16_t kLongTenthsYd = 900;                 // 90.0 yd

// ---- Buttons ----
// The panel buttons are MOMENTARY; the latching is done here in software (DEC-011). The HAL
// counts every debounced edge (btn_toggles) and btn_consume() hands them out one per loop
// pass, so fast double pushes are neither merged nor dropped. Only PRESS edges become events
// (take_pending); a release is consumed and ignored. The rules live in stpalpha::Controller
// (include/stpalpha/controller.h).

using stpalpha::Beep;
using stpalpha::Controller;
using stpalpha::Inputs;
using stpalpha::Outputs;

static stpalpha::HostLink g_link; // host-link interpreter and the live parameters
static Controller g_ctl;          // panel/host state machine

// ---- Beep sequence config ----

static constexpr uint8_t kBeepCount = 3;
static constexpr uint16_t kBeepMs = 300;   // duration of each beep
static constexpr uint8_t kBeepGapMs = 150; // silence between beeps
// Failure alarm (DEC-010): long beeps, repeated every kAlarmEveryMs while the SHORT result is held,
// at most kAlarmRepeats times (about a minute) so it cannot sound forever unattended.
static constexpr uint8_t kAlarmBeeps = 3;
static constexpr uint16_t kAlarmMs = 700;
static constexpr uint16_t kAlarmGapMs = 250;
static constexpr uint32_t kAlarmEveryMs = 6000;
static constexpr uint8_t kAlarmRepeats = 10;

// Sentinel for "no count rendered yet", forcing the first winding redraw.
static constexpr uint32_t kNoCount = 0xFFFFFFFFu;

// ---- Watchdog ----
// A hung main loop (a stuck LCD bus, a runaway wait) must not leave the relay on with
// nobody reading the panel: the watchdog resets the MCU, and hal_init boots with PB6 low.
// The reset flag is cleared and the watchdog stopped as early as possible so a restarted
// image does not inherit the short post-reset timeout.

static constexpr uint8_t kWatchdogTimeout = WDTO_500MS;

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P8"
static void wdt_early_off(void) __attribute__((naked, used, section(".init3")));
static void wdt_early_off(void) {
    MCUSR = 0;
    wdt_disable();
}

// ---- Atomics ----

static int32_t read_enc_raw(void) {
    cli();
    int32_t v = enc_count;
    sei();
    return v;
}

static uint32_t read_enc(void) {
    int32_t v = read_enc_raw();
    // Either mounting direction works: length is the magnitude of the count.
    // Unsigned negation: |INT32_MIN| is well defined.
    return v < 0 ? uint32_t{0} - static_cast<uint32_t>(v) : static_cast<uint32_t>(v);
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

// ---- Non-blocking beep sequence ----
// Gap timing uses the 32-bit uptime (ms) refreshed once per pass by tick_uptime(); call
// tick_beep() every main-loop iteration after it.

static uint32_t g_uptime_ms = 0;
static uint8_t g_beep_remaining = 0;
static uint32_t g_beep_gap_start = 0;
static bool g_beep_in_gap = false;
static uint16_t g_beep_len = kBeepMs;    // length of each beep of the running sequence
static uint16_t g_beep_gap = kBeepGapMs; // silence between them
static uint32_t g_alarm_at = 0;          // when the alarm sequence last started
static uint8_t g_alarm_count = 0;        // alarm sequences sounded for the current failure

static void start_beep_sequence(void) {
    g_beep_remaining = kBeepCount - 1; // first beep fires immediately below
    g_beep_in_gap = false;
    g_beep_len = kBeepMs;
    g_beep_gap = kBeepGapMs;
    buzz(g_beep_len);
}

// The long failure pattern (distinct from the 300 ms finish pattern).
static void start_alarm_sequence(void) {
    g_beep_remaining = kAlarmBeeps - 1;
    g_beep_in_gap = false;
    g_beep_len = kAlarmMs;
    g_beep_gap = kAlarmGapMs;
    g_alarm_at = g_uptime_ms;
    ++g_alarm_count;
    buzz(g_beep_len);
}

static void cancel_beep(void) {
    g_beep_remaining = 0;
    g_beep_in_gap = false;
    buzz(0);
}

static void tick_beep(void) {
    if (g_beep_in_gap) {
        if (g_uptime_ms - g_beep_gap_start >= g_beep_gap) {
            g_beep_in_gap = false;
            if (g_beep_remaining > 0) {
                --g_beep_remaining;
                buzz(g_beep_len);
            }
        }
        return;
    }
    if (g_beep_remaining > 0 && buzz_ticks == 0) {
        g_beep_in_gap = true;
        g_beep_gap_start = g_uptime_ms;
    } else if (g_beep_remaining == 0 && buzz_ticks == 0 && g_ctl.alarm() &&
               g_alarm_count < kAlarmRepeats && g_uptime_ms - g_alarm_at >= kAlarmEveryMs) {
        start_alarm_sequence(); // the failure is still on the display: sound it again
    }
}

// ---- LCD helpers ----

// True when a stop / pause toggle waits for the controller: the repaint then yields at once.
static bool stop_pending(void) {
    return (btn_pending(0) | btn_pending(1) | btn_pending(2)) != 0;
}

// The panel's output side for stpalpha::Panel (include/stpalpha/panel.h).
struct LcdIo {
    void move(uint8_t col, uint8_t row) {
        lcd_set_cursor(col, row);
    }
    void put(char c) {
        lcd_write_char(c);
        wdt_reset(); // a hang inside the character write still trips the watchdog
    }
    bool faulted() {
        return lcd_faulted();
    }
    bool interrupted() {
        return stop_pending();
    }
};

// What the panel is believed to show; draw() pumps a few cells of g_lcd into it every pass.
static stpalpha::Panel<LcdIo> g_panel;

// ---- Application loop ----

static stpalpha::SpeedMeter g_speed; // pulses/s over a 100 ms window, for GET/DUMP/STREAM
static LCDPacket g_lcd;
static uint32_t g_last_count = kNoCount;
static uint16_t g_last_errors = 0;
static uint8_t g_last_manual = 0;
static bool g_redraw = true;

// Take at most one pending toggle per button: the controller's per-pass input.
static void take_pending(Inputs& in) {
    in.pending = 0;
    in.down_after = 0;
    for (uint8_t idx = 0; idx < 4; idx++) {
        uint8_t b = static_cast<uint8_t>(1 << idx);
        cli();
        bool hit = btn_consume(b);
        // Level this toggle left the button at: current level, flipped once per later pending toggle.
        bool level = (btn_state & b) != 0;
        if (btn_pending(idx) & 1) {
            level = !level;
        }
        sei();
        if (hit &&
            level) { // press edge = one click; the release edge of a momentary button is dropped
            in.pending |= b;
            in.down_after |= b;
        }
    }
}

static bool g_driving = false; // the previous pass drove the relay

static void apply(const Outputs& o) {
    if (o.reset_count) {
        enc_clear_cut();
        reset_enc();
        g_last_count = kNoCount;
        g_last_errors = 0;
        g_speed.rebase(g_uptime_ms, 0); // a zeroed count is not motion
    }
    // Arm the ISR stop and drive the relay in ONE critical section: the 40 kHz ISR can
    // then never cut the relay between the count check and relay_on() (which would
    // re-energise it past the stop point), and enc_stop_at is never seen half-written.
    if (o.relay && !g_driving) {
        enc_clear_cut(); // a new drive period: forget the previous period's ISR cut
    }
    g_driving = o.relay;
    uint32_t stop_at = g_ctl.stop_at(); // armed after the reset, before the relay
    cli();
    enc_stop_at = stop_at;
    int32_t raw = enc_count;
    uint32_t mag = raw < 0 ? uint32_t{0} - static_cast<uint32_t>(raw) : static_cast<uint32_t>(raw);
    // Not again once the ISR has cut for this drive period: a one-step bounce back below
    // stop_at must not re-close the relay.
    if (o.relay && mag < stop_at && !enc_cut_pending()) {
        relay_on();
    } else {
        relay_off();
    }
    sei();
    set_leds(o.leds);
    if (o.beep == Beep::CANCEL) {
        cancel_beep();
    } else if (o.beep == Beep::START) {
        start_beep_sequence();
    } else if (o.beep == Beep::ALARM) {
        g_alarm_count = 0;
        start_alarm_sequence();
    }
    if (o.changed) {
        g_redraw = true;
    }
}

// ---- Host link (docs/hardware/serial-protocol.md) ----

static constexpr uint16_t kSettleMs = 500;
static constexpr uint16_t kStreamMs = 100;

// Persist the parameters a few bytes per pass, then read them back (DEC-009): an EEPROM byte
// that changes takes ~3.4 ms, so the whole 34 bytes (~115 ms) must not hold one pass.
static constexpr uint8_t kSaveBytesPerPass = 4;
static stpalpha::Stored g_save_img;
static uint8_t g_save_pos = 0;
static bool g_saving = false;

static void save_begin(void) {
    g_save_img = stpalpha::make_stored(g_link.params);
    g_save_pos = 0;
    g_saving = true;
}

// Write the next few bytes; when the image is complete send the SAVE reply ("OK", or "ERR io"
// if the read-back differs). Called once per pass while g_saving.
static void save_step(char* reply, size_t cap) {
    const uint8_t* img = reinterpret_cast<const uint8_t*>(&g_save_img);
    for (uint8_t n = 0; n < kSaveBytesPerPass && g_save_pos < sizeof(g_save_img); ++n) {
        eeprom_update_byte(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(g_save_pos)),
                           img[g_save_pos]);
        ++g_save_pos;
    }
    if (g_save_pos < sizeof(g_save_img)) {
        return;
    }
    g_saving = false;
    stpalpha::Stored back;
    eeprom_read_block(&back, nullptr, sizeof(back));
    if (memcmp(&g_save_img, &back, sizeof(back)) == 0) {
        STP_SNPRINTF(reply, cap, STP_FMT("OK"));
    } else {
        STP_SNPRINTF(reply, cap, STP_FMT("ERR io"));
    }
    uart_write_line(reply);
}

// Load the parameters; returns false (keeping the current ones) if none are valid.
static bool params_load(void) {
    stpalpha::Stored s;
    eeprom_read_block(&s, nullptr, sizeof(s));
    stpalpha::Params loaded;
    if (stpalpha::check_stored(s, &loaded) != stpalpha::LoadStatus::OK) {
        return false; // empty, a fw<=5 image, torn or invalid: keep the defaults / current values
    }
    g_link.params = loaded;
    g_link.sync(g_ctl);
    return true;
}

// Latch the 32-bit 1 kHz tick once per pass: every timer of the pass sees one consistent time,
// however long the pass was (DEC-009; the old 8-bit fold lost 256 ms per overflow).
static stpalpha::PassMeter g_pass; // longest loop pass, for DUMP `pass_max_ms`

static void tick_uptime(void) {
    g_uptime_ms = millis();
    g_pass.tick(g_uptime_ms);
}

// ---- Stack high-water mark (DEC-009) ----
// The free RAM between .bss and the stack is painted at boot; the longest run of untouched
// bytes from the bottom is the stack headroom ever left, reported as `stack_free=` in DUMP.

extern "C" uint8_t __bss_end; // first free byte above .bss (avr-libc linker script)
static constexpr uint8_t kStackPaint = 0xAA;
static constexpr uint8_t kPaintGuard = 32; // bytes below SP left alone (this function's frame)

static void paint_stack(void) {
    uint8_t* p = &__bss_end;
    uint8_t* top = reinterpret_cast<uint8_t*>(SP) - kPaintGuard;
    while (p < top) {
        *p++ = kStackPaint;
    }
}

static uint16_t stack_free(void) {
    const uint8_t* p = &__bss_end;
    const uint8_t* end = reinterpret_cast<const uint8_t*>(RAMEND);
    while (p < end && *p == kStackPaint) {
        ++p;
    }
    return static_cast<uint16_t>(p - &__bss_end);
}

static stpalpha::Snapshot snapshot(void) {
    stpalpha::Snapshot s{};
    cli();
    s.count = enc_count;
    s.speed = g_speed.pulses_per_s();
    s.errors = enc_errors;
    for (uint8_t i = 0; i < 4; ++i) {
        s.toggles[i] = btn_toggles[i];
    }
    sei();
    s.btn = btn_state;
    for (uint8_t i = 0; i < 4; ++i) {
        s.pending[i] = btn_pending(i);
    }
    s.enc_dir = enc_direction();
    s.uptime_ms = g_uptime_ms;
    s.stack_free = stack_free();
    s.pass_max_ms = g_pass.max_ms();
    s.isr_lat_max = enc_isr_lat_max();
    return s;
}

static char g_line[88];
// A pass that sent a long line (DUMP / EVT state, ~320 B = ~28 ms of UART) gives the LCD one cell-time
// less, so the pass stays inside its ~68 ms bound (DEC-010).
static constexpr size_t kLongLine = 200;
static bool g_heavy_tx = false;
static void note_tx(const char* line) {
    if (strlen(line) > kLongLine) {
        g_heavy_tx = true;
    }
}
// Largest reply is the DUMP line, ~320 B at its widest (tests/unit/test_hostlink.py pins it).
static char g_reply[352];

// Debug test-run bookkeeping: after a stop, report the settled count.
static bool g_settling = false;
static int32_t g_at_stop = 0;
static int32_t g_settle_last = 0;
static uint32_t g_settle_since = 0;
static uint32_t g_stream_at = 0;

// Last values the STREAM change detector saw (compared field by field, never XOR-folded).
static int32_t g_stream_count = 0;
static uint16_t g_stream_errors = 0;
static uint8_t g_stream_btn = 0;
static bool g_stream_relay = false;

// Emit the EVT lines for a debug RUN that just ended, with the count the ISR really cut at.
static void report_run_stop(const Outputs& o) {
    uint32_t at_stop = o.at_stop;
    uint32_t cut = 0;
    bool isr_cut = enc_take_cut(&cut);
    if (isr_cut) {
        at_stop = cut; // main's sample is late by a whole loop pass; the ISR latched the truth
    }
    bool target = isr_cut || at_stop >= g_ctl.eff_target();
    STP_SNPRINTF(g_reply, sizeof(g_reply), STP_FMT("EVT stop target=%lu at_stop=%lu reason=%s"),
                 static_cast<unsigned long>(g_ctl.target()), static_cast<unsigned long>(at_stop),
                 target ? "target" : "abort");
    uart_write_line(g_reply);
    g_settling = true;
    g_at_stop = static_cast<int32_t>(at_stop);
    g_settle_last = static_cast<int32_t>(read_enc());
    g_settle_since = g_uptime_ms;
}

// Out of line: keeps main()'s stack frame small (the frame limit test, DEC-009 item 9).
static __attribute__((noinline)) void host_link_poll(const Outputs& o) {
    if (g_saving) {
        save_step(g_reply, sizeof(g_reply)); // the host waits for the reply: no new line meanwhile
    } else if (uart_read_line(g_line, sizeof(g_line))) {
        bool was_running = g_ctl.running();
        g_link.handle(g_ctl, snapshot(), g_line, g_reply, sizeof(g_reply));
        // SAVE/LOAD run now, and their reply tells the truth about the outcome.
        const bool saving = (g_link.requests & stpalpha::REQ_SAVE) != 0;
        if (saving) {
            save_begin(); // the reply is sent when the last byte is verified
        }
        if ((g_link.requests & stpalpha::REQ_LOAD) && !params_load()) {
            STP_SNPRINTF(g_reply, sizeof(g_reply), STP_FMT("ERR no_data"));
        }
        if (g_link.requests & stpalpha::REQ_ZERO) {
            reset_enc();
            g_ctl.clear_result(); // a zeroed count would make the result screen read 0.0
            g_speed.rebase(g_uptime_ms, 0);
            g_pass.clear(); // ZERO also restarts the pass and ISR latency evidence
            enc_isr_lat_clear();
            g_last_count = kNoCount;
            g_settling = false; // a zeroed count would report a nonsense follow-through
        }
        if (!was_running && g_ctl.running()) {
            g_settling = false; // a new RUN supersedes the previous one's settle report
        }
        g_link.requests = stpalpha::REQ_NONE;
        if (!saving) {
            note_tx(g_reply);
            uart_write_line(g_reply);
        }
        g_redraw = true;
    }
    for (uint8_t n = uart_take_dropped(); n > 0; --n) {
        uart_write_line("ERR busy"); // a line arrived while the previous one was unread
    }
    if (o.run_stopped) {
        report_run_stop(o);
    }
    if (o.debug_exit != stpalpha::DebugExit::NONE) {
        STP_SNPRINTF(g_reply, sizeof(g_reply), STP_FMT("EVT debug_exit reason=%s"),
                     o.debug_exit == stpalpha::DebugExit::TIMEOUT ? "timeout" : "panel");
        uart_write_line(g_reply);
    }
    if (g_settling) {
        int32_t now = static_cast<int32_t>(read_enc());
        if (now != g_settle_last) {
            g_settle_last = now;
            g_settle_since = g_uptime_ms;
        } else if (g_uptime_ms - g_settle_since >= kSettleMs) {
            STP_SNPRINTF(g_reply, sizeof(g_reply), STP_FMT("EVT settled final=%ld follow=%ld"),
                         static_cast<long>(now), static_cast<long>(now - g_at_stop));
            uart_write_line(g_reply);
            g_settling = false;
        }
    }
    if (!g_ctl.debug()) {
        g_link.stream = false; // also covers a debug session that ended by itself
    }
    if (g_link.stream) {
        stpalpha::Snapshot s = snapshot();
        bool relay = g_ctl.relay();
        bool changed = s.count != g_stream_count || s.errors != g_stream_errors ||
                       s.btn != g_stream_btn || relay != g_stream_relay;
        if (changed || g_uptime_ms - g_stream_at >= kStreamMs) {
            g_stream_count = s.count;
            g_stream_errors = s.errors;
            g_stream_btn = s.btn;
            g_stream_relay = relay;
            g_stream_at = g_uptime_ms;
            g_link.dump(g_ctl, s, g_reply, sizeof(g_reply), "EVT state");
            note_tx(g_reply);
            uart_write_line(g_reply);
        }
    } else {
        g_stream_at = g_uptime_ms;
    }
}

// How soon to retry the LCD after a stuck-bus timeout.
static constexpr uint16_t kLcdRetryMs = 500;
static uint32_t g_lcd_retry_at = 0;

// Motor and relay switching can glitch the I2C lines or the panel's supply and leave the HD44780
// showing solid blocks or garbage. So the panel is (a) repainted at most 25 times a second, (b)
// re-initialised and fully repainted a moment after every relay edge and every 2 s whatever the
// state (so a corrupted character never lingers), and (c) only changed characters are sent.
//
// DEC-009: the LCD never blocks the loop for long. render_* only fill g_lcd; each pass pumps at
// most kPanelBudget cells of the difference into the panel (rows 1 and 2, percent and length,
// first), the re-initialisation gets a pass of its own, and a pending stop/pause toggle makes the
// repaint yield so the controller acts first.
static constexpr uint16_t kPaintMs = 40;
static constexpr uint16_t kRelaySettleMs = 30;
static constexpr uint16_t kHealMs = 2000;
static bool g_last_relay = false;
static bool g_heal_due = false;
static uint32_t g_heal_at = 0;
static uint32_t g_last_heal = 0;
static uint32_t g_last_paint = 0;

// Heal phase: 0 = none, 1 = first half of the HD44780 init done, second half due next pass.
static uint8_t g_heal_phase = 0;

// Start the HD44780 init (the first half takes a pass of its own, ~21 ms; the second half the next).
static void heal_panel(void) {
    lcd_recover_begin();
    g_heal_phase = 1;
    g_last_heal = g_uptime_ms;
}

static __attribute__((noinline)) void draw(uint32_t count) {
    bool healed = false; // an init half takes the whole pass: no characters are pumped in it
    if (lcd_faulted()) {
        g_heal_phase = 0;
        if (g_uptime_ms - g_lcd_retry_at < kLcdRetryMs) {
            return; // the bus is stuck: leave the panel alone, the loop must keep running
        }
        heal_panel(); // a timeout may have cut a byte in half: resynchronise the panel too
        healed = true;
    } else if (g_heal_phase == 1) {
        lcd_recover_finish();
        g_heal_phase = 0;
        g_panel.invalidate(); // the panel may hold anything now: rewrite every character
        g_redraw = true;
        healed = true;
    }
    bool relay = g_ctl.relay();
    if (relay != g_last_relay) {
        g_last_relay = relay;
        g_heal_due = true;
        g_heal_at = g_uptime_ms + kRelaySettleMs; // let the switching transient die first
    } else if (!g_heal_due && g_uptime_ms - g_last_heal >= kHealMs) {
        g_heal_due = true;
        g_heal_at = g_uptime_ms;
    }
    if (!healed && g_heal_due && static_cast<int32_t>(g_uptime_ms - g_heal_at) >= 0 &&
        !stop_pending()) {
        g_heal_due = false;
        heal_panel();
        healed = true;
    }
    bool paint_ok = g_redraw || g_uptime_ms - g_last_paint >= kPaintMs;
    MachineState st = g_ctl.state();
    uint16_t errors = read_enc_errors();
    uint8_t manual = g_ctl.manual() ? stpalpha::kManualOn : stpalpha::kManualOff;
    const uint32_t wheel = g_link.params.wheel_um;
    const uint32_t cpr = g_link.params.cpr;
    if (g_ctl.debug()) {
        if (g_redraw) {
            stpalpha::render_debug(g_lcd);
        }
    } else if (st == MachineState::IDLE && g_ctl.result() != stpalpha::Result::NONE) {
        if (g_redraw || (paint_ok && (count != g_last_count || errors != g_last_errors))) {
            stpalpha::render_result(g_lcd, count, g_ctl.target(),
                                    g_ctl.result() == stpalpha::Result::SETTLING, errors, wheel,
                                    cpr, g_ctl.outcome(), g_ctl.goal());
            g_last_count = count;
            g_last_errors = errors;
        }
    } else if (st == MachineState::IDLE) {
        if (g_redraw || manual != g_last_manual || errors != g_last_errors) {
            stpalpha::render_idle(g_lcd, manual, errors);
            g_last_manual = manual;
            g_last_errors = errors;
        }
    } else if (g_redraw || manual != g_last_manual || errors != g_last_errors ||
               (paint_ok && count != g_last_count)) {
        stpalpha::render_winding(g_lcd, count, g_ctl.target(), st == MachineState::PAUSED, manual,
                                 errors, wheel, cpr, st == MachineState::TRIM);
        g_last_count = count;
        g_last_errors = errors;
        g_last_manual = manual;
    }
    if (g_redraw || paint_ok) {
        g_last_paint = g_uptime_ms;
    }
    g_redraw = false;
    const bool heavy = g_heavy_tx;
    g_heavy_tx = false;
    if (!healed && g_heal_phase == 0 && !lcd_faulted()) {
        LcdIo io;
        (void)g_panel.pump(g_lcd, io,
                           heavy ? static_cast<uint8_t>(stpalpha::kPanelBudget - 1)
                                 : stpalpha::kPanelBudget);
    }
    if (lcd_faulted()) {
        g_redraw = true; // repaint everything once the bus answers again
        g_lcd_retry_at = g_uptime_ms;
        if (g_ctl.debug()) {
            uart_write_line("EVT lcd_fault");
        }
    }
}

// Out of line on purpose: the trim logic inlined into main() would grow its stack frame (the frame
// limit test, DEC-009 item 9).
static __attribute__((noinline)) Outputs controller_step(const Inputs& in) {
    return g_ctl.step(in);
}

// ---- Entry point ----

// docs/runbooks/hardware-tests.md P1, simulator evidence tests/system/test_firmware.py"
// frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0302
int main(void) {
    wdt_enable(kWatchdogTimeout);
    paint_stack();
    lcd_init();
    lcd_clear();

    // Latching buttons hold their last physical state across power cycles.
    // Wait past the 16 ms debounce window (plus margin) so the ISR has
    // settled on every button, then discard the startup events.
    uint32_t settle_start = millis();
    while (millis() - settle_start < 40) {
        wdt_reset();
    }
    btn_flush();
    uart_init();
    (void)params_load();
    g_link.sync(g_ctl);
    sei();

    for (;;) {
        wdt_reset(); // one kick per pass: a hang anywhere below resets the MCU, relay low
        tick_uptime();
        g_speed.update(g_uptime_ms, read_enc_raw());
        tick_beep();
        Inputs in{};
        take_pending(in);
        in.count = read_enc();
        in.cut = enc_cut_pending();
        in.now_ms = g_uptime_ms;
        Outputs o = controller_step(in);
        apply(o);
        host_link_poll(o);
        draw(in.count);
    }
}
