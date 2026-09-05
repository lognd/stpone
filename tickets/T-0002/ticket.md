---
id: T-0002
title: Port the desktop core to typani and the house logging layout
state: in-progress
kind: feature
origin: human
created: '2026-09-05'
priority: high
parent: T-0001
tier: ticket
sprint: null
runs_last: false
milestone: 1.0.0
runs_last_parallel_safe: false
runs_last_parallel_safe_reason: null
scope:
- src/stpone/__init__.py
- src/stpone/py.typed
- src/stpone/exception.py
- src/stpone/common/**
- src/stpone/logging/**
- src/stpone/serial/**
- tests/conftest.py
- tests/unit/test_logging.py
- tests/unit/test_serial_packets.py
- tests/unit/test_serial_generic.py
- tests/integration/test_serial_stream.py
- tests/integration/test_wiring.py
- tests/system/test_package.py
- docs/spec/L5-component-design/SUB-06-desktop-core.md
- docs/spec/L5-component-design/SUB-07-serial-protocol.md
scope_breadth_ack: false
scope_breadth_ack_reason: null
no_scope_declared: false
no_scope_declared_reason: null
scope_changes:
- op: add
  glob: docs/spec/L5-component-design/SUB-06-desktop-core.md
  reason: the L5 rows are the doc anchors of the scoped symbols
  actor: logan
  at: '2026-09-05'
- op: add
  glob: docs/spec/L5-component-design/SUB-07-serial-protocol.md
  reason: the L5 rows are the doc anchors of the scoped symbols
  actor: logan
  at: '2026-09-05'
evidence:
- tests/integration/test_wiring.py::test_package_exports_typani_vocabulary
- tests/unit/test_logging.py::test_error_ctx_logs_and_reraises
- tests/integration/test_serial_stream.py::test_handshake_round_trip
- tests/integration/test_serial_stream.py::test_composite_packet_round_trip
designated_repro_test: null
acceptance:
- text: Given the stpone package, when it is imported, then Result/Ok/Err/Unreachable
    resolve to typani's types and no homebrew copy remains (UT-0602, SUBT-06)
  evidence:
  - tests/integration/test_wiring.py::test_package_exports_typani_vocabulary
- text: Given the logging package, when get_logger and error_ctx are used, then DEBUG/INFO
    go to stdout, WARNING+ to stderr, and error_ctx logs then re-raises (UT-0601)
  evidence:
  - tests/unit/test_logging.py::test_error_ctx_logs_and_reraises
- text: Given the carried-over serial types, when every wire type round-trips and
    a composite packet streams through an in-memory transport, then decode errors
    yield None with one error() call and the handshake constants are singletons (UT-0701..0706,
    SIT-020, SIT-021, CT-030)
  evidence:
  - tests/integration/test_serial_stream.py::test_handshake_round_trip
  - tests/integration/test_serial_stream.py::test_composite_packet_round_trip
threat: null
component: desktop-core
anchor: false
anchor_reason: null
land_commit: null
---
SUB-06 and the M1 carry-over of SUB-07 (docs/spec/L5-component-design/SUB-06-desktop-core.md, SUB-07-serial-protocol.md). Replaces stp-upgrade's common/result.py, empty.py, singleton.py with typani; adopts the frob scaffold logging layout; fixes the five serial-stream bugs listed under comp-0701.