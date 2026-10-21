# DEC-003 -- Four-button panel, signed count, 40 kHz encoder poll

Date: 2026-10-20. Owner: logan. Status: accepted.
Amended by DEC-011: the buttons latch and every debounced edge (down or up)
is one click; "start only when pushed down" no longer holds.

## Decision

1. Buttons (latching, slow travel): BTN1 winds 30 yd, BTN2 winds 90 yd,
   BTN3 pauses/plays mid cycle (new state PAUSED), BTN4 manuals the winder
   while latched down. In WINDING/PAUSED, BTN1 or BTN2 stops the run.
2. Measuring wheel diameter is 4.0015 in (midpoint of the measured
   4.001/4.002). Targets: 1080 in -> 206187 pulses, 3240 in -> 618562.
3. The count is a signed 32-bit value; length is its magnitude, so the
   wheel may turn either way. No saturation at zero.
4. Timer1 polls the encoder at 40 kHz (`OCR1A = 399`). A two-bit jump
   (a missed state) is recovered by extrapolating the last direction and
   counted in `enc_errors`, which the idle and winding screens show.
5. Debounce is a 16-sample (16 ms) register, plus a 200 ms per-button
   lockout in the application. Manual is level-driven and armed only after
   BTN4 has been seen up in IDLE. `TIMER0_COMPA` is `ISR_NOBLOCK` so the
   encoder ISR can preempt it.

## Why

The panel buttons are latching with a long, slow travel, so a press is
an edge that may chatter for longer than 8 ms, and a button can be left
down across a power cycle. The old "reliable to about 250 RPM at 10 kHz"
claim sat at the Nyquist limit with no margin. The design point is now
90 RPM (a state lasts about 278 us, about 11 samples at 40 kHz). The
ISR costs about 120 of 400 cycles, so the energy saving of a slower
poll is negligible against a missed count.

## Consequences

- Supersedes: REQ-006 (negative clamp), and the 8 ms debounce, 10 kHz
  poll, 165011-pulse target and single-button start of SPEC-001/002/003/
  005/006/011/012 and SYS-002/003, which are edited in the same change.
- `enc_count` is `int32_t`; `enc_errors` is new ISR-owned state.
- Behaviour on the bench must be re-confirmed with P1 and P2.
