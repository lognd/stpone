# L5 -- SUB-03 firmware-app

Satisfies SUB-03. Owned code: `src/stpalpha/main.cpp`,
`include/stpalpha/machine.h`. Unchanged from `stp-upgrade` apart from
formatting.

## Components

| ID | Component | Path | Responsibility | satisfies | verified-by | Milestone |
|---|---|---|---|---|---|---|
| COMP-0301 | types | include/stpalpha/machine.h | `MachineState` (IDLE, WINDING, PAUSED) and `LCDPacket` (4 x 21-char frame buffer). | SUB-03 | UT-0301 | M1 |
| COMP-0302 | winder loop | src/stpalpha/main.cpp | Length constants (SPEC-001), `read_enc` (magnitude)/`read_enc_errors`/`reset_enc` under cli/sei, `btn_edge` lockout, the non-blocking beep sequencer, `render_idle`/`render_winding`, `run_idle`/`run_winding`, `main`. | SUB-03, SPEC-001, SPEC-002, SPEC-003, SPEC-004, SPEC-005 | UT-0302 | M1 |

### comp-0301
<!-- frob:describes include/stpalpha/machine.h::MachineState -->
<!-- frob:describes include/stpalpha/machine.h::LCDPacket -->

### comp-0302
<!-- frob:describes src/stpalpha/main.cpp::main -->

`main` clears `btn_events` once after `lcd_init` so latching buttons
that held their state across a power cycle do not start a wind; it waits 40 ms first so the 16 ms debounce has settled.

## Unit test plan

| ID | Case | verifies | runnable |
|---|---|---|---|
| UT-0301 | `firmware` builds (types are compile-time only). | COMP-0301 | tests/system/test_firmware.py::test_every_image_builds |
| UT-0302 | The float32 recomputation of SPEC-001 equals 206187 (30 yd) and 618562 (90 yd) and the source carries the same constants (bench P1 for the behaviour). | COMP-0302 | tests/unit/test_length_model.py::test_target_pulses_match_firmware_constants |
