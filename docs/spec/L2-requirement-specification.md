# L2 -- Requirement specification

Refines `L1-requirements.md` into precise, testable statements. Every
SPEC satisfies one or more REQs and is verified by one or more CTP items
in [`L2-customer-test-plan.md`](L2-customer-test-plan.md). Numbers here
are the contract; lower levels may not loosen them without a decision
record. All numbers are those of the accepted `stp-upgrade` firmware.

## A. Winding

| ID | Specification | satisfies | verified-by |
|---|---|---|---|
| SPEC-001 | Length model: measuring wheel diameter 4.0015 in (DEC-003), circumference `3.14159265 * 4.0015` = 12.571 in (float32); encoder 600 PPR read at 4x = 2400 counts/rev; targets 1080 in (30.0 yd, BTN1) and 3240 in (90.0 yd, BTN2); target pulses = `uint32(L * 2400 / 12.571)` = 206187 and 618562; yards shown = `|count| * 12.571 / (2400 * 36)`. | REQ-001, REQ-003 | CTP-001 |
| SPEC-002 | State machine `IDLE -> WINDING` on a BTN1 (30 yd) or BTN2 (90 yd) event; `WINDING <-> PAUSED` on a BTN3 event (PAUSED: relay OFF, count kept and still read); `WINDING/PAUSED -> IDLE` on a BTN1 or BTN2 event OR `|count| >= target`. Entering WINDING: cancel beeps, zero count and `enc_errors`, relay ON, LEDs 0b1111. Leaving: relay OFF, LEDs 0b0000, start the beep sequence. In IDLE the relay follows BTN4 while latched down, armed only after BTN4 was seen up; buttons are edges with a 200 ms lockout. | REQ-001, REQ-002 | CTP-001, CTP-002 |
| SPEC-003 | Screens (20x4, fixed columns): idle rows ` -= PAPER WINDER =- `, `      JOGGING       ` / `!! ENC MISS:%-5u  !` / blank, `[1] 30yd    [2] 90yd`, `[4] JOG while down  `; winding rows `     WINDING...     ` (`      PAUSED        `, or `%-11s ERR:%-4u` when `enc_errors` != 0), `CNT:%-10lu  %3u%%`, `LEN %4u.%01u/%4u.%01u yd`, `[1/2]STOP   [3]PAUSE` (`[3]PLAY `); percent = `|count|*100/target` capped at 100; the screen is redrawn only when the count, errors or state change. | REQ-003 | CTP-003 |
| SPEC-004 | Beep sequence: 3 beeps of 300 ms with 150 ms silence between, driven from the 1 kHz tick without blocking the loop; `buzz(0)` cancels; a BTN1 or BTN2 press in IDLE cancels before starting. | REQ-004 | CTP-004 |
| SPEC-005 | LEDs: idle pattern 0b0101 (LED1+LED3); bargraph segment = `min(|count|,target) * 4 / target`; jog lights LED4 mapped to 0b0000, 0b0001, 0b0011, 0b0111, 0b1111. | REQ-005 | CTP-005 |
| SPEC-006 | Encoder: sampled at 40 kHz; 4x quadrature transition table (index `(prev<<2)|cur`, state `(B<<1)|A`); one direction +1, the other -1, signed 32-bit count (no saturation) read under `cli/sei`; a two-bit jump adds 2 in the last direction and increments `enc_errors`. Designed for 90 RPM with about 11 samples per state. | REQ-006 | CTP-006 |

## B. Board and I/O

| ID | Specification | satisfies | verified-by |
|---|---|---|---|
| SPEC-010 | Pin map (Pro Micro label = port bit): BTN1 D9=PB5, BTN2 D8=PB4, BTN3 D7=PE6, BTN4 D6=PD7 (inputs, external pull-downs, active-high); ENC_A D4=PD4, ENC_B D5=PC6 (inputs, pull-ups on); LED1..LED4 D18..D21 = PF4..PF7 (outputs); buzzer D14=PB3; brightness D16=PB2; relay D10=PB6 (active-high); fault LED D15=PB1; I2C SDA D2=PD1, SCL D3=PD0. | REQ-010 | CTP-010 |
| SPEC-011 | Buttons: debounced by a 16-sample shift register at 1 ms (16 ms of 1s sets, 16 ms of 0s clears); every edge of the debounced state latches a bit in `btn_events`; `btn_consume(bit)` atomically reads-and-clears one bit; startup events are discarded by `main()` clearing `btn_events` once. | REQ-001, REQ-002, REQ-010 | CTP-011 |
| SPEC-012 | Timers: Timer0 CTC, prescaler 64, `OCR0A = 249` -> 1000 Hz `TIMER0_COMPA` (debounce, event latch, buzzer countdown, `timer0_ticks`); Timer1 CTC, no prescaler, `OCR1A = 399` -> 40 kHz `TIMER1_COMPA` (encoder, `enc_isr_count`); `TIMER0_COMPA` is `ISR_NOBLOCK` so the encoder preempts it. | REQ-006, REQ-010 | CTP-012 |
| SPEC-013 | Display: HD44780 20x4 behind a PCF8574 backpack at I2C address 0x27, TWI at 100 kHz (`TWBR = 72`), 4-bit mode, row offsets 0x00/0x40/0x14/0x54, backlight bit controllable. | REQ-003 | CTP-013 |
| SPEC-014 | Flashing: Caterina bootloader (avr109 protocol, 57600 baud, `-D` no chip erase), bootloader USB id `2341:0036`, appears as `/dev/ttyACM*` for about 8 s after a double-tap on RST. | REQ-011 | CTP-014 |
| SPEC-015 | Faults: every unused vector routes to a handler that sets D15 high and returns (`BADISR_vect`); `panic()` sets D15, disables interrupts and spins; `main()` returning reaches `panic()` semantics via avr-libc's `exit` (interrupts off, halt). | REQ-012 | CTP-015 |
| SPEC-016 | Harness images `test_buttons_leds`, `test_buzzer`, `test_encoder`, `test_lcd`, `test_leds_pins`, `test_timer0` each build from `tests/avr/<name>.cpp` against the same HAL and show the screens in `docs/runbooks/hardware-tests.md`. | REQ-013 | CTP-016 |

## C. Developer workflow

| ID | Specification | satisfies | verified-by |
|---|---|---|---|
| SPEC-020 | `scripts/flash.py` is the only operator entrypoint: `[--target NAME] [--build-dir DIR] [--no-build] [--build-only] [--port DEV] [--setup] [--attach-timeout S] [--port-timeout S]`; defaults from `[tool.stpone.flash]` in `pyproject.toml`; `--target all` only with `--build-only`. Exit 0 on success, 1 with the error and a next-step hint otherwise. | REQ-020 | CTP-020 |
| SPEC-021 | Host detection: WSL when `/proc/version` contains `microsoft`; on WSL the tool polls `usbipd.exe attach --wsl --hardware-id 2341:0036` once per second for up to 15 s; on any other Linux the attach step is skipped with a log line; `--setup` runs `usbipd bind --hardware-id 2341:0036` elevated through `powershell.exe Start-Process -Verb RunAs`. | REQ-020 | CTP-021 |
| SPEC-022 | After attach the tool waits up to 10 s for `/dev/ttyACM*`, settles 0.5 s, and runs `avrdude -v -patmega32u4 -cavr109 -P<port> -b57600 -D -Uflash:w:<hex>:i` -- the same invocation as the previous `upload.sh`. | REQ-020, REQ-011 | CTP-022 |
| SPEC-023 | Firmware structure: avr-libc crt owns the vector table, stack and `.data/.bss` init; `hal_init()` runs from `.init8` and ends with `sei()`; ISRs are `ISR(TIMER0_COMPA_vect)`, `ISR(TIMER1_COMPA_vect)`, `ISR(BADISR_vect)`; no `-nostartfiles`, no assembly source. | REQ-021 | CTP-023 |
| SPEC-024 | Build: CMake >= 3.20 with `cmake/avr-gcc-toolchain.cmake`, `-mmcu=atmega32u4 -DF_CPU=16000000UL -Os -ffunction-sections -fdata-sections -Wl,--gc-sections`, every image links `src/stpalpha/hal/setup.c`; a `.hex` is produced next to each `.elf`; `ctest` runs a simavr smoke on `hello_world`. | REQ-022 | CTP-024 |

## D. Desktop monitor (M2)

| ID | Specification | satisfies | verified-by |
|---|---|---|---|
| SPEC-030 | Wire format: little-endian fixed-width integers U8..U64/I8..I64 and Bool (0/1), IEEE floats F16/F32/F64 (finite only), `String` = 4-byte little-endian length + UTF-8, `FastString` = 64 bytes; handshake constants `b"u up?"`, `b"ye; wbu?"`, `b"lol yeah"`; composite packets are pydantic models whose fields are all serializable and are written field by field in declaration order. | REQ-030 | CTP-030 |
| SPEC-031 | Transport: an `RDTCommunication` abstraction with `read(n)`, `write(bytes)` returning typani `Result`s and `error(code)` reporting; the concrete pyserial transport and any GUI are M2 design work. | REQ-030 | CTP-030 |

## E. Process

| ID | Specification | satisfies | verified-by |
|---|---|---|---|
| SPEC-040 | `frob.toml` at the root drives the gates; trace tables in `docs/spec` are the V-model; `tests/system/test_process.py` checks that every `satisfies`/`verified-by` id resolves and that every artifact row has a verifier. | REQ-040 | CTP-040 |
| SPEC-041 | Every public symbol has a one-line docstring/comment and a `frob:doc` anchor into its L5 row; AVR-only symbols carry `frob:waive TEST001 reason="..."` naming the bench procedure. | REQ-041 | CTP-041 |
| SPEC-042 | Tickets live in `tickets/`, every leaf ticket cites test ids and closes with evidence; `FROBLEMS.md` is gitignored. | REQ-042 | CTP-042 |
