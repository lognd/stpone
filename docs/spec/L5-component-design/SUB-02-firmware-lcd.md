# L5 -- SUB-02 firmware-lcd

Satisfies SUB-02. Owned code: `src/stpalpha/hal/lcd.c`,
`include/stpalpha/hal/lcd.h`. Unchanged from `stp-upgrade` apart from
formatting.

## Components

| ID | Component | Path | Responsibility | satisfies | verified-by | Milestone |
|---|---|---|---|---|---|---|
| COMP-0201 | init | src/stpalpha/hal/lcd.c | `lcd_init`: TWI at 100 kHz, HD44780 4-bit power-on sequence, display on, clear, entry mode. | SUB-02, SPEC-013 | UT-0201 | M1 |
| COMP-0202 | text API | src/stpalpha/hal/lcd.c | `lcd_set_cursor` (row offsets 0x00/0x40/0x14/0x54), `lcd_write_char`, `lcd_write_str`, `lcd_clear`, `lcd_backlight`. | SUB-02, SYS-005 | UT-0202 | M1 |

### comp-0201
<!-- frob:describes src/stpalpha/hal/lcd.c::lcd_init -->

Blocking TWI helpers (`twi_start`, `twi_write`, `twi_stop`) and the
PCF8574 nibble protocol are file-static; the init timing follows the
HD44780 datasheet figure 24.

### comp-0202
<!-- frob:describes src/stpalpha/hal/lcd.c::lcd_set_cursor -->
<!-- frob:describes src/stpalpha/hal/lcd.c::lcd_write_char -->
<!-- frob:describes src/stpalpha/hal/lcd.c::lcd_write_str -->
<!-- frob:describes src/stpalpha/hal/lcd.c::lcd_clear -->
<!-- frob:describes src/stpalpha/hal/lcd.c::lcd_backlight -->

Rows are written as fixed 20-column strings by the callers, so the
driver never needs a partial clear.

## Unit test plan

| ID | Case | verifies | runnable |
|---|---|---|---|
| UT-0201 | `test_lcd` image builds (bench P4: all four screens render). | COMP-0201 | tests/system/test_firmware.py::test_every_image_builds |
| UT-0202 | Every text-showing image builds (bench P4: corner cursors, backlight toggle). | COMP-0202 | tests/system/test_firmware.py::test_every_image_builds |
