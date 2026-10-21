# DEC-007 -- LCD bus: internal I2C pull-ups on, 10 kHz, changed characters only

Date: 2026-10-20. Owner: logan. Status: accepted.

## Context

The LCD showed solid blocks and occasional garbage as soon as a wind started.
The built machine has no external pull-ups on SDA (D2/PD1) and SCL (D3/PD0);
relay and motor lines have flyback diodes and protection. The firmware drove
those pins as plain inputs with the pull-ups off, so the I2C lines floated
between transfers and a glitch could desynchronise the HD44780's 4-bit nibble
phase.

## Decision

1. `hal_init` turns the ATmega32U4's internal pull-ups ON for PD0 and PD1.
   They stay active while the TWI peripheral drives the pins.
2. The internal pull-ups are weak (about 20-50 kOhm), so the bit rate drops
   from 100 kHz (`TWBR = 72`) to about 10 kHz (`TWBR = 50`, prescaler 16, `TWPS = 2`, 9.9 kHz). The first attempt at 50 kHz still left corrupted characters (digits wrong, text jumping to another row) because the rise time of ~30 kOhm into the wiring capacitance is several microseconds. The
   HD44780 E pulse stays far above its 450 ns minimum (a byte takes 0.9 ms).
3. Each nibble is sent as ONE I2C transfer (data, data|E, data&~E) instead of
   three, which halves the bus time; and only characters that differ from what the panel is believed to show are sent (the `Panel` shadow, `include/stpalpha/panel.h`; consecutive changes share one cursor move). A full rewrite of all 80 characters takes about 0.7 s of bus time, a normal in-wind update 10-30 ms. DEC-009 supersedes the original "gaps up to 3 bridged" and the blocking repaint: the panel is written 5 cell-times per main-loop pass, percent and length first.
4. `lcd_recover` (re-run the HD44780 software init without clearing) plus
   a full rewrite (shadow invalidated) every 2 s in every state (not only while the relay is on), as described in SUB-02 COMP-0202, remain as a safety net: a corrupted character can only stay on screen for about 2 s.
5. The real fix is hardware: 4.7 kOhm pull-ups from SDA and SCL to the backpack supply. With them `TWI_BAUD`/prescaler can go back to 72/1 (100 kHz).

## Supersedes

- L2 SPEC-013: "TWI at 100 kHz (`TWBR = 72`)" becomes "TWI at about 10 kHz
  (`TWBR = 50`, prescaler 16) with the internal SDA/SCL pull-ups enabled".
- `docs/hardware/pinout.md` D2/D3 rows ("input, no pull-up").
- DEC-005 / L3 SYS-006 / SUB-03 COMP-0302 / serial-protocol.md: the watchdog is
  500 ms (was 250 ms), kicked once per loop pass and after every LCD character
  (DEC-009: a pass is at most about 68 ms; the first version's "a run is at most
  about 170 ms, a full rewrite 0.7 s" is obsolete, nothing is written in one go
  any more).
- SUB-02 COMP-0201 wording "TWI at 100 kHz".

## Consequences

Fitting external 4.7 kOhm pull-ups to 3.3 V or 5 V (per the backpack) would
make the internal ones redundant and allows 100 kHz again; the constant is
`TWI_BAUD`/`TWI_PRESCALE_BITS` in `src/stpalpha/hal/lcd.c`. Unverified on the bench until P4 and
P10 are run.
