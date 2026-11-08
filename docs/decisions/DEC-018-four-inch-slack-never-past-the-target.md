# DEC-018 -- Four inch stop-short slack; never past the target

Date: 2026-11-07. Owner: logan. Status: accepted. Basis: the fourth bench wind (the stored
calibration was on and the wind needed no bump, but the owner wants it shorter still) and the
owner's rule: paper can be wound on by hand and cannot be unwound, so the wind must never go past
the target.

Amends [DEC-013](DEC-013-feet-bindings-and-stop-short-slack.md) /
[DEC-014](DEC-014-first-bench-calibration-wheel-coast-and-one-stop-bumps.md) (slack),
[DEC-016](DEC-016-zero-shot-carried-follow-through.md) (first-bump safety 3/2) and
[DEC-017](DEC-017-zero-shot-lands-before-the-goal-and-btn3-forgets-calibration.md) (`kZeroOver`).

## Decisions

1. `kSlackPulses` = 755 (4.0 in at the 4.0462 in wheel; was 378 = 2 in). The goal is
   `target + margin - 755`: 30 ft = 67215, 90 ft = 203156. A zero-shot wind lands about 0.5 in
   before the goal, so about 4.5 in short of the label.
2. `kZeroOver` = 0: a main run that settles at all past the goal drops the carried follow-through, so
   the next wind measures again (it was the 2 in slack).
3. The first-bump safety is 7/4 (was 3/2): the host model's jittered coast no longer carries any
   bump past the 4 in slack (overshoot of the goal <= 755 pulses in the fuzz), i.e. never past the
   target. The model gate `_TRIM_OVER_MAX` is 755 (was 800). Only the characterizing wind bumps, so
   the extra bump or two lands there.
4. Docs, runbook goals (30 ft 67215, 90 ft 203156, main cut 49090) and the manual use 4 in.

## Consequences

- A PASS still means the settled count is at or past the goal (zero-shot: within 188 pulses below
  it); the paper is then about 4 in short of the label and the operator winds the rest by hand.
- The model bound covers the bumps. A characterizing main run can still land past the goal when the
  true follow-through exceeds the assumed 96 in; that wind then needs no bump, drops nothing it
  never had, and the next wind measures again. A main run cannot be un-wound.
