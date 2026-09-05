# stpone -- agent contract

Specification-first, frob-enforced. Read `docs/spec/README.md` before
touching anything; read `docs/guides/agent-playbook.md` before every
ticket. The global rules in `~/.claude/CLAUDE.md` apply unchanged (ASCII
only, no emojis, no Co-Authored-By, typani Result for fallible
operations, pydantic v2, log everything, document as you go, no
duplication).

## Status

Firmware v1, maintenance mode: the M1 behaviour is frozen by the spec.
Bug fixes only, each through a ticket; feature work for newer machines
belongs to their own repositories.

## The order of work

1. `docs/spec/` L1-L4 are closed. Changing them needs a decision record
   in `docs/decisions/` (DEC-nnn) with a `supersedes` line.
2. L5 (component design + unit test plan) is written per milestone,
   then the tests, then the code. Every ticket cites the UT/SUBT/SIT/CT
   ids it makes pass and closes with the collected pytest node id.
3. Firmware behaviour is the contract of `stp-upgrade` (the previous
   repository, `../stp-upgrade`): pin map, timers, screens, beeps, LED
   patterns are all specified in L2/L3 and must not drift without a
   decision record.
4. `frob check` must be green for your ticket. Gate findings are fixed
   genuinely; a `frob:waive` needs a specific, honest reason (the AVR-only
   symbols carry TEST001 waivers because host unit tests cannot execute
   them -- the bench runbook and the simavr system tests are their
   evidence).

## Layout

```
src/stpalpha/, include/stpalpha/, tests/avr/   firmware (cmake, avr-gcc)
src/stpone/                                   Python package (flash tool, logging, serial)
scripts/flash.py                              the single operator entrypoint
tests/{unit,integration,system}/              pytest (system = builds every image, runs simavr)
docs/spec/                                    the V-model (source of truth)
docs/runbooks/                                flashing, bench hardware tests
```

## Commands

```
frob ticket doable               what to work on
frob check                       the whole gate (also runs cmake + ctest)
frob test --base main            tests bound to what you touched
uv run python scripts/flash.py   build + flash (see README)
```

## Friction with frob

Append to the gitignored `FROBLEMS.md` (format inside). Never edit the
frob repository from here.
