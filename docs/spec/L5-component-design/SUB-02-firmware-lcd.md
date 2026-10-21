# L5 -- SUB-02 firmware-lcd

Satisfies SUB-02. Owned code: `src/stpalpha/hal/lcd.c`,
`include/stpalpha/hal/lcd.h`. Unchanged from `stp-upgrade` apart from
formatting.

## Components

| ID | Component | Path | Responsibility | satisfies | verified-by | Milestone |
|---|---|---|---|---|---|---|
| COMP-0201 | init | src/stpalpha/hal/lcd.c | `lcd_init`: TWI at about 10 kHz with the internal SDA/SCL pull-ups on (DEC-007), one I2C transfer per nibble, HD44780 4-bit power-on sequence, display on, entry mode, Return Home (0x02, 2 ms; DEC-009: undoes a display shift a glitch left behind, without clearing DDRAM), clear. | SUB-02, SPEC-013 | UT-0201 | M1 |
| COMP-0202 | text API | src/stpalpha/hal/lcd.c | `lcd_set_cursor` (row offsets 0x00/0x40/0x14/0x54), `lcd_write_char`, `lcd_write_str`, `lcd_clear`, `lcd_backlight`, `lcd_faulted`/`lcd_clear_fault`, and `lcd_recover_begin` / `lcd_recover_finish` (the HD44780 software init without clearing, in two halves of ~21 ms and ~34 ms, each called from its own main-loop pass, so a panel left showing blocks or garbage by a glitch resynchronises; the caller then repaints every character). The application heals after a bus fault, about 30 ms after every relay edge and every 2 s in every state (deferred while a stop toggle is pending), repaints at most 25 times a second (40 ms), and sends only the characters that changed through `stpalpha::Panel` (`include/stpalpha/panel.h`, DEC-009): a shadow of the panel, at most 5 cell-times (about 39 ms) per main-loop pass, cells served in priority classes (percent number and LEN digits, then the bar, then everything else) with every 2nd pass serving the lowest class first so nothing starves (waits: class 0 about 150 ms, classes 1-2 under 2 s), yielding at once when a BTN1/2/3 toggle is pending; a heal invalidates the shadow, so every character is rewritten within about 1.5 s. Every TWI wait is bounded (~7 ms); a timeout releases the bus, sets the fault flag and turns later LCD calls into no-ops until the caller clears it (DEC-005). | SUB-02, SYS-005 | UT-0202 | M1 |

### comp-0201
<!-- frob:describes src/stpalpha/hal/lcd.c::lcd_init -->

Blocking TWI helpers (`twi_start`, `twi_write`, `twi_stop`) and the
PCF8574 nibble protocol are file-static; the init timing follows the
HD44780 datasheet figure 24. Bus timing at 9.9 kHz: one byte 0.91 ms, a nibble transfer (address plus three bytes) 3.9 ms, a character or cursor move 7.8 ms. The init is exposed in two halves (`lcd_recover_begin`: TWI reconfigure and the 0x30/0x30/0x30/0x20 nibble resync, about 21 ms; `lcd_recover_finish`: function set, display on, entry mode, Return Home, about 34 ms) so the application never blocks a pass for more than ~35 ms for it. Return Home matters because a glitch can turn a data byte into 0x18/0x1C (display shift) or 0x05/0x07 (entry mode with shift): entry mode is rewritten by the init and Return Home resets the shift, so a scrolled panel is repaired by the next heal instead of staying scrolled until power-cycle. The shadow in `Panel` trusts what it sent; a byte corrupted on the bus (for example RS cleared so a letter becomes Set-CGRAM-address) is therefore repaired only by the periodic heal (at most 2 s).

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
