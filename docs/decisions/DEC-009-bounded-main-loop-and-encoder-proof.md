# DEC-009 -- bounded main-loop pass, wide millisecond tick, prioritised LCD scheduler, encoder proof

Date: 2026-10-20. Owner: logan. Status: accepted.

> Amended by [DEC-010](DEC-010-default-bump-trim-mode-with-minimum-amount-guarantee.md): the default `coast` is 2291 (not 0), `margin` (default 191) is added to the target and panel winds trim by default; statements below about a zero default coast or a stop exactly at `target - coast` read accordingly.
>
> Amended by [DEC-012](DEC-012-two-yard-coast-and-per-wind-learned-bumps.md): the default `coast` is 13746 (72 in), not 2291; the trim sizes its bumps from the follow-through learned in the same wind. The worst-case timing and RAM figures are re-derived in DEC-010 (longest pass 62 ms, `.data` 482 bytes after the format strings moved to flash).

## Context

The fourth audit (audit4) of the working tree found that the LCD rewrite added by
DEC-007/DEC-008 made one main-loop pass up to ~0.8 s long (a full-panel rewrite at 9.9 kHz):

- the 8-bit uptime fold lost 256 ms per overflow, so the 5 s host lease, the `speed=` meter
  (3x spikes that fail every automatic calibration wind) and every ms timer ran wrong;
- BTN1/BTN2/BTN3 and the debug emergency stop were acted on only once per pass (up to ~0.8 s);
- `hd44780_setup` could not undo a glitch-shifted display, `SAVE` blocked ~90 ms, and nothing
  measured the stack, the pass length or the encoder-poll entry latency.

Two owner requirements are fixed here as requirements: **every button acts within 100 ms of
the push (end to end, including the 16 ms debounce)** and **no encoder tick is ever missed**.

## Decision

1. **Wide tick.** `timer0_ticks` is a `volatile uint32_t` (ms, wraps after 49 days) incremented
   in `TIMER0_COMPA`; `millis()` reads it under `cli/sei` (about 10 cycles). `tick_uptime()`
   latches `g_uptime_ms = millis()` once per pass; the beep sequencer uses the same value. A
   pass of any length can no longer lose time.
2. **Bounded LCD slice per pass.** `render_*` (now `include/stpalpha/render.h`, pure) only fill
   the wanted packet `g_lcd`. `stpalpha::Panel` (`include/stpalpha/panel.h`, pure, templated on
   the I/O so the host tests run the same code) holds the shadow of the panel and writes at most
   `kPanelBudget` = 5 cell-times per main-loop pass (a character is 1, a cursor move is 1;
   cursor moves are skipped when the HD44780 address counter already points at the cell). It never
   loops until the display is current.
3. **Priority classes.** Class 0: the percent number (row 1, cols 0-2) and the `LEN` digits
   (row 2, cols 4-9). Class 1: the bar cells (row 1, cols 6-18). Class 2: everything else (state
   row, hints, title, target, heal rewrites). A pass serves class 0 first, then 1, then 2, so a changed digit
   pre-empts the rest of any rewrite at the next pass (chunk) boundary. Every 2nd pass
   (`kLowSlotEvery`) serves the classes in reverse order, so class 2 cannot starve under
   continuous percent/length changes. A pending BTN1/BTN2/BTN3 toggle makes the pump yield at
   the next cell and defers a heal, so the controller acts first.
4. **Heal in small pieces.** `lcd_recover` is split into `lcd_recover_begin` (~21 ms: nibble
   resync) and `lcd_recover_finish` (~34 ms: function set, display on, entry mode, **Return Home**);
   each runs in a pass of its own, with the whole loop in between. After the second half the shadow
   is invalidated and the 80 cells are rewritten in the class order above (never one blocking
   rewrite). Return Home (0x02, 2 ms) undoes a display shift a glitch left behind (0x18/0x1C) without
   clearing DDRAM, so there is no flicker.
5. **`SAVE` is chunked**: 4 EEPROM bytes per pass (<= 14 ms), read-back compare at the end, reply
   `OK`/`ERR io` then; no new host line is read meanwhile.
6. **Diagnostics** (`fw=5`, appended to `DUMP`/`EVT state`, optional for clients):
   `stack_free` (painted-stack high-water mark; the stack is painted from `__bss_end` at boot),
   `pass_max_ms` (longest pass, `PassMeter`), `isr_lat_max` (largest `TCNT1` read as the Timer1
   ISR starts = cycles from the compare match to the first ISR instruction, including the
   ~39 cycle response, vector jump and prologue). `ZERO` restarts the last two.
7. **Cut record rules.** The ISR cut record is cleared at the start of every drive period (the
   relay output rising) and on a count reset, and `apply()` does not re-energise the relay while
   the record is set (one bounce step back below `enc_stop_at` no longer re-closes it). |count|
   uses unsigned negation (no signed overflow).
8. **Format guard.** All panel text and host-link replies are shared code
   (`render.h`, `length.h`, `hostlink.h`); `tests/avr/test_render_fmt.cpp` runs the identical case
   table on the ATmega32U4 in simavr with avr-libc's printf and `tests/system/test_render_fmt.py`
   requires its output to equal the host (glibc) run line for line; a source grep rejects `%*`,
   `%.*` and `hh`/`j`/`z`/`t` modifiers.
9. **Stack frames.** avr-gcc has no `-Wstack-usage`; a system test compiles the firmware with
   `-fstack-usage` and fails if any frame exceeds 160 bytes (largest today: 123, `HostLink::dump`).

## Worst-case numbers (derived from the built ELF and the 9.9 kHz bus)

I2C at 9.9 kHz: one byte (8 bits + ACK) = 0.91 ms. A nibble is one transfer of address plus 3
bytes plus start/stop = 3.9 ms (with the 50 us settle); a cell (character or cursor move) is two
nibbles = 7.8 ms. The slice budget is 5 cell-times = 39 ms.

| Part of one pass | Worst case |
|---|---|
| LCD pump (5 cells) | 39 ms (a pending stop toggle cuts it to the next cell, <= 8 ms) |
| LCD heal half (own pass) | 21 ms / 34 ms |
| UART TX of one line (DUMP/EVT state <= ~277 B at 86.8 us/byte) | 24 ms (typical 230 B = 20 ms) |
| SAVE slice (4 EEPROM bytes x 3.4 ms) | 14 ms |
| controller, speed meter, snapshot, formatting | < 3 ms |
| **Longest pass** (pump + one DUMP line) | **about 68 ms** (bench limit for `pass_max_ms`: 70) |

Button response = debounce 16 ms + the rest of the pass in progress + the next pass's controller
step. The worst case is a toggle that becomes pending just after `take_pending`: UART 24 ms + at
most one more cell (8 ms; or 34 ms if a heal half has already started) = 58 ms, so
**16 + 58 + 2 = 76 ms, under the 100 ms requirement** (the relay is driven in `apply()` of the
next pass, before any LCD work, because the controller step and `apply()` always run first in a
pass).

Waits per class under the schedule (host-fuzzed in `tests/unit/test_panel.py` with a digit
change every 3rd pass, 5x the real rate, and a full heal every 60 passes; a repainting pass is
about 45 ms): class 0 (percent, LEN digits) <= 8 passes in the fuzz, in practice the next pass or
two (<= ~150 ms from the change including the 40 ms paint gate `kPaintMs`); class 1 (bar)
and class 2 (state row, hints, heal) <= 32 passes = about 1.5 s, the stated bound is 2 s.

Nothing starves in either direction: the LCD gets one slice in every pass (a busy pass, a
`SAVE` or UART traffic only make the pass longer, never skip the slice), and the slice is bounded,
so the watchdog kick, uptime tick, speed sample, `take_pending`, `Controller::step`, `apply()`,
host poll, stream output and beeps run in every pass, in that order, before any LCD work.

## Encoder: no tick can be missed (analysis from `avr-objdump -d build/firmware.elf`)

`TIMER1_COMPA_vect` fires every 400 cycles. Its body, worst path (double step and relay cut):
39 cycles of hardware response, vector `jmp` and 15-register prologue, ~174 cycles of body and 35
cycles of epilogue = **~213 cycles** (a normal step ~130), always less than 400, so the next
compare match never finds the ISR still running. Nothing nests inside it (it is not
`ISR_NOBLOCK`).

What can delay its entry (cycles, counted from the disassembly; add up to 5 for the instruction in
progress):

| Source | Cycles with interrupts off |
|---|---|
| `snapshot()` `cli`..`sei` (counts, toggles, speed) | 58 (largest in the program) |
| `apply()` `cli` section (stop_at, relay, `enc_count` read) | 54 |
| `take_pending` per-button `cli` | 31 |
| `enc_take_cut`, `uart_read_line`, `reset_enc`, `millis`, `enc_isr_lat_max`, `buzz`, `uart_take_dropped` | 25, 16, 14, 10, 6, 6, 6 |
| `eeprom_update_byte` EEMPE/EEPE sequence | 6 (the write itself runs with interrupts on) |
| `wdt_enable` / `wdt_early_off` | 10 (boot only) |
| `TIMER0_COMPA` (ISR_NOBLOCK: `sei` is its first instruction) | 7 (response plus vector) |
| `USART1_RX` ISR (not NOBLOCK) | 149 worst (all instructions summed) + 7 = 156; typical ~70 |
| `BADISR` | 3 (never reached in service) |
| LCD, delays, TWI waits, UART TX polling | 0 (interrupts stay enabled) |

Maximum entry latency = 156 + 5 = **161 cycles** (a UART byte landing just before the compare
match). Timer1 (vector 17) outranks Timer0 (21) and USART (25) when several are pending, and the
latency cannot accumulate: the ISR finishes (<= 161 + 213 = 374 cycles after its match) before the
next match, and a UART byte arrives at most every 1389 cycles. The on-target reading
`isr_lat_max` adds the 39 cycle response and prologue: expected at most ~200 (bench limit 200,
P10 step 4 and P2).

Highest safe encoder rate: a state must outlast the largest gap between two samples, 400 + 161
= 561 cycles, i.e. **28 500 counts/s (about 710 RPM at 2400 counts/rev)**. The machine is 90 RPM =
3600 counts/s (7.9x margin); 500 RPM = 20 000 counts/s is still safe (1.4x).
`tests/unit/test_encoder_model.py` runs the real ISR source on the host against a synthetic
quadrature at 3600, 8000 and 20 000 counts/s, both directions, reversals and the illegal
double step, with 0, 60 and 150 cycles of random entry jitter: the count equals the injected
count and `enc_errors` is 0 (and the model is shown to lose counts above the limit). The 16/32-bit
shared variables are read under `cli` (`read_enc_raw`, `read_enc_errors`, `reset_enc`, `snapshot`,
`enc_take_cut`, `enc_isr_lat_max`, `millis`); `enc_stop_at` is written under `cli` together with
the relay; every shared variable is `volatile`; `g_cut_valid` and the single-byte flags are atomic;
the ISR is the only writer of `enc_count`/`enc_errors` apart from the `cli`-guarded reset.

## Supersedes

- DEC-007 item 3 and Supersedes bullet on the watchdog: "gaps up to 3 bridged" and "a full rewrite
  of all 80 characters takes about 0.7 s" and "a run is at most about 170 ms": the repaint is now
  incremental (5 cell-times per pass, no gap bridging); a full rewrite takes about 80 cells =
  ~17 passes (~0.7 s of bus time spread over ~1 s) without ever blocking a pass for more than ~40
  ms. The watchdog is kicked every pass and after every character.
- DEC-005 item 2 ("acts immediately") and L3 SYS-006: the panel stop acts within 100 ms, not
  within one `loop` of unbounded length.
- L3 SYS-003: `timer0_ticks` is 32 bits wide and read through `millis()` under `cli/sei`, not
  "8-bit values directly".
- L2 SPEC-013 / SUB-02 COMP-0201: the init ends with Return Home; `lcd_recover` is two calls.
- `docs/hardware/serial-protocol.md`: `fw=5`; `final`/`follow` are magnitudes.

## Consequences

- Bench (docs/runbooks/hardware-tests.md P2, P7, P10): `isr_lat_max` < 200, `errors=0`,
  `pass_max_ms` <= 70, buttons within 100 ms during repaint and heal, `stack_free` > 100.
- The stack headroom is analytic (static 1198 + 816 bytes of 2560 leaves 546; deepest chain
  `main` 62 + `handle` 107 + `dump` 123 + vfprintf ~70 + ISR context ~65 = ~430) until the bench
  reads `stack_free`.
