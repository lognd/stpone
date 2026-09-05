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
| SYS-002 | Boot sequence (fixed order): reset vector -> avr-libc crt (SP=RAMEND, r1=0, `.data` copy, `.bss` clear) -> `.init8: hal_init()` (IVSEL cleared via IVCE, USBCON=0, pin directions, Timer0 1 kHz, Timer1 10 kHz, `sei`) -> `main()`. Interrupts are enabled before `main` runs; `main` never returns. | SPEC-023, SPEC-012, SPEC-015 | SIT-002 |
| SYS-003 | Shared-state contract between ISRs and the main loop: ISR-owned writers `btn_state`, `btn_events`, `enc_count`, `enc_isr_count`, `timer0_ticks`, `buzz_ticks`; main reads 8-bit values directly, reads/zeroes `enc_count` only under `cli/sei`, consumes `btn_events` only through `btn_consume`; `buzz()` writes `buzz_ticks` atomically. ISRs do not nest (default avr-libc `ISR` keeps I cleared). Budget: `TIMER1_COMPA` <= 100 us (must finish before the next 10 kHz tick), `TIMER0_COMPA` <= 1 ms. | SPEC-006, SPEC-011, SPEC-012 | SIT-003 |
| SYS-004 | Actuator interface (C linkage, header `include/stpalpha/hal/setup.h`): `set_leds(nibble)`, `relay_on()`, `relay_off()`, `buzz(ms)`, `btn_consume(bit)`, `panic()`; plus the variables of SYS-003. This header is the only contract between the winder application, the harness images, and the HAL. | SPEC-002, SPEC-004, SPEC-005, SPEC-015 | SIT-004 |
| SYS-005 | LCD interface (`include/stpalpha/hal/lcd.h`): blocking TWI driver, no interrupts, `lcd_init()` called by `main` after boot; text is written as fixed 20-column rows so no clear is needed between frames. | SPEC-003, SPEC-013 | SIT-005 |
| SYS-006 | Fault surface: unused vectors -> `BADISR_vect` (D15 on, return); `panic()` (D15 on, halt); no watchdog in M1; the relay is only ever closed by the WINDING entry action, so no fault path can energize it. | SPEC-015 | SIT-006 |

## Build and images

| ID | Specification | satisfies | verified-by |
|---|---|---|---|
| SYS-010 | One CMake project (`CMakeLists.txt`, toolchain `cmake/avr-gcc-toolchain.cmake`) produces eight images: `firmware`, `hello_world`, `test_buttons_leds`, `test_buzzer`, `test_encoder`, `test_lcd`, `test_leds_pins`, `test_timer0`; each links `src/stpalpha/hal/setup.c`; images that show text also link `src/stpalpha/hal/lcd.c`; each yields `<name>.elf` and `<name>.hex` in the build directory. `ctest` runs the simavr smoke. | SPEC-016, SPEC-024 | SIT-010 |
| SYS-011 | Flash workflow as a state machine (`stpone.flash.app.FlashApp`): `setup?` -> `build?` (configure, build target) -> `build_only?` -> image exists? -> host kind -> `attach` (WSL only) -> `port` (given or waited) -> `avrdude`. Every step returns `Result[None, FlashError]`; the CLI maps `Err` to exit 1 plus a remedy line, `Ok` to exit 0. | SPEC-020, SPEC-021, SPEC-022 | SIT-011 |
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
