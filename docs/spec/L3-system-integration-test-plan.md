# L3 -- System integration test plan

Paired with [`L3-system-specification.md`](L3-system-specification.md).

| ID | Case | verifies | runnable |
|---|---|---|---|
| SIT-001 | The pin map in `docs/hardware/pinout.md` and the constants in `src/stpalpha/hal/setup.c` agree (bench P2..P6 confirm electrically). | SYS-001 | `manual` (hardware-tests.md P2..P6) |
| SIT-002 | Disassembly of `firmware.elf`: `hal_init_hook` sits between `__do_clear_bss` and the `call main`, calls `hal_init`, and `hal_init` ends with `sei`. | SYS-002 | tests/system/test_firmware.py::test_hal_init_runs_before_main |
| SIT-003 | Every image links the same HAL and builds; the vector table binds slots 17 and 21 only. | SYS-003 | tests/system/test_firmware.py::test_every_image_builds |
| SIT-004 | `test_buttons_leds`, `test_buzzer`, `test_leds_pins` exercise every function of the actuator interface on the bench. | SYS-004 | `manual` (hardware-tests.md P3, P5, P6) |
| SIT-005 | `test_lcd` shows all four rows, the corner cursors, and toggles the backlight. | SYS-005 | `manual` (hardware-tests.md P4) |
| SIT-006 | Every vector other than 17 and 21 jumps to `__bad_interrupt`, which reaches the BADISR handler. | SYS-006 | tests/system/test_firmware.py::test_firmware_vector_table_binds_timer_isrs |
| SIT-010 | A `--build-only --target all` run produces all eight `.hex` files and `hello_world` prints in simavr. | SYS-010 | tests/system/test_firmware.py::test_hello_world_prints_on_usart1_in_simavr |
| SIT-011 | FlashApp with fake tools walks setup / build-only / linux-flash / wsl-flash / missing-image / build-failure paths and returns the specified `Result`s. | SYS-011 | tests/unit/test_flash_app.py::test_build_failure_short_circuits |
| SIT-012 | Host detection and tool lookup: WSL banner, generic banner, unreadable banner, non-Linux; missing `usbipd.exe`/`powershell.exe` yields `ToolMissing`. | SYS-012 | tests/unit/test_flash_host.py::test_attach_requires_usbipd |
| SIT-020 | Composite packets and scalars stream through an in-memory `RDTCommunication` and decode back; a short read yields `None` and one `error()` call. | SYS-020 | tests/integration/test_serial_stream.py::test_fixed_width_and_string_from_stream |
| SIT-021 | The three-way handshake constants round-trip through the in-memory transport in order. | SYS-021 | tests/integration/test_serial_stream.py::test_handshake_round_trip |
| SIT-030 | The trace tables close and the ignore rules hold. | SYS-030 | tests/system/test_process.py::test_trace_tables_are_closed |
