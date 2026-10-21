# L3 -- System specification

The system as black boxes with named interfaces. Every SYS satisfies one
or more SPECs and is verified by SIT items in
[`L3-system-integration-test-plan.md`](L3-system-integration-test-plan.md)
(tests that drive a whole interface: the built image in simavr, the
flash tool against fake tools, the protocol against an in-memory
transport).

## Topology and trust

| ID | Specification | satisfies | verified-by |
|---|---|---|---|
| SYS-001 | Nodes: `winder-mcu` (ATmega32U4 Pro Micro running `stpalpha`), `host` (the developer's Linux/WSL machine running `stpone`), `windows-side` (usbipd-win, only under WSL), `desktop` (M2 monitor, same host or a laptop). Interfaces: USB bootloader (avr109 over CDC) host<->mcu; USART1 (D0/D1, 115200 8N1) mcu->desktop in M2; I2C bus mcu->LCD; GPIO to the panel. Nothing on the mcu trusts input beyond the pins listed in SPEC-010. | SPEC-010, SPEC-014 | SIT-001 |
| SYS-002 | Boot sequence (fixed order): reset vector -> avr-libc crt (SP=RAMEND, r1=0, `.data` copy, `.bss` clear) -> `.init8: hal_init()` (IVSEL cleared via IVCE, USBCON=0, pin directions, Timer0 1 kHz, Timer1 40 kHz, `sei`) -> `main()`. Interrupts are enabled before `main` runs; `main` never returns. | SPEC-023, SPEC-012, SPEC-015 | SIT-002 |
| SYS-003 | Shared-state contract between ISRs and the main loop: ISR-owned writers `btn_state`, `btn_toggles[4]`, `enc_count` (signed), `enc_errors`, `enc_isr_count`, `timer0_ticks` (32-bit, DEC-009), `buzz_ticks`; main reads 8-bit values directly (including the single-byte cut record `enc_cut_pending()`, once per pass) and the 32-bit millisecond tick through `millis()` under `cli/sei`, samples the signed count under `cli/sei` once per pass for the speed meter (DEC-006), reads/zeroes `enc_count` only under `cli/sei`, consumes toggles only through `btn_consume` (lock-free: ISR writes `btn_toggles[i]`, main writes its private seen-counter) and discards them with `btn_flush`; main is the only writer of `enc_stop_at` (written under `cli/sei` together with the relay drive, so the ISR cut can never be undone by a stale main-loop check), which `TIMER1_COMPA` reads to cut the relay itself; the USART1 RX ISR is the only writer of the line buffer and main hands lines out through `uart_read_line`; `buzz()` writes `buzz_ticks` atomically. `TIMER1_COMPA` never nests; `TIMER0_COMPA` is `ISR_NOBLOCK` and may be preempted by it. Budget: `TIMER1_COMPA` <= 25 us (at most ~213 of 400 cycles used at 40 kHz on its longest path, entry delayed by at most 161 cycles, so no encoder tick is ever missed up to 28 500 counts/s; DEC-009), `TIMER0_COMPA` <= 1 ms; every button acts within 100 ms of the push (debounce 16 ms plus a main-loop pass of at most ~68 ms; DEC-009). | SPEC-006, SPEC-011, SPEC-012 | SIT-003 |
| SYS-004 | Actuator interface (C linkage, header `include/stpalpha/hal/setup.h`): `set_leds(nibble)`, `relay_on()`, `relay_off()`, `buzz(ms)`, `btn_consume(bit)`, `btn_flush()`, `btn_pending(idx)`, `enc_take_cut(&count)` (the count at which the ISR dropped the relay), `enc_direction()`, `panic()`; plus the variables of SYS-003 (including `enc_stop_at`). Host link (`include/stpalpha/hal/uart.h`): USART1 on D0/D1 at 115200 baud 8N1 (5 V logic), `uart_init()`, `uart_read_line(buf, cap)`, `uart_take_dropped()`, `uart_write_line(s)`; the line protocol is `docs/hardware/serial-protocol.md` (DEC-004; `fw=6` adds `margin`, `bump`, `state=TRIM`, `bumps`, `trim`, `pass`, DEC-010). Parameters are validated by `Params::valid()` before use or storage, and a stored set that fails (or has a bad checksum or the old fw<=5 tag) falls back to the defaults. This header is the only contract between the winder application, the harness images, and the HAL. | SPEC-002, SPEC-004, SPEC-005, SPEC-015 | SIT-004 |
| SYS-005 | LCD interface (`include/stpalpha/hal/lcd.h`): blocking TWI driver, no interrupts, `lcd_init()` called by `main` after boot; text is written as fixed 20-column rows so no clear is needed between frames. | SPEC-003, SPEC-013 | SIT-005 |
| SYS-006 | Fault surface: unused vectors -> `BADISR_vect` (D15 on, return); `panic()` (D15 on, halt); the relay is closed only by the controller (a wind and its trim bumps, a manual click in IDLE/PAUSED, or the host in debug mode), and every such path is bounded: the encoder ISR cuts a wind or a trim bump at `enc_stop_at` (the trim also cuts a bump that makes no progress after 1.5 s and ends after 24 bumps or 45 s, DEC-010, DEC-012), a silent host is timed out of debug mode after 5 s (DEC-005), and a 500 ms hardware watchdog (kicked once per main-loop pass and after every LCD character, LCD TWI waits bounded; no pass is longer than about 68 ms and the LCD gets at most 39 ms of it, DEC-009) resets the MCU, which boots with the relay low, if the loop ever hangs. | SPEC-015 | SIT-006 |

## Build and images

| ID | Specification | satisfies | verified-by |
|---|---|---|---|
| SYS-010 | One CMake project (`CMakeLists.txt`, toolchain `cmake/avr-gcc-toolchain.cmake`) produces eight images: `firmware`, `hello_world`, `test_buttons_leds`, `test_buzzer`, `test_encoder`, `test_lcd`, `test_leds_pins`, `test_timer0`; each links `src/stpalpha/hal/setup.c`; images that show text also link `src/stpalpha/hal/lcd.c`; each yields `<name>.elf` and `<name>.hex` in the build directory. `ctest` runs the simavr smoke. | SPEC-016, SPEC-024 | SIT-010 |
| SYS-011 | Flash workflow as a state machine (`src/stpone/flash/app.py::FlashApp`): `setup?` -> `build?` (configure, build target) -> `build_only?` -> image exists? -> host kind -> `attach` (WSL only) -> `port` (given or waited) -> `avrdude`. Every step returns `Result[None, FlashError]`; the CLI maps `Err` to exit 1 plus a remedy line, `Ok` to exit 0. | SPEC-020, SPEC-021, SPEC-022 | SIT-011 |
| SYS-012 | Host environments: `wsl` (attach through `usbipd.exe`, setup through `powershell.exe`), `linux` (no forwarding), `other` (no forwarding, best effort). Detection reads `/proc/version`; tool presence is checked with `shutil.which` before any call, never by trying and crashing. | SPEC-021 | SIT-012 |

## Desktop link (M2)

| ID | Specification | satisfies | verified-by |
|---|---|---|---|
| SYS-020 | Wire protocol: the `Serializable` hierarchy (`FixedWidth`, `Constant`, `Pydantic`) in `stpone.serial.packets`; every packet is a self-describing pydantic model or fixed-width scalar; decode errors are reported to the transport via `error(code)` and yield `None`, never an exception across the boundary. | SPEC-030 | SIT-020 |
| SYS-021 | Transport: `RDTCommunication[Status]` with `read`, `write`, `error`; the M2 concrete transport wraps pyserial on the host and USART1 on the mcu. | SPEC-031 | SIT-021 |

## Process

| ID | Specification | satisfies | verified-by |
|---|---|---|---|
| SYS-030 | Verification layout: `tests/unit` (pure Python, fakes only), `tests/integration` (real package wiring, in-memory transports), `tests/system` (spawns cmake/avr-gcc/simavr and the process checks); `frob.toml` binds pytest as the single runner; firmware evidence kinds are `integration` (it builds) and `e2e` (it runs in simavr); bench evidence is a ticket attachment. | SPEC-040, SPEC-041, SPEC-042 | SIT-030 |
