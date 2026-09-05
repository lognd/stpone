# L4 -- Subsystem integration test plans

Paired with [`L4-system-design.md`](L4-system-design.md). Each SUBT
exercises the boundary between two real subsystems.

| ID | Case | verifies | runnable |
|---|---|---|---|
| SUBT-01 | HAL <-> crt: the built image's init sequence and vector table are as designed (objdump), and every image that links the HAL boots to `main` in simavr. | SUB-01 | tests/system/test_firmware.py::test_hal_init_runs_before_main |
| SUBT-02 | LCD driver <-> HAL: the `test_lcd` image builds against the HAL and, on the bench, renders all screens (P4). | SUB-02 | `manual` (hardware-tests.md P4) |
| SUBT-03 | Application <-> HAL/LCD: `firmware.hex` builds, its constants match the length model, and the bench acceptance (P1) passes. | SUB-03 | tests/unit/test_length_model.py::test_target_pulses_match_firmware_constants |
| SUBT-04 | Harness images <-> HAL: all six harness images plus `hello_world` build, and `hello_world` prints under simavr. | SUB-04 | tests/system/test_firmware.py::test_every_image_builds |
| SUBT-05 | Flash tool <-> cmake/avrdude/usbipd: `scripts/flash.py --build-only --target all` drives a real cmake build; the CLI, config, and app agree on defaults and overrides. | SUB-05 | tests/integration/test_flash_cli.py::test_main_reports_missing_image |
| SUBT-06 | Desktop core wiring: `get_logger` configures once, `error_ctx` logs and re-raises, `stpone` imports cleanly. | SUB-06 | tests/integration/test_wiring.py::test_logging_and_exception_wiring |
| SUBT-07 | Protocol <-> transport: packets stream through an in-memory `RDTCommunication` and back. | SUB-07 | tests/integration/test_serial_stream.py::test_handshake_round_trip |
| SUBT-08 | Process: trace tables closed, waivers name evidence, `FROBLEMS.md` ignored. | SUB-08 | tests/system/test_process.py::test_trace_tables_are_closed |
