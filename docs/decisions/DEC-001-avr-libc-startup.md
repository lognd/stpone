# DEC-001 -- avr-libc startup and ISR() replace the hand-written vector table

Date: 2026-09-05. Owner: logan. Status: accepted.

## Decision

`stpalpha` links avr-libc's C runtime (no `-nostartfiles`) and binds its
interrupt handlers with `ISR(TIMER0_COMPA_vect)`,
`ISR(TIMER1_COMPA_vect)` and `ISR(BADISR_vect)` in
`src/stpalpha/hal/setup.c`. Peripheral bring-up moved from the old
`_start` into `hal_init()`, called from the `.init8` section so it still
runs before `main()` with interrupts enabled at the end, exactly where
the assembly version called `.Lsetup` and `sei`. The 43-entry vector
table, `WEAK_VECTOR` macro, `.bss` clear and `.data` copy loops of
`stp-upgrade/src/stpalpha/startup.asm` are gone.

## Why

The assembly startup predates the microprocessor applications course;
it duplicated what the toolchain already guarantees and made every ISR
a hand-maintained jump slot. The course idiom (`<avr/interrupt.h>`,
`ISR()`, `.init` sections) is what the code should read like now, and
it is what a future maintainer will expect. The built image is 192
bytes smaller and the vector table is verified by
`tests/system/test_firmware.py` instead of by eye.

## Behaviour preserved (checked by disassembly and by the spec numbers)

- Vectors 17 and 21 bind the two timer ISRs; every other slot lands on
  the fault-LED handler that returns (same as `__bad_interrupt`).
- Boot order: IVSEL cleared through IVCE, `USBCON = 0`, pin directions
  and pull-ups, Timer0 (CTC, /64, 249), Timer1 (CTC, /1, 1599), `sei`.
- Debounce, event latch, buzzer countdown, encoder table and saturating
  decrement are line-for-line ports of the assembly.
- `btn_consume` still uses `cli`/`sei` (unconditional re-enable) because
  it is documented as main-loop-only.

## Deliberate differences

- `buzz()` writes the 16-bit countdown inside `ATOMIC_BLOCK` so the 1 kHz
  ISR cannot observe a torn value; the assembly wrote the two bytes
  non-atomically and warned about the hazard in a comment.
- The quadrature table lives in `PROGMEM` and is read with
  `pgm_read_byte`, matching the old `lpm`.

## Consequences

- SPEC-023 and SYS-002 describe the new boot contract.
- `hal_init` is a public symbol so the boot sequence is documented and
  testable; nothing should call it twice.
- Superseded artifact: none (this decision predates the closed spec).
