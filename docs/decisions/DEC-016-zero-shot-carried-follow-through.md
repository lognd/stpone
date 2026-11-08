# DEC-016 -- Zero-shot winds from a carried follow-through; first-bump safety back to 3/2

Date: 2026-11-07. Owner: logan. Status: accepted. Basis: the third bench wind of a 30 ft roll
(+2.7 in by the estimate, +4 in actual, again two large bumps and one small) and the owner's
direction: stop short (it is easy to roll more, hard to unroll), the towel damper makes a
moderate overshoot tolerable, and characterize once then wind without stop/starts.

Supersedes [DEC-015](DEC-015-one-bump-first-bump-and-win7-settings-tag.md) (first-bump safety 1
-> 3/2, overshoot gate 6000 -> 800; the `WIN7` tag stays) and amends
[DEC-012](DEC-012-two-yard-coast-and-per-wind-learned-bumps.md) (the main-run F now outlives a wind).

## Decisions

1. `kFullSafetyNum/Den` = 3/2 again: the first bump after the main run leaves 3F/2 for the coast,
   so a characterizing wind lands short rather than over. Safety 1 had overshot the goal by about
   6 in.
2. The full-speed follow-through F measured after a clean main-run cut is carried in RAM. For the
   next `kCarryWinds` = 10 panel winds the main run cuts at `goal + kZeroAim - F` (`kZeroAim` = 94
   pulses, about 0.5 in), so it lands just past the goal and passes with no bump. The goal is still
   `kSlackPulses` (2 in) short of the label, so the paper ends about 1.5 in short.
3. The carry is refreshed when a wind ends short of the goal (F was too large), dropped when a main
   run ends more than `kZeroOver` (= the 2 in slack) past the goal, dropped when `coast` is changed,
   and expires after 10 winds so the 11th characterizes again with bumps. It is never saved or
   settable and power-up clears it. A paused main run is not clean and teaches nothing.
4. The debug `RUN` is unchanged and never uses the carry.

## Consequences

- The first wind after power-up (and every 11th) needs bumps; the others should not. If the
  true follow-through drifts (a wetter towel, a different roll) a zero-shot wind can land short
  and bump, which refreshes F.
- The wheel diameter is unchanged: +2.7 vs +4 is a 0.36 % estimate error (about 1.3 in over 30 ft),
  within the reading error of one roll; re-measure over a few rolls before moving it.
- Tests: `trim_sim.cpp` drives one controller through 12 steady winds (characterize, 10 zero-shot
  winds in [goal, label], re-characterize) and checks a fresh controller does not start zero-shot.
