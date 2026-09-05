/*
 * test_encoder -- Diagnostic display for quadrature encoder + INT2/INT3 config.
 *
 * Row 0: Timer1 config registers (expected values in parens)
 *   TCCR1B should be 0x09  (CTC WGM12=1, no prescaler CS10=1)
 *   TIMSK1 should be 0x02  (OCIE1A enabled)
 *
 * Row 1: Raw pin levels + ISR fire counter
 *   ENA = PD4 (D4): should toggle as encoder moves
 *   ENB = PC6 (D5): should toggle as encoder moves
 *   ISR = enc_isr_count: wrapping counter incremented at 10 kHz.
 *     Should increment rapidly even with no encoder movement.
 *     If ISR stays 0 -> Timer1 not running or vector wrong.
 *
 * Row 2: Encoder count and estimated revolutions (2400 counts/rev)
 *
 * Row 3: BTN_1 resets count to zero.
 *
 * Diagnosis guide:
 *   TIMSK1=0x00 -> Timer1 interrupt not enabled
 *   TCCR1B=0x00 -> Timer1 not clocked
 *   ISR=0       -> Timer1 ISR not running; check __vector_17
 *   ISR counts but ENA/ENB stuck -> encoder not connected or no power
 *   ISR counts, pins toggle, COUNT=0 -> quadrature table bug
 */

#include <stpalpha/hal/lcd.h>
#include <stpalpha/hal/setup.h>

#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdio.h>

static constexpr float kCountsPerRev = 2400.0f;

static uint32_t read_enc_atomic(void) {
    cli();
    uint32_t v = enc_count;
    sei();
    return v;
}

int main(void) {
    sei();
    lcd_init();
    lcd_clear();

    uint8_t last_isr = 0;
    uint32_t last_count = 0xFFFFFFFFUL;

    for (;;) {
        char buf[21];

        // Row 0: Timer1 config
        uint8_t tccr1b = TCCR1B;
        uint8_t timsk1 = TIMSK1;
        snprintf(buf, sizeof(buf), "1B=%02X(09) MK1=%02X(02)", tccr1b, timsk1);
        buf[20] = '\0';
        lcd_set_cursor(0, 0);
        lcd_write_str(buf);

        // Row 1: raw pin levels + ISR fire count
        uint8_t ena_raw = (PIND >> 4) & 1; // PD4 (D4)
        uint8_t enb_raw = (PINC >> 6) & 1; // PC6 (D5)
        uint8_t isr_cnt = enc_isr_count;
        snprintf(buf, sizeof(buf), "ENA=%c ENB=%c ISR=%3u  ", ena_raw ? 'H' : 'L',
                 enb_raw ? 'H' : 'L', isr_cnt);
        buf[20] = '\0';
        lcd_set_cursor(0, 1);
        lcd_write_str(buf);

        // Row 2: count + revolutions
        uint32_t count = read_enc_atomic();
        if (count != last_count || isr_cnt != last_isr) {
            float revs = (float)count / kCountsPerRev;
            uint32_t rev_int = (uint32_t)revs;
            uint16_t rev_frac = (uint16_t)((revs - (float)rev_int) * 100.0f);
            snprintf(buf, sizeof(buf), "CNT=%-8lu %3lu.%02u R ", count, rev_int, rev_frac);
            buf[20] = '\0';
            lcd_set_cursor(0, 2);
            lcd_write_str(buf);
            last_count = count;
            last_isr = isr_cnt;
        }

        // Row 3: reset button
        lcd_set_cursor(0, 3);
        lcd_write_str("  [BTN1] RESET      ");

        if (btn_consume(1 << 0)) {
            cli();
            enc_count = 0;
            sei();
            last_count = 0xFFFFFFFFUL;
        }
    }
}
