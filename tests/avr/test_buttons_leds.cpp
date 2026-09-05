/*
 * test_buttons_leds -- Mirror btn_state directly to the status LEDs.
 *
 * What to look for:
 *   BTN_1 pressed  ->  LED_1 (D18/PF4) lights
 *   BTN_2 pressed  ->  LED_2 (D19/PF5) lights
 *   BTN_3 pressed  ->  LED_3 (D20/PF6) lights
 *   BTN_4 pressed  ->  LED_4 (D21/PF7) lights
 *
 * Failure interpretations:
 *   No LED ever lights     -- Timer0 ISR not running, or wrong button pin
 *   Wrong LED lights       -- Pin assignment mismatch in startup.asm
 *   All LEDs ON at rest    -- Buttons wired active-low; need pull-ups in startup.asm
 *   LED stays ON after rel -- Debounce shift register stuck (check pull-down resistors)
 */

#include <stpalpha/hal/setup.h>

int main(void) {
    for (;;) {
        set_leds(btn_state & 0x0F);
    }
}
