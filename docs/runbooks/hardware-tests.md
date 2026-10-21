# Bench hardware tests

Each procedure flashes one image (`scripts/flash.py --target <name>`),
then checks the panel against the expected behaviour. Record the result
(pass/fail, notes, a photo if useful) as an attachment on the milestone
ticket; that attachment is the evidence for every `manual` runnable in
`docs/spec`.

Wiring reference: `docs/hardware/pinout.md`.

## P1 -- firmware acceptance (`firmware`)

1. Power on with all four latches UP. Expect LED1 and LED3 lit and the
   idle screen: ` -= PAPER WINDER =- ` / blank / `[1] 30yd    [2] 90yd` /
   `[4] click = manual  `. Then power off, latch BTN1, BTN2 and BTN4
   DOWN (one at a time, then all together) and power on again: nothing
   may start or run (relay open, idle screen, manual OFF). With BTN4
   left down, the first BTN4 click turns manual ON (motor runs).
2. Press BTN1. Expect the relay to close, all four LEDs on, and
   `     WINDING...     ` with `  0% [-------------]` and `LEN    0.0/  30.0 yd` (DEC-008: no `CNT:` row).
3. Turn the wheel. Expect the count to climb, the percent to follow, and
   LEDs to add one per quarter (25/50/75/100 %).
4. Let it reach 30.0 yd (206187 counts). Expect the relay to open, the
   RESULT screen (P9) to replace the winding screen, and three beeps (300 ms
   on, 150 ms off).
5. Start again and press BTN1 mid-wind. Expect an immediate stop and the
   beeps; press BTN1 during the beeps and expect them to stop and a new
   wind to start from zero.
6. Pull paper backwards: the length still grows (magnitude of the count)
   and `ERR` never appears. Repeat with BTN2: stops at 90.0 yd
   (618562 counts).
7. Start only on press-down: after a wind, latch BTN1 back UP. Expect
   no start. Push it DOWN again: a new wind starts. A BTN1 or BTN2
   toggle (either direction) mid-wind stops it.
8. Pause: press BTN3 mid-wind. Expect the relay open and `PAUSED`; BTN3
   again resumes.
9. Manual in idle: click BTN4 (either physical direction). Expect the
   relay to run and `MANUAL: motor ON    ` / `[4] click = auto    `;
   click again and the relay stops. Manual is OFF at power-up even if
   the BTN4 switch is left down, and is cleared when a wind starts or
   ends.
10. Pause + manual: start a wind, press BTN3 (paused), click BTN4.
    Expect the paper to nudge and `PAUSED - MANUAL RUN `; click again to
    stop it. BTN4 clicks while WINDING (not paused) do nothing, and
    resuming with BTN3 clears manual. Run manual in a pause right up to
    the target: the motor must still stop at the target length (manual
    cannot overrun).
11. Stop by ISR: with a slow wind near the end, the relay must open at
    the target without waiting for a screen redraw. Confirm by timing
    (scope on the relay pin PB6 against the encoder, or the overshoot in
    counts: with the factory `coast = 2291` the main run is cut about a
    foot short of the goal, and `SET bump 0` + `SET coast 0` gives the old
    single cut, whose final count is at most a few counts above 206187 /
    618562 plus the motor's mechanical follow-through; measure that with P7
    `RUN`/`EVT settled`). The default trim is P11.
12. Fast double push: push BTN3 twice within 100 ms. Expect two toggles
    (paused then playing again), not one merged push.

## P2 -- encoder (`test_encoder`)

Row 0 must read `1B=09(09) MK1=02(02)`. Row 1: `ENA`/`ENB` toggle as the
wheel turns and `ISR=` counts continuously even at rest. Row 2: one full
turn of the wheel changes `CNT` by 2400 and `R` by 1.00. BTN1 resets.

Diagnosis: `ISR=0` -> Timer1 not running; pins stuck -> encoder not
powered; pins toggle but `CNT=0` -> transition table.

Encoder never missed (DEC-009), with the `firmware` image: `DEBUG 1`, `ZERO`,
`STREAM 1`, run a 30 yd wind at full speed with the display repainting, the
2 s heal running (it does by itself) and the host streaming. Then `DUMP`:
`errors=0` and `isr_lat_max` below 200 (cycles; the poll period is 400; a
value near 400 or more would mean a poll period was lost). Idle with no
traffic it reads about 40-60; a UART byte arriving at the wrong moment adds up
to about 150. Record the figure; clear it with `ZERO`. Turn the wheel by hand
at the fastest you can, both directions, and confirm `errors=0` and that one
revolution still changes `count` by exactly 2400.

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
and nothing else; a press shorter than 16 ms must not register.

One push, one action (`firmware`, DEC-011; the simavr evidence is
`tests/system/test_buttons_sim.py`). The buttons latch, so each push is one
edge and must act once whichever way it moves the latch:

1. Power up with BTN1 latched DOWN: the panel stays IDLE, relay off.
2. Push BTN1 (it pops UP): a 30 yd wind starts. Push it again (DOWN): the
   wind stops. Repeat with the start in the DOWN direction.
3. Same for BTN2 (90 yd). BTN3 during a wind: each push alternates
   pause/play. BTN4 in IDLE: each push toggles manual.
4. Let a short wind finish on target with BTN1 latched down, then push BTN1
   once (it pops up): the next wind starts at count 0 -- no second push.
5. Pause a wind near its end, turn manual on and nudge past the stop: the
   relay drops, the panel stays paused and no bump runs until BTN3 (play).

Contact bounce (open item, bench measurement): with a scope or the `test_buttons_leds`
image, press each latching button slowly and at normal speed, and record
the longest interval during which the contact reads the wrong level
mid-travel. If any press shows a gap of 16 ms or more (the debounce window)
it produces a doubled toggle (a click that appears lost, or a wind that
starts and immediately stops). Note the figure for each button here; if it
exceeds about 10 ms add a per-button lockout of 60-100 ms after an accepted
edge in `debounce_one` (ticket it first).

## P7 -- UART host link (`firmware`)

Needs a 5 V USB-UART adapter (3.3 V adapters will not drive the Pro
Micro's 5 V RX reliably). Wire adapter RX to D1 (TX), adapter TX to D0
(RX), grounds together; no power from the adapter. Open the adapter port
at 115200 8N1 in a terminal that sends `\n` (CR is ignored). Protocol:
`docs/hardware/serial-protocol.md`. The panel must stay in IDLE with all
latches UP unless a step says otherwise.

1. `HELLO` -> `OK stpone proto=1 fw=5`.
2. `GET` -> `OK state=IDLE count=0 speed=0 errors=0 relay=0 btn=0000`
   (`speed=` sits right after `count=`; a fresh power-up has no result, so the
   count is 0). Latch
   BTN2 DOWN and repeat: `btn=0010`. Restore it.
3. `PARAM` -> `OK wheel_um=101638 cpr=2400 short=206187 long=618562 coast=0`
   on a board with a blank EEPROM (or the last saved values).
4. `SET short 100` -> `ERR bad_arg` (fails `Params::valid()`); `SET
   nope 5` -> `ERR bad_arg`; `SET coast 50` -> `OK`, then `PARAM` shows
   `coast=50`. Also `FOO` -> `ERR bad_command`. A line over 87 bytes
   is dropped whole and answered `ERR busy`; the link keeps working. `SET short 1x`
   and `RUN 5abc` -> `ERR bad_arg`.
5. `DUMP` -> one `OK state=IDLE prev=- ...` line with every field of the
   protocol table (`fw=5` ends with `stack_free=`, `pass_max_ms=`,
   `isr_lat_max=`: record `stack_free`, which must stay above 100 after all
   of P1-P10, and `pass_max_ms`, which must stay at or below 70); turn the wheel one revolution and check `count`
   changes by 2400 and `enc_dir` follows the direction; push a button
   and check `toggles` rises by 1 while `pending` returns to
   `0,0,0,0`.
6. `SAVE`, power-cycle, `PARAM` shows the saved values. `DEFAULTS`
   restores them in RAM only; `LOAD` brings the saved set back.
7. `DEBUG 1` -> `OK`; pressing BTN1 now starts nothing (panel ignored).
   `RELAY 1` closes the relay, `RELAY 0` opens it. `STREAM 1` -> `OK`
   and `EVT state ...` lines appear on every change and at least every
   100 ms; `STREAM 0` stops them.
8. `ZERO`, `RUN 2400`: the relay closes, `EVT stop target=2400 ...
   at_stop=<n> reason=target` follows at the target minus `coast`, then
   `EVT settled final=<n> follow=<n>` about 500 ms later. `RUN 0` and
   `RUN -5` -> `ERR bad_arg`. Start another `RUN 24000` and `ABORT`:
   `reason=abort`.
9. `DEBUG 0` -> `OK`, the panel works again, relay open, manual
   off. `DEBUG 1` while a panel wind is in progress ->
   `ERR bad_state`, and so is `SET` during a wind.
10. Power on with any latch down and the adapter connected: the board
    still answers `HELLO`, and nothing starts or runs.
11. Host silence: `DEBUG 1`, `RELAY 1` (motor runs), then send nothing. After
    5 s the relay opens by itself, `EVT debug_exit reason=timeout` arrives,
    the LCD returns to the idle screen with no error banner (count and
    `enc_errors` cleared). With `STREAM 1` or a `GET` every second the
    relay stays closed past 5 s. Repeat with `RUN 24000` instead of `RELAY 1`:
    `EVT stop ... reason=abort` precedes `EVT debug_exit`. Also unplug the
    adapter during `RELAY 1`: the motor must stop within about 5 s.
12. Panel emergency stop: `DEBUG 1`, `RELAY 1`, push BTN1 or BTN2: the relay
    opens at once and `EVT debug_exit reason=panel` arrives. With the relay
    open the buttons stay ignored. The LCD shows `DEBUG: HOST CONTROL` in debug.
13. Pipelining: paste `SET coast 50` and `SAVE` as two lines in one burst; the
    first is answered normally and the second either runs or gets `ERR busy`
    (never a stray `ERR bad_command`); `SAVE` is acknowledged only after the
    write is verified (`ERR io` otherwise; the EEPROM is written 4 bytes per loop
    pass, so the reply takes 25-100 ms, and a button pushed during a `SAVE` is still
    acted on within 100 ms). Wipe the EEPROM image (erase
    fuse or a blank board): `LOAD` -> `ERR no_data`. A terminal that sends
    only CR also works. Pull the adapter with `STREAM 0`: no spurious replies.
14. Calibration geometry: `SET wheel_um 101000` changes `short`/`long` in
    `PARAM` (207490 / 622470 at cpr 2400) and the LCD target/length shows the
    same yardage for the same count; `SET short 150000` overrides afterwards.
    During a wind `SET coast 0`, `LOAD`, `DEFAULTS` all answer `ERR bad_state`.
15. `EVT stop at_stop=` equals the count at which the relay really opened
    (ISR cut), not a later sample: with `STREAM 1` and a slow redraw the
    last streamed count before the stop may be lower, but `at_stop` is
    within one count of `target - coast`.

## P8 -- watchdog and stuck LCD bus (`firmware`)

Bench-only: the simulator cannot model a stuck I2C bus or a hung loop.

1. Watchdog: with the relay open, hold SDA (or SCL) of the LCD bus low with a
   jumper to ground for a few seconds while the board is running. The
   firmware must not freeze: the buttons, the host link and the 40 kHz stop
   keep working, and the LCD resumes within about a second of releasing the
   line (`EVT lcd_fault` shows on the host link in debug mode).
2. In IDLE press BTN4 (manual ON, relay closed) and ground SDA at the same
   moment: the relay must open again after at most 500 ms to 1.5 s (watchdog
   reset, the board reboots to the idle screen with the relay open) or the
   next BTN4 click must work. It must never stay closed until power is pulled.
3. After a watchdog reset the board must come back normally (Caterina
   bootloader and the `.init3` handler clear the reset flag): no reset loop,
   `HELLO` answers, LCD shows the idle screen. If a board loops, the bootloader
   in use leaves the watchdog armed and the early-off hook needs adjusting.
4. Check the fuse bytes: brown-out detection should be enabled (EEPROM
   writes during a brown-out can tear the stored parameters; the CRC-16 and
   `valid()` then reject the image and the defaults apply).

## P9 -- result screen and roll speed (`firmware`)

The expected texts below are derived from the source (`length.h`,
`controller.h`, defaults wheel 101638 um, cpr 2400, 30 yd = 206187 pulses,
the legacy single cut `SET bump 0`, `SET coast 0`, `SET margin 0` for steps 1-2, and
the default trim for the rest, see P11), not from a bench run: record the actual figures when
this is run. One tenth of a yard is about 687 pulses.

1. Power on: the normal idle screen, no result. Press BTN1 and let the wind
   reach 30.0 yd. The motor coasts; while the count still moves rows 1 and 2
   read `NOW  30.0/30.0 yd` and `settling...` (the first number follows the
   coasting paper). Row 0 stays ` -= PAPER WINDER =- ` and row 3 stays
   `[1] 30yd    [2] 90yd`. The relay is open, LED1+LED3 lit, manual off.
2. About 500 ms after the paper has stopped the rows become, for a final
   count of 207000: `DONE 30.1/30.0 yd` and `+4.3 in over` (the miss in inches under one yard, `+1.3 yd over` from one yard on); for 206400:
   `DONE 30.0/30.0 yd` and `+1.1 in over`; for 206190 (3 pulses over):
   `DONE 30.0/30.0 yd` and `on target`. They must not change again. Note the
   figure for 3 winds each at 30 and 90 yd: a late stop shows as `over`.
3. Early stop: wind with BTN1, press BTN1 again at about 150000 counts. After
   the settle (default trim mode, DEC-010) the display is the held failure screen:
   ` !! UNDER TARGET !! `, `STOPPED -8.2 yd` (the shortfall against the goal 206378; a miss of
   one yard or more stays in yards), `LEN 21.8/30.0 yd` (values follow where you stopped); in
   legacy mode (`SET bump 0`) it is the old `DONE 21.8/30.0 yd` / `-8.2 yd short`. Same with BTN2/90 yd and a stop with BTN3 pause first.
4. Hold-and-clear: leave the result for 60 s. The idle screen returns on its own
   (`GET` then shows `count=0` again). Show the result again, then click BTN4:
   manual runs the motor and the idle screen (`MANUAL: motor ON`) replaces it.
   Click BTN4 again: idle, no result. Show the result, press BTN2 (latch DOWN):
   a new 90 yd wind starts from `  0% [-------------]` and `LEN    0.0/  90.0 yd`. BTN4 and BTN3
   behave exactly as in IDLE/WINDING while the result is shown.
5. Late stop: wind 30 yd with the paper clutch loosened so the roll keeps
   moving past the target. The `LEN` row keeps counting past `30.0` (for
   example `LEN   31.2/  30.0 yd`) and never clamps or wraps; a count beyond
   9999.9 yd reads `9999.9`.
6. Host: with the result shown, `GET` returns the held `count`/`errors`;
   `ZERO` clears the result and the screen goes idle; `DEBUG 1` also clears it
   and the DEBUG banner shows. After a power cycle there is no result.
7. Speed: `DEBUG 1`, `STREAM 1`, `ZERO`, `RUN 24000` and watch `speed=` in the
   `EVT state` lines: 0 at rest, then a value within a few percent of
   `40 * RPM` pulses/s at a steady speed (for example `speed=3600` at 90 RPM),
   quantised to steps of 10. Turn by hand backwards: `speed` stays positive.
   After `ZERO` mid-run the next value is not a spike.

## P10 -- panel survives the relay (`firmware`, LCD self-healing)

Symptom this guards against: the LCD turning to solid blocks or random text
when a wind starts (relay and motor switching glitch the I2C lines or the
panel supply, or floating I2C lines when no pull-ups are fitted). Expected values below come from the source, not a bench run.

1. Start BTN1 (30 yd) five times, and BTN4 manual on/off five times: the panel
   must show readable text throughout. If a glitch does scramble it, it must
   recover by itself within about 2 s (worst case: the next 2 s repair) without
   a power cycle. Note how often it glitches and whether it is only at the
   relay edge.
2. While winding, only the percent/bar and `LEN` rows change; the title, state row
   and button hints stay put and the numbers update up to 25 times a second
   (40 ms), not continuously. A changed percent or `LEN` digit must be on the
   panel within about 150 ms even while a heal is repainting (DEC-009).
3. SDA and SCL have no external pull-ups (DEC-007): confirm with a scope or
   multimeter that both lines idle HIGH (about 5 V) and that rise times are
   under about 5 us. The flyback diodes and protection on the relay and motor
   lines are fitted. If it still glitches, add 4.7 kOhm pull-ups from SDA and
   SCL to the backpack's supply and a 100 nF capacitor at its power pins
   (then TWI_BAUD 72 with TWPS = 0 gives 100 kHz again). Record which fix cured it.
4. Encoder priority (the LCD must never cost an encoder sample): wind 30 yd at
   full speed while the display repaints, and again with manual on and BTN4
   clicked quickly, and with the PC link streaming (`STREAM 1`). The `ENC MISS`
   / `errors=` count must stay 0 and the final length must match the
   unloaded figure. By inspection only these things mask interrupts: the main
   loop's short cli/sei sections (at most about 60 cycles each, against the 400
   cycle poll period) and the UART RX ISR (at most about 160 cycles); the LCD code, its delays and the
   TWI waits run with interrupts enabled, and the 1 kHz Timer0 ISR is
   ISR_NOBLOCK so the 40 kHz Timer1 poll preempts it (worst-case analysis:
   SUB-01 COMP-0103, DEC-009). Confirm on the bench: `DUMP` during the wind
   shows `isr_lat_max` below 200.
5. Button response is 100 ms or better (DEC-009): `DEBUG 0`, start a 90 yd wind
   and `ZERO`ed `pass_max_ms`. While the panel repaints, push BTN1 (stop) and,
   in separate winds, BTN3 (pause) and BTN2; and again exactly while the 2 s
   heal repaints (watch for the LCD flicker-free rewrite, push at a random
   moment, ten tries each). The relay must open and the screen react within 100 ms
   of the push (film it or scope PB6 against the button contact; debounce is
   16 ms of that). Repeat in IDLE for BTN1/BTN2/BTN4 starts and with
   `STREAM 1` running in a debug session (`RELAY 1`, push BTN1). Afterwards
   `DUMP`: `pass_max_ms` must be at most 70.
6. Scheduler worst waits (DEC-009): during a 90 yd wind at full speed the
   percent number and the `LEN` digits must follow the paper without visible
   lag (within about 150 ms), also while the 2 s heal is rewriting the panel; the
   bar, state row and hints must be correct again within 2 s after any glitch
   even while the numbers keep changing, and with `STREAM 1` at full rate.

Check added by DEC-008 (P1/P9): on every wind screen the `LEN` row shows digits
(`LEN    0.0/  30.0 yd`, never ` / yd`) and the result row shows a number
(`+3.6 in over`, `-7.2 in short`, `+1.3 yd over`); if either prints empty, the
firmware is not using `length.h`'s printf-free formatter.

## P11 -- default bump (trim) mode and the minimum-amount guarantee (`firmware`, DEC-010)

Expected values come from the source (defaults wheel 101638 um, cpr 2400, `coast = 2291` = 12 in,
`margin = 191` = 1.0 in, 30 yd = 206187 pulses so goal = 206378 and the main cut is at 204087), not
from a bench run: record the actual figures when this is run. Use a paper roll that feeds freely and
`GET`/`DUMP` over the host link to read `state`, `bumps`, `trim` and `pass`.

1. Normal wind: press BTN1 (latch DOWN). The motor runs at full speed and the relay opens about a
   foot (12 in) before the end (count 204087; `DUMP` `eff_target=204087`). Row 0 changes from
   `     WINDING...     ` to `     TRIMMING...    ` (state `TRIM`), the percent stays 98-100 and
   `LEN` keeps updating. Then you see and hear the relay: a pause of about half a second, then a few
   short bumps (the number is `bumps=`; typically 2 to 4), each followed by half a second of quiet.
   The bargraph is full. The wind ends with the three-beep finish pattern, row 1 `DONE 30.0/30.0 yd`
   and row 2 `+1.x in over` (about 1 to 3 inches; the margin alone is 1.0 in), `GET` shows `pass=1`
   and `state=IDLE`. The final count is at least 206378: write it down for 5 winds each of 30 and
   90 yd. If it is ever below 206378 with `pass=1`, that is a firmware bug: stop and report it.
2. Pause inside the trim: during the bumps press BTN3. The relay opens at once, `PAUSED` shows,
   `GET` shows `state=PAUSED` and the same `bumps=`; BTN4 clicks nudge the paper by hand and never
   past the goal; BTN3 again resumes (`TRIMMING...`, half a second of quiet, then the next bump).
3. Stop inside the trim: press BTN1 or BTN2 while trimming. The relay opens within one pass
   (< 100 ms), you hear the single-beep cancel pattern, the screen is ` !! UNDER TARGET !! `,
   `STOPPED -x.x in` (or yards) and `LEN a/b yd`, `GET` shows `pass=0`. The screen stays after 60 s;
   the next button press clears it (`pass` stays `0` until the next wind starts).
4. Jam test: start a wind and, once `TRIMMING...` shows, hold the paper (or the wheel) so the count
   cannot advance. Each bump must be cut by the controller after about 1.5-1.8 s, the second stall
   ends the wind: the long alarm pattern (three 700 ms beeps) sounds, repeating every 6 s,
   ` !! UNDER TARGET !! ` and `SHORT -x.x in` show and `pass=0`. It must END UNDER TARGET and never
   read `DONE`. Release the paper: nothing restarts; press any button to silence and clear.
5. Empty roll / slip: let the roll run out or slip the paper on the wheel so the count advances
   slowly; the 30 s trim limit (or 16 bumps) ends it SHORT with the alarm.
6. Start from the trim: while `TRIMMING...` shows, BTN1/BTN2 are STOP, not start; no new wind starts.
7. Button response: during trimming, with the host sending `DUMP` and `STREAM` lines, BTN1/BTN2/BTN3
   still act within 100 ms (relay edge on a scope against the push, or by eye and ear); `pass_max_ms`
   stays at or below 70 and `isr_lat_max` below 200 (`DUMP` after the wind).
8. Legacy mode: `DEBUG 1`, `SET bump 0`, `SET coast 0`, `SET margin 0`, `DEBUG 0`: a wind stops once at
   the target as before (`pass=-` and the old `DONE` wording). `DEFAULTS` restores `coast=2291
   margin=191 bump=1`. `SAVE`, power-cycle, `PARAM` shows the saved values. A board last saved by
   firmware 5 answers `LOAD` with `ERR no_data` and boots with the defaults: `SAVE` once.
9. `DUMP` after a normal wind shows `margin=191 bump=1 bumps=<n> trim=0 pass=1`; during it `trim=1`.
