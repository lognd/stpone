# DEC-011 -- Momentary panel buttons, latching done in software

Date: 2026-10-20. Owner: logan. Status: accepted.

## Decision

The panel buttons are momentary switches. Every button acts on its PRESS
edge only (`take_pending` in `src/stpalpha/main.cpp`); the release edge is
consumed and ignored. The state a click toggles lives in the controller:
BTN1/BTN2 start (idle) or stop (winding/paused), BTN3 pause/play, BTN4
manual on/off.

## Why

DEC-003 assumed latching hardware and treated both debounced edges as a
click. On momentary buttons the release edge of the start click looked like
a stop click, so a wind needed two pushes. The owner wants the latching in
software.

## Consequences

- Supersedes: DEC-003 item 1 ("latching, slow travel", "while latched
  down") and the latching wording in `docs/hardware/pinout.md`.
- The HAL still counts both edges (`btn_toggles`); the host-link
  `toggles`/`pending` fields keep counting edges.
- Bench: P6 must press each button once and see exactly one action.
