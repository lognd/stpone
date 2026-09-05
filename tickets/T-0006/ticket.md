---
id: T-0006
title: 'M2: desktop monitor over USB serial (roadmap, unscheduled)'
state: queued
kind: feature
origin: human
created: '2026-09-05'
priority: low
parent: null
tier: story
sprint: null
runs_last: false
milestone: null
runs_last_parallel_safe: false
runs_last_parallel_safe_reason: null
scope:
- src/stpone/serial/**
- src/stpone/monitor/**
scope_breadth_ack: false
scope_breadth_ack_reason: null
no_scope_declared: false
no_scope_declared_reason: null
designated_repro_test: null
threat: null
component: desktop
anchor: false
anchor_reason: null
land_commit: null
---
The roadmap the M1 code already anticipates (docs/spec/README.md milestones): a concrete pyserial RDTCommunication, the mcu side of the protocol on USART1, and a monitor UI. Firmware v1 is in maintenance mode, so this is recorded, not scheduled. Until it starts, the SUB-07 wire types and the logging error_ctx helper carry WIRE001 waivers naming this ticket as their follow-up.