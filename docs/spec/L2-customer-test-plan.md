# L2 -- Customer test plan

Paired with [`L2-requirement-specification.md`](L2-requirement-specification.md).
Bench items reference the procedures (P1..P6) in
`docs/runbooks/hardware-tests.md`; the operator records the result as an
attachment on the milestone ticket.

| ID | Case | verifies | runnable |
|---|---|---|---|
| CTP-001 | Wind to completion: the count freezes at >= 165011 and the yards line reads 30.0 (P1). Cross-check the constants against a host recomputation of SPEC-001 in float32. | SPEC-001, SPEC-002 | tests/unit/test_length_model.py::test_target_pulses_match_firmware_constants |
| CTP-002 | Early stop and restart: BTN1 in WINDING opens the relay within one main-loop pass; the next BTN1 restarts from zero (P1). | SPEC-002 | `manual` (P1) |
| CTP-003 | Screen text matches SPEC-003 character for character on the idle and winding screens (P1). | SPEC-003 | `manual` (P1) |
| CTP-004 | Beep timing 300/150 ms x3 measured on D14 with a scope or by ear; BTN1 during the beeps cancels (P3, P1). | SPEC-004 | `manual` (P3) |
| CTP-005 | LED bargraph steps at 25/50/75/100 % and the idle pattern is LED1+LED3 (P1, P5). | SPEC-005 | `manual` (P5) |
| CTP-006 | Encoder: a full turn adds 2400 counts CW, subtracts CCW, never below 0; ISR counter increments with no movement (P2). | SPEC-006 | `manual` (P2) |
| CTP-010 | Every pin in SPEC-010 toggles the expected element with the harness images (P2..P6). | SPEC-010 | `manual` (P2..P6) |
| CTP-011 | A press shorter than 8 ms is ignored; a held button produces exactly one event per edge; startup events are discarded (P4, P6). | SPEC-011 | `manual` (P6) |
| CTP-012 | `test_timer0` shows TCCR0B=03, TIMSK0=02, MC=00 and a running tick; `test_encoder` shows TCCR1B=09, TIMSK1=02 (P6, P2). | SPEC-012 | `manual` (P6) |
| CTP-013 | `test_lcd` screens 0..3 render correctly including corner cursors and backlight control (P4). | SPEC-013 | `manual` (P4) |
| CTP-014 | Double-tap RST makes `2341:0036` appear (Windows `usbipd list` / Linux `dmesg`) and `/dev/ttyACM*` follows within 2 s. | SPEC-014 | `manual` (runbooks/flash.md) |
| CTP-015 | The built vector table routes every unused slot to `__bad_interrupt` and the handler sets PB1; `panic` disassembles to cli + sbi + spin. | SPEC-015 | tests/system/test_firmware.py::test_firmware_vector_table_binds_timer_isrs |
| CTP-016 | All six harness `.hex` files build from one `--build-only --target all` run. | SPEC-016 | tests/system/test_firmware.py::test_every_image_builds |
| CTP-020 | CLI: `--help` exits 0; `--target all` without `--build-only` exits 2; a missing image exits 1 with a hint; pyproject defaults load and CLI overrides win. | SPEC-020 | tests/integration/test_flash_cli.py::test_all_target_requires_build_only |
| CTP-021 | Host detection from a WSL and a generic kernel banner; attach retries until success and times out with a `usbipd list` dump; bind goes through PowerShell `RunAs`; plain Linux never calls usbipd. | SPEC-021 | tests/unit/test_flash_host.py::test_attach_retries_until_success |
| CTP-022 | The avrdude argv equals the legacy `upload.sh` invocation; the port wait picks the lowest `/dev/ttyACM*` and times out. | SPEC-022 | tests/unit/test_flash_build.py::test_avrdude_argv_matches_legacy_upload_script |
| CTP-023 | `hal_init` is called from `.init8` before `main`; slots 17 and 21 bind the two ISRs; no assembly sources exist in the tree. | SPEC-023 | tests/system/test_firmware.py::test_hal_init_runs_before_main |
| CTP-024 | Every image builds and `hello_world` prints on the simulated USART1 under simavr. | SPEC-024 | tests/system/test_firmware.py::test_hello_world_prints_on_usart1_in_simavr |
| CTP-030 | Every wire type round-trips through `to_bytes`/`from_bytes`, rejects out-of-range and non-finite values, and the handshake constants are singletons with the exact bytes. | SPEC-030, SPEC-031 | tests/unit/test_serial_packets.py::test_integer_round_trip_and_range |
| CTP-040 | The trace tables close: every referenced id exists and every artifact row has a verifier. | SPEC-040 | tests/system/test_process.py::test_trace_tables_are_closed |
| CTP-041 | Every `frob:waive TEST001` in firmware sources names `hardware-tests.md`. | SPEC-041 | tests/system/test_process.py::test_waivers_name_their_evidence |
| CTP-042 | `FROBLEMS.md` and `.frob/` are gitignored. | SPEC-042 | tests/system/test_process.py::test_froblems_is_gitignored |
