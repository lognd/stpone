# DEC-017 -- Zero-shot winds land before the goal; BTN3 in IDLE forgets the calibration

> Amended by [DEC-018](DEC-018-four-inch-slack-never-past-the-target.md): slack 4 in, `kZeroOver` 0, first-bump safety 7/4.

Date: 2026-11-07. Owner: logan. Status: accepted. Basis: owner direction after DEC-016: paper is
easy to wind on by hand and impossible to unwind, so a zero-shot wind must land just BEFORE the goal,
never after; and the operator needs a button to throw the stored measurement away for a new roll or
a bad calibration.

Amends [DEC-016](DEC-016-zero-shot-carried-follow-through.md) (the aim point) and
[DEC-012](DEC-012-two-yard-coast-and-per-wind-learned-bumps.md) (the PASS rule on a zero-shot wind).

## Decisions

1. A zero-shot main run cuts at `goal - kZeroAim - F` (`kZeroAim` = 94 pulses, about 0.5 in), so it
   lands about 0.5 in before the goal, about 2.5 in short of the label.
2. On a zero-shot wind the settled count passes (no bump) when it is `>= goal - kZeroTol`
   (`kZeroTol` = 188 pulses, about 1 in), i.e. the guarantee is relaxed by up to 1 in below the goal
   there; below that the usual bumps run up to the goal. A characterizing wind still passes only at
   `>= goal`. The goal is already `kSlackPulses` (2 in) short of the label.
3. The carried F is refreshed when a wind settles outside `[goal - kZeroTol, goal]`; it is dropped
   when it settles more than `kZeroOver` past the goal (as in DEC-016).
4. BTN3 in IDLE (previously unmapped) forgets the carried F with one CANCEL beep; the next wind
   measures again with bumps. The idle screen row 1 shows `cal: learns next` or
   `[3] new roll: cal ok` (unless manual mode or an encoder miss is shown there).

## Consequences

- A zero-shot wind never ends after the goal unless F drifts; it is then refreshed or dropped.
- A paper count up to 3 in short of the label is possible on a zero-shot wind.
- Tests: the 12-wind scenario in `trim_sim.cpp` asserts zero-shot winds end in
  `[goal - kZeroTol, goal]`; a BTN3 case checks the forget; the render cases cover the new rows.
