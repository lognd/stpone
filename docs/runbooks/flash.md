# Flashing the Pro Micro

One entrypoint: `uv run python scripts/flash.py`. It builds the image,
hands the bootloader to WSL when needed, waits for the serial port, and
runs avrdude. The logic lives in `src/stpone/flash/` (SUB-05).

## One-time setup (WSL only)

1. Install usbipd-win on Windows (`winget install usbipd`). `usbipd.exe`
   and `powershell.exe` must be visible from WSL (they are by default).
2. Put the board in bootloader mode (double-tap RST) and, within 8 s, run

       uv run python scripts/flash.py --setup

   A UAC prompt appears; it runs `usbipd bind --hardware-id 2341:0036`
   elevated. This persists across reboots. (It replaces the old
   `setup_usb.bat`.)

## Every flash

    uv run python scripts/flash.py                 # the production firmware
    uv run python scripts/flash.py --target test_encoder   # a harness image

1. The image is configured and built with cmake (skip with `--no-build`).
2. The tool prints `Double-tap RST on the Pro Micro now.`
3. WSL: it polls `usbipd.exe attach --wsl --hardware-id 2341:0036` for up
   to 15 s (replaces `flash.bat`). Plain Linux: nothing to do, the
   bootloader shows up by itself.
4. It waits up to 10 s for `/dev/ttyACM*`, settles half a second, and
   runs `avrdude -v -patmega32u4 -cavr109 -P<port> -b57600 -D
   -Uflash:w:<hex>:i`.

Exit code 0 means avrdude verified the flash. Exit 1 prints the error
and a "next step" line (the `REMEDIES` table in `src/stpone/flash/errors.py`).

## Options

| Flag | Meaning |
|---|---|
| `--target NAME` | cmake target without `.elf` (default `firmware`; `all` only with `--build-only`) |
| `--build-dir DIR` | cmake build tree (default `build`) |
| `--no-build` | flash the existing `.hex` |
| `--build-only` | stop after building (CI, simulator tests) |
| `--port DEV` | skip the port wait and use this device |
| `--attach-timeout S`, `--port-timeout S` | override the two waits |
| `--pyproject PATH` | read `[tool.stpone.flash]` from another file |

Defaults live in `pyproject.toml` under `[tool.stpone.flash]`.

## When it fails

- `AttachTimeout`: the bootloader never appeared to usbipd. Re-run
  `--setup`, tap RST faster, compare `usbipd.exe list` with
  `dmesg | tail -20`.
- `PortTimeout`: Caterina only waits ~8 s; tap RST and re-run at once.
- `AvrdudeFailed`: something else holds the port, or the port is the
  running firmware (`2341:8036`), not the bootloader (`2341:0036`).
