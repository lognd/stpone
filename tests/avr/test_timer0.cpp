/*
 * test_timer0 -- Diagnose Timer0 / button ISR failure.
 *
 * Row 0: Timer0 config registers (expected values shown in parens)
 *   TCCR0B should be 0x03 (CTC, prescaler 64)
 *   TIMSK0 should be 0x02 (OCIE0A enabled)
 *
 * Row 1: Runtime evidence the timer is actually counting
 *   TCNT0  -- live counter value (should cycle 0-249 at 1 kHz)
 *   TIFR0  -- bit 1 = OCF0A flag; if ISR is running, usually 0x00
 *             if ISR is NOT running, bit 1 stays 1 (flag never cleared)
 *
 * Row 2: ISR execution counter
 *   timer0_ticks -- incremented inside the ISR each call (~1 kHz).
 *   If this NUMBER IS NOT CHANGING, the ISR body is never executing.
 *   If it IS changing, the ISR runs but the debounce logic is suspect.
 *
 * Row 3: Raw pin reads and debounced btn_state
 *   D6(PD7)=BTN1 D7(PE6)=BTN2 D8(PB4)=BTN3 D9(PB5)=BTN4
 *
 * Diagnosis guide:
 *   TIMSK0=0x00 -> interrupt not enabled; Timer0 setup failed
 *   TCNT0 stuck 0 and TIFR0=0x00 -> Timer0 not clocked (TCCR0B=0?)
 *   TCNT0 cycling but TIFR0 stuck 0x02 -> ISR not registered (vector wrong)
 *   timer0_ticks not changing -> ISR body not reached despite flag being set
 *   timer0_ticks changing, btn_state=0 -> debounce logic or pin read bug
 */

#include <stpalpha/hal/lcd.h>
#include <stpalpha/hal/setup.h>

#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdio.h>
#include <util/delay.h>

int main(void) {
    sei();
    lcd_init();
    lcd_clear();

    uint8_t last_ticks = 0;

    for (;;) {
        char buf[21];

        // Row 0: Timer0 config + MCUCR (IVSEL must be 0)
        uint8_t tccr0b = TCCR0B;
        uint8_t timsk0 = TIMSK0;
        uint8_t mcucr = MCUCR;
        snprintf(buf, sizeof(buf), "0B=%02X MK=%02X MC=%02X  ", tccr0b, timsk0, mcucr);
        buf[20] = '\0';
        lcd_set_cursor(0, 0);
        lcd_write_str(buf);

        // Row 1: live counter and interrupt flag
        uint8_t tcnt0 = TCNT0;
        uint8_t tifr0 = TIFR0;
        snprintf(buf, sizeof(buf), "TCNT0=%3u TIFR0=%02X  ", tcnt0, tifr0);
        buf[20] = '\0';
        lcd_set_cursor(0, 1);
        lcd_write_str(buf);

        // Row 2: ISR execution counter (changes at ~1 kHz if ISR runs)
        uint8_t ticks = timer0_ticks;
        snprintf(buf, sizeof(buf), "ISR_TICKS=%3u %s     ", ticks,
                 (ticks != last_ticks) ? "RUN" : "---");
        buf[20] = '\0';
        last_ticks = ticks;
        lcd_set_cursor(0, 2);
        lcd_write_str(buf);

        // Row 3: raw pins (BTN1=D6/PD7 .. BTN4=D9/PB5) + btn_state
        uint8_t d6 = (PIND >> 7) & 1;
        uint8_t d7 = (PINE >> 6) & 1;
        uint8_t d8 = (PINB >> 4) & 1;
        uint8_t d9 = (PINB >> 5) & 1;
        snprintf(buf, sizeof(buf), "%c%c%c%c btn=0x%02X       ", d6 ? 'H' : 'L', d7 ? 'H' : 'L',
                 d8 ? 'H' : 'L', d9 ? 'H' : 'L', btn_state);
        buf[20] = '\0';
        lcd_set_cursor(0, 3);
        lcd_write_str(buf);

        _delay_ms(100);
    }
}
