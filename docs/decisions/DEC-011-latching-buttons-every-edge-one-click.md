# DEC-011 -- Latching panel buttons: every edge is one click, state kept in software

Date: 2026-10-20. Owner: logan. Status: accepted (rewritten the same day:
the first version assumed momentary buttons and was wrong).

Supersedes: DEC-003 item 1 ("start only when pushed down", "popping up does
nothing", manual "while latched down") and the first version of this record
("momentary panel buttons, press edge only").

## Decision

The panel buttons are LATCHING switches: a push flips the contact and it
stays flipped until the next push. A push is therefore ONE debounced edge,
down or up, and every edge is one click (`take_pending` in
`src/stpalpha/main.cpp`; there is no level filter). What a click does lives
in the controller, never in the switch level:

- IDLE: a BTN1 click starts the short wind, a BTN2 click the long one,
  whichever way the latch moved; a BTN4 click toggles manual.
- WINDING/PAUSED/TRIM: a BTN1/BTN2 click stops the wind, BTN3 pauses/plays,
  BTN4 toggles manual while paused.

Edges present at power-up (a button left latched down) are flushed after the
40 ms boot settle (`btn_flush`), so a latched-down button starts nothing.

## Why

Two rules both made one push insufficient on latching hardware:

- The first DEC-011 treated the buttons as momentary and dropped every
  release edge. On a latching switch half of all pushes ARE release edges,
  so every second push was ignored: a wind needed two pushes to start or to
  stop.
- DEC-003's "start only when pushed down" meant that after a wind finished
  with the start button latched down, popping it up did nothing and a
  second push was needed to start the next wind.

The simavr regression `tests/system/test_buttons_sim.py` reproduces both
on the old firmware and proves one push, one action on the fixed one.

## Consequences

- `Inputs::down_after` is gone; the fuzz drivers check "a wind starts only
  on a BTN1/BTN2 click" and "every BTN1/BTN2 click in IDLE starts a wind".
- The panel latch positions carry no meaning; the operator never has to
  "reset" a button before the next wind.
- The HAL still counts both edges (`btn_toggles`); the host-link
  `toggles`/`pending` fields count clicks.
- Bench: P6 latches each button down and up and sees exactly one action
  per push, in both directions.
