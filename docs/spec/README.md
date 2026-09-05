# Specification cascade (the V-model)

This directory is the left side of the V and, next to each level, its
paired right side. Nothing under `src/`, `include/`, `tests/avr/` or
`scripts/` may exist without a typed path from a customer requirement
down to the component that implements it and the test that verifies it
at the paired level.

| Level | Left artifact (this dir)                | Paired verification                       | ID prefixes      |
|-------|-----------------------------------------|-------------------------------------------|------------------|
| L1    | `L1-requirements.md`                    | `L1-customer-tests.md`                    | REQ / CT         |
| L2    | `L2-requirement-specification.md`       | `L2-customer-test-plan.md`                | SPEC / CTP       |
| L3    | `L3-system-specification.md`            | `L3-system-integration-test-plan.md`      | SYS / SIT        |
| L4    | `L4-system-design.md`                   | `L4-subsystem-integration-test-plans.md`  | SUB / SUBT       |
| L5    | `L5-component-design/<subsystem>.md`    | unit test plans inside the same file      | COMP / UT        |

## Process: partial waterfall into agile

1. L1 through L4 are written and closed for the WHOLE product before
   milestone code is accepted (the waterfall half). They change only
   through a decision record under `docs/decisions/` (see
   [DEC-001](../decisions/DEC-001-avr-libc-startup.md) and
   [DEC-002](../decisions/DEC-002-m1-port-bootstrap.md)) that carries a
   `supersedes` line with a reason -- never by silently editing a
   requirement.
2. L5 and the code are produced per milestone (the agile half): for each
   milestone the component designs and unit-test plans of the subsystems
   in that milestone are written, then the tests (failing), then the
   implementation makes them pass. `frob ticket` carries the work; every
   ticket's acceptance criteria cite the CT/SIT/SUBT/UT ids it satisfies
   and its evidence is the collected test node id.
3. M1 is special: it is the port of the working `stp-upgrade` firmware
   and tooling, landed as one bootstrap (DEC-002). Its L5 rows were
   written against the code as ported; from M2 on, rows come first.

## ID and trace conventions

- Every artifact is one row in a trace table:
  `| ID | statement | satisfies | verified-by |`.
- `satisfies` lists the ids one level up that this artifact traces to.
  `verified-by` lists the ids of the paired-level tests. An artifact with
  an empty `verified-by` is a defect in the cascade, on purpose.
- Test ids carry a `runnable` column: the pytest node id that is, or will
  be, the executable evidence. Hardware behaviour that no simulator can
  observe is marked `manual` with the bench procedure in
  `docs/runbooks/hardware-tests.md`; the result is recorded as a ticket
  attachment.
- Milestones: `M1` (1.0.0) the modernized port of firmware v1: firmware,
  flash tool, desktop core, process. v1 is feature-complete for the
  original machine and in maintenance mode; newer machines are served by
  newer firmware lines in other repositories. `M2` (the desktop monitor
  over USB serial, whose wire types SUB-07 already carries) and `M3`
  (operator features beyond parity) are recorded as the roadmap the
  code anticipates, not as scheduled work.

## Reading order

`L1-requirements.md` first; it is the only document written in the
customer's voice. Every lower level assumes it.

## Component designs (L5)

- [SUB-01-firmware-hal.md](L5-component-design/SUB-01-firmware-hal.md)
- [SUB-02-firmware-lcd.md](L5-component-design/SUB-02-firmware-lcd.md)
- [SUB-03-firmware-app.md](L5-component-design/SUB-03-firmware-app.md)
- [SUB-04-hardware-harnesses.md](L5-component-design/SUB-04-hardware-harnesses.md)
- [SUB-05-flash-tool.md](L5-component-design/SUB-05-flash-tool.md)
- [SUB-06-desktop-core.md](L5-component-design/SUB-06-desktop-core.md)
- [SUB-07-serial-protocol.md](L5-component-design/SUB-07-serial-protocol.md)
- [SUB-08-process.md](L5-component-design/SUB-08-process.md)
