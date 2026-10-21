# DEC-004 -- Controller extraction, toggle counters, manual rename, host link

Date: 2026-10-20. Owner: logan. Status: accepted.

> Amended by [DEC-010](DEC-010-default-bump-trim-mode-with-minimum-amount-guarantee.md): the default `coast` is 2291 (not 0), `margin` (default 191) is added to the target and panel winds trim by default; statements below about a zero default coast or a stop exactly at `target - coast` read accordingly.
>
> Amended by [DEC-012](DEC-012-two-yard-coast-and-per-wind-learned-bumps.md): the default `coast` is 13746 (72 in), not 2291; the trim sizes its bumps from the follow-through learned in the same wind.

## Decision

1. Button events are per-button wrapping toggle counters
   (`btn_toggles[4]`) incremented on every debounced edge; the main loop
   consumes one pending toggle per button per pass. The 200 ms
   application lockout is removed.
2. Panel logic lives in the hardware-free `stpalpha::Controller`
   (`include/stpalpha/controller.h`); `main.cpp` is a thin shell. The host
   fuzz test drives the code that runs on the AVR.
3. "Jog" is renamed "manual" everywhere (BTN4, screens, docs). BTN4 is a
   SOFTWARE latch: each click (a toggle in either physical direction)
   flips manual mode, which is off at power-up whatever the switch
   position. There is no arming and the physical level is not followed.
   It works in IDLE and while PAUSED mid-wind; clicks while WINDING (and
   the click in the pass that pauses) are ignored. Manual is cleared on
   wind start, resume, stop, and entering or leaving debug. The LCD hint
   reads "[4] click = manual" / "[4] click = auto". The wind stop rule
   always still applies.
4. A finished or stopped wind returns to a pristine IDLE: relay off,
   manual off, count and `enc_errors` cleared. The miss count of a run is
   therefore not shown after it ends.
5. Latching start buttons (BTN1/BTN2) start a wind only when toggled DOWN;
   toggling UP in IDLE is ignored. Any BTN1/BTN2 toggle still stops a run.
6. The Timer1 encoder ISR drops the relay itself when
   `|enc_count| >= enc_stop_at`, so a slow LCD redraw cannot delay a stop.
7. `coast` (follow-through, in pulses) is subtracted from every target:
   the relay drops at `target - coast`.
8. A host link (`include/stpalpha/hostlink.h`, `docs/hardware/serial-protocol.md`)
   runs over UART1 (D0/D1, 115200 8N1) for calibration and debug; params
   (wheel_um, short, long, coast) persist in EEPROM with a checksum.

## Why

A single event latch plus a lockout could swallow a stop push or turn a
button release into a new start. Extracting the controller makes every
interleaving testable on the host. The follow-through of the motor makes
the stop point a tunable that must be measured on the bench.

## Consequences

- Partly superseded by DEC-005 (debug-mode fail-safes, watchdog, host-link hardening) and by DEC-006 (item 4: a stopped wind now holds its count and `enc_errors` for the result screen).

- Supersedes: DEC-003 item 1 (jog naming) and item 5 (200 ms lockout,
  event-latch debounce handling); the rest of DEC-003 stands.
- New HAL symbols: `btn_flush`, `uart_*`, `enc_direction`, `btn_pending`,
  `enc_stop_at`. Bench procedures P1 and the new UART check must be run.
