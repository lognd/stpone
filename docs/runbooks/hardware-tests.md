# Bench hardware tests

Each procedure flashes one image (`scripts/flash.py --target <name>`),
then checks the panel against the expected behaviour. Record the result
(pass/fail, notes, a photo if useful) as an attachment on the milestone
ticket; that attachment is the evidence for every `manual` runnable in
`docs/spec`.

Wiring reference: `docs/hardware/pinout.md`.

## P1 -- firmware acceptance (`firmware`)

1. Power on. Expect LED1 and LED3 lit and the idle screen:
   ` -= PAPER WINDER =- ` / blank / ` TARGET: 30.0 yds   ` /
   `   [BTN1]  START    `.
2. Press BTN1. Expect the relay to close, all four LEDs on, and
   `     WINDING...     ` with `CNT:0  0%` and `LEN    0.0/  30.0 yd`.
3. Turn the wheel. Expect the count to climb, the percent to follow, and
   LEDs to add one per quarter (25/50/75/100 %).
4. Let it reach 30.0 yd (165011 counts). Expect the relay to open, the
   idle screen to return, and three beeps (300 ms on, 150 ms off).
5. Start again and press BTN1 mid-wind. Expect an immediate stop and the
   beeps; press BTN1 during the beeps and expect them to stop and a new
   wind to start from zero.
6. Pull paper backwards: the count decreases but never goes below 0.

## P2 -- encoder (`test_encoder`)

Row 0 must read `1B=09(09) MK1=02(02)`. Row 1: `ENA`/`ENB` toggle as the
wheel turns and `ISR=` counts continuously even at rest. Row 2: one full
turn of the wheel changes `CNT` by 2400 and `R` by 1.00. BTN1 resets.

Diagnosis: `ISR=0` -> Timer1 not running; pins stuck -> encoder not
powered; pins toggle but `CNT=0` -> transition table.

## P3 -- buzzer (`test_buzzer`)

BTN1 = 200 ms, BTN2 = 500 ms, BTN3 = 1000 ms beeps, BTN4 = stop. Row 0
shows the countdown and the PB3 level; the level must return to `L`
when the countdown reaches 0. The LEDs mirror the last button pressed.

## P4 -- LCD (`test_lcd`)

BTN1 advances four screens: row identification (offsets 0x00/0x40/0x14/
0x54), an ASCII fill, corner cursors A/B/C/D, and backlight control
(BTN2 off, BTN3 on).

## P5 -- LEDs and raw pins (`test_leds_pins`)

Phase 1 (~3 s): LED chase then three flashes with no button involved.
Phase 2: the raw levels of PD7/PE6/PB4/PB5 and the debounced
`btn_state` byte update every 50 ms; the LEDs mirror `btn_state`.

Raw pins stay `L` when pressed -> wiring or pull-down; raw goes `H` but
`btn_state` stays 0x00 -> Timer0 ISR not running.

## P6 -- Timer0 and buttons (`test_timer0`, `test_buttons_leds`)

`test_timer0` row 0 must read `0B=03 MK=02 MC=00` (Timer0 CTC /64,
OCIE0A on, IVSEL cleared), row 1 shows TCNT0 cycling and TIFR0 mostly
00, row 2 says `RUN`. `test_buttons_leds` lights LEDn while BTNn is held
and nothing else; a press shorter than 8 ms must not register.
