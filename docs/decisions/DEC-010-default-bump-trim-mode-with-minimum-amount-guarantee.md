# DEC-010 -- default bump (trim) mode with a minimum-amount guarantee, margin, loud failure

Date: 2026-10-20. Owner: logan. Status: accepted (the numbers below are derived from the source and
from the host model in `tests/unit/trim_sim.cpp`; none comes from a bench run).

> Amended by [DEC-012](DEC-012-two-yard-coast-and-per-wind-learned-bumps.md): the default `coast` is 13746 (72 in); algorithm steps 3 and 4 are replaced
> by per-wind learned bump sizing with a 2-pulse probe and short final steps (no final window, no
> final pass to the goal); `kMaxBumps` is 24, `kTrimMaxMs` 45 s, `kMinBump` 2. The circumference
> below is 319305.2 um (319297.6 was a typo; 2291 and 191 are unchanged by it).

## Context

The owner's customers must receive AT LEAST the ordered amount of paper. Until now the machine cut
the relay at `target - coast` with `coast = 0` and no calibration, so an uncalibrated machine
overshot by the whole follow-through and nothing ever told the operator whether a wind had reached
its length. Requirement from the owner: a default "bump" mode that needs no calibration (assume the
wheel stops within a foot at full speed), cuts early, estimates, and then bumps until the length is
reached; a completed wind must be GUARANTEED to pass the target; a failure must be loud and must
never look like success.

## Decision

### Parameters (persisted, `fw=6`)

| Name | Meaning | Default | Range |
|---|---|---|---|
| `coast` | assumed follow-through at full speed (pulses) | **2291** (12 in) | `0 .. min(short,long)-1` |
| `margin` | safety allowance added to the target (pulses) | **191** (1.0 in) | `0 .. 2000`, and `max(short,long) + margin <= 4000000` |
| `bump` | 1 = trim mode with the guarantee, 0 = legacy single cut | **1** | `0` or `1` |

Derivations (wheel 101638 um, 2400 counts/rev, circumference `pi * 101638` = 319297.6 um per 2400
counts): 12 in = 304800 um -> `304800 / 319297.6 * 2400` = 2291.0 -> **2291**; 1.0 in = 25400 um ->
`25400 / 319297.6 * 2400` = 190.9 -> **191**; 2.0 in -> 381.8 -> **382** (`kFinalWindow`). The
Params factory defaults, the EEPROM-less boot, `DEFAULTS` and the Rust `FACTORY` object all carry
these values. `goal = target + margin` (`Controller::goal()`).

The margin exists because the encoder measures the wheel, not the paper: wheel wear or a wrong
diameter makes the machine over-count, and the owner may add a safety allowance. The UI/docs say so.

### Algorithm (panel winds started by BTN1/BTN2 with `bump = 1`; the debug-mode `RUN` is unchanged)

1. **Main run.** The relay is on and the 40 kHz ISR cuts it at `goal - coast` (`eff_target()`), as
   it always did. State `WINDING`.
2. **Settle.** State `TRIM`, relay off, `stop_at() == kNoStop`. Wait until `|count|` has not changed
   for `kTrimSettleMs` = 500 ms (= `kResultSettleMs`). `remaining = goal - |count|`; if `<= 0` the
   wind PASSED (finish).
3. **Conservative bumps** while `remaining > kFinalWindow` (382 pulses): bump size `b =
   remaining/2` for the first bump (nothing learned yet), later `b = remaining - over_est` where
   `over_est` is the largest overshoot-after-cut seen this wind (`settled move - commanded b`,
   floored at 0). If `b < kMinBump` (8) the final pass takes over. A bump arms `enc_stop_at =
   |count| + b` and drives the relay in the same `cli/sei` section of `apply()` as before; the
   ISR makes the cut, so main-loop latency never matters. Then settle again and recompute.
4. **Final pass bump.** When `remaining <= kFinalWindow`, or the conservative step cannot shrink
   the gap, or only `kFinalReserve` (3) of the `kMaxBumps` (16) are left: `enc_stop_at = goal`
   exactly and the relay stays on until the ISR sees `|count| >= goal`. The cut therefore
   guarantees `|count| >= goal` at the moment of the cut; only the small coast of a slow bump lands
   above it. If a bump stalls short of the goal the final pass is repeated.
5. **PASS only** when the settled `|count| >= goal >= target`: beep START, result `DONE`
   (`pass=1`). Everything else is a failure (below).
6. **Give up safely.** A bump whose relay has been on for more than `kBumpMaxOnMs` (1500 ms)
   without `|count|` changing for `kBumpStallMs` (300 ms) is cut and counted as a stall; two stalls
   end the trim. The trim also ends at `kMaxBumps` bumps or `kTrimMaxMs` (30 s, pauses excluded).
7. **Panel rules during the trim.** BTN1/BTN2 stop at once (relay off, ISR disarmed, in the same
   pass); BTN3 pauses (relay off, `stop_at` disarmed) and resumes the trim from the settle phase
   (the learned `over_est`, bump count and the time limit carry over; the pause time is not
   charged); BTN4 is manual only while PAUSED, capped by the ISR at the goal; a new wind cannot be
   started from the trim (BTN1/BTN2 are stop). `MachineState::TRIM` is reported as `state=TRIM`
   (a paused trim reports `PAUSED`, with `trim=1`).

### Failure is loud and never looks like success

A trim that ends without a PASS (two stalls/jam, bump or time budget exhausted) is **SHORT**; an
operator BTN1/BTN2 stop (in a trim or in the main run) is **STOPPED** and counts as short by
definition. Both:

- show on the display `!! UNDER TARGET !!` (row 0), `SHORT -x.x in` / `STOPPED -x.x in` (row 1; the
  miss against the GOAL, inches under one yard, yards from there), `LEN a/b yd` (row 2); the DONE
  wording is never used;
- hold the result screen: no 60 s auto-clear; it clears on the next button press;
- report `pass=0` in `GET` and `DUMP`/`EVT state` (`pass=-` while running, never run, or legacy mode;
  `pass=1` after a PASS) until the next wind starts;
- beep: SHORT sounds the long failure pattern (3 x 700 ms, 250 ms apart) again every 6 s while the
  screen is held, at most 10 times (about a minute); an operator stop sounds the single-beep CANCEL
  pattern once.

A watchdog reset also ends a wind with the relay open, but the count and the result are lost: the
machine boots to a normal idle screen and `pass` reads `-` (never `1`). The firmware does not claim
more: it cannot tell a watchdog reset from the bootloader's own reset without bench evidence.

### The guarantee, stated exactly

> Given that the encoder counts correctly (no `enc_errors` beyond those the ISR compensates) and the
> wheel-size parameters are right, a **completed** wind (the machine reports `pass=1`, `DONE`) has
> a settled `|count| >= target + margin`. Anything else is reported SHORT or STOPPED.

Not guaranteed: an upper bound on the overshoot. It is bounded only by the assumption that the true
full-speed coast is at most about 12 in and a bump's coast is no larger than the coast learned from
the earlier bumps; a larger coast still passes but with more paper over. A jam in the main run
(before the trim) is not detected by the stall rule (it exists only for bumps; the operator's BTN1/2
and the host watchdog are the means). The guarantee is about the encoder count; it says nothing
about slip between paper and wheel beyond what `margin` absorbs.

### Modeled overshoot (`tests/unit/trim_sim.cpp`, 20000 random winds, 100 seeds)

Model: constant acceleration to full speed (3600 pulses/s) over a 300..1500 pulse ramp, Coulomb
friction (a bump that reached speed `v` coasts `C (v/vmax)^2`) plus 5..30 ms of relay-open lag,
start lag 20..200 ms, +-30 % jitter on every coast, true full-speed coast `C` drawn in 0..4582 pulses
(twice the 12 in assumption), random BTN3 pauses with BTN4 nudges, jams, creeping slip, weak motors
and operator stops. Results: every PASS ended with the roll's true count `>= goal`; every SHORT or
STOPPED was loud and held; fault-free winds always PASSED (17186 PASS, 2575 SHORT, 239 STOPPED in
the faulted ones); at most 10 bumps and 29.9 s of trim. Overshoot `final - goal` of a PASS: **mean
252 pulses (1.3 in) with the true coast within the 2291 pulse assumption, worst modeled 2605
pulses (13.6 in)**; with the coast up to twice the assumption the mean is 684 pulses (3.6 in) and
the worst 5080 pulses (26.5 in). The mean overshoot is dominated by the final pass bump, which holds
the relay on until the goal and so coasts a bump's worth beyond it; a more cautious final window is
a possible later trade of time for paper (owner's call).

## Why the ISR is untouched

Every bump is armed through `enc_stop_at` exactly as `apply()` arms a wind today; `setup.c` and the
Timer1 handler are unchanged, so the DEC-009 cycle analysis (worst path ~213 of 400 cycles, entry
latency <= 161) still holds. `main` additionally reads `enc_cut_pending()` (a single byte, atomic,
no `cli`) once per pass.

## Timing and memory (re-proved from the built image)

- Controller work per pass is O(1) with no waits (`millis()`-based timers, `controller_step` out of
  line). The longest pass is a pass that sends a ~320 byte `DUMP`/`EVT state` line (27.8 ms of UART)
  with the LCD slice reduced by one cell-time to 4 cells (31.2 ms, `g_heavy_tx`) plus < 3 ms of
  logic = about 62 ms (before: 68 ms with 5 cells); without a long line 39 + < 12 ms. Button response
  worst case 16 ms debounce + 27.8 ms UART + 34 ms heal half + 2 ms = 80 ms (< 100 ms).
- The new fields lengthen the widest `DUMP` to 319 bytes; `g_reply` grew from 320 to 352.
- RAM: format strings now live in flash (`snprintf_P` through `include/stpalpha/fmt.h`): `.data`
  482 bytes (was 1198 before this change set and 1416 with the new strings in RAM), `.bss` 862
  (was 816), leaving 1216 of 2560 bytes for the stack (was 546); the deepest chain (main, poll,
  `handle`, `dump`, `vfprintf`, ISR) is about 560 bytes.
- Flash: `.text` 25300 bytes (was 19066) of 28672 usable with the Caterina bootloader.

## Supersedes

L1 (closed spec text changed by this record):

- **REQ-001** ("runs until exactly that much paper has passed the measuring wheel, then stops by
  itself"): now "runs until at least the ordered amount plus the configured safety margin has
  passed the measuring wheel and then stops by itself; if it cannot, it says so loudly". The new
  **REQ-007** (minimum-amount guarantee and loud failure) is added to L1 with **CT-007**.
- **REQ-002** ("the motor stops immediately" on BTN1/BTN2): unchanged for the stop, and BTN3 also
  pauses/resumes the new trim phase.
- **CT-001** ("stops on its own at 30.0 yd (90.0 yd)") and **CT-002**: see their rewritten rows.

L2:

- **SPEC-001**: targets are unchanged but the stop point is `target + margin - coast`; the factory
  `coast` is 2291, no longer 0, and `margin`, `bump` are new persisted parameters.
- **SPEC-002**: the `WINDING -> IDLE` rule "`|count| >= target`" is replaced for `bump = 1` by
  `WINDING -> TRIM -> IDLE` as above; the stop rule `|count| >= target - coast` is `>= target +
  margin - coast`; the count is held for the result as before.
- **SPEC-003**: new rows `     TRIMMING...    ` (winding screen, TRIM state) and the failed-wind
  result screen (`!! UNDER TARGET !!`, `SHORT -x.x in` / `STOPPED -x.x in`, `LEN a/b yd`); a failed
  result is held until a button press (the 60 s clear applies to PASS and legacy results only).
- **SPEC-004**: adds the failure pattern (3 x 700 ms, repeated every 6 s up to 10 times).
- **SPEC-005**: the bargraph covers TRIM too (it is full: the nominal target is reached).
- **CTP-001..CTP-004**: rewritten rows (L2 test plan); new **CTP-007**.

L3 (system spec and SIT rows):

- **SYS-003** (shared state): `enc_cut_pending()` is read by the main loop each pass.
- **SYS-004** (host link and actuators): `fw=6`; `PARAM` adds `margin=`, `bump=`; `GET` adds `pass=`; `DUMP` and
  `EVT state` add `margin= bump= bumps= trim= pass=`; `SET margin|bump`; `state=TRIM`.
- **SIT rows** for the bench procedures P1 and P9 gain the trim steps (runbook P11).

Earlier decisions:

- **DEC-004, DEC-005, DEC-006, DEC-009** and `docs/hardware/serial-protocol.md`: every statement
  that the default `coast` is 0 ("short 206187, long 618562, coast 0") is replaced by `coast 2291,
  margin 191, bump 1`; the 5 s lease, debug RUN semantics and the fail-safes are unchanged.
- **DEC-006 item 2**: the result screen clears after 60 s, EXCEPT a held SHORT/STOPPED result,
  which clears on a button press.
- **DEC-009** worst-case table: longest pass 62 ms (4-cell slice on a pass that sent a long line),
  UART line 27.8 ms, RAM figures above; the `.data`/`.bss` headroom sentence is replaced by the
  numbers in this record.

## Consequences

- EEPROM image: magic `WIN6` (`kEepromMagic`), 34 bytes (was 26, magic `WIND`). A fw<=5 image is
  refused (`check_stored` answers `OLD_VERSION`), the machine keeps the compiled-in defaults and
  `LOAD` answers `ERR no_data`: the owner must `SAVE` again after updating (the UI says so).
- `SAVE` writes 34 bytes at 4 per pass (9 passes).
- The Rust tool parses every new field as optional (fw<6 still parses with `margin = bump = 0`);
  `DevState::Trim` and an `Unknown` fallback keep a newer firmware from breaking the poll.
- The calibration jobs still work with the factory coast 2291: test winds are made with the debug
  `RUN`, which cuts at `length - coast`, so the shortest test wind is `coast + 2400` pulses (one
  encoder turn of run-up) and the base length is 50000 pulses.
- UI-visible changes: see the done report; the UI agent updates the copy and adds the settings.
- Owner decisions to confirm: that the shortfall on the alarm screen is measured against the GOAL (target + margin),
  not the target; the 2.0 in final window (more overshoot, fewer bumps) and the failure alarm
  repeat count.
