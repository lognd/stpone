# Pro Micro pin map

The wiring the customer's machine already has (REQ-010). Firmware
constants live in `src/stpalpha/hal/setup.c`; this table is the human
copy (SPEC-010).

| Silkscreen | Port bit | Function | Direction / pull | Notes |
|---|---|---|---|---|
| D2 | PD1 | I2C SDA | input, internal pull-up on | TWI peripheral drives it; no external pull-up fitted |
| D3 | PD0 | I2C SCL | input, internal pull-up on | PCF8574 backpack at 0x27, about 10 kHz (slow, because only the weak internal pull-ups are fitted) |
| D4 | PD4 | ENC_A | input, pull-up on | LN11-ERGA 600 P/R, sampled at 40 kHz |
| D5 | PC6 | ENC_B | input, pull-up on | no external-interrupt capability on either pin |
| D6 | PD7 | BTN4 | input, external pull-down | latching, active-high, 16 ms debounce; every push (an edge either way) is one click (DEC-011); a click toggles manual mode |
| D7 | PE6 | BTN3 | input, external pull-down | latching, one click per push; pause / play |
| D8 | PB4 | BTN2 | input, external pull-down | latching, one click per push; wind 90 yd / stop |
| D9 | PB5 | BTN1 | input, external pull-down | latching, one click per push; wind 30 yd / stop |
| D10 | PB6 | RELAY | output | active-high, motor contactor coil |
| D14 | PB3 | BUZZER | output | CMI-1295-0585T active buzzer |
| D15 | PB1 | FAULT LED | output | set by `BADISR_vect` and `panic()` |
| D16 | PB2 | BRIGHTNESS | output | reserved (held low) |
| D18 | PF7 | LED4 | output | `set_leds` bit 3 |
| D19 | PF6 | LED3 | output | bit 2 |
| D20 | PF5 | LED2 | output | bit 1 |
| D21 | PF4 | LED1 | output | bit 0 |
| D0/D1 | PD2/PD3 | USART1 RX/TX | -- | `hello_world` and the M2 desktop link (115200 8N1) |
| USB | -- | Caterina bootloader | -- | `2341:0036` in bootloader mode, `2341:8036` running |

Clock: external 16 MHz crystal (`F_CPU=16000000UL`). SRAM 2.5 KB
(RAMEND 0x0AFF). Flash 32 KB, of which the bootloader owns the top 4 KB
at 0x7000 (word 0x3800), which is why `hal_init` must clear IVSEL.
