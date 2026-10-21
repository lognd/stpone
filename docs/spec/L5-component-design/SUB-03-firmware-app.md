# L5 -- SUB-03 firmware-app

Satisfies SUB-03. Owned code: `src/stpalpha/main.cpp`,
`include/stpalpha/machine.h`, `include/stpalpha/controller.h`,
`include/stpalpha/hostlink.h`, `include/stpalpha/length.h`,
`include/stpalpha/speed.h`, `include/stpalpha/params_image.h`,
`include/stpalpha/fmt.h`. The panel behaviour is that of
`stp-upgrade` as amended by DEC-003/DEC-004.

## Components

| ID | Component | Path | Responsibility | satisfies | verified-by | Milestone |
|---|---|---|---|---|---|---|
| COMP-0301 | types | include/stpalpha/machine.h | `MachineState` (IDLE, WINDING, PAUSED, TRIM), `Outcome` (NONE, PASS, SHORT, STOPPED) and `LCDPacket` (4 x 21-char frame buffer). | SUB-03 | UT-0301 | M1 |
| COMP-0302 | winder loop | src/stpalpha/main.cpp | Length constants (SPEC-001), `read_enc` (magnitude)/`read_enc_errors`/`reset_enc` under cli/sei, `take_pending` (one toggle per button per pass), `apply` (relay, LEDs, beeps; arms `enc_stop_at` and drives the relay in one `cli/sei` section), the 500 ms watchdog (reset in `.init3`, kicked once per pass and after every LCD character), the non-blocking beep sequencer, the panel scheduler glue (`draw`: heal halves in passes of their own, `Panel::pump` once per pass, DEC-009), the `SpeedMeter` and `PassMeter` sampled once per pass, the stack paint and `stack_free`, the chunked EEPROM save (`save_begin`/`save_step`, 4 bytes per pass, read-back verify)/`params_load` (`check_stored`: tag + CRC-16 + `valid()`), the failure alarm sequencer (`start_alarm_sequence`, repeated while `Controller::alarm()`), the one-cell-shorter LCD slice after a long host line (`g_heavy_tx`, DEC-010), `host_link_poll`, `main`. The 32-bit uptime is latched once per pass from `millis()` (`tick_uptime`). Pass order, with the LCD last: watchdog kick, uptime, speed, beeps, `take_pending`, `Controller::step`, `apply`, host link, `draw`. | SUB-03, SPEC-001, SPEC-002, SPEC-003, SPEC-004, SPEC-005 | UT-0302 | M1 |
| COMP-0303 | controller | include/stpalpha/controller.h | Hardware-free `Controller` state machine: `step(Inputs) -> Outputs`, software manual flag toggled by BTN4 clicks, press-down-only start, stop rule with coast, `stop_at()` for the ISR, and the host debug interface: a host-silence timeout (5 s without a valid line, DEC-005), BTN1/BTN2 emergency stop while the host holds the motor, a pristine IDLE on every way out of debug, the quarter-progress LED bargraph, the RESULT bookkeeping (`result()`, `clear_result()`, DEC-006) and the default trim (bump) mode with its guarantee (`goal()`, `outcome()`, `bumps()`, `trimming()`, `alarm()`, DEC-010). | SUB-03, SPEC-002, SPEC-003, SPEC-004, SPEC-005 | UT-0303 | M1 |
| COMP-0304 | host link | include/stpalpha/hostlink.h | `HostLink::handle` turns one request line into one reply line plus `Request` side effects; `Params` holds the persisted set and `Params::valid()` gates every set. | SUB-03, SYS-004 | UT-0304 | M1 |
| COMP-0305 | length math | include/stpalpha/length.h | The only paper-length conversion and formatter (DEC-006): `pulses_to_tenths` (integer, rounded, saturating at 9999.9 yd), `pulses_to_inch_tenths`, `percent_done`, the printf-free `Text` builder, `format_tenths` (`W.t` with a fixed-width whole part), `format_progress` (the percent-and-bar row) and `format_result` (the two RESULT rows, the miss in inches or yards; DEC-008). | SUB-03, SPEC-001, SPEC-003 | UT-0305 | M1 |
| COMP-0306 | speed meter | include/stpalpha/speed.h | `SpeedMeter`: pulses per second from the absolute count delta over a fixed 100 ms window, for the `speed=` field of GET/DUMP/EVT state (DEC-006); `PassMeter`: longest main-loop pass for `pass_max_ms` (DEC-009). | SUB-03, SYS-004 | UT-0306 | M1 |
| COMP-0307 | panel scheduler | include/stpalpha/panel.h | `Panel<Io>`: the shadow of the LCD and the bounded, prioritised repaint (DEC-009): 5 cell-times per pass, classes percent/LEN digits, bar, rest, a reverse-order slot every 2nd pass, yield on a pending BTN1/2/3 toggle, `invalidate()` for heals. | SUB-03, SPEC-003, SPEC-013 | UT-0308 | M1 |
| COMP-0308 | screens | include/stpalpha/render.h | `render_idle`/`render_debug`/`render_winding`/`render_result`: pure functions that fill an `LCDPacket`; the exact code runs on the host and in simavr (UT-0309). | SUB-03, SPEC-003 | UT-0309 | M1 |
| COMP-0309 | params image | include/stpalpha/params_image.h | The persisted image `Stored` (tag `WIN6` + `Params` + CRC-16), `make_stored`, `check_stored` (OK / OLD_VERSION / BAD_MAGIC / BAD_CRC / INVALID), `crc16_update`; also `fmt.h` (`STP_FMT`/`STP_SNPRINTF`: format strings in flash on the AVR). | SUB-03, SYS-004 | UT-0313 | M1 |

### comp-0301
<!-- frob:describes include/stpalpha/machine.h::MachineState -->
<!-- frob:describes include/stpalpha/machine.h::LCDPacket -->
<!-- frob:describes include/stpalpha/machine.h::Outcome -->

### comp-0302
<!-- frob:describes src/stpalpha/main.cpp::main -->

`main` calls `btn_flush` once after `lcd_init` so latching buttons
that held their state across a power cycle do not start a wind; it waits 40 ms first so the 16 ms debounce has settled. It then starts the
UART, loads the EEPROM parameters (defaults if the magic, checksum or
`Params::valid()` fails) and enters the loop: take pending toggles, step
the controller, apply outputs (arming `enc_stop_at` before the relay),
service the host link, redraw.

### comp-0303
<!-- frob:describes include/stpalpha/controller.h::Controller -->
<!-- frob:describes include/stpalpha/controller.h::Inputs -->
<!-- frob:describes include/stpalpha/controller.h::Outputs -->
<!-- frob:describes include/stpalpha/controller.h::Beep -->
<!-- frob:describes include/stpalpha/controller.h::Result -->

All panel rules live here so the host fuzz test
(`tests/unit/test_controller.py`) runs the code the AVR runs.

- IDLE: a start toggle counts only if `down_after` says the push left
  the latch down; BTN1 starts the short length, BTN2 the long one. BTN4
  toggles the software manual flag (off at power-up regardless of switch position); the relay follows the flag.
- WINDING/PAUSED: a BTN1/BTN2 toggle or `count >= eff_target()` ends the
  wind (legacy `bump = 0`: relay off, manual cleared, beep; the count is KEPT, DEC-006; with `bump = 1` a BTN1/BTN2
  toggle ends it STOPPED and `count >= eff_target()` starts the TRIM phase, below); BTN3 toggles pause
  (resuming clears manual); BTN4 clicks toggle manual while PAUSED
  only (ignored while WINDING and in the pass that pauses), which runs
  the motor. `manual()` exposes the flag. `eff_target() = goal() - coast` for a panel wind (`target - coast` for a debug RUN), `goal() = target + margin`.
- RESULT (DEC-006): a stop opens `Result::SETTLING` with the stop-pass count;
  each IDLE pass compares `Inputs::count` with the last seen count, restarts
  the 500 ms timer on any change and flips to `DONE` after 500 ms of stillness
  (movement flips it back). It clears (and the next pass asks for a count
  reset) on manual ON, `clear_result()` (`ZERO`, `enter_debug`), the next
  wind, or 60 s after the stop. Only the display reads it: relay, manual and
  state are those of a pristine IDLE, and every button works as in IDLE.
- TRIM (DEC-010, `bump = 1`): after the main run the state is `TRIM` (`trimming()`), relay off. SETTLE until
  `|count|` is still for `kTrimSettleMs`; learn `over_est_` from the last bump; PASS when `count >= goal()`;
  else (`bumps_ < kMaxBumps`) `start_bump`: conservative `b = remaining/2` (first) or `remaining - over_est`
  while `remaining > kFinalWindow`, otherwise (or `b < kMinBump`, or the last `kFinalReserve` bumps) the
  final pass with the stop at `goal()`. A bump ends when `count >= bump_stop_` or `Inputs::cut` (the ISR
  cut); a bump on longer than `kBumpMaxOnMs` with no count change for `kBumpStallMs` is a stall
  (`kMaxStalls` = 2 end the trim). `kTrimMaxMs` bounds the trim (pauses excluded). BTN1/BTN2 end it
  STOPPED; BTN3 pauses (state PAUSED, `trimming()` stays true) and resumes into SETTLE; BTN4 manual only
  while PAUSED, capped at `goal()`. Endings: PASS -> `Outcome::PASS`, START beep, result DONE; SHORT ->
  `Beep::ALARM`; STOPPED -> `Beep::CANCEL`; SHORT/STOPPED hold the result (`alarm()`) until any button press.
  `bump = 0` is the single cut (`Outcome::NONE`).
- `stop_at()` is the count the ISR must cut the relay at, or `kNoStop`: `eff_target()` while WINDING/PAUSED
  (main run) and in debug RUN, the bump stop during a bump, `kNoStop` while settling, `goal()` for manual in a paused trim.
- Debug (host) mode: panel buttons cannot start anything; the host owns the relay
  (`set_relay`) or runs a counted `start_run`/`abort_run` (pulses must exceed
  `coast` and not exceed 4000000) which stops by the same rule as a wind.
  Fail-safes (DEC-005): a BTN1/BTN2 toggle while the relay is on or a RUN is
  active drops it and leaves debug (`DebugExit::PANEL`); `host_activity(now)` is
  called for every line answered without `ERR` and, once `Inputs::now_ms`
  is more than `kHostTimeoutMs` (5 s) past it, debug ends (`DebugExit::TIMEOUT`).
  Every exit (host `DEBUG 0`, panel, timeout) lands in a pristine IDLE: relay
  and manual off, the next pass asks for a count and `enc_errors` reset and
  reports an interrupted RUN through `Outputs::run_stopped`.
- `bargraph()` gives the WINDING/PAUSED LED pattern, recomputed every pass.

### comp-0305
<!-- frob:describes include/stpalpha/length.h::pulses_to_tenths -->
<!-- frob:describes include/stpalpha/length.h::format_tenths -->
<!-- frob:describes include/stpalpha/length.h::format_result -->
<!-- frob:describes include/stpalpha/length.h::format_short -->
<!-- frob:describes include/stpalpha/length.h::put_miss -->

`len_um = (pulses / cpr) * circ + (pulses % cpr) * circ / cpr` with
`circ = pi * wheel_um` (pi in millionths), so no product leaves 64 bits;
tenths = `len_um * 10 / 914400` rounded to nearest, clamped to 99999. Pulse
counts above 40 million also read 99999. `format_result` pads both rows to
20 characters (`snprintf` stops at its NUL, so a short row would leave stale
characters on the LCD).

### comp-0306
<!-- frob:describes include/stpalpha/speed.h::SpeedMeter -->

`update(now_ms, signed_count)` does nothing until 100 ms have passed since the
window start, then `speed = |count - last| * 1000 / elapsed_ms` (the real
elapsed time, so a slow pass does not over-read), restarts the window and
keeps the value until the next window. No smoothing. `rebase` restarts the
window at a zeroed count so `ZERO` never reads as motion.

### comp-0309
<!-- frob:describes include/stpalpha/params_image.h::Stored -->
<!-- frob:describes include/stpalpha/params_image.h::check_stored -->
<!-- frob:describes include/stpalpha/params_image.h::make_stored -->

The EEPROM image is `{magic 'WIN6', Params, crc16}` (34 bytes on the AVR). `check_stored` refuses an image
with the fw<=5 tag (`OLD_VERSION`), any other tag, a CRC mismatch or a set that fails `Params::valid()`; the
caller then keeps the defaults. The host test round-trips and corrupts the same bytes the AVR stores.

### comp-0304
<!-- frob:describes include/stpalpha/hostlink.h::HostLink -->
<!-- frob:describes include/stpalpha/hostlink.h::Params -->
<!-- frob:describes include/stpalpha/hostlink.h::Snapshot -->
<!-- frob:describes include/stpalpha/hostlink.h::Request -->

Commands and replies are specified in docs/hardware/serial-protocol.md
(DEC-004). `SET`, `LOAD`, `DEFAULTS` and `ZERO` are refused unless the machine is IDLE (debug counts as IDLE) and no RUN is active
(`ERR bad_state`), so a target can never move mid-wind. `SET wheel_um` or `SET cpr`
re-derives `short`/`long` (1080 in / 3240 in) from the new wheel and counts per revolution,
so the targets and the display always agree; a later `SET short`/`long` overrides them. Numbers
are whole-token unsigned decimals (junk, signs and overflow are `ERR bad_arg`). `SET` applies
to a copy that is kept only if `Params::valid()` still holds (else
`ERR bad_arg`); `SAVE`/`LOAD`/`ZERO` raise `Request` bits that
`main` performs before the reply is sent (a `SAVE` is written 4 bytes per pass and its reply comes when the last byte is verified): a failed save (EEPROM read-back
differs) answers `ERR io`, a load with no valid stored set (or one written by fw<=5, tag `WIND`) answers `ERR no_data`. `SET margin` (0..2000) and `SET bump` (0/1) join the five older names; `PARAM` reports them, `GET` and `DUMP` add `pass=`, `DUMP`/`EVT state` add `margin= bump= bumps= trim= pass=`. Parameters can be changed only here, never from the
panel.

## Unit test plan

| ID | Case | verifies | runnable |
|---|---|---|---|
| UT-0301 | `firmware` builds (types are compile-time only). | COMP-0301 | tests/system/test_firmware.py::test_every_image_builds |
| UT-0302 | The float32 recomputation of SPEC-001 equals 206187 (30 yd) and 618562 (90 yd) and the source carries the same constants (bench P1 for the behaviour). | COMP-0302 | tests/unit/test_length_model.py::test_target_pulses_match_firmware_constants |
| UT-0303 | Random panel/host input sequences never violate the controller invariants (stop at target, no relay in IDLE unless manual is on, no start on toggle-up, ...). | COMP-0303 | tests/unit/test_controller.py::test_controller_invariants_hold_for_random_sequences |
| UT-0304 | HELLO/GET/PARAM/DUMP replies, SET validation and apply, SAVE/LOAD/ZERO requests, DEBUG rules, and SET refused during a panel wind. | COMP-0304 | tests/unit/test_hostlink.py::test_hello_get_param_defaults, tests/unit/test_hostlink.py::test_dump_lists_every_internal, tests/unit/test_hostlink.py::test_set_validates_and_applies, tests/unit/test_hostlink.py::test_save_load_zero_raise_requests, tests/unit/test_hostlink.py::test_debug_rules, tests/unit/test_hostlink.py::test_panel_wind_blocks_changes, tests/unit/test_hostlink.py::test_get_and_dump_put_speed_right_after_count |
| UT-0305 | Tenths match a float model and round to nearest, the 4-digit field saturates at 9999.9 without wrapping, and the RESULT rows read over / short / on target / settling. | COMP-0305 | tests/unit/test_length.py::test_tenths_match_a_float_model_and_round_to_nearest, tests/unit/test_length.py::test_len_field_is_four_digits_and_saturates_without_wrapping, tests/unit/test_length.py::test_result_rows_over_short_on_target_and_settling, tests/unit/test_length.py::test_miss_switches_to_yards_at_one_yard_and_never_prints_empty_numbers, tests/unit/test_length.py::test_progress_row_shows_percent_and_a_bar |
| UT-0306 | The meter samples a fixed 100 ms window, divides by the real elapsed time, handles reverse, rebase and jumps. | COMP-0306 | tests/unit/test_hostlink.py::test_speed_meter_samples_a_fixed_100ms_window, tests/unit/test_hostlink.py::test_speed_meter_divides_by_the_real_elapsed_time |
| UT-0307 | The result screen settles, completes, expires, restarts on movement and clears on manual / new wind / debug (bench P9 for the LCD text). | COMP-0303 | tests/unit/test_hostlink.py::test_result_settles_then_done_then_expires, tests/unit/test_hostlink.py::test_result_restarts_when_the_count_moves_again, tests/unit/test_hostlink.py::test_result_clears_on_manual_and_new_wind_and_debug |
| UT-0308 | The panel scheduler is bounded per pass, converges, writes percent/LEN digits first, lets a digit change pre-empt a full rewrite, yields to a pending stop toggle, never marks a cell shown after a bus fault, and (fuzz, continuous digit changes plus heals) starves no class (waits in passes per class asserted). | COMP-0307 | tests/unit/test_panel.py::test_every_pump_is_bounded_and_the_panel_converges, tests/unit/test_panel.py::test_percent_and_length_digits_are_written_before_anything_else, tests/unit/test_panel.py::test_a_digit_change_preempts_the_rest_of_a_full_rewrite, tests/unit/test_panel.py::test_a_pending_stop_toggle_makes_the_repaint_yield, tests/unit/test_panel.py::test_a_bus_fault_stops_the_pump_without_marking_the_cell_shown, tests/unit/test_panel.py::test_fuzz_no_starvation_and_bounded_waits_under_continuous_changes |
| UT-0309 | Every screen and host-link reply formats identically under avr-libc (simavr) and glibc, and no firmware source uses a printf construct avr-libc lacks (`%*`, `hh`, ...). | COMP-0308 | tests/system/test_render_fmt.py::test_avr_libc_formats_every_screen_and_reply_like_glibc, tests/system/test_render_fmt.py::test_no_printf_width_star_or_hh_modifiers_in_firmware_sources |
| UT-0310 | The 5 s lease and the speed meter stay right with 780 ms passes; the pass meter keeps the maximum and clears; the widest DUMP fits the reply buffer. | COMP-0304, COMP-0306 | tests/unit/test_hostlink.py::test_the_5s_lease_and_speed_hold_with_a_long_780ms_lcd_pass, tests/unit/test_hostlink.py::test_pass_meter_tracks_the_longest_pass_and_clears, tests/unit/test_hostlink.py::test_dump_reply_fits_the_firmware_reply_buffer_at_its_widest |
| UT-0311 | No firmware function takes a stack frame over 160 bytes. | COMP-0302 | tests/system/test_firmware.py::test_no_function_takes_more_than_the_stack_frame_limit |
| UT-0312 | The trim algorithm: main cut, settle, half bump, learned bump, final-window bump armed at the goal, PASS only at/over the goal, pause/resume with manual capped at the goal, operator stop, two stalls, the 30 s limit, legacy `bump = 0`; and the physics-model fuzz (random coast 0..2x the assumption, lag, jitter, pauses, jams, creep, weak motor, stops) asserting every PASS has a true count >= goal and every failure is loud and held. | COMP-0303 | tests/unit/test_hostlink.py::test_trim_main_cut_settles_then_bumps_half_the_gap, tests/unit/test_hostlink.py::test_trim_bump_is_cut_by_the_isr_then_settles_again, tests/unit/test_hostlink.py::test_trim_final_window_arms_the_goal_and_passes_only_at_or_over_it, tests/unit/test_hostlink.py::test_trim_main_overshoot_past_the_goal_passes_after_settling, tests/unit/test_hostlink.py::test_trim_pause_resume_keeps_the_trim_and_caps_manual_at_the_goal, tests/unit/test_hostlink.py::test_trim_operator_stop_is_short_loud_and_held, tests/unit/test_hostlink.py::test_trim_cannot_start_a_new_wind_from_the_trim, tests/unit/test_hostlink.py::test_trim_stall_twice_ends_short_with_the_alarm, tests/unit/test_hostlink.py::test_trim_limit_30s_ends_short, tests/unit/test_hostlink.py::test_legacy_bump_zero_cuts_once_at_the_goal_minus_coast, tests/unit/test_trim_sim.py::test_every_completed_wind_ends_at_or_over_the_goal_and_failures_are_loud |
| UT-0313 | `margin`/`bump` validate and round-trip through SET/PARAM/DUMP and the EEPROM image; the old-version, foreign-tag and corrupt images are refused. | COMP-0304, COMP-0309 | tests/unit/test_hostlink.py::test_margin_and_bump_set_validate_and_default, tests/unit/test_hostlink.py::test_eeprom_image_round_trips_margin_bump_and_the_new_default_coast, tests/unit/test_hostlink.py::test_eeprom_image_old_corrupt_or_foreign_is_refused |
| UT-0314 | The TRIMMING row and the failed-wind screens read as documented and never use the DONE wording. | COMP-0308 | tests/system/test_render_fmt.py::test_trim_and_failed_wind_screens_have_the_documented_rows |
