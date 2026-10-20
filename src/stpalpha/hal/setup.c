/* frob:waive PARSE002 reason="ISR(vector) macro handlers trip the pure-Python tree-sitter path -- the native parser reads the file fully" */
/*
 * Board HAL for the Sandkey paper winder (ATmega32U4 Pro Micro).
 *
 * This file is the C port of the hand-written startup.asm from the previous
 * repository. avr-libc's crt now owns the vector table, stack pointer, .data
 * copy and .bss clear; the two timer ISRs are bound by name with ISR(), the
 * unhandled-vector fallback is ISR(BADISR_vect), and the peripheral setup runs
 * from .init8 (after .data/.bss init, before main) exactly where the old
 * _start called it. Datasheet: ATmega16U4/32U4 (doc7766), sections 8, 9, 13,
 * 14, 21.
 */
#include <stpalpha/hal/setup.h>

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/atomic.h>

/* ---- Pin map (Pro Micro silkscreen -> port bit) ---- */

#define PANIC_BIT 1 /* D15 = PB1, fault LED (active-high) */

#define I2C_SDA_BIT 1 /* D2 = PD1 (TWI takes over once enabled) */
#define I2C_SCL_BIT 0 /* D3 = PD0 */

/* LN11-ERGA 600P/R encoder. Neither pin has external-interrupt capability
 * on the 32U4, so TIMER1_COMPA samples them at 40 kHz. At the machine's
 * 90 RPM ceiling (2400 counts/rev) a state lasts ~278 us, i.e. ~11 samples,
 * and the poll preempts every other ISR, so a state cannot be skipped. */
#define ENC_BIT_A 4 /* D4 = PD4 */
#define ENC_BIT_B 6 /* D5 = PC6 */

/* 4 buttons with external pull-downs (active-high when pressed). */
#define BTN_BIT_1 5 /* D9 = PB5 */
#define BTN_BIT_2 4 /* D8 = PB4 */
#define BTN_BIT_3 6 /* D7 = PE6 */
#define BTN_BIT_4 7 /* D6 = PD7 */

/* Status LEDs D18..D21 = PF7..PF4 (LED_1 = PF4, LED_4 = PF7). */
#define LED_MASK 0xF0

#define BUZZ_BIT 3   /* D14 = PB3, active buzzer */
#define BRIGHT_BIT 2 /* D16 = PB2, LCD brightness */
#define RELAY_BIT 6  /* D10 = PB6, relay coil (active-high) */

/* ---- Timer constants (F_CPU = 16 MHz) ---- */

#define TIMER0_TOP 249 /* CTC, /64 prescaler: 16e6 / 64 / 250 = 1 kHz */
#define TIMER1_TOP 399 /* CTC, no prescaler: 16e6 / 400 = 40 kHz */

/* ---- Shared state (declared in setup.h) ---- */

volatile uint8_t timer0_ticks;
volatile uint8_t btn_state;
volatile uint8_t btn_events;
volatile int32_t enc_count;
volatile uint16_t enc_errors;
volatile uint8_t enc_isr_count;
volatile uint16_t buzz_ticks;

/* Shift-register debounce buffers (16 samples per button, 1 ms each). The
 * panel buttons are latching with long, slow travel, so the contact needs a
 * wide window to settle. */
static uint16_t g_btn_sr[4];
/* Previous debounced snapshot used to detect edges for btn_events. */
static uint8_t g_btn_prev_state;
/* Previous encoder sample (bit0 = A, bit1 = B). */
static uint8_t g_enc_prev;
/* Last valid direction (+1 / -1), used to resolve a two-bit jump. */
static int8_t g_enc_dir = 1;

/* Quadrature transition table. Index = (prev << 2) | cur, state = (B<<1)|A.
 * 0 = no change, +1 / -1 = one step, 2 = both lines changed between samples
 * (a missed state: counted as two steps in the last direction). Kept in
 * flash. */
static const int8_t kEncTable[16] PROGMEM = {
    0,  1,  -1, 2,  /* prev=00 */
    -1, 0,  2,  1,  /* prev=01 */
    1,  2,  0,  -1, /* prev=10 */
    2,  -1, 1,  0,  /* prev=11 */
};

/* ---- ISRs ---- */

/* Unhandled vector: light the fault LED and keep running so the LED acts as
 * a persistent "something unexpected fired" indicator. */
// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P6"
// frob:doc docs/spec/L5-component-design/SUB-01-firmware-hal.md#comp-0104
ISR(BADISR_vect) {
    DDRB |= (1 << PANIC_BIT);
    PORTB |= (1 << PANIC_BIT);
}

/* 40 kHz encoder poll: the highest-priority work in the firmware. */
// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P2"
// frob:doc docs/spec/L5-component-design/SUB-01-firmware-hal.md#comp-0103
ISR(TIMER1_COMPA_vect) {
    enc_isr_count++;

    uint8_t cur = 0;
    if (PIND & (1 << ENC_BIT_A)) {
        cur |= 1;
    }
    if (PINC & (1 << ENC_BIT_B)) {
        cur |= 2;
    }

    uint8_t index = (uint8_t)((g_enc_prev << 2) | cur);
    g_enc_prev = cur;

    int8_t step = (int8_t)pgm_read_byte(&kEncTable[index]);
    if (step == 0) {
        return;
    }
    if (step == 2) {
        /* A state was skipped: keep the length honest and make it visible. */
        enc_errors++;
        step = (int8_t)(g_enc_dir * 2);
    } else {
        g_enc_dir = step;
    }
    enc_count += step;
}

/* One button's 16-sample shift register: 16 ms of 1s sets the debounced bit,
 * 16 ms of 0s clears it, anything else leaves it alone. */
static inline void debounce_one(uint8_t idx, uint8_t pressed) {
    uint16_t sr = (uint16_t)(g_btn_sr[idx] << 1);
    if (pressed) {
        sr |= 1;
    }
    g_btn_sr[idx] = sr;
    if (sr == 0x0000) {
        btn_state &= (uint8_t) ~(1 << idx);
    } else if (sr == 0xFFFF) {
        btn_state |= (uint8_t)(1 << idx);
    }
}

/* 1 kHz tick: button debounce, event latch, buzzer countdown. ISR_NOBLOCK
 * re-enables interrupts on entry so the 40 kHz encoder poll can preempt it:
 * nothing here may delay an encoder sample. Timer0 cannot re-enter itself
 * (its body is far shorter than 1 ms). */
// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P6"
// frob:doc docs/spec/L5-component-design/SUB-01-firmware-hal.md#comp-0102
ISR(TIMER0_COMPA_vect, ISR_NOBLOCK) {
    timer0_ticks++;

    debounce_one(0, PINB & (1 << BTN_BIT_1));
    debounce_one(1, PINB & (1 << BTN_BIT_2));
    debounce_one(2, PINE & (1 << BTN_BIT_3));
    debounce_one(3, PIND & (1 << BTN_BIT_4));

    /* Latch every changed bit (either edge) until btn_consume() clears it,
     * so a press is never lost even if the main loop is slower than 1 kHz. */
    uint8_t state = btn_state;
    btn_events |= (uint8_t)(g_btn_prev_state ^ state);
    g_btn_prev_state = state;

    if (buzz_ticks != 0) {
        buzz_ticks--;
        if (buzz_ticks == 0) {
            PORTB &= (uint8_t) ~(1 << BUZZ_BIT);
        }
    }
}

/* ---- Public API ---- */

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P5"
// frob:doc docs/spec/L5-component-design/SUB-01-firmware-hal.md#comp-0105
void set_leds(uint8_t nibble) {
    uint8_t hi = (uint8_t)((uint8_t)(nibble << 4) & LED_MASK);
    PORTF = (uint8_t)((PORTF & (uint8_t)~LED_MASK) | hi);
}

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P1"
// frob:doc docs/spec/L5-component-design/SUB-01-firmware-hal.md#comp-0105
void relay_on(void) {
    PORTB |= (1 << RELAY_BIT);
}

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P1"
// frob:doc docs/spec/L5-component-design/SUB-01-firmware-hal.md#comp-0105
void relay_off(void) {
    PORTB &= (uint8_t) ~(1 << RELAY_BIT);
}

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P3"
// frob:doc docs/spec/L5-component-design/SUB-01-firmware-hal.md#comp-0105
void buzz(uint16_t ms) {
    if (ms == 0) {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            buzz_ticks = 0;
        }
        PORTB &= (uint8_t) ~(1 << BUZZ_BIT);
        return;
    }
    /* 16-bit store must not be torn by the 1 kHz ISR's decrement. */
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        buzz_ticks = ms;
    }
    PORTB |= (1 << BUZZ_BIT);
}

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P6"
// frob:doc docs/spec/L5-component-design/SUB-01-firmware-hal.md#comp-0105
bool btn_consume(uint8_t bit) {
    cli();
    uint8_t hit = (uint8_t)(btn_events & bit);
    btn_events &= (uint8_t)~bit;
    sei();
    return hit != 0;
}

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P6"
// frob:doc docs/spec/L5-component-design/SUB-01-firmware-hal.md#comp-0104
void panic(void) {
    cli();
    DDRB |= (1 << PANIC_BIT);
    PORTB |= (1 << PANIC_BIT);
    for (;;) {
    }
}

/* ---- Boot ---- */

// docs/runbooks/hardware-tests.md P6, simulator evidence tests/system/test_firmware.py"
// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P6"
// frob:doc docs/spec/L5-component-design/SUB-01-firmware-hal.md#comp-0101
void hal_init(void) {
    /* Caterina leaves IVSEL=1 (vectors in the boot section) and does not
     * clear it before jumping to the application; until cleared every
     * interrupt lands in the bootloader. Datasheet 10.1: write IVCE, then the
     * new MCUCR within 4 cycles. */
    MCUCR = (1 << IVCE);
    MCUCR = 0;

    /* Caterina also leaves the USB controller running; its interrupts fire
     * so often they starve Timer0. */
    USBCON = 0;

    /* I2C pins: inputs, pull-ups off (TWI peripheral takes over). */
    DDRD &= (uint8_t) ~((1 << I2C_SDA_BIT) | (1 << I2C_SCL_BIT));
    PORTD &= (uint8_t) ~((1 << I2C_SDA_BIT) | (1 << I2C_SCL_BIT));

    /* Encoder inputs with pull-ups so the lines are stable when unplugged. */
    DDRD &= (uint8_t) ~(1 << ENC_BIT_A);
    PORTD |= (1 << ENC_BIT_A);
    DDRC &= (uint8_t) ~(1 << ENC_BIT_B);
    PORTC |= (1 << ENC_BIT_B);

    /* Buttons: inputs, no pull-ups (external pull-downs, active-high). */
    DDRB &= (uint8_t) ~((1 << BTN_BIT_1) | (1 << BTN_BIT_2));
    PORTB &= (uint8_t) ~((1 << BTN_BIT_1) | (1 << BTN_BIT_2));
    DDRE &= (uint8_t) ~(1 << BTN_BIT_3);
    PORTE &= (uint8_t) ~(1 << BTN_BIT_3);
    DDRD &= (uint8_t) ~(1 << BTN_BIT_4);
    PORTD &= (uint8_t) ~(1 << BTN_BIT_4);

    /* Status LEDs: outputs, off. */
    DDRF |= LED_MASK;
    PORTF &= (uint8_t)~LED_MASK;

    /* Fault LED, buzzer, brightness, relay: outputs, off. */
    DDRB |= (1 << PANIC_BIT) | (1 << BUZZ_BIT) | (1 << BRIGHT_BIT) | (1 << RELAY_BIT);
    PORTB &= (uint8_t) ~((1 << PANIC_BIT) | (1 << BUZZ_BIT) | (1 << BRIGHT_BIT) | (1 << RELAY_BIT));

    /* Timer0: CTC, /64, TOP=249 -> 1 kHz COMPA. */
    TCCR0A = (1 << WGM01);
    OCR0A = TIMER0_TOP;
    TCCR0B = (1 << CS01) | (1 << CS00);
    TIMSK0 = (1 << OCIE0A);

    /* Timer1: CTC, no prescaler, TOP=399 -> 40 kHz COMPA. */
    TCCR1A = 0;
    TCCR1B = (1 << WGM12) | (1 << CS10);
    OCR1A = TIMER1_TOP;
    TIMSK1 = (1 << OCIE1A);

    sei();
}

/* .init8 runs after .data/.bss initialisation (.init4) and before main
 * (.init9). A naked function placed there must not return -- it is spliced
 * into the startup sequence -- so it only calls the real init. */
static void hal_init_hook(void) __attribute__((naked, used, section(".init8")));
static void hal_init_hook(void) {
    __asm__ volatile("call hal_init" ::: "memory");
}
