# L4 -- System design (subsystems)

Decomposes the system of L3 into subsystems with owned code, a
responsibility, and interfaces. Each SUB satisfies SYS items and is
verified by SUBT items in
[`L4-subsystem-integration-test-plans.md`](L4-subsystem-integration-test-plans.md).
Component design (L5) exists per subsystem under `L5-component-design/`.

## Firmware subsystems

| ID | Subsystem | Owned code | Responsibility and interfaces | satisfies | verified-by | Milestone |
|---|---|---|---|---|---|---|
| SUB-01 | firmware-hal | src/stpalpha/hal/setup.c, include/stpalpha/hal/setup.h | Boot (`hal_init` from `.init8`), the two timer ISRs, the bad-interrupt handler, `panic`, the actuator API, and the shared ISR/main state. Out: the C-linkage header. | SYS-002, SYS-003, SYS-004, SYS-006 | SUBT-01 | M1 |
| SUB-02 | firmware-lcd | src/stpalpha/hal/lcd.c, include/stpalpha/hal/lcd.h | Blocking TWI + HD44780 4-bit driver behind a PCF8574. Out: `lcd_*` API. | SYS-005 | SUBT-02 | M1 |
| SUB-03 | firmware-app | src/stpalpha/main.cpp, include/stpalpha/machine.h | The winder state machine, length model constants, screen renderers, the non-blocking beep sequencer, LED bargraph. In: SUB-01, SUB-02. | SPEC-001, SPEC-002, SPEC-003, SPEC-004, SPEC-005, SYS-004 | SUBT-03 | M1 |
| SUB-04 | hardware-harnesses | tests/avr/*.cpp | One image per panel element plus `hello_world` (USART1 smoke used by the simulator tests). In: SUB-01, SUB-02. | SYS-010, SPEC-016 | SUBT-04 | M1 |

## Host tooling subsystems

| ID | Subsystem | Owned code | Responsibility and interfaces | satisfies | verified-by | Milestone |
|---|---|---|---|---|---|---|
| SUB-05 | flash-tool | src/stpone/flash/**, scripts/flash.py, CMakeLists.txt, cmake/** | Config (pyproject + CLI), cmake configure/build, host detection, usbipd attach/bind, port wait, avrdude, the `FlashApp` state machine and the CLI. | SYS-010, SYS-011, SYS-012 | SUBT-05 | M1 |
| SUB-06 | desktop-core | src/stpone/logging/**, src/stpone/common/**, src/stpone/exception.py, src/stpone/__init__.py | House logging (dictConfig, stdout/stderr split), typani re-exports, `DeveloperException`. | SYS-030 | SUBT-06 | M1 |
| SUB-07 | serial-protocol | src/stpone/serial/** | Wire types, the `Serializable` hierarchy, the handshake constants, the `RDTCommunication` transport contract. | SYS-020, SYS-021 | SUBT-07 | M2 (types carried over in M1) |

## Process subsystem

| ID | Subsystem | Owned code | Responsibility and interfaces | satisfies | verified-by | Milestone |
|---|---|---|---|---|---|---|
| SUB-08 | process | docs/spec/**, docs/decisions/**, docs/guides/**, frob.toml, tickets/**, tests/system/test_process.py, .github/** | The V-model, the gate configuration, the ticket ledger, CI, and the process tests. | SYS-030 | SUBT-08 | M1 |
