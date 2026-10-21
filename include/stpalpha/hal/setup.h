#pragma once
// frob:waive PARSE002 reason="extern C guard is C++ syntax inside a C header -- tree-sitter-c \
// salvages around it and every declaration still parses"

// Board HAL for the Sandkey paper winder (ATmega32U4 Pro Micro).
//
// Everything here is initialised automatically before main() by hal_init(),
// which runs from avr-libc's .init8 section; the timers are already ticking
// and interrupts are already enabled when main() starts. The ISRs that feed
// the shared state below are bound with avr-libc's ISR() macro in setup.c.
//
// Pin map (Pro Micro silkscreen -> port bit): see docs/hardware/pinout.md.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Wrapping 32-bit millisecond counter incremented every TIMER0_COMPA ISR call (1 kHz; wraps
// after 49 days). If this value is not changing, the ISR is not running. It is 32 bits wide so
// a long main-loop pass can never lose time (DEC-009); read it with millis(), not directly.
extern volatile uint32_t timer0_ticks;

// Milliseconds since boot (wrapping), read atomically; subtract two readings with unsigned
// arithmetic to get an elapsed time. Main-loop use.
uint32_t millis(void);

// Debounced button state. Bits 3:0 = BTN_4:BTN_1. 1 = pressed.
// Updated every 1 ms by TIMER0_COMPA ISR (16 ms debounce window). Single-byte;
// reads are atomic. The panel buttons are latching: the level toggles on each
// push, so act on toggles (btn_consume) and use the level only for "held".
extern volatile uint8_t btn_state;

// Per-button count of debounced changes (either edge = one push of a latching
// button), index 0..3 = BTN1..BTN4. Wraps at 256; written only by TIMER0_COMPA.
extern volatile uint8_t btn_toggles[4];

// Take ONE pending toggle of the button(s) in `bit` (a single-bit mask).
// Returns true once per push, however many pushes queued while the caller was
// busy; none are merged or lost. Main-loop only; takes no interrupt lock.
bool btn_consume(uint8_t bit);

// Discard every pending toggle (used once at boot, after the debounce settles).
void btn_flush(void);

// Encoder count. Incremented CW, decremented CCW; signed so either mounting
// direction works (consumers use the magnitude). 4x quadrature on a 600 PPR
// encoder: 2400 counts/revolution. 32-bit; read atomically with cli/sei (see
// read_enc() in main.cpp). Sampled at 40 kHz by TIMER1_COMPA.
extern volatile int32_t enc_count;

// Number of two-bit (skipped-state) transitions seen by the encoder poll. Each
// is corrected by two steps in the last direction; any non-zero value means a
// sample was missed and is shown on the display. Wraps at 65536.
extern volatile uint16_t enc_errors;

// |count| at which the encoder ISR drops the relay itself (0xFFFFFFFF = never).
extern volatile uint32_t enc_stop_at;

// Wrapping 8-bit counter incremented inside the encoder ISR on every sample.
// If this stays 0 while the encoder spins, TIMER1_COMPA is not firing.
extern volatile uint8_t enc_isr_count;

// Worst Timer1 entry latency since the last clear, in CPU cycles (TCNT1 read as the ISR starts;
// includes the ~39 cycle response, vector and prologue). The poll period is 400 cycles, so < 400 means no sample was
// ever lost to a late entry. Atomic read; clear with enc_isr_lat_clear().
uint16_t enc_isr_lat_max(void);
void enc_isr_lat_clear(void);

// Active buzzer countdown (milliseconds remaining). 0 = buzzer off.
// Written by buzz(); decremented each tick by TIMER0_COMPA ISR.
extern volatile uint16_t buzz_ticks;

// Write lower 4 bits to status LEDs PF4-PF7.
// Bit 0 = LED_1 (D18/PF4), bit 3 = LED_4 (D21/PF7).
void set_leds(uint8_t nibble);

// Energize or de-energize the relay coil on PB6.
void relay_on(void);
void relay_off(void);

// If the encoder ISR dropped the relay since the last relay_on(), store the
// |count| at which it did (the true stop point, not main's late sample) in
// *count, clear the record, and return true. Atomic; main-loop use.
bool enc_take_cut(uint32_t* count);

// True while the ISR cut record of the current drive period is set (the relay was dropped at
// the stop count and not yet re-armed); apply() must not re-energise the relay then.
bool enc_cut_pending(void);

// Forget the ISR cut record (a new drive period or a zeroed count starts).
void enc_clear_cut(void);

// Activate the active buzzer on PB3 for the given number of milliseconds.
// The TIMER0_COMPA ISR counts down and turns off the buzzer when it reaches
// zero. Pass 0 to stop an in-progress beep immediately.
void buzz(uint16_t ms);

// Assert fault LED on D15 (PB1) and halt. Never returns.
__attribute__((noreturn)) void panic(void);

// Peripheral bring-up: vector relocation, USB off, pin directions, Timer0 at
// 1 kHz, Timer1 at 40 kHz, then sei(). Runs automatically from .init8 before
// main(); exposed only so the boot sequence is documented and testable.
void hal_init(void);

// Last decoded encoder direction (+1 / -1), for diagnostics.
int8_t enc_direction(void);

// Toggles of button idx (0..3) counted by the ISR but not yet taken by btn_consume.
uint8_t btn_pending(uint8_t idx);

#ifdef __cplusplus
}
#endif
