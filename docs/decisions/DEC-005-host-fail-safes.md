# DEC-005 -- Fail-safes: debug host timeout, watchdog, ISR cut record, host-link hardening

Date: 2026-10-20. Owner: logan. Status: accepted.

> Amended by [DEC-010](DEC-010-default-bump-trim-mode-with-minimum-amount-guarantee.md): the default `coast` is 2291 (not 0), `margin` (default 191) is added to the target and panel winds trim by default; statements below about a zero default coast or a stop exactly at `target - coast` read accordingly.
>
> Amended by [DEC-012](DEC-012-two-yard-coast-and-per-wind-learned-bumps.md): the default `coast` is 13746 (72 in), not 2291; the trim sizes its bumps from the follow-through learned in the same wind.

## Decision

1. Debug mode (DEC-004 item 8) ends by itself after 5 s (`kHostTimeoutMs`)
   without a valid host line. A valid line is any command answered without
   `ERR`; `GET`, `STREAM` and `HELLO` count, so a polling or streaming host
   stays alive. On timeout the relay drops, a running RUN is reported
   (`EVT stop reason=abort`), the machine returns to a pristine IDLE (count
   and `enc_errors` cleared) and `EVT debug_exit reason=timeout` is sent.
2. While the host holds the motor (relay on or a RUN active) a BTN1/BTN2
   toggle on the panel is an emergency stop: same exit as the timeout, with
   `EVT debug_exit reason=panel`. The other buttons stay ignored in debug.
   The LCD shows a DEBUG banner. `RUN` is bounded to 4000000 pulses and must
   exceed `coast` (`ERR bad_arg`).
3. Every way out of debug (`DEBUG 0`, panel, timeout) is a pristine IDLE;
   `DEBUG 0` during a RUN reports the interrupted run as `EVT stop`.
4. The AVR watchdog runs at 500 ms (250 ms until DEC-007) and is kicked once per main-loop pass;
   every LCD TWI wait is bounded (~7 ms) and a timeout marks the LCD faulted
   (retried every 500 ms) instead of hanging. A hung loop therefore resets
   the MCU, which boots with the relay low.
5. `apply()` arms `enc_stop_at` and drives the relay inside one critical
   section, so the ISR cut can no longer be undone by a stale check.
6. The encoder ISR latches the count at which it dropped the relay
   (`enc_take_cut`); `EVT stop at_stop` and `follow` use it instead of main's
   late sample.
7. `SET`, `LOAD`, `DEFAULTS` are refused unless IDLE with no RUN active.
   `SET wheel_um` and the new `SET cpr` re-derive `short`/`long` (30/90 yd).
   Numbers are parsed strictly. `SAVE` verifies by read-back (`ERR io`),
   `LOAD` answers `ERR no_data` when nothing valid is stored. The EEPROM
   image is checked with a CRC-16 (images written by older firmware fail
   the check and fall back to the defaults).
8. UART: a line arriving while the previous one is unread is dropped whole
   and answered `ERR busy` (the pending line is untouched); `\r` also ends a
   line; framing/overrun errors discard the line; RXD1 has its pull-up on.
9. `fw=3`.

## Why

An audit found that a crashed or unplugged host left the motor running
with the panel locked out, that a stuck LCD bus froze the loop with the
relay on, and several races and silent failures in the host link.

## Consequences

- Supersedes: DEC-004 item 8 (host link) where it says debug mode is
  unbounded; the rest of DEC-004 stands. Supersedes DEC-004 item 6 only in
  that `apply()` now holds the relay under `cli` (the ISR stop is unchanged).
- DEC-006 bumps `fw` to 4 (additive `speed=` field); items 1 and 3 (pristine IDLE out of debug) stand.
- Bench procedures P7 (host link) and the new P8 (watchdog, stuck LCD) must be run.
