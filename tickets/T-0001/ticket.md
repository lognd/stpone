---
id: T-0001
title: 'M1: modernize stp-upgrade into stpone (firmware v1)'
state: in-progress
kind: feature
origin: human
created: '2026-09-05'
priority: medium
parent: null
tier: epic
sprint: null
runs_last: false
milestone: 1.0.0
runs_last_parallel_safe: false
runs_last_parallel_safe_reason: null
scope:
- docs/spec/README.md
- src/**
- include/**
- tests/**
- scripts/**
- docs/**
- CMakeLists.txt
- pyproject.toml
- frob.toml
- uv.lock
- .clang-format
- README.md
- LICENSE
- Makefile
- .github/**
- .gitignore
- .gitattributes
- .python-version
- invariants/**
- tickets/**
- .clang-tidy
- cmake/**
- CLAUDE.md
scope_breadth_ack: false
scope_breadth_ack_reason: null
no_scope_declared: false
no_scope_declared_reason: null
scope_changes:
- op: add
  glob: src/**
  reason: the epic owns the whole M1 port landed as one bootstrap (DEC-002); the leaves
    own their subsets
  actor: logan
  at: '2026-09-05'
- op: add
  glob: include/**
  reason: the epic owns the whole M1 port landed as one bootstrap (DEC-002); the leaves
    own their subsets
  actor: logan
  at: '2026-09-05'
- op: add
  glob: tests/**
  reason: the epic owns the whole M1 port landed as one bootstrap (DEC-002); the leaves
    own their subsets
  actor: logan
  at: '2026-09-05'
- op: add
  glob: scripts/**
  reason: the epic owns the whole M1 port landed as one bootstrap (DEC-002); the leaves
    own their subsets
  actor: logan
  at: '2026-09-05'
- op: add
  glob: docs/**
  reason: the epic owns the whole M1 port landed as one bootstrap (DEC-002); the leaves
    own their subsets
  actor: logan
  at: '2026-09-05'
- op: add
  glob: CMakeLists.txt
  reason: the epic owns the whole M1 port landed as one bootstrap (DEC-002); the leaves
    own their subsets
  actor: logan
  at: '2026-09-05'
- op: add
  glob: pyproject.toml
  reason: the epic owns the whole M1 port landed as one bootstrap (DEC-002); the leaves
    own their subsets
  actor: logan
  at: '2026-09-05'
- op: add
  glob: frob.toml
  reason: the epic owns the whole M1 port landed as one bootstrap (DEC-002); the leaves
    own their subsets
  actor: logan
  at: '2026-09-05'
- op: add
  glob: uv.lock
  reason: the epic owns the whole M1 port landed as one bootstrap (DEC-002); the leaves
    own their subsets
  actor: logan
  at: '2026-09-05'
- op: add
  glob: .clang-format
  reason: the epic owns the whole M1 port landed as one bootstrap (DEC-002); the leaves
    own their subsets
  actor: logan
  at: '2026-09-05'
- op: add
  glob: README.md
  reason: root files of the bootstrap belong to the epic
  actor: logan
  at: '2026-09-05'
- op: add
  glob: LICENSE
  reason: root files of the bootstrap belong to the epic
  actor: logan
  at: '2026-09-05'
- op: add
  glob: Makefile
  reason: root files of the bootstrap belong to the epic
  actor: logan
  at: '2026-09-05'
- op: add
  glob: .github/**
  reason: root files of the bootstrap belong to the epic
  actor: logan
  at: '2026-09-05'
- op: add
  glob: .gitignore
  reason: root files of the bootstrap belong to the epic
  actor: logan
  at: '2026-09-05'
- op: add
  glob: .gitattributes
  reason: root files of the bootstrap belong to the epic
  actor: logan
  at: '2026-09-05'
- op: add
  glob: .python-version
  reason: root files of the bootstrap belong to the epic
  actor: logan
  at: '2026-09-05'
- op: add
  glob: invariants/**
  reason: root files of the bootstrap belong to the epic
  actor: logan
  at: '2026-09-05'
- op: add
  glob: tickets/**
  reason: root files of the bootstrap belong to the epic
  actor: logan
  at: '2026-09-05'
- op: add
  glob: .clang-tidy
  reason: root files of the bootstrap belong to the epic
  actor: logan
  at: '2026-09-05'
- op: add
  glob: cmake/**
  reason: root files of the bootstrap belong to the epic
  actor: logan
  at: '2026-09-05'
- op: add
  glob: CLAUDE.md
  reason: the agent contract is part of the bootstrap
  actor: logan
  at: '2026-09-05'
evidence:
- tests/system/test_firmware.py::test_firmware_vector_table_binds_timer_isrs
- tests/unit/test_flash_app.py::test_flash_on_wsl_attaches_first
- tests/integration/test_serial_stream.py::test_handshake_round_trip
- tests/system/test_process.py::test_trace_tables_are_closed
designated_repro_test: null
threat: null
component: null
anchor: false
anchor_reason: null
land_commit: null
---
Umbrella for the port of ../stp-upgrade into this repository: the C HAL with avr-libc ISR() bindings (DEC-001), the single flash entrypoint, the desktop core on typani, and the process itself. Behaviour is frozen by docs/spec (firmware v1, maintenance mode). Landed as a bootstrap per DEC-002.