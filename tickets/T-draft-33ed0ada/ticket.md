---
id: T-draft-33ed0ada
title: Revisit WIRE001 waivers on framework-wired symbols once frob can declare them
state: queued
kind: docs
origin: human
created: '2026-09-05'
priority: low
parent: null
tier: ticket
sprint: null
runs_last: false
milestone: null
runs_last_parallel_safe: false
runs_last_parallel_safe_reason: null
scope:
- src/stpone/logging/filter.py
- src/stpone/logging/formatter.py
- src/stpone/flash/proc.py
scope_breadth_ack: false
scope_breadth_ack_reason: null
no_scope_declared: false
no_scope_declared_reason: null
designated_repro_test: null
threat: null
component: process
anchor: false
anchor_reason: null
land_commit: null
---
BelowLevelFilter and SimpleFormatter are instantiated by logging.config.dictConfig from strings in logging/config.toml, and Runner is a typing Protocol referenced only from annotations. frob's call graph sees no caller for any of them, so each carries a frob:waive WIRE001 that WIRE002 requires to name an open follow-up ticket. This is that ticket: it closes when frob grows a way to declare framework-wired or type-only symbols (FROBLEMS F-016), or if the logging wiring moves into code. No product work is implied.