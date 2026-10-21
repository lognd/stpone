# DEC-008 -- Winding screen shows percent and a bar; the result shows the miss in inches

Date: 2026-10-20. Owner: logan. Status: accepted.

## Context

On the real panel the `LEN` row printed only ` / yd` and the over/short line
printed no number, because the formatter used `snprintf("%*lu")` and avr-libc's
printf does not support `*` widths (the PC test used glibc, which does). The
operator also cares about the raw winding progress only as percent done and
yards so far, and wants the miss in inches (or feet/yards when large), not in
whole tenths of a yard (3.6 in).

## Decision

1. `include/stpalpha/length.h` no longer uses printf at all: a small bounded
   `Text` builder writes digits and characters directly, so the AVR and the host
   test run identical code.
2. Winding / paused screen row 1 becomes ` 45% [#####--------]` (percent done
   right-aligned in 3 columns, then a 13-cell bar); the raw `CNT:` count is no
   longer shown. Row 2 (`LEN LLLL.l/TTTT.t yd`) is unchanged in shape.
3. The result screen row 1 is `DONE 30.1/30.0 yd` (`NOW ` while settling) as in
   DEC-006; row 2 is the miss against the target, computed from the pulse
   difference: under one yard `+7.2 in over` / `-12.4 in short` (tenths of an
   inch), from one yard on `+1.3 yd over` / `-2.0 yd short`, `on target` when the
   miss rounds to 0.0 in, and `settling...` while the count still moves.
4. The display refresh limit is 10 Hz (was 4 Hz): only changed characters are
   sent (DEC-007), so a percent or length change is a few bytes.

## Supersedes

- L1 CT-003 and L2 SPEC-003: the `CNT:%-10lu  %3u%%` winding row, and the
  result row text `+0.1 yd over` / `-0.2 yd short` (DEC-006 item 1).
- DEC-006 item 1: the difference "in whole tenths of a yard".

## Consequences

The raw count stays visible to the PC tool (`GET`, `DUMP`, stream) and on the
debug screens. Unverified on the bench until P1 and P9 are run.
