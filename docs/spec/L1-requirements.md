# L1 -- Customer requirements

Customer: Sandkey Services, operator of the paper winder. Developer and
co-owner of the work: Logan Dapp, LLC. Elicited 2026-09-05 from the
working `stp-upgrade` firmware (the behaviour the customer already
accepted on the bench), its hardware harnesses, and the modernization
brief for this repository.

Paired verification: [`L1-customer-tests.md`](L1-customer-tests.md)
(CT-nnn). Each REQ must be verified by at least one CT.

## A. Winding

| ID | Requirement | Milestone | verified-by |
|---|---|---|---|
| REQ-001 | I press one button and the winder runs until exactly 30 yards of paper have passed the measuring wheel, then stops by itself. | M1 | CT-001 |
| REQ-002 | I can stop a wind early with the same button, and the motor stops immediately. | M1 | CT-002 |
| REQ-003 | The display tells me what the target is when idle, and while winding shows the live count, the percent done, and yards so far against the target. | M1 | CT-003 |
| REQ-004 | When a wind finishes or is stopped the machine beeps three times so I hear it from across the shop; starting the next wind silences it. | M1 | CT-004 |
| REQ-005 | The four LEDs show progress in quarters while winding and a distinct resting pattern when idle. | M1 | CT-005 |
| REQ-006 | Paper pulled backwards is subtracted from the count, but the count never goes negative, and a reset happens only when I start a wind. | M1 | CT-006 |

## B. The machine as wired

| ID | Requirement | Milestone | verified-by |
|---|---|---|---|
| REQ-010 | The controller stays the Pro Micro already wired into the machine, with the existing pin assignment for the encoder, buttons, LEDs, buzzer, relay, and I2C display; no rewiring. | M1 | CT-010 |
| REQ-011 | New firmware goes in over the USB cable using the board's own bootloader; no programmer, no opening the enclosure beyond the reset tap. | M1 | CT-011 |
| REQ-012 | If something goes wrong inside the controller (an unexpected interrupt, the program falling off the end) the fault LED comes on and the motor is never started by the fault. | M1 | CT-012 |
| REQ-013 | Each part of the panel (buttons, LEDs, buzzer, encoder, display, timing) can be checked on its own with a dedicated test program when diagnosing wiring. | M1 | CT-013 |

## C. Developer workflow (owner requirement)

| ID | Requirement | Milestone | verified-by |
|---|---|---|---|
| REQ-020 | Building and flashing is one command from my WSL machine, including the Windows USB hand-off, and the same command works on plain Linux without any Windows steps. | M1 | CT-020, CT-021 |
| REQ-021 | The firmware is written the way the microprocessor course teaches it: avr-libc startup and `ISR()` handlers, no hand-written vector table. Behaviour is identical to the previous repository. | M1 | CT-022 |
| REQ-022 | The firmware builds and its boot wiring is verified automatically in the test suite (simulator), so a broken vector table is caught before a board is flashed. | M1 | CT-023 |

## D. Desktop monitor

| ID | Requirement | Milestone | verified-by |
|---|---|---|---|
| REQ-030 | A desktop program can talk to the winder over USB serial with a small typed packet protocol (handshake, fixed-width numbers, strings) so I can watch and debug it from a laptop. | M2 | CT-030 |

## E. The development process itself (owner requirement)

| ID | Requirement | Milestone | verified-by |
|---|---|---|---|
| REQ-040 | The project is built specification-first: every requirement traces to design and to a test at its level, checked by frob before code is accepted. | M1 | CT-040 |
| REQ-041 | Every public symbol carries documentation and test bindings that frob enforces; AVR-only code that no host test can run is waived explicitly with the bench runbook as its evidence. | M1 | CT-041 |
| REQ-042 | All work is tracked as frob tickets with acceptance criteria and recorded evidence; friction with frob itself is recorded in `FROBLEMS.md`. | M1 | CT-042 |

## Status

These are the requirements of firmware v1 for the original machine.
They are satisfied by M1 and frozen: v1 is in maintenance mode, and
requirements for newer machines live with their own firmware lines.

## Out of scope (decided, not forgotten)

- Configurable target length, multiple jobs, and a job counter are M3
  candidates; M1 keeps the fixed 30 yd target of the accepted firmware.
- The USB CDC port of the Pro Micro is only used by the bootloader in
  M1; runtime USB is disabled by the firmware (SPEC-030). The desktop
  link in M2 decides between USART1 and USB CDC.
