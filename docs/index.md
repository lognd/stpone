# stpone documentation

Firmware v1 for the original Sandkey winder: feature-complete and in
maintenance mode (newer machines have their own firmware lines).
Start with the specification cascade; everything else hangs off it.

## Specification (the V-model)

- [How the cascade works](spec/README.md)
- L1 [Customer requirements](spec/L1-requirements.md) and [customer tests](spec/L1-customer-tests.md)
- L2 [Requirement specification](spec/L2-requirement-specification.md) and [customer test plan](spec/L2-customer-test-plan.md)
- L3 [System specification](spec/L3-system-specification.md) and [system integration test plan](spec/L3-system-integration-test-plan.md)
- L4 [System design](spec/L4-system-design.md) and [subsystem integration test plans](spec/L4-subsystem-integration-test-plans.md)
- L5 component designs (one per subsystem, written per milestone):
  [firmware HAL](spec/L5-component-design/SUB-01-firmware-hal.md),
  [LCD driver](spec/L5-component-design/SUB-02-firmware-lcd.md),
  [winder application](spec/L5-component-design/SUB-03-firmware-app.md),
  [hardware harnesses](spec/L5-component-design/SUB-04-hardware-harnesses.md),
  [flash tool](spec/L5-component-design/SUB-05-flash-tool.md),
  [desktop core](spec/L5-component-design/SUB-06-desktop-core.md),
  [serial protocol](spec/L5-component-design/SUB-07-serial-protocol.md),
  [process](spec/L5-component-design/SUB-08-process.md)

## Hardware

- [Pinout and electrical notes](hardware/pinout.md) -- the Pro Micro pin map the firmware is written against

## Runbooks

- [Flashing the Pro Micro](runbooks/flash.md) -- `scripts/flash.py` on WSL and on plain Linux
- [Bench hardware tests](runbooks/hardware-tests.md) -- the `test_*` images and what each screen means

## Decisions

- [DEC-001 avr-libc startup and ISR() replace the hand-written vector table](decisions/DEC-001-avr-libc-startup.md)
- [DEC-002 M1 landed as a port, later milestones are ticket-first](decisions/DEC-002-m1-port-bootstrap.md)
- [DEC-003 four-button panel, signed count, 40 kHz encoder poll](decisions/DEC-003-buttons-signed-count-40khz-poll.md)
- [DEC-004 controller extraction, toggle counters, manual rename, host link](decisions/DEC-004-controller-host-link-manual.md)
- [DEC-005 fail-safes: debug host timeout, watchdog, ISR cut record, host-link hardening](decisions/DEC-005-host-fail-safes.md)
- [DEC-006 result screen after a wind, one length formatter, roll speed](decisions/DEC-006-result-screen-and-speed.md)
- [DEC-007 LCD bus: internal I2C pull-ups, 10 kHz, changed characters only](decisions/DEC-007-lcd-internal-pullups-slow-i2c.md)
- [DEC-008 winding screen percent bar, result miss in inches](decisions/DEC-008-display-progress-and-inch-miss.md)
- [DEC-009 bounded main-loop pass, 32-bit ms tick, prioritised LCD scheduler, encoder proof](decisions/DEC-009-bounded-main-loop-and-encoder-proof.md)
- [DEC-010 default bump (trim) mode with a minimum-amount guarantee, margin, loud failure](decisions/DEC-010-default-bump-trim-mode-with-minimum-amount-guarantee.md)

## Working in this repository

- [CLAUDE.md](../CLAUDE.md) at the root is the agent contract; [docs/guides/agent-playbook.md](guides/agent-playbook.md) is the per-ticket checklist.
- `FROBLEMS.md` (gitignored) records friction with frob itself.
- Root directories read by tools outside the source graph:

<!-- frob:external-reader dir=".github" reason="GitHub Actions reads the workflows under .github/workflows" -->
<!-- frob:external-reader dir="cmake" reason="cmake reads the AVR toolchain file named on the scripts/flash.py command line" -->
<!-- frob:external-reader dir="invariants" reason="frob's invariant gate reads invariants/ by convention" -->
<!-- frob:external-reader dir="tickets" reason="frob's ticket ledger, read by frob ticket" -->
