#pragma once
// frob:waive PARSE002 reason="extern C guard is C++ syntax inside a C header -- tree-sitter-c salvages around it and every declaration still parses"
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

// Wrapping 8-bit counter incremented every TIMER0_COMPA ISR call (~1 kHz).
// If this value is not changing, the ISR is not running.
extern volatile uint8_t timer0_ticks;

// Debounced button state. Bits 3:0 = BTN_4:BTN_1. 1 = pressed.
// Updated every 1 ms by TIMER0_COMPA ISR. Single-byte; reads are atomic.
extern volatile uint8_t btn_state;

// Button event latch. Each bit mirrors btn_state but is set on any edge
// (press or release) and held until cleared. Use btn_consume() to read.
extern volatile uint8_t btn_events;

// Atomically read and clear one bit of btn_events.
// Returns true once per button state change; subsequent calls return false
// until the button changes again. Main-loop only: it re-enables interrupts.
bool btn_consume(uint8_t bit);

// Encoder count. Incremented CW, decremented CCW, saturating at zero.
// 4x quadrature on 600 PPR encoder: 2400 counts/revolution.
// 32-bit; read atomically with cli/sei (see read_enc() in main.cpp).
extern volatile uint32_t enc_count;

// Wrapping 8-bit counter incremented inside the encoder ISR on every sample.
// If this stays 0 while the encoder spins, TIMER1_COMPA is not firing.
extern volatile uint8_t enc_isr_count;

// Active buzzer countdown (milliseconds remaining). 0 = buzzer off.
// Written by buzz(); decremented each tick by TIMER0_COMPA ISR.
extern volatile uint16_t buzz_ticks;

// Write lower 4 bits to status LEDs PF4-PF7.
// Bit 0 = LED_1 (D18/PF4), bit 3 = LED_4 (D21/PF7).
void set_leds(uint8_t nibble);

// Energize or de-energize the relay coil on PB6.
void relay_on(void);
void relay_off(void);

// Activate the active buzzer on PB3 for the given number of milliseconds.
// The TIMER0_COMPA ISR counts down and turns off the buzzer when it reaches
// zero. Pass 0 to stop an in-progress beep immediately.
void buzz(uint16_t ms);

// Assert fault LED on D15 (PB1) and halt. Never returns.
__attribute__((noreturn)) void panic(void);

// Peripheral bring-up: vector relocation, USB off, pin directions, Timer0 at
// 1 kHz, Timer1 at 10 kHz, then sei(). Runs automatically from .init8 before
// main(); exposed only so the boot sequence is documented and testable.
void hal_init(void);

#ifdef __cplusplus
}
#endif
