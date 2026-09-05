# L5 -- SUB-05 flash-tool

Satisfies SUB-05. Owned code: `src/stpone/flash/**`, `scripts/flash.py`,
`CMakeLists.txt`, `cmake/**`. Replaces `setup_usb.bat` + `flash.bat` +
`scripts/upload.sh` + the Makefile upload targets of `stp-upgrade`.

## Components

| ID | Component | Path | Responsibility | satisfies | verified-by | Milestone |
|---|---|---|---|---|---|---|
| COMP-0501 | config | src/stpone/flash/config.py | `FlashConfig` (frozen pydantic) and `from_external`: pyproject `[tool.stpone.flash]` then CLI; `CLI_FIELDS` names what the CLI may override. | SUB-05, SPEC-020 | UT-0501 | M1 |
| COMP-0502 | build | src/stpone/flash/build.py | `configure` (toolchain file), `build` (one target or `ALL_TARGET`), `hex_path`. | SUB-05, SPEC-024 | UT-0502 | M1 |
| COMP-0503 | host | src/stpone/flash/host.py | `HostKind`, `detect_host` (/proc/version), `attach_bootloader` (usbipd poll), `bind_bootloader` (PowerShell RunAs). | SUB-05, SPEC-021, SYS-012 | UT-0503 | M1 |
| COMP-0504 | port | src/stpone/flash/port.py | `wait_for_port`: glob poll with deadline, lowest match wins. | SUB-05, SPEC-022 | UT-0504 | M1 |
| COMP-0505 | avrdude | src/stpone/flash/avrdude.py | `avrdude_argv` (legacy invocation), `flash_image`. | SUB-05, SPEC-022 | UT-0505 | M1 |
| COMP-0506 | app | src/stpone/flash/app.py | `FlashApp`: the SYS-011 state machine over injected runner/host/which. | SUB-05, SYS-011 | UT-0506 | M1 |
| COMP-0507 | cli | src/stpone/flash/cli.py | `build_parser`, `main` (exit codes, remedy line); `scripts/flash.py` is the launcher. | SUB-05, SPEC-020 | UT-0507 | M1 |
| COMP-0508 | proc | src/stpone/flash/proc.py | `Runner` protocol and `run_command` (argv logging, never raises). | SUB-05 | UT-0508 | M1 |
| COMP-0509 | errors | src/stpone/flash/errors.py | `FlashError` (typani ErrorSet) and `REMEDIES`. | SUB-05, SPEC-020 | UT-0509 | M1 |

### comp-0501
<!-- frob:describes src/stpone/flash/config.py::CLI_FIELDS -->
<!-- frob:describes src/stpone/flash/config.py::FlashConfig -->
<!-- frob:describes src/stpone/flash/config.py::FlashConfig.from_external -->

### comp-0502
<!-- frob:describes src/stpone/flash/build.py::ALL_TARGET -->
<!-- frob:describes src/stpone/flash/build.py::TOOLCHAIN_FILE -->
<!-- frob:describes src/stpone/flash/build.py::hex_path -->
<!-- frob:describes src/stpone/flash/build.py::configure -->
<!-- frob:describes src/stpone/flash/build.py::build -->

### comp-0503
<!-- frob:describes src/stpone/flash/host.py::USBIPD -->
<!-- frob:describes src/stpone/flash/host.py::POWERSHELL -->
<!-- frob:describes src/stpone/flash/host.py::HostKind -->
<!-- frob:describes src/stpone/flash/host.py::detect_host -->
<!-- frob:describes src/stpone/flash/host.py::attach_bootloader -->
<!-- frob:describes src/stpone/flash/host.py::bind_bootloader -->

`bind_bootloader` is the one place PowerShell is used: elevation
(`Start-Process -Verb RunAs`) is what `usbipd bind` needs and what a
plain `usbipd.exe` call from WSL cannot get. `attach_bootloader` calls
`usbipd.exe` directly and dumps `usbipd list` on timeout.

### comp-0504
<!-- frob:describes src/stpone/flash/port.py::wait_for_port -->

### comp-0505
<!-- frob:describes src/stpone/flash/avrdude.py::avrdude_argv -->
<!-- frob:describes src/stpone/flash/avrdude.py::flash_image -->

### comp-0506
<!-- frob:describes src/stpone/flash/app.py::FlashApp -->

Every external effect goes through the injected `runner` and `which`,
so the whole state machine is unit-tested without a board.

### comp-0507
<!-- frob:describes src/stpone/flash/cli.py::build_parser -->
<!-- frob:describes src/stpone/flash/cli.py::main -->

### comp-0508
<!-- frob:describes src/stpone/flash/proc.py::Runner -->
<!-- frob:describes src/stpone/flash/proc.py::run_command -->

### comp-0509
<!-- frob:describes src/stpone/flash/errors.py::FlashError -->
<!-- frob:describes src/stpone/flash/errors.py::REMEDIES -->

## Unit test plan

| ID | Case | verifies | runnable |
|---|---|---|---|
| UT-0501 | Defaults without pyproject; table + CLI merge with CLI winning; the repo's own table is valid. | COMP-0501 | tests/unit/test_flash_config.py::test_from_external_reads_tool_table_and_cli_wins |
| UT-0502 | configure argv carries the toolchain file; build argv for one target and for all; ToolMissing/BuildFailed. | COMP-0502 | tests/unit/test_flash_build.py::test_build_target_and_all |
| UT-0503 | WSL/Linux/other detection; attach retries then succeeds; attach times out and lists; bind via RunAs; missing tools. | COMP-0503 | tests/unit/test_flash_host.py::test_attach_times_out_and_lists_devices |
| UT-0504 | First match (lowest) after one empty scan; timeout. | COMP-0504 | tests/unit/test_flash_port.py::test_wait_for_port_returns_first_match_sorted |
| UT-0505 | argv equals the legacy upload.sh; AvrdudeFailed and ToolMissing. | COMP-0505 | tests/unit/test_flash_build.py::test_avrdude_argv_matches_legacy_upload_script |
| UT-0506 | setup-only, build-only, linux flash, wsl flash, missing image, build failure. | COMP-0506 | tests/unit/test_flash_app.py::test_flash_on_wsl_attaches_first |
| UT-0507 | `--help` exits 0, unset options are None, `--target all` needs `--build-only`, missing image exits 1. | COMP-0507 | tests/integration/test_flash_cli.py::test_all_target_requires_build_only |
| UT-0508 | `run_command` returns the CompletedProcess of a real trivial command and never raises on non-zero. | COMP-0508 | tests/unit/test_flash_proc.py::test_run_command_never_raises |
| UT-0509 | Every `FlashError` member has a remedy. | COMP-0509 | tests/unit/test_flash_proc.py::test_every_error_has_a_remedy |
