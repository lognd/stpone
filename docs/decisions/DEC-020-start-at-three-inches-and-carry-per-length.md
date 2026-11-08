# DEC-020 -- The count starts at 3 in; the carry is per roll length

Status: accepted. Amends [DEC-018](DEC-018-four-inch-slack-never-past-the-target.md).

## Context

Loading the roll already unrolls about 3 in, so the count should start
there. Separately, a 90 ft roll ended 2.2 ft over on the bench when it
used the follow-through carried from a 30 ft calibration.

## Decision

1. `kStartPulses` = 566 (3 in): `goal = target + margin - kSlackPulses -
   kStartPulses`. Every wind is 3 in shorter on the wheel; the 4 in
   stop-short slack is unchanged. The panel DISPLAY adds `kStartPulses`
   to the count during a wind and on the result screen (and to the goal it
   compares with), so the count starts at 3 in and a finished wind reads the
   nominal length minus only the 4 in slack. Debug mode and idle stay raw
   (the host tool measures wheel pulses).
2. A carried follow-through is reused only for the same target length
   (`carry_target_`): a 30 ft F is never applied to a 90 ft wind. Bench: a
   90 ft wind straight after a 30 ft calibration ended 2.2 ft over; the 90 ft
   calibration itself and the zero-shot wind after it were fine. An earlier
   idea of cutting long rolls early was dropped as unnecessary.
