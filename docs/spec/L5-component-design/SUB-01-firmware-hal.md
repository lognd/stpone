# L5 -- SUB-01 firmware-hal

Satisfies SUB-01. Owned code: `src/stpalpha/hal/setup.c`,
`include/stpalpha/hal/setup.h`, `src/stpalpha/hal/uart.c`,
`include/stpalpha/hal/uart.h`. The C port of `startup.asm` (DEC-001).

## Components

| ID | Component | Path | Responsibility | satisfies | verified-by | Milestone |
|---|---|---|---|---|---|---|
| COMP-0101 | boot | src/stpalpha/hal/setup.c | `hal_init` from `.init8`: IVSEL clear, USB off, pin map, Timer0/Timer1, `sei`. | SUB-01, SYS-002 | UT-0101 | M1 |
| COMP-0102 | 1 kHz tick | src/stpalpha/hal/setup.c | `TIMER0_COMPA_vect`: 16-sample debounce per button, per-button toggle counters `btn_toggles[]`, buzzer countdown, the 32-bit millisecond `timer0_ticks` read through `millis()` (DEC-009). | SUB-01, SPEC-011, SPEC-012 | UT-0102 | M1 |
| COMP-0103 | encoder poll | src/stpalpha/hal/setup.c | `TIMER1_COMPA_vect`: 40 kHz sample, PROGMEM transition table, signed 32-bit count, two-bit-jump recovery into `enc_errors`, `enc_isr_count`, relay cut in the ISR at `enc_stop_at`, latching the exact cut count for `enc_take_cut`, recording the worst entry latency (`TCNT1` at entry) as `isr_lat_max`. | SUB-01, SPEC-006 | UT-0103 | M1 |
| COMP-0104 | fault handling | src/stpalpha/hal/setup.c | `BADISR_vect` (fault LED, return) and `panic` (fault LED, halt). | SUB-01, SPEC-015 | UT-0104 | M1 |
| COMP-0105 | actuator API | src/stpalpha/hal/setup.c | `set_leds`, `relay_on`, `relay_off`, `buzz` (atomic countdown write), `btn_consume` (take one pending toggle), `btn_flush` (discard all pending toggles), `enc_take_cut` (atomic read of the ISR cut count; cleared by `relay_on`), `enc_cut_pending`/`enc_clear_cut`, `millis`, `enc_isr_lat_max`/`enc_isr_lat_clear`. | SUB-01, SYS-004 | UT-0105 | M1 |
| COMP-0106 | host-link UART | src/stpalpha/hal/uart.c | USART1 at 115200 8N1 on D0/D1: `uart_init`, interrupt-fed line assembler (`USART1_RX_vect`; `\n` or `\r` ends a line, RXD1 pull-up, framing/overrun errors discard the line; a line arriving while one is unread is dropped whole and counted, never disturbing the pending one), `uart_read_line`, `uart_take_dropped`, polled `uart_write_line`. | SUB-01, SYS-003, SYS-004 | UT-0106 | M1 |
| COMP-0107 | encoder direction | src/stpalpha/hal/setup.c | `enc_direction`: last decoded step direction (+1/-1) for diagnostics (host-link `DUMP enc_dir`). | SUB-01 | UT-0107 | M1 |
| COMP-0108 | pending toggles | src/stpalpha/hal/setup.c | `btn_pending`: toggles counted by the ISR but not yet taken by `btn_consume`, per button (host-link `DUMP pending`). | SUB-01 | UT-0108 | M1 |

### comp-0101
<!-- frob:describes src/stpalpha/hal/setup.c::hal_init -->

`hal_init` is reached through a naked `.init8` trampoline
(`hal_init_hook`, one `call`), because a naked function in an init
section must not return. It ends with `sei()`; `main()` starts with
interrupts already enabled and both timers ticking.

### comp-0102
<!-- frob:describes src/stpalpha/hal/setup.c::debounce_one -->

The debounce shift registers and the countdown are file-static; the
ISR is the only writer of `btn_state`, `btn_toggles[]`, the 32-bit `timer0_ticks` (read only through `millis()`, under `cli`)
and the only decrementer of `buzz_ticks`. Every debounced change
(either edge) is one push of a latching button and increments that
button's `btn_toggles` byte, so two pushes inside one slow main-loop
pass stay distinct instead of merging into one flag.

### comp-0103
<!-- frob:describes src/stpalpha/hal/setup.c::TIMER1_COMPA_vect -->

<!-- frob:describes src/stpalpha/hal/setup.c::enc_stop_at -->

Sampling instead of external interrupts because PD4/PC6 have none on
the 32U4. The count is signed (CW up, CCW down; consumers use the
magnitude). After each sample the ISR compares `|enc_count|` with
`enc_stop_at` and clears the relay pin itself when it is reached, so a
slow LCD redraw in the main loop cannot delay the stop (SPEC-003);
`0xFFFFFFFF` (the boot value) means "never". The main loop writes
`enc_stop_at` (via the controller's `stop_at()`) each pass and still
drops the relay through its own path as the backstop.

Worst-case timing (DEC-009; counts taken from `avr-objdump -d` of the built
firmware, 16 MHz, poll period 400 cycles):

- Body: 39 cycles of response, vector `jmp` and prologue (15 pushes), at most
  ~174 cycles of body (isr latency record 16, pins and table 34, double-step
  18, count update 25, abs 17, stop compare 16, cut 13) and 35 of epilogue:
  **~213 cycles worst, ~130 for a normal step**; never reaches the next
  match (400), and Timer1 is not `ISR_NOBLOCK`, so it never nests.
- Entry delay, by source (cycles with interrupts masked): the longest
  `cli`/`sei` section in the program is `snapshot()` at 58 (`apply()` 54,
  `take_pending` 31, `enc_take_cut` 25, `uart_read_line` 16, the rest
  <= 14); `eeprom_update_byte` masks 6 for its EEMPE/EEPE sequence only;
  `TIMER0_COMPA` is `ISR_NOBLOCK` (`sei` is its first instruction: 7);
  `USART1_RX` is the longest masked block at 149 (sum of every instruction)
  + 7 entry = 156, typical ~70; `BADISR` 3. LCD code, `_delay_*`, TWI waits and
  UART TX polling run with interrupts enabled. Add up to 5 cycles for the
  instruction in progress: **maximum entry latency 161 cycles**. Vector
  priority (Timer1 17 < Timer0 21 < USART 25) lets Timer1 go first when
  several are pending, and the delays cannot accumulate (a UART byte every
  1389 cycles; the ISR ends within 161 + 213 = 374 < 400).
- Highest safe rate: a state must outlast the longest sample gap, 400 + 161
  = 561 cycles = **28 500 counts/s (710 RPM at 2400 counts/rev)**, 7.9x the
  machine's 90 RPM (3600 counts/s) and 1.4x a 500 RPM motor (20 000).
- Evidence: `isr_lat_max` (this ISR stores the largest `TCNT1` seen on entry:
  cycles since the compare match, ~39 of them fixed response and prologue;
  400 or more would mean a lost period; bench limit 200), cleared by `ZERO`;
  `tests/unit/test_encoder_model.py` runs this very source against a
  synthetic quadrature at up to 20 000 counts/s with random entry jitter;
  bench steps P2 and P10.

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
<!-- frob:describes src/stpalpha/hal/setup.c::btn_flush -->

The only functions the application and the harness images call. `buzz`
writes its 16-bit countdown under `ATOMIC_BLOCK(ATOMIC_RESTORESTATE)`.
`btn_consume` is lock-free and main-loop-only: the ISR owns
`btn_toggles[i]`, the main loop owns the private `g_btn_seen[i]`, and
the pending count is their wrapping 8-bit difference, so one call takes
exactly one toggle and the encoder ISR is never delayed. `btn_flush`
sets every `g_btn_seen` to the current `btn_toggles`; `main` calls it
once after the debounce settles so a latch that was already down at
power-on is never seen as a push.

### comp-0106
<!-- frob:describes src/stpalpha/hal/uart.c::uart_init -->
<!-- frob:describes src/stpalpha/hal/uart.c::uart_read_line -->
<!-- frob:describes src/stpalpha/hal/uart.c::uart_write_line -->

USART1 (D0 RX / D1 TX, 5 V logic) at 115200 baud, 8N1: UBRR1 = 16 with
U2X1 (about 117647, under 2 % error at 16 MHz). `USART1_RX_vect`
assembles bytes into a single 87-byte line buffer (CR ignored, LF ends
the line); a line that overflows, or that arrives while the previous one
is still unread, is dropped whole. `uart_read_line` hands out one
complete line; `uart_write_line` sends a string plus LF, blocking and
polled. Protocol: docs/hardware/serial-protocol.md. The RX ISR is short
and not `ISR_NOBLOCK`; at 115200 baud a byte arrives every 87 us, well
inside the encoder sample budget.

### comp-0107
<!-- frob:describes src/stpalpha/hal/setup.c::enc_direction -->

Returns the last valid step direction the encoder ISR decoded (+1 or
-1). Diagnostic only; it feeds `enc_dir` in the host-link `DUMP` reply.

### comp-0108
<!-- frob:describes src/stpalpha/hal/setup.c::btn_pending -->

Returns `btn_toggles[idx] - g_btn_seen[idx]` (wrapping), the number of
pushes the main loop has not consumed yet. Diagnostic only; it feeds
`pending` in `DUMP`.

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
| UT-0105 | Every image that uses the API builds and links (bench: P3 buzzer, P5 LEDs, P1 relay and latch handling at power-on). | COMP-0105 | tests/system/test_firmware.py::test_every_image_builds |
| UT-0106 | Slot 25 is `__vector_25` (USART1_RX) (bench: P7 HELLO/GET/PARAM/DUMP round trip). | COMP-0106 | tests/system/test_firmware.py::test_firmware_vector_table_binds_timer_isrs |
| UT-0107 | `firmware` builds and links `enc_direction` (bench: P7 `DUMP` shows `enc_dir` flip with the spin direction). | COMP-0107 | tests/system/test_firmware.py::test_every_image_builds |
| UT-0108 | `firmware` builds and links `btn_pending` (bench: P7 `DUMP` shows `pending=0,0,0,0` at rest, because the main loop takes one toggle per pass). | COMP-0108 | tests/system/test_firmware.py::test_every_image_builds |
