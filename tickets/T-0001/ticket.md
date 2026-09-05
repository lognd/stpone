---
id: T-0001
title: 'M1: modernize stp-upgrade into stpone (firmware v1)'
state: queued
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
scope_breadth_ack: false
scope_breadth_ack_reason: null
no_scope_declared: false
no_scope_declared_reason: null
designated_repro_test: null
threat: null
component: null
anchor: false
anchor_reason: null
land_commit: null
---
Umbrella for the port of ../stp-upgrade into this repository: the C HAL with avr-libc ISR() bindings (DEC-001), the single flash entrypoint, the desktop core on typani, and the process itself. Behaviour is frozen by docs/spec (firmware v1, maintenance mode). Landed as a bootstrap per DEC-002.