/*
 * test_lcd -- LCD hardware verification: character display, cursor positioning,
 *             and backlight control.
 *
 * BTN_1 advances through four screens:
 *
 *   Screen 0: Row identification
 *   Screen 1: ASCII character fill
 *   Screen 2: Corner cursor test
 *   Screen 3: Backlight toggle (BTN_2 = off, BTN_3 = on)
 */

#include <stpalpha/hal/lcd.h>
#include <stpalpha/hal/setup.h>

#include <stdio.h>
#include <string.h>

static void write_row(uint8_t row, const char* text) {
    char buf[21];
    snprintf(buf, sizeof(buf), "%-20s", text);
    buf[20] = '\0';
    lcd_set_cursor(0, row);
    lcd_write_str(buf);
}

static void show_screen_0(void) {
    lcd_clear();
    write_row(0, "ROW 0: offset 0x00  ");
    write_row(1, "ROW 1: offset 0x40  ");
    write_row(2, "ROW 2: offset 0x14  ");
    write_row(3, "ROW 3: [BTN1] NEXT  ");
}

static void show_screen_1(void) {
    lcd_clear();
    write_row(0, "ASCII FILL:         ");
    char buf[21];
    for (uint8_t r = 1; r <= 2; ++r) {
        for (uint8_t c = 0; c < 20; ++c) {
            uint8_t code = 0x21 + ((uint8_t)(r * 20 + c)) % 94;
            buf[c] = (char)code;
        }
        buf[20] = '\0';
        lcd_set_cursor(0, r);
        lcd_write_str(buf);
    }
    write_row(3, "         [BTN1] NEXT");
}

static void show_screen_2(void) {
    lcd_clear();
    write_row(0, "CURSOR CORNERS:     ");
    write_row(2, "TL=A TR=B BL=C BR=D ");
    write_row(3, "         [BTN1] NEXT");

    lcd_set_cursor(0, 1);
    lcd_write_char('A');
    lcd_set_cursor(19, 1);
    lcd_write_char('B');
    lcd_set_cursor(0, 2);
    lcd_write_char('C');
    lcd_set_cursor(19, 2);
    lcd_write_char('D');
}

static void show_screen_3(void) {
    lcd_clear();
    write_row(0, "BACKLIGHT CTRL:     ");
    write_row(1, "BTN_2: off          ");
    write_row(2, "BTN_3: on           ");
    write_row(3, "         [BTN1] NEXT");
}

int main(void) {
    lcd_init();

    uint8_t screen = 0;
    show_screen_0();

    for (;;) {
        if (btn_consume(1 << 0)) {
            screen = (screen + 1) % 4;
            switch (screen) {
            case 0:
                show_screen_0();
                break;
            case 1:
                show_screen_1();
                break;
            case 2:
                show_screen_2();
                break;
            case 3:
                show_screen_3();
                break;
            }
        }

        if (screen == 3) {
            if (btn_consume(1 << 1))
                lcd_backlight(0);
            if (btn_consume(1 << 2))
                lcd_backlight(1);
        }
    }
}
