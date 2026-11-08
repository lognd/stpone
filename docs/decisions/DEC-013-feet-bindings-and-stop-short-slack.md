# DEC-013 -- 30 ft / 90 ft bindings and a stop-short slack the operator pulls by hand

Date: 2026-11-07. Owner: logan. Status: accepted. Numbers come from the source and the host model
in `tests/unit/trim_sim.cpp`; none comes from a bench run.

Supersedes (in part) [DEC-006](DEC-006-result-screen-and-speed.md),
[DEC-008](DEC-008-display-progress-and-inch-miss.md),
[DEC-010](DEC-010-default-bump-trim-mode-with-minimum-amount-guarantee.md) and
[DEC-012](DEC-012-two-yard-coast-and-per-wind-learned-bumps.md): the button lengths, the display
unit, the default `margin` and the PASS rule.

> Amended by [DEC-014](DEC-014-first-bench-calibration-wheel-coast-and-one-stop-bumps.md): slack is 378 pulses, `coast` 18125 (96 in), wheel 4.0462 in, 30 ft = 67970 and 90 ft = 203911 pulses.

## Context

Owner request: "change bindings from 30 yd to 30 ft and 90 yd to 90 ft. Also, change so that it
stops a couple inches short but make it obvious, so that they can take slack out by hand."

## Decision

1. Button 1 = 30 ft (360 in, 68729 pulses). Button 2 = 90 ft (1080 in, 206187 pulses). The old
   30 yd length is now the 90 ft length. `coast` stays 13746 (72 in).
2. The display shows feet (tenths of a foot). A miss under 1 ft is shown in inches, else in ft.
3. `kSlackPulses` = 382 (2.0 in). The wind's goal is `(target + margin) - 382`, saturating at 0.
   The default `margin` is 0 (was 191). A PASS means `count >= goal`, so the paper may end up to
   2 in short of the nominal length by design; the operator takes the slack out by hand.
4. When `goal < target` a PASS is "slack": two 700 ms beeps (`Beep::SLACK`) and the result screen
   reads `** PULL SLACK **` / `PULL <len> BY HAND`. A margin of 382 or more cancels the slack and
   the PASS beeps and reads as before.
5. Hostlink and the `fw=6` field set are unchanged; `pass=1` still means PASS.

## Consequences

- A saved EEPROM (`WIN6`) keeps its old short/long/margin. Send `DEFAULTS` then `SAVE` to adopt
  the new values.
- The RUN debug command still cuts at the target itself (no slack), so the PC length check is
  unchanged.
- Spec L2/L3/L5 text still quotes yards in places; this record governs where they differ.
