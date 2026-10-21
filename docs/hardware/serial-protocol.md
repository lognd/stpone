# Host link protocol (calibration and debug)

Status: accepted (DEC-004; fail-safes and hardening in DEC-005; `speed=` field and the held post-wind count in DEC-006, `fw=4`; non-blocking SAVE, `stack_free=`, `pass_max_ms=`, `isr_lat_max=` in DEC-009, `fw=5`; default trim mode, `margin`, `bump`, `state=TRIM`, `bumps`, `trim`, `pass` in DEC-010, `fw=6`). Transport: the Pro Micro hardware UART1
(pins D0 RX / D1 TX) at 115200 8N1, reached from the PC through a 5 V
USB-UART adapter. The Pro Micro's own USB port is used only by the
bootloader (flash). The protocol is transport-agnostic ASCII lines, so a
USB CDC transport can replace the UART later without changes here.

## Framing

- One command or reply per line, terminated by `\n` or `\r` (so `\r\n`
  and CR-only terminals both work; an empty line is ignored). ASCII only,
  max 87 bytes per line. Numbers in commands (host to device) are whole-token
  unsigned decimals (no sign, no trailing junk); a flag is exactly `0` or `1`.
  Numbers in replies and events (device to host) may be signed: only `count`
  (in `GET`, `DUMP`, `EVT state`) is signed; `at_stop`, `final`, `follow` and
  `speed` are magnitudes (`|count|`), so a reversed encoder shows as a
  negative `count` and nothing else.
- Every command gets exactly one reply line, starting `OK` or `ERR <code>`.
- The device may also emit unsolicited event lines starting `EVT`.
- Replies and events carry `key=value` fields separated by single spaces.
  Counts are signed decimal integers.
- After a panel wind ends, `count` and `errors` keep their final values in
  IDLE (the LCD shows the result, DEC-006) until the result clears: manual
  ON, `ZERO`, `DEBUG 1`, the next wind, or 60 s. Send `ZERO` before relying on
  `count == 0` in IDLE.
- Compatibility: `speed=` is purely additive and `proto` stays 1. A client must
  read fields by name and ignore unknown ones; the Rust tool
  (`tools/winder/src/protocol.rs`, `Fields::parse` into a map, lookup by key;
  the reference implementation) already does, so a `fw=3` client keeps
  working against `fw=4` and a `fw=4` client must treat `speed` as absent
  on `fw=3`. `fw=5` adds `stack_free`, `pass_max_ms` and `isr_lat_max` at the
  end of `DUMP`/`EVT state`; a client must treat them as absent on `fw<5`
  (the Rust tool parses them as optional). `fw=6` adds `margin`, `bump` to `PARAM`, `pass` to `GET`, and
  `margin bump bumps trim pass` at the very end of `DUMP`/`EVT state`; all are optional for a client
  (treat them as absent on `fw<6`, `margin = bump = 0` meaning the legacy single cut), and a client must
  accept a state name it does not know (`TRIM` is new in `fw=6`).

## Commands

| Command | Reply | Meaning |
|---|---|---|
| `HELLO` | `OK stpone proto=1 fw=<ver>` | Identify; use to probe the port. |
| `GET` | `OK state=<IDLE\|WINDING\|TRIM\|PAUSED\|DEBUG> count=<n> speed=<n> errors=<n> relay=<0\|1> btn=<4 bits BTN4..BTN1> pass=<1\|0\|->` | Live snapshot. Poll at <= 20 Hz. `TRIM` is the bump phase after the main run (a paused trim reports `PAUSED`). `pass` (`fw>=6`) is the verdict of the last panel wind: `1` PASS (settled `abs(count) >= target + margin`), `0` SHORT or STOPPED (shown as UNDER TARGET on the panel), `-` while a wind runs, before any wind, after a power-up/reset, or in legacy mode (`bump=0`). It is cleared when the next wind starts. |
| `PARAM` | `OK wheel_um=<n> cpr=<n> short=<pulses> long=<pulses> coast=<pulses> margin=<pulses> bump=<0\|1>` | Current parameters (`margin`, `bump`: `fw>=6`). |
| `SET <name> <int>` | `OK` | Set a parameter in RAM. Names: `wheel_um` (wheel diameter in micrometres), `cpr`, `short`, `long` (target pulses), `coast`, `margin`, `bump`. `coast` is the follow-through: the main run drops the relay when `abs(count) >= goal - coast` with `goal = target + margin` (a debug `RUN`: `target - coast`). `margin` (0..2000 pulses, default 191 = 1.0 in) is a safety allowance added to the target, because the encoder measures the wheel, not the paper. `bump` (0 or 1, default 1) switches the trim mode (DEC-010): 1 = after the main run the machine bumps until the settled count is at least the goal and reports PASS, else UNDER TARGET; 0 = the legacy single cut at `goal - coast` with no guarantee. `max(short,long) + margin` must stay within 4000000. Valid only in IDLE (no panel wind, no RUN active), else `ERR bad_state`. `wheel_um` and `cpr` (counts per revolution) re-derive `short` and `long` for 30 yd and 90 yd (1080 in / 3240 in); a later `SET short`/`long` overrides. A set that fails `Params::valid()` is `ERR bad_arg`. |
| `SAVE` | `OK` / `ERR io` | Persist parameters to EEPROM (CRC-16 protected). The 34 bytes (tag `WIN6`; an image written by `fw<=5` is refused and `LOAD` answers `ERR no_data`) are written 4 per main-loop pass (an EEPROM byte takes up to 3.4 ms; DEC-009), so the reply comes ~7 passes (about 25-100 ms) later, after a read-back check; `ERR io` if the EEPROM did not take them. Lines sent meanwhile are not read (the host is synchronous, so it never does). |
| `LOAD` | `OK` / `ERR no_data` | Reload parameters from EEPROM; `ERR no_data` (RAM unchanged) when nothing valid is stored. Only when nothing is running (IDLE, or DEBUG without a RUN), else `ERR bad_state`. |
| `DEFAULTS` | `OK` | Restore the compiled-in defaults in RAM (does not save). Same rule as `LOAD`. |
| `DEBUG <0\|1>` | `OK` | Enter or leave debug mode. In debug mode the panel buttons cannot start anything and the host owns the relay. Leaving (this command, the panel emergency stop, or the host timeout) returns to a pristine IDLE: relay and manual off, count and error counter cleared. `DEBUG 0` during a RUN also reports it with `EVT stop`. Only from IDLE or DEBUG. `DEBUG 1` while already in debug mode is accepted and silently drops a RUN and the relay (no `EVT stop`; the count is kept). Debug mode is a lease: see Fail-safes. |
| `RELAY <0\|1>` | `OK` | Drive the relay directly. DEBUG only. |
| `ZERO` | `OK` | Reset count and error counter to 0, and restart the `pass_max_ms` / `isr_lat_max` maxima. IDLE or DEBUG without a RUN only (`ERR bad_state` during a RUN). |
| `RUN <pulses>` | `OK` | Start a test wind of `<pulses>` using the same stop rule (including `coast`) as the panel. DEBUG only. `<pulses>` must be greater than `coast` and at most 4000000, else `ERR bad_arg`. Emits events below. |
| `ABORT` | `OK` | Stop a running test wind. |
| `DUMP` | `OK state=<..> prev=<..> count=<n> speed=<n> errors=<n> relay=<0\|1> manual=<0\|1> target=<pulses> eff_target=<pulses> coast=<n> btn=<4 bits> toggles=<t1,t2,t3,t4> pending=<p1,p2,p3,p4> enc_dir=<-1\|0\|1> uptime_ms=<n> run=<0\|1> stack_free=<n> pass_max_ms=<n> isr_lat_max=<n> margin=<n> bump=<0\|1> bumps=<n> trim=<0\|1> pass=<1\|0\|->` | Full controller and HAL state: every internal the panel logic uses (pending = toggles not yet consumed by the main loop, eff_target = target - coast). Available in any state. `speed` is described below. `prev` is reserved and always `-`. The last three are diagnostics (`fw>=5`): `stack_free` is the never-used part of the stack in bytes (painted at boot; the high-water mark), `pass_max_ms` the longest main-loop pass in ms since boot or `ZERO` (budget: 60 ms), `isr_lat_max` the worst encoder-poll entry latency in CPU cycles since boot or `ZERO` (the poll period is 400 cycles; the analytic bound is about 200, the bench limit 200, see DEC-009). The last five (`fw>=6`): the persisted `margin` and `bump`, `bumps` (bumps made in the current or last wind), `trim` (1 while the trim phase is active, paused or not) and `pass` as in `GET`. The widest line is 319 bytes (the firmware buffer is 352). |
| `STREAM <0\|1>` | `OK` | In DEBUG mode, push an `EVT state ...` line (same fields as `DUMP`) on every change and at least every 100 ms. Cleared when leaving DEBUG. |

Errors: `ERR bad_command`, `ERR bad_arg`, `ERR bad_state`, `ERR busy`
(a line was dropped whole because the previous one was still unread, or it
was over-long or corrupted; the pending line is not disturbed, so wait for
its reply before pipelining), `ERR io` (SAVE), `ERR no_data` (LOAD).

## Fail-safes (debug mode is a lease)

- Any line answered without `ERR` (`GET`, `HELLO`, `STREAM`, ...) renews the
  lease. If more than 5 s pass without one while in DEBUG, the firmware
  drops the relay, abandons any RUN and returns to a pristine IDLE. Send
  `GET` at least once a second during long runs. The device-to-host `EVT state`
  stream does not renew the lease; only host lines do.
- BTN1 or BTN2 on the panel while the relay is on or a RUN is active does the
  same immediately.
- A hung main loop resets the board through the 500 ms hardware watchdog;
  the relay is open after the reset and the host sees a fresh power-up.

## Host rules (what `winder` does, and why)

- A reply that times out is never matched to a later command: after a timeout
  the host discards any late non-`EVT` line before sending the next command.
- A wind that does not end with `EVT stop ... reason=target` then
  `EVT settled` (timeout, link error, `reason=abort`, `EVT debug_exit`) is a
  failed wind: the host sends `ABORT` at once and never records it as a
  sample. Stopping a run of several winds stops all of them.
- The host keeps the lease alive only while a person is there: if no browser
  page has been connected for 10 s while the machine is in DEBUG, the host
  sends `ABORT`, `RELAY 0` and `DEBUG 0` itself. (The 5 s lease alone cannot
  do this, because the host's own polling would renew it.)
- `SET wheel_um` always re-derives `short` and `long` (even when the value is
  unchanged: 101638 derives 206188 / 618563, one more than the compiled
  defaults 206187 / 618562). A host that restores an earlier configuration
  sets `coast` down first, then `wheel_um`, then `short` and `long`
  explicitly, then `coast` last (`margin` and `bump` are independent of the others, but `short`/`long` plus `margin` must stay within 4000000).
- `SAVE` is sent only if `PARAM` still equals the numbers the operator
  reviewed.

## Events

- `EVT state <DUMP fields>`: see `STREAM`.
- `EVT stop target=<n> at_stop=<n> reason=<target\|abort>`: the relay was
  switched off. `at_stop` is `|count|` at which the encoder ISR actually
  dropped it (reason `target`), or the loop's sample for an abort.
- `EVT debug_exit reason=<timeout\|panel>`: debug mode ended by itself (after
  an `EVT stop` if a RUN was active).
- `EVT lcd_fault`: (debug mode) a display transfer timed out; the display is
  retried every 500 ms and the machine keeps running.
- `EVT settled final=<n> follow=<n>`: emitted 500 ms after the count has
  stopped changing following a stop. `final` is `|count|` and `follow` is
  `final - at_stop`, the measured follow-through in pulses; both are
  magnitudes, so they are right for either encoder direction. A reversed
  (negative-counting) encoder is visible only in the signed `count` of `GET`
  and `DUMP`; the winder tool refuses a wind during which `count` went
  negative and tells the operator to swap the encoder wires. The report is
  cancelled if `ZERO` or a new `RUN` arrives before it is emitted. This is the
  number to feed into `coast` (average several runs).

## Speed

`speed=<n>` (in `GET`, `DUMP` and `EVT state`, right after `count=`) is the
encoder speed in pulses per second, unsigned: the absolute count delta over a
fixed 100 ms window times 1000 over the real elapsed time of that window,
taken once per window from the main loop (`SpeedMeter`, `speed.h`), no
smoothing. Resolution is 10 pulses/s; the value holds until the next window
completes (so it is at most 100 ms plus one loop pass old) and is 0 until the
first window and after `ZERO`. At 2400 counts/rev, 1 RPM = 40 pulses/s. A host
that wants "up to speed" should wait for several consecutive `speed` values
within a tolerance of each other (for example three in a row within 5 %).

## Calibration procedure (what the UI must support)

1. `HELLO`, `PARAM`, then `DEBUG 1`.
2. Follow-through: for each test length, `ZERO`, `RUN <pulses>`, wait for
   `EVT settled`, record `follow`. Suggest `coast = round(mean(follow))`,
   then `SET coast`. Repeat with the new coast and show the residual
   `final - target`, which should approach 0.
3. Actual length: after a run the operator measures the real paper length
   (inches) and types it in. With `final` the settled count, the UI
   suggests `wheel_in = measured_in * cpr / (pi * final)` and
   `wheel_um = wheel_in * 25400`.
4. `SET` the suggestions (`SET wheel_um` also re-derives `short` and `long`;
   `SET coast` last, since it is validated against them), then `SAVE`, then `DEBUG 0`.

Units: 1 inch = 25400 um. Defaults: wheel 4.0015 in = 101638 um, cpr 2400,
short 206187, long 618562, coast 2291 (12 in), margin 191 (1.0 in), bump 1. The test winds of the
calibration are debug `RUN`s, so they cut at `length - coast`: use lengths of at least `coast + 2400`
(the tool does).

## Trim (bump) mode as seen over the link (`fw=6`, DEC-010)

Panel winds with `bump=1` pass through `state=WINDING` (main run, cut at `goal - coast`), `state=TRIM`
(`trim=1`; relay off while the count settles, on during each bump; `bumps=` counts them) and back to
`IDLE` with `pass=1` (PASS) or `pass=0` (SHORT: two stalls, 16 bumps or 30 s; STOPPED: the operator
pushed BTN1/BTN2). A `pass=0` wind leaves the held UNDER TARGET screen and the alarm on the machine until a
button is pressed; the host sees `pass=0` until the next wind starts. The debug `RUN` never trims and never
changes `pass`. Guarantee (conditional on correct encoder counting and wheel parameters): `pass=1` means
the settled `abs(count) >= target + margin`.
