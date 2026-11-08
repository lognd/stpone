# DEC-019 -- Tolerate scatter in the carried follow-through; show the reset cause

Status: accepted. Supersedes the `kZeroOver` value of
[DEC-018](DEC-018-four-inch-slack-never-past-the-target.md) (0 -> 377).

## Context

Bench report: the machine re-measured (characterized) about every other
wind instead of every ~10, and measuring winds needed 5-6 bumps. Two causes
are plausible: (1) a power blip resets the MCU and clears the RAM-held
follow-through; (2) with `kZeroOver` = 0 any settle even one pulse past the
goal dropped the carry, and the aim is only ~0.5 in short of the goal.

## Decision

1. `kZeroOver` = 377 pulses (2 in). A zero-shot wind that settles past the
   goal by no more than that refreshes F (re-aims the next cut shorter) and
   keeps the carry. Past that it is dropped. The result is still at least
   2 in short of the target (slack is 4 in), so the paper never goes past
   the target.
2. Boot reads MCUSR (kept in GPIOR0 by the `.init3` stub) and shows the
   cause for about 1.2 s on the LCD: `PWR power-on`, `BOR brown-out`,
   `WDT hang` or `EXT reset`. No serial reader is needed to see whether
   blips are resetting the machine.

## Not decided

The bump-sizing model (speed-dependent follow-through) is deferred until
bench evidence shows the shape; the host model already uses the Coulomb
law (follow-through grows with speed squared). Persisting F in EEPROM is
deferred until the reset display shows resets really happen.
