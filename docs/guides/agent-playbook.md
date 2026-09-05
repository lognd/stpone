# Agent playbook (per-ticket checklist)

The contract is `CLAUDE.md` at the repository root; this is the
per-ticket checklist it points to. The agent is a leaf worker: it does all
the work of its ticket itself and reports back in at most 15 lines.

## 0. Setup

1. `uv sync` at the root (Python 3.11 via `.python-version`); the AVR
   toolchain, `simavr`, and `clang-format` must be on PATH for the
   firmware tests to run (they skip, loudly, otherwise).
2. `frob ticket start T-nnnn` (the lease). Read the ticket body, its
   acceptance criteria, and the L5 rows it cites.

## 1. Work

- Tests first. Write the test file(s) named in the ticket's UT/SUBT rows
  (the runnable column IS the node id that must collect), commit
  them red, then implement. Bind with `# frob:tests <path>::<symbol>
  kind="unit"` immediately above each test (at most two per test).
- Every new public symbol gets a one-line docstring (Python) or comment
  (C), a `frob:doc docs/spec/L5-component-design/SUB-nn-*.md#comp-nnnn`
  directive (methods: first line inside the body, after the docstring),
  and a matching `<!-- frob:describes path::Symbol -->` line under that
  `### comp-nnnn` anchor in the L5 doc. The L5 row is the symbol's
  documentation.
- Firmware: keep behaviour inside SPEC-001..016 (the numbers are the
  contract). AVR-only symbols get `frob:waive TEST001` naming the bench
  procedure; their executable evidence is `tests/system/test_firmware.py`.
  Run `cmake --build build --target format` before committing.
- Stay inside the ticket's `scope` globs. Anything else discovered along the way:
  `frob ticket new --origin agent` with a tight scope, one paragraph.
- Logging: module logger (`stpone.logging.get_logger`), log every
  meaningful branch and error path. typani `Result` for anything that
  can fail; `DeveloperException` only for programmer errors.

## 2. Gate and close

1. `frob format`, then `frob check --ticket T-nnnn` until green.
2. `frob test --base main` (touched set) and the full
   `frob test --all` before closing.
3. Commit, bind evidence (`frob ticket evidence`), write the done
   report, `frob ticket close`. Evidence before the done report; commit
   before either.
4. Friction with frob: append to `FROBLEMS.md` (gitignored).
