# stpone -- Sandkey Services paper winder

Firmware and tooling for the paper winder upgrade built by Logan Dapp,
LLC for Sandkey Services. One ATmega32U4 (SparkFun Pro Micro) counts a
600 PPR encoder on a 5-inch measuring wheel, drives the winder motor
through a relay, and stops at 30 yards; a 20x4 I2C LCD, four buttons,
four LEDs, and a buzzer are the operator interface. This repository
supersedes the earlier `stp-upgrade` tree: same behaviour, but the
firmware uses avr-libc's startup and `ISR()` bindings instead of a
hand-written vector table, and the two-script Windows/WSL flashing dance
is one Python entrypoint.

## Status: firmware v1, maintenance mode

This is version 1 of the winder firmware, for the original Sandkey
machine. It is feature-complete and no longer actively developed:
newer machines are served by newer firmware lines in their own
repositories. Changes here are limited to bug fixes against the
specification in `docs/spec` and to keeping the flash tooling working
on the developer machines.

Start with [docs/index.md](docs/index.md). Agents start at
[CLAUDE.md](CLAUDE.md).

## Layout

```
src/stpalpha/      firmware (C/C++ for avr-gcc): hal/setup.c, hal/lcd.c, main.cpp
include/stpalpha/  firmware headers
tests/avr/         hardware test images (one .elf per subsystem) + hello_world
src/stpone/        Python: flash workflow (stpone.flash), logging, serial protocol
scripts/flash.py   THE operator entrypoint: build -> attach bootloader -> avrdude
tests/             pytest: unit/, integration/, system/ (builds + simavr)
docs/              the V-model specification cascade, runbooks, decisions
cmake/, CMakeLists.txt   AVR build (avr-gcc + avr-libc crt)
```

## Prerequisites

- Linux or WSL2 with `avr-gcc`, `avr-libc`, `binutils-avr`, `avrdude`,
  `cmake` >= 3.20, `simavr` (for the simulator tests), `clang-format`.
- `uv` (Python 3.11 is pinned in `.python-version`).
- WSL only: `usbipd-win` on the Windows side (`usbipd.exe` on PATH).
- The sibling checkouts `../typani` and `../frob` (see pyproject
  `[tool.uv.sources]`; typani is consumed from its local tree while it is
  modernized alongside this repo).

## Everyday commands

```sh
uv sync                                   # bootstrap (or: make install)
uv run python scripts/flash.py --setup    # once per Windows machine (WSL): elevated usbipd bind
uv run python scripts/flash.py            # build firmware, double-tap RST, flash
uv run python scripts/flash.py --target test_lcd      # flash a hardware harness instead
uv run python scripts/flash.py --build-only --target all
frob check                                # the aggregate gate (ruff, ty, cmake+ctest, frob gates)
frob test                                 # tests bound to what you touched (--all for everything)
frob format                               # ruff fix + format
cmake --build build --target format       # clang-format the firmware
cmake --build build --target tidy         # clang-tidy the firmware
```

On plain Linux the bootloader appears directly as `/dev/ttyACM*`, so the
usbipd attach step is skipped automatically; only the double-tap on RST
is still yours to do.

## Reference documentation

| Document | Use |
|---|---|
| [ATmega16U4/32U4 datasheet (doc7766)](https://ww1.microchip.com/downloads/en/devicedoc/atmel-7766-8-bit-avr-atmega16u4-32u4_datasheet.pdf) | sections 8 (interrupts), 10.1 (IVSEL), 13/14 (Timer0/1), 21 (TWI) |
| [AVR Instruction Set Manual](https://ww1.microchip.com/downloads/aemDocuments/documents/MCU08/ProductDocuments/ReferenceManuals/AVR-InstructionSet-Manual-DS40002198.pdf) | reading `avr-objdump -d` output |
| [avr-libc: interrupts](https://www.nongnu.org/avr-libc/user-manual/group__avr__interrupts.html) | `ISR()`, `BADISR_vect`, `.init` sections |

## License

Proprietary; see [LICENSE](LICENSE).
