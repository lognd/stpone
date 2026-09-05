/*
 * test_buzzer -- Test active buzzer (CMI-1295-0585T on D14/PB3).
 *
 * Row 0: Live buzz countdown (buzz_ticks) and PB3 pin level.
 *   If buzz_ticks counts down to 0 but PB3 stays H, the cbi is not executing.
 *   If buzz_ticks stays nonzero, the Timer0 ISR is not decrementing it.
 *
 * Row 1: btn_state (raw) so you can see which buttons are active.
 *
 * Row 2: blank
 *
 * Row 3: Button assignments
 *   BTN_1: 200 ms beep
 *   BTN_2: 500 ms beep
 *   BTN_3: 1000 ms beep
 *   BTN_4: stop immediately
 *
 * Status LED mirrors which beep was last triggered.
 */

#include <stpalpha/hal/lcd.h>
#include <stpalpha/hal/setup.h>

#include <avr/io.h>
#include <stdio.h>
#include <util/delay.h>

int main(void) {
    lcd_init();
    lcd_clear();

    lcd_set_cursor(0, 2);
    lcd_write_str("                    ");
    lcd_set_cursor(0, 3);
    lcd_write_str("1:200 2:500 3:1s 4:0");

    for (;;) {
        if (btn_consume(1 << 0)) {
            set_leds(0b0001);
            buzz(200);
        } else if (btn_consume(1 << 1)) {
            set_leds(0b0010);
            buzz(500);
        } else if (btn_consume(1 << 2)) {
            set_leds(0b0100);
            buzz(1000);
        } else if (btn_consume(1 << 3)) {
            set_leds(0b0000);
            buzz(0);
        }

        char buf[21];

        // Row 0: countdown and PB3 level
        uint16_t ticks = buzz_ticks;
        uint8_t pb3 = (PINB >> 3) & 1;
        snprintf(buf, sizeof(buf), "TICKS=%-5u PB3=%c   ", ticks, pb3 ? 'H' : 'L');
        buf[20] = '\0';
        lcd_set_cursor(0, 0);
        lcd_write_str(buf);

        // Row 1: raw btn_state
        snprintf(buf, sizeof(buf), "btn=0x%02X            ", btn_state);
        buf[20] = '\0';
        lcd_set_cursor(0, 1);
        lcd_write_str(buf);

        _delay_ms(20);
    }
}
