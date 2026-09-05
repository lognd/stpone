---
id: T-0003
title: 'Single flash entrypoint: build, usbipd attach on WSL, avrdude'
state: in-progress
kind: feature
origin: human
created: '2026-09-05'
priority: high
blocked_by:
- T-0002
parent: T-0001
tier: ticket
sprint: null
runs_last: false
milestone: 1.0.0
runs_last_parallel_safe: false
runs_last_parallel_safe_reason: null
scope:
- src/stpone/flash/**
- scripts/flash.py
- tests/unit/test_flash_app.py
- tests/unit/test_flash_build.py
- tests/unit/test_flash_config.py
- tests/unit/test_flash_host.py
- tests/unit/test_flash_port.py
- tests/unit/test_flash_proc.py
- tests/unit/test_flash_parser.py
- tests/integration/test_flash_cli.py
- docs/spec/L5-component-design/SUB-05-flash-tool.md
scope_breadth_ack: false
scope_breadth_ack_reason: null
no_scope_declared: false
no_scope_declared_reason: null
scope_changes:
- op: add
  glob: docs/spec/L5-component-design/SUB-05-flash-tool.md
  reason: the L5 rows are the doc anchors of the scoped symbols
  actor: logan
  at: '2026-09-05'
evidence:
- tests/unit/test_flash_app.py::test_flash_on_wsl_attaches_first
designated_repro_test: null
acceptance:
- text: Given a WSL host, when scripts/flash.py runs, then it builds, polls usbipd
    attach by hardware id, waits for /dev/ttyACM*, and runs the legacy avrdude invocation
    (CT-020, CTP-021, CTP-022, UT-0503, UT-0506)
  evidence:
  - tests/unit/test_flash_app.py::test_flash_on_wsl_attaches_first
- text: Given a plain Linux host, when scripts/flash.py runs, then no usbipd or PowerShell
    step is attempted (CT-021)
  evidence: []
- text: Given --setup on WSL, when it runs, then usbipd bind is executed elevated
    through powershell.exe Start-Process -Verb RunAs (SPEC-021, UT-0503)
  evidence: []
- text: Given the CLI, when --help / --target all / a missing image are used, then
    exit codes are 0 / 2 / 1 with a remedy line (UT-0507, CTP-020)
  evidence: []
threat: null
component: flash-tool
anchor: false
anchor_reason: null
land_commit: null
---
SUB-05 (docs/spec/L5-component-design/SUB-05-flash-tool.md). Replaces setup_usb.bat, flash.bat, scripts/upload.sh and the Makefile upload targets of stp-upgrade with one pydantic-configured, typani-Result state machine in stpone.flash and the launcher scripts/flash.py.