---
id: T-0004
title: Port the firmware HAL from startup.asm to avr-libc ISR() bindings
state: in-progress
kind: feature
origin: human
created: '2026-09-05'
priority: high
blocked_by:
- T-0003
parent: T-0001
tier: ticket
sprint: null
runs_last: false
milestone: 1.0.0
runs_last_parallel_safe: false
runs_last_parallel_safe_reason: null
scope:
- src/stpalpha/**
- include/stpalpha/**
- tests/avr/**
- CMakeLists.txt
- tests/system/test_firmware.py
- tests/unit/test_length_model.py
- docs/spec/L5-component-design/SUB-01-firmware-hal.md
- docs/spec/L5-component-design/SUB-02-firmware-lcd.md
- docs/spec/L5-component-design/SUB-03-firmware-app.md
- docs/spec/L5-component-design/SUB-04-hardware-harnesses.md
scope_breadth_ack: false
scope_breadth_ack_reason: null
no_scope_declared: false
no_scope_declared_reason: null
scope_changes:
- op: add
  glob: docs/spec/L5-component-design/SUB-01-firmware-hal.md
  reason: the L5 rows are the doc anchors of the scoped symbols
  actor: logan
  at: '2026-09-05'
- op: add
  glob: docs/spec/L5-component-design/SUB-02-firmware-lcd.md
  reason: the L5 rows are the doc anchors of the scoped symbols
  actor: logan
  at: '2026-09-05'
- op: add
  glob: docs/spec/L5-component-design/SUB-03-firmware-app.md
  reason: the L5 rows are the doc anchors of the scoped symbols
  actor: logan
  at: '2026-09-05'
- op: add
  glob: docs/spec/L5-component-design/SUB-04-hardware-harnesses.md
  reason: the L5 rows are the doc anchors of the scoped symbols
  actor: logan
  at: '2026-09-05'
designated_repro_test: null
acceptance:
- text: Given the source tree, when it is inspected, then there is no assembly startup
    file and the two timer ISRs are ISR() functions in src/stpalpha/hal/setup.c (CT-022,
    SPEC-023)
  evidence: []
- text: Given the built firmware.elf, when its vector table is disassembled, then
    slots 17 and 21 bind __vector_17/__vector_21 and every other slot jumps to __bad_interrupt,
    and hal_init is called from .init8 before main (CTP-015, CTP-023, SIT-002, SIT-006,
    UT-0101..0104)
  evidence: []
- text: Given the test suite on a machine with avr-gcc and simavr, when it runs, then
    every image builds and hello_world prints on the simulated USART1 (CT-023, CTP-016,
    CTP-024, UT-0401, UT-0402)
  evidence: []
- text: Given the length model in main.cpp, when recomputed in float32 on the host,
    then the target is 165011 pulses for 30.0 yd (CTP-001, UT-0302)
  evidence: []
threat: null
component: firmware
anchor: false
anchor_reason: null
land_commit: null
---
SUB-01..04 (docs/spec/L5-component-design/SUB-01..SUB-04). DEC-001 records the decision. Behaviour is line-for-line with stp-upgrade's startup.asm; the only deliberate difference is the atomic 16-bit buzz countdown write.