# DEC-012 -- 2 yd assumed coast, per-wind learned bump sizing, probe and short final steps

Date: 2026-10-21. Owner: logan. Status: accepted. The numbers below come from the source and from
the host model in `tests/unit/trim_sim.cpp`. None comes from a bench run; the relay-drop lag, the
true coast and the trim time on the machine are unverified.

Supersedes (in part) [DEC-010](DEC-010-default-bump-trim-mode-with-minimum-amount-guarantee.md): the
default `coast`, algorithm steps 3 and 4, and the bump/time limits.

## Context

Owner request: "Set the bump to within 2 yards rather than 1 foot." The true full-speed coast of
the machine is unknown; the owner estimates 1 to 6 ft. With DEC-010's 12 in assumption, any true
coast above 12 in made the main run itself overshoot by `true - 12 in`. A coast of 3 ft already
gave about 35 in of extra paper, and 6 ft gave about 80 in. The DEC-010 final pass also held the
relay on until the goal, so every wind that needed bumps coasted one bump's worth past it.

Simply changing the default to 72 in under the DEC-010 algorithm is unsafe. In the model a
machine with no coast then **fails SHORT without a fault**: the halving bumps use up the
16-bump/30 s budget. With small true coasts the final pass still overshoots by up to 2970 pulses.

## Decision

### Default coast

`coast` = **13746** pulses = 72 in (2 yd). The firmware's own wheel math gives a circumference of
`3141593 * 101638 / 1e6` = 319305.2 um per 2400 counts. 72 in = 1828800 um, so
`1828800 * 2400 / 319305.2` = 13745.8 -> 13746. DEC-010 printed 319297.6 um; that figure was a
typo, and its 2291 and 191 are the same under either figure. Default 30 yd wind: goal = 206187 +
191 = 206378, main cut `eff_target` = 192632 (was 204087). `margin` stays 191 and `bump` stays 1.
The Params factory defaults, the EEPROM-less boot, `DEFAULTS` and the Rust `FACTORY` carry 13746.

### Bump sizing (replaces DEC-010 steps 3-4)

After every settle (`remaining = goal - |count| > 0`) the next bump `b` is the largest of the
rules below that applies. Each bump is armed at `enc_stop_at = |count| + b <= goal`, and the ISR
cuts it.

1. **Full-speed rule.** `F` is the largest follow-through measured this wind: the main run's
   `settled - eff_target`, then the coast of any later bump if that is larger. The rule is
   `b = remaining - kCoastSafety * F`. No bump can coast further than a full-speed run.
2. **Ratio rule.** It applies once a probe has been measured. `P` is the worst follow-through of
   a `kMinBump` step this wind. `r` is the follow-through per commanded pulse: from the probe at
   first, then from the last ratio-sized bump. The rule is
   `b = (remaining - kCoastSafety * P) / (1 + kCoastSafety * r)`, computed in x16 fixed point.
3. **Probe and final steps.** With nothing learned, or when both rules give less than
   `kMinBump`, the bump is a `kMinBump` (2 pulse) step. The first such step is the probe. Near the
   goal the steps are therefore short, and the overshoot past the goal is at most the coast of one
   short step. No bump is ever armed past the goal: `b` is capped at `remaining`.

`kCoastSafety` = 2 means a bump is sized so that twice the worst follow-through seen this wind
still lands at or short of the goal. When unsure, the machine goes slower rather than overshooting.

### Learning is per wind and RAM only

- All learned values (`full_coast_`, `probe_over_`, `ratio16_` and their flags) are reset by
  `forget()`, which `begin()` calls at every wind start. They are not in `Params`, not in the
  EEPROM image, not reported as settable by `SET`, and never carried into the next wind.
- They are learned only from the coast after a relay cut: the main-run cut, or an ISR-cut bump.
  A pause during the main run makes its coast unusable as a sample. A bump interrupted by a pause
  teaches nothing and is not charged to `kMaxBumps`. A BTN4 manual toggle while paused discards
  the sample being measured.
- A debug-mode `RUN` has no trim and learns nothing. `RUN n` with `n <= coast` is refused, as
  before; the hostlink checks the argument first, so `RUN 5000` now answers `ERR bad_arg`.
- **Counts that move in IDLE** (for example, the roll turned by hand) cause no action: no
  learning, no relay, no result and no alarm, and they do not alter later winds. There is no
  hand-spin calibration feature.
- Test: wind A followed by wind B gives B exactly the bump sequence of a fresh controller
  (`trim_sim` scenario 9997, which also moves counts in IDLE and runs a debug `RUN` between them).

### Limits

| Constant | DEC-010 | DEC-012 | Why |
|---|---|---|---|
| `kDefaultCoastPulses` | 2291 (12 in) | 13746 (72 in) | owner request |
| `kMaxBumps` | 16 | 24 | probe plus short final steps; model fault-free max 15 |
| `kTrimMaxMs` | 30000 | 45000 | model fault-free max 25.7 s at a 3 ft true coast; slow winds must PASS |
| `kMinBump` | 8 | 2 | smaller final steps; trim max 245-553 pulses at 2, 509-931 at 8 |
| `kCoastSafety` | -- | 2 | new |
| `kFinalReserve`, `kFinalWindow` | 3, 382 | removed | the final pass to the goal is gone |

Unchanged: `kBumpMaxOnMs` 1500, `kBumpStallMs` 300 and `kMaxStalls` 2 (jam and stall detection),
`kTrimSettleMs` 500, the panel rules, the loop bound, PASS only when the settled `|count| >= goal`,
and the loud SHORT/STOPPED failure.

## Modeled results (`tests/unit/trim_sim.cpp`, 5 seeds x 300 winds per row, fault-free PASS)

Overshoot is `final - goal` in pulses (191 = 1 in). OLD is the DEC-010 algorithm with coast 2291;
NEW is this record with coast 13746. The true coast is fixed per row and jittered +-30 % per coast.

| True coast | OLD mean / max | NEW mean / p99 / max | NEW bumps mean / max | NEW trim time mean / max |
|---|---|---|---|---|
| 0 | 21 / 86 | 7 / 58 / 67 (0.4 in) | 4.8 / 11 | 8.0 / 11.9 s |
| 6 in (1146) | 334 / 1062 | 9 / 64 / 151 (0.8 in) | 8.9 / 14 | 12.8 / 16.4 s |
| 12 in (2291) | 303 / 2798 | 14 / 106 / 124 (0.6 in) | 9.5 / 15 | 15.3 / 19.3 s |
| 36 in (6873) | 4648 / 6600 | 37 / 297 / 352 (1.8 in) | 9.4 / 15 | 20.9 / 25.7 s |
| 72 in (13746) | 11477 / 15453 | 567 / 3631 / 3988 | 2.2 / 12 | 11.6 / 24.9 s |
| 96 in (18328) | 16069 / 21414 | 4821 / 9401 / 9846 | ~0 / 5 | 10.8 / 15.2 s |
| 144 in (27492) | 25316 / 33031 | 13771 / 21637 / 21827 | 0 / 0 | 15.7 / 20.2 s |

The DEC-010 algorithm used at most 3 bumps and 4.5 s of trim. The new one uses more short bumps
and up to about 26 s, so it trades time for paper as asked. Across 20 random seeds (true coast
0..2x the assumption, faults and pauses included), the overshoot of winds that ended on a bump
was at most 479 pulses (2.5 in); with a fixed 72 in coast it was at most 574 pulses (3.0 in).
The pytest gate is 800 pulses. Every PASS in every run ended with the true count >= goal, and
every fault-free wind PASSED.

### The physical floor (stated honestly)

- **Trim floor.** Even a 2-pulse step coasts. The relay drops 5-30 ms after the cut, and the
  roll keeps the speed it gained in that time, so a step coasts about `C * (a * t_lag)^2 /
  vmax^2`. That term sets the 2-3 in worst case of a wind that ends on bumps. It cannot be lowered
  without a faster relay or a brake.
- **Main-run floor.** The main cut is fixed at `goal - coast`. When a wind's true coast exceeds
  the assumed 72 in (by nature or by the +-30 % jitter), the main run alone overshoots by
  `true - assumed`, and no trim can take paper back. Learning is per wind, so it cannot move the
  next wind's main cut. The fix is a calibrated `coast` (runbook calibration), not the trim.

## Why the ISR is untouched

Bumps are armed through `enc_stop_at` exactly as in DEC-010; `setup.c` and the Timer1 handler are
unchanged, so the DEC-009 cycle analysis holds. The new sizing is O(1) integer work per settle.

## Memory (from the built image, avr-size, new vs HEAD)

`.text` 25976 bytes (+610) of 28672 usable; `.data` 494 (+12, entirely `g_ctl`: the Controller
grew from 99 to 111 bytes for the learned state); `.bss` 862 (unchanged). RAM is 1356 of 2560
bytes, leaving 1204 for the stack (was 1216; the deepest chain is about 560).

## Consequences

- **EEPROM: a SAVEd image keeps its old coast.** The magic `WIN6` and `fw=6` are unchanged, so a
  machine that has `SAVE`d parameters boots with its stored `coast` (for example 2291), not
  13746. To use the new default the owner must run `DEFAULTS` then `SAVE`, or `SET coast 13746`
  then `SAVE`. With a stored 2291 the new sizing still applies to the bumps, but the main run
  overshoots any true coast above 12 in as before.
- The winder tool's calibration length check now allows for the larger coast: 36 in, or 36 in +
  coast when the coast exceeds the shortest test length (`length_check_pulses()`). Debug `RUN`
  test winds must exceed 13746 pulses.
- A wind that needs bumps takes longer (up to about 26 s of trim in the model, 45 s worst case
  before SHORT). This is unverified on hardware.

## Supersedes

- **DEC-010**: the default coast (2291 -> 13746), algorithm steps 3 and 4 (half bump, learned
  `over_est`, final window, final pass to the goal), the limits (`kMaxBumps` 16 -> 24,
  `kTrimMaxMs` 30 s -> 45 s, `kMinBump` 8 -> 2, `kFinalReserve` and `kFinalWindow` removed) and
  the circumference figure 319297.6 -> 319305.2 um.
- **DEC-004, DEC-005, DEC-006, DEC-009**: their DEC-010 amendment notes that quote `coast` 2291
  read 13746.
- L1 is unchanged (it states no coast figure). **L2** SPEC-001 (default coast), SPEC-007
  (trim algorithm and limits), CTP-007 (random coast range); **L3** SYS-006 ("16 bumps or 30 s" ->
  "24 bumps or 45 s").
- `docs/hardware/serial-protocol.md`, `docs/runbooks/calibration.md`, the runbook P11 and the
  manual follow.
