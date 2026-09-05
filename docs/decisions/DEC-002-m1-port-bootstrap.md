# DEC-002 -- M1 landed as a port; later milestones are ticket-first

Date: 2026-09-05. Owner: logan. Status: accepted.

## Decision

Milestone M1 (the modernized port of `stp-upgrade`: firmware HAL in C,
the single flash entrypoint, the desktop core, and the V-model itself)
was written as one bootstrap and then split into tickets whose evidence
was bound after the fact. From M2 on, every ticket's tests are committed
red before the implementation, as `docs/spec/README.md` prescribes.

## Why

The behaviour being ported already existed and was accepted on the
bench; the requirement was "functionally the same". Writing the cascade
first would have meant reverse-specifying the assembly and then
re-implementing it, with the spec and the port drifting during the
gap. Porting first, then writing L1-L5 against the ported code and the
old assembly side by side, produced a spec that matches what runs.

## Consequences

- The M1 tickets cite their UT/SUBT/SIT/CT ids and bind evidence, but
  their commit history does not show a red test before the code.
  `tests/system/test_process.py` therefore does not enforce TDD ordering
  in M1 history; a later ticket adds that check for commits after the
  M1 tag.
- The serial protocol types (SUB-07) were carried over unchanged except
  for the bug fixes listed in its L5 doc; the M2 design owns the rest.
- Supersedes: nothing.
