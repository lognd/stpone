/*
 * test_leds_pins -- LED sequence test + live raw pin reads on LCD.
 *
 * Phase 1 (startup, ~3 s): Chase each LED individually then flash all four.
 *   This confirms the LEDs and set_leds() work with no button involvement.
 *
 * Phase 2 (loop): Display the raw hardware pin level for each button pin
 *   AND the debounced btn_state byte, updating every 50 ms.
 *   LEDs mirror btn_state so both can be cross-checked at once.
 *
 *   Row 1: D6=PD7 (BTN_1)  D7=PE6 (BTN_2)   -- raw PIND/PINE reads
 *   Row 2: D8=PB4 (BTN_3)  D9=PB5 (BTN_4)   -- raw PINB reads
 *   Row 3: btn_state hex                       -- debounced ISR value
 *
 * If LEDs light in Phase 1 but not when buttons are pressed in Phase 2:
 *   -> btn_state is not being set -> check ISR / wiring / pull-downs.
 * If raw pins stay L when buttons are pressed:
 *   -> hardware wiring or pull-down resistor problem.
 * If raw pins go H but btn_state stays 0x00:
 *   -> debounce ISR is not running (sei() not called, or Timer0 not set up).
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

    // ---- Phase 1: LED chase ----
    lcd_set_cursor(0, 0);
    lcd_write_str("    LED  TEST       ");
    lcd_set_cursor(0, 1);
    lcd_write_str(" Watch LEDs cycle   ");
    lcd_set_cursor(0, 2);
    lcd_write_str(" then press buttons ");
    lcd_set_cursor(0, 3);
    lcd_write_str("                    ");

    for (uint8_t i = 0; i < 4; ++i) {
        set_leds((uint8_t)(1 << i));
        _delay_ms(350);
    }
    set_leds(0b1111);
    _delay_ms(500);
    set_leds(0b0000);
    _delay_ms(150);
    set_leds(0b1111);
    _delay_ms(150);
    set_leds(0b0000);
    _delay_ms(150);
    set_leds(0b1111);
    _delay_ms(150);
    set_leds(0b0000);

    // ---- Phase 2: Live pin monitor ----
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_write_str("  LIVE PIN READS    ");

    for (;;) {
        uint8_t pb = PINB;
        uint8_t pe = PINE;
        uint8_t pd = PIND;

        uint8_t d6 = (pd >> 7) & 1; // PD7 BTN_1
        uint8_t d7 = (pe >> 6) & 1; // PE6 BTN_2
        uint8_t d8 = (pb >> 4) & 1; // PB4 BTN_3
        uint8_t d9 = (pb >> 5) & 1; // PB5 BTN_4

        char buf[21];

        snprintf(buf, sizeof(buf), "D6(PD7)=%c D7(PE6)=%c", d6 ? 'H' : 'L', d7 ? 'H' : 'L');
        buf[20] = '\0';
        lcd_set_cursor(0, 1);
        lcd_write_str(buf);

        snprintf(buf, sizeof(buf), "D8(PB4)=%c D9(PB5)=%c", d8 ? 'H' : 'L', d9 ? 'H' : 'L');
        buf[20] = '\0';
        lcd_set_cursor(0, 2);
        lcd_write_str(buf);

        snprintf(buf, sizeof(buf), "btn_state:   0x%02X   ", btn_state);
        buf[20] = '\0';
        lcd_set_cursor(0, 3);
        lcd_write_str(buf);

        set_leds(btn_state & 0x0F);

        _delay_ms(50);
    }
}
