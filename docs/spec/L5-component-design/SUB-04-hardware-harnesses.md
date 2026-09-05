# L5 -- SUB-04 hardware-harnesses

Satisfies SUB-04. Owned code: `tests/avr/*.cpp`, including the simulator
smoke image [tests/avr/hello_world.cpp](../../../tests/avr/hello_world.cpp).
Each file is a `main` that links the HAL (and the LCD driver where it shows text). Screens and
expected readings are in `docs/runbooks/hardware-tests.md`.

## Components

| ID | Component | Path | Responsibility | satisfies | verified-by | Milestone |
|---|---|---|---|---|---|---|
| COMP-0401 | hello_world | tests/avr/hello_world.cpp | USART1 115200 8N1 print loop; the simulator smoke image. | SUB-04, SPEC-024 | UT-0401 | M1 |
| COMP-0402 | test_buttons_leds | tests/avr/test_buttons_leds.cpp | Mirrors `btn_state` to the LEDs (P6). | SUB-04, SPEC-016 | UT-0402 | M1 |
| COMP-0403 | test_buzzer | tests/avr/test_buzzer.cpp | Beep durations per button, countdown and PB3 level on the LCD (P3). | SUB-04, SPEC-016 | UT-0402 | M1 |
| COMP-0404 | test_encoder | tests/avr/test_encoder.cpp | Timer1 registers, raw pins, ISR counter, count and revolutions (P2). | SUB-04, SPEC-016 | UT-0402 | M1 |
| COMP-0405 | test_lcd | tests/avr/test_lcd.cpp | Four LCD screens (P4). | SUB-04, SPEC-016 | UT-0402 | M1 |
| COMP-0406 | test_leds_pins | tests/avr/test_leds_pins.cpp | LED chase then raw pin monitor (P5). | SUB-04, SPEC-016 | UT-0402 | M1 |
| COMP-0407 | test_timer0 | tests/avr/test_timer0.cpp | Timer0 registers, TCNT0/TIFR0, tick counter, raw pins (P6). | SUB-04, SPEC-016 | UT-0402 | M1 |

## Unit test plan

| ID | Case | verifies | runnable |
|---|---|---|---|
| UT-0401 | `hello_world.elf` prints on the simulated USART1. | COMP-0401 | tests/system/test_firmware.py::test_hello_world_prints_on_usart1_in_simavr |
| UT-0402 | All six harness `.hex` files build. | COMP-0402, COMP-0403, COMP-0404, COMP-0405, COMP-0406, COMP-0407 | tests/system/test_firmware.py::test_every_image_builds |
