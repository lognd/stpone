# DEC-006 -- Result screen after a wind, one length formatter, roll speed

Date: 2026-10-20. Owner: logan. Status: accepted.

> Amended by [DEC-010](DEC-010-default-bump-trim-mode-with-minimum-amount-guarantee.md): the default `coast` is 2291 (not 0), `margin` (default 191) is added to the target and panel winds trim by default; statements below about a zero default coast or a stop exactly at `target - coast` read accordingly. A held SHORT/STOPPED result does not clear after 60 s.

## Decision

1. A panel wind that ends (target reached, or BTN1/BTN2) no longer zeroes
   the encoder count or `enc_errors`. The count is kept while the motor
   coasts. The IDLE display shows a RESULT screen instead of the idle
   screen:
   - while the count is still changing (or has been still for less than
     500 ms, `kResultSettleMs`): `NOW  <len>/<target> yd` and `settling...`;
   - once the count has held still for 500 ms: `DONE <len>/<target> yd` and
     (DEC-008: the miss is shown in inches/yards, not tenths of a yard) `+0.1 yd over` / `-0.2 yd short` / `on target` (lengths and the
     difference are in whole tenths of a yard, the difference taken between
     the two displayed tenths);
   - any later movement of the count returns it to the `settling` form.
2. The machine itself is unchanged and still a pristine IDLE: state IDLE,
   relay OFF, manual OFF, LEDs idle. Every button behaves exactly as in
   IDLE. The result screen clears (and the held count and `enc_errors` are
   zeroed on the next pass) when manual is turned ON, when `ZERO` or
   `DEBUG 1` arrives, 60 s after the stop (`kResultHoldMs`), or when the
   next wind starts (which zeroes the count as before). Power-up is a
   normal IDLE with no result; debug mode never shows one.
3. The result state lives in the pure `Controller` (`result()`,
   `clear_result()`, `Result::{NONE,SETTLING,DONE}`), timed by
   `Inputs::now_ms`, so the host fuzz and unit tests cover it.
4. All paper-length math and formatting is in one header,
   `include/stpalpha/length.h` (`pulses_to_tenths`, `format_tenths`,
   `format_result`): integer only (no float), rounded to nearest, and
   saturating at 9999.9 yd, so the in-wind `LEN` row keeps counting past
   the target when the stop is late and its `%4u.%u` field can never
   overflow or wrap. This replaces the float `circum_in`/`pulses_to_tenths`
   and the truncating `LEN` computation in `main.cpp`; the LEN row now
   rounds like the target (30.0 at exactly 206187 pulses instead of 29.9).
5. Host link, `fw=4`: `GET`, `DUMP` and `EVT state` carry a new
   `speed=<pulses per second>` field immediately after `count=`. It is the
   absolute count delta over a fixed window of 100 ms (`kSpeedWindowMs`)
   times 1000 over the real elapsed time of that window, unsmoothed,
   unsigned, refreshed once per window by `SpeedMeter`
   (`include/stpalpha/speed.h`) from the main loop (resolution 10
   pulses/s, 0 until the first window completes; 2400 counts/rev make
   90 RPM = 3600 pulses/s). A `ZERO` or a jump above 4000000 pulses in one
   window reads as 0, not as motion. The change is purely additive, so
   `kProtoVersion` stays 1: the Rust tool's `Fields::parse` and the Python
   `protocol.py` read `key=value` tokens by name and ignore unknown ones.
   The 5 s debug host-silence lease of DEC-005 is unchanged.

## Why

The operator never saw where the paper really ended: the count was zeroed
in the pass the relay dropped, so overwinding past 30.0/90.0 yd while the
motor coasted was invisible, and the length could not be used to tune
`coast` by eye. The PC calibration tool needs the roll speed to know when
the roll is up to speed before it trusts a measurement.

## Consequences

- Supersedes: DEC-004 item 4 (a finished or stopped wind returns to a
  pristine IDLE with count and `enc_errors` cleared): the machine is still
  pristine (relay, manual, state), but the count and `enc_errors` are held
  for the result screen until it clears. The "pristine IDLE on every way out
  of debug" of DEC-004 item 8 / DEC-005 items 1 and 3 is NOT changed.
- Supersedes: DEC-003 and DEC-004 wording that the idle screen follows a
  stop immediately (DEC-003 item 4: `enc_errors` is shown on the idle and
  winding screens; it is now also shown on the result screen). DEC-003 has no
  statement about zeroing the count.
- Supersedes: SPEC-002 (leaving a wind: the result bookkeeping is added and
  the count is not zeroed), SPEC-003 (adds the RESULT screen rows and the
  rounded, saturating `LEN` row), SPEC-001 note that display lengths are
  float32 (the display now uses integer math; the target pulses were already
  integer-derived). The L3 requirement rows contain no "count cleared at
  stop" wording; SYS-003 gains one sentence.
- After a wind the host sees a non-zero `count` (and `errors`) in `IDLE`
  until the result clears; `ZERO` clears it. Calibration already sends
  `ZERO` before every `RUN`.
- RAM: the format strings add about 100 bytes of `.data`; measured in
  the build (see the done report).
- New bench procedure P9 (result screen and speed); P7 step 1/2 updated.
