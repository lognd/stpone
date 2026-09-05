# L5 -- SUB-01 firmware-hal

Satisfies SUB-01. Owned code: `src/stpalpha/hal/setup.c`,
`include/stpalpha/hal/setup.h`. The C port of `startup.asm` (DEC-001).

## Components

| ID | Component | Path | Responsibility | satisfies | verified-by | Milestone |
|---|---|---|---|---|---|---|
| COMP-0101 | boot | src/stpalpha/hal/setup.c | `hal_init` from `.init8`: IVSEL clear, USB off, pin map, Timer0/Timer1, `sei`. | SUB-01, SYS-002 | UT-0101 | M1 |
| COMP-0102 | 1 kHz tick | src/stpalpha/hal/setup.c | `TIMER0_COMPA_vect`: 8-sample debounce per button, edge latch into `btn_events`, buzzer countdown, `timer0_ticks`. | SUB-01, SPEC-011, SPEC-012 | UT-0102 | M1 |
| COMP-0103 | encoder poll | src/stpalpha/hal/setup.c | `TIMER1_COMPA_vect`: 10 kHz sample, PROGMEM transition table, saturating 32-bit count, `enc_isr_count`. | SUB-01, SPEC-006 | UT-0103 | M1 |
| COMP-0104 | fault handling | src/stpalpha/hal/setup.c | `BADISR_vect` (fault LED, return) and `panic` (fault LED, halt). | SUB-01, SPEC-015 | UT-0104 | M1 |
| COMP-0105 | actuator API | src/stpalpha/hal/setup.c | `set_leds`, `relay_on`, `relay_off`, `buzz` (atomic countdown write), `btn_consume` (cli/sei read-and-clear). | SUB-01, SYS-004 | UT-0105 | M1 |

### comp-0101
<!-- frob:describes src/stpalpha/hal/setup.c::hal_init -->

`hal_init` is reached through a naked `.init8` trampoline
(`hal_init_hook`, one `call`), because a naked function in an init
section must not return. It ends with `sei()`; `main()` starts with
interrupts already enabled and both timers ticking.

### comp-0102
<!-- frob:describes src/stpalpha/hal/setup.c::TIMER0_COMPA_vect -->

The debounce shift registers, the previous-state snapshot and the
countdown are file-static; the ISR is the only writer of `btn_state`,
`btn_events`, `timer0_ticks` and the only decrementer of `buzz_ticks`.

### comp-0103
<!-- frob:describes src/stpalpha/hal/setup.c::TIMER1_COMPA_vect -->

Sampling instead of external interrupts because PD4/PC6 have none on
the 32U4. CCW steps stop at zero rather than wrapping.

### comp-0104
<!-- frob:describes src/stpalpha/hal/setup.c::BADISR_vect -->
<!-- frob:describes src/stpalpha/hal/setup.c::panic -->

An unexpected vector must be visible (D15) but must not reset the
board, so the handler returns; `panic` is the deliberate halt used when
`main` would otherwise return.

### comp-0105
<!-- frob:describes src/stpalpha/hal/setup.c::set_leds -->
<!-- frob:describes src/stpalpha/hal/setup.c::relay_on -->
<!-- frob:describes src/stpalpha/hal/setup.c::relay_off -->
<!-- frob:describes src/stpalpha/hal/setup.c::buzz -->
<!-- frob:describes src/stpalpha/hal/setup.c::btn_consume -->

The only functions the application and the harness images call. `buzz`
writes its 16-bit countdown under `ATOMIC_BLOCK(ATOMIC_RESTORESTATE)`;
`btn_consume` is documented main-loop-only because it re-enables
interrupts unconditionally.

## Unit test plan

Host unit tests cannot execute AVR code; every symbol carries a
`frob:waive TEST001` naming its bench procedure, and the executable
evidence is the disassembly and simulator checks below.

| ID | Case | verifies | runnable |
|---|---|---|---|
| UT-0101 | `hal_init_hook` precedes `call main` and calls `hal_init`; `hal_init` ends in `sei` (bench: P6 shows `MC=00`, `0B=03`, `MK=02`). | COMP-0101 | tests/system/test_firmware.py::test_hal_init_runs_before_main |
| UT-0102 | Slot 21 of the vector table is `__vector_21` (bench: P6 `RUN`, P5 debounced byte follows the buttons). | COMP-0102 | tests/system/test_firmware.py::test_firmware_vector_table_binds_timer_isrs |
| UT-0103 | Slot 17 is `__vector_17` (bench: P2 `ISR` counts and one turn = 2400). | COMP-0103 | tests/system/test_firmware.py::test_firmware_vector_table_binds_timer_isrs |
| UT-0104 | Every other slot is `__bad_interrupt` (bench: not reachable without a fault injection image; tracked for M3). | COMP-0104 | tests/system/test_firmware.py::test_firmware_vector_table_binds_timer_isrs |
| UT-0105 | Every image that uses the API builds and links (bench: P3 buzzer, P5 LEDs, P1 relay). | COMP-0105 | tests/system/test_firmware.py::test_every_image_builds |
