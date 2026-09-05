# L1 -- Customer tests

Paired with [`L1-requirements.md`](L1-requirements.md). Each CT is an
acceptance scenario the customer can watch pass: given / when / then, in
plain words. The `runnable` column is the pytest node id that automates
it; hardware behaviour is `manual`, executed per
`docs/runbooks/hardware-tests.md` on the real panel and recorded as a
ticket attachment.

| ID | Scenario (given / when / then) | verifies | runnable |
|---|---|---|---|
| CT-001 | Given the winder idle with paper loaded, when I press BTN1 and let it run, then the motor starts, the display counts up, and the motor stops on its own at 30.0 yd with the count frozen. | REQ-001 | `manual` (hardware-tests.md, procedure P1) |
| CT-002 | Given a wind in progress, when I press BTN1, then the relay opens at once, the display returns to the idle screen, and the beeps play. | REQ-002 | `manual` (P1) |
| CT-003 | Given the idle screen, then it reads "-= PAPER WINDER =-", "TARGET: 30.0 yds" and "[BTN1] START"; given a wind, then it reads "WINDING...", a "CNT:" line with a percent, a "LEN x/30.0 yd" line and "[BTN1] STOP". | REQ-003 | `manual` (P1) |
| CT-004 | Given a wind that just finished or was stopped, then three beeps sound with short gaps; given I press BTN1 during the beeps, then they stop and the next wind starts. | REQ-004 | `manual` (P1) |
| CT-005 | Given a wind, then LED1..LED4 light one more at each quarter of the target; given idle, then LED1 and LED3 are lit; given a start, then all four flash on before the bargraph takes over. | REQ-005 | `manual` (P1) |
| CT-006 | Given a wind, when I pull paper backwards, then the count goes down but not below zero; when I start the next wind, then the count restarts at zero. | REQ-006 | `manual` (P2, test_encoder) |
| CT-010 | Given the machine as wired today, when the M1 firmware is flashed, then every panel element responds on the pins listed in `docs/hardware/pinout.md` with no wiring change. | REQ-010 | `manual` (P2..P5) |
| CT-011 | Given the board in its enclosure, when I double-tap RST and run `scripts/flash.py`, then the new firmware is running within a minute with nothing but the USB cable. | REQ-011 | `manual` (runbooks/flash.md) |
| CT-012 | Given firmware that deliberately triggers an unused vector or returns from main, then the fault LED (D15) comes on and the relay stays open. | REQ-012 | tests/system/test_firmware.py::test_firmware_vector_table_binds_timer_isrs |
| CT-013 | Given the six `test_*` images, when each is flashed, then it shows the screen described in `docs/runbooks/hardware-tests.md` and reacts to the buttons as described. | REQ-013 | `manual` (P2..P6) |
| CT-020 | Given a WSL machine with usbipd bound once, when I run `uv run python scripts/flash.py` and double-tap RST, then it builds, attaches the bootloader, waits for the port and flashes with no other command. | REQ-020 | tests/unit/test_flash_app.py::test_flash_on_wsl_attaches_first |
| CT-021 | Given a plain Linux machine, when I run the same command, then no usbipd or PowerShell step is attempted and flashing proceeds from `/dev/ttyACM*`. | REQ-020 | tests/unit/test_flash_app.py::test_flash_on_linux_skips_attach |
| CT-022 | Given the source tree, then there is no `.asm`/`.S` startup file, the two timer ISRs are `ISR()` functions, and the built vector table binds exactly slots 17 and 21 with every other slot on the bad-interrupt handler. | REQ-021 | tests/system/test_firmware.py::test_firmware_vector_table_binds_timer_isrs |
| CT-023 | Given the test suite, when it runs on a machine with avr-gcc and simavr, then every image builds and the hello_world image prints on the simulated USART1. | REQ-022 | tests/system/test_firmware.py::test_hello_world_prints_on_usart1_in_simavr |
| CT-030 | Given the desktop program and the winder on a serial link, when they connect, then the three-way handshake completes and typed values round-trip. | REQ-030 | tests/integration/test_serial_stream.py::test_handshake_round_trip (M2) |
| CT-040 | Given the repository, when `frob check` runs, then every REQ/SPEC/SYS/SUB/COMP row has a verifying test at its paired level and no row is orphaned. | REQ-040 | tests/system/test_process.py::test_trace_tables_are_closed |
| CT-041 | Given the repository, when `frob check` runs, then it reports no COV001/TEST001 errors and every waiver names its bench evidence. | REQ-041 | tests/system/test_process.py::test_waivers_name_their_evidence |
| CT-042 | Given the ticket ledger, then every closed ticket has acceptance criteria, evidence node ids and a done report, and `FROBLEMS.md` is gitignored. | REQ-042 | tests/system/test_process.py::test_froblems_is_gitignored |
