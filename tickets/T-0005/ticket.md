---
id: T-0005
title: 'Process tests: trace closure, waiver evidence, ignore rules'
state: queued
kind: feature
origin: human
created: '2026-09-05'
priority: medium
blocked_by:
- T-0004
parent: T-0001
tier: ticket
sprint: null
runs_last: false
milestone: 1.0.0
runs_last_parallel_safe: false
runs_last_parallel_safe_reason: null
scope:
- tests/system/test_process.py
- docs/spec/L5-component-design/SUB-08-process.md
scope_breadth_ack: false
scope_breadth_ack_reason: null
no_scope_declared: false
no_scope_declared_reason: null
scope_changes:
- op: add
  glob: docs/spec/L5-component-design/SUB-08-process.md
  reason: the L5 rows are the doc anchors of the scoped symbols
  actor: logan
  at: '2026-09-05'
designated_repro_test: null
acceptance:
- text: Given docs/spec, when the trace tables are parsed, then every referenced id
    exists, every artifact row has a verifier, every test row verifies something,
    and every non-manual runnable collects (CT-040, CTP-040, SIT-030, UT-0801)
  evidence: []
- text: Given the firmware sources, when TEST001 waivers are scanned, then each names
    docs/runbooks/hardware-tests.md (CT-041, UT-0802)
  evidence: []
- text: Given the repository, when git check-ignore runs, then FROBLEMS.md, .frob/
    and build/ are ignored (CT-042, UT-0803)
  evidence: []
threat: null
component: process
anchor: false
anchor_reason: null
land_commit: null
---
SUB-08 (docs/spec/L5-component-design/SUB-08-process.md): the executable half of the V-model discipline.