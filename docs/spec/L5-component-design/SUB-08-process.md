# L5 -- SUB-08 process

Satisfies SUB-08. Owned code: `docs/spec/**`, `docs/decisions/**`,
`docs/guides/**`, `frob.toml`, `tickets/**`,
`tests/system/test_process.py`, `.github/**`.

## Components

| ID | Component | Path | Responsibility | satisfies | verified-by | Milestone |
|---|---|---|---|---|---|---|
| COMP-0801 | trace closure test | tests/system/test_process.py | Parses every table in `docs/spec`, checks that referenced ids exist, that artifact rows have verifiers and test rows verify something, and that non-manual runnables collect. | SUB-08, SPEC-040 | UT-0801 | M1 |
| COMP-0802 | waiver evidence test | tests/system/test_process.py | Every `frob:waive TEST001` in firmware sources names the bench runbook. | SUB-08, SPEC-041 | UT-0802 | M1 |
| COMP-0803 | ignore rules test | tests/system/test_process.py | `FROBLEMS.md` and `.frob/` are gitignored. | SUB-08, SPEC-042 | UT-0803 | M1 |
| COMP-0804 | gate configuration | frob.toml, .github/workflows/ci.yml | pytest as the only frob runner, the CLI declared for flag coverage, entrypoint refs, CI installing the AVR toolchain. | SUB-08, SYS-030 | UT-0801 | M1 |

## Unit test plan

| ID | Case | verifies | runnable |
|---|---|---|---|
| UT-0801 | Trace tables close. | COMP-0801, COMP-0804 | tests/system/test_process.py::test_trace_tables_are_closed |
| UT-0802 | Waivers name their evidence. | COMP-0802 | tests/system/test_process.py::test_waivers_name_their_evidence |
| UT-0803 | Ignore rules hold. | COMP-0803 | tests/system/test_process.py::test_froblems_is_gitignored |
