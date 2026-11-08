# DEC-015 -- One-bump sizing (first bump = remaining - F) and the WIN7 settings tag

> Amended by [DEC-016](DEC-016-zero-shot-carried-follow-through.md): first-bump safety is 3/2 again.

Date: 2026-11-07. Owner: logan. Status: accepted. Basis: the second bench review of the 30 ft roll
(three bumps: two large, one small) and the host model in `tests/unit/trim_sim.cpp`.

Supersedes (in part) [DEC-014](DEC-014-first-bench-calibration-wheel-coast-and-one-stop-bumps.md)
(the 3F/2 first-bump safety) and amends [DEC-010](DEC-010-default-bump-trim-mode-with-minimum-amount-guarantee.md)
(the EEPROM tag).

## Decisions

1. The first bump after the main run is sized `remaining - F` (`kFullSafetyNum/Den` = 1/1), F the
   measured full-speed follow-through. The owner asked for the wind to finish in one bump.
2. The EEPROM tag is `WIN7` (`kEepromMagic`); `WIN6` joins `WIND` as refused (`OLD_VERSION`). A
   reflashed board therefore ignores any saved settings and starts on the compiled-in defaults
   (wheel 4.0462 in, coast 96 in, 30 ft = 67970, 90 ft = 203911) with no `DEFAULTS`/`SAVE` step.
   Calibrations saved before this are lost and must be redone (or left at the new defaults).
3. The model's overshoot gate (`_TRIM_OVER_MAX`) is 6000 pulses (was 800).

## Consequences

- The guarantee is unchanged: PASS still means the settled count is at or past the goal, and the
  goal is still `kSlackPulses` (2.0 in) short of the label length.
- The model jitters each coast by up to +30 %, so a bump sized exactly for F can overshoot the goal
  by up to 0.3 F (5850 pulses, about 31 in, seen at the 2x assumed coast). 3/2 F kept the model
  under 800 pulses; 5/4 F still reached 3914. Real bumps start from rest and should coast less
  than F, so a real overshoot is expected to be far smaller; this is unmeasured.
- One bump is not guaranteed: if a bump coasts less than F the wind lands short, and the next steps
  are the 2-pulse probe and a ratio-sized bump. If bench winds still need more than two bumps the
  lever is a first-bump safety below 1, which needs the measured bump follow-through.
