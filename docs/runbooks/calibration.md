# Calibration with `winder`

`winder` is one self-contained program (no Python, no installer) that tunes
the coast and checks the length reading of an M1 winder over a USB serial
adapter. It replaced the old `stpone.calibrate` Python package.

## Get the program

Download the file for your computer from the release page:

| Computer | File |
|---|---|
| Windows 64-bit | `winder-windows-x86_64.exe` |
| macOS Intel / Apple silicon | `winder-macos-x86_64.tar.gz` / `winder-macos-aarch64.tar.gz` |
| Linux Intel/AMD / ARM | `winder-linux-x86_64.tar.gz` / `winder-linux-aarch64.tar.gz` |

Chromebook: open the Linux Terminal and run `uname -m`. `x86_64` means
download `winder-linux-x86_64.tar.gz`; `aarch64` means
`winder-linux-aarch64.tar.gz`. `winder --help` and `winder doctor` print your
machine and the matching file.

macOS and Linux files are archives (they keep the "runnable" mark). Unpack
and start in a terminal, in the folder you downloaded to:

```
tar -xzf winder-linux-x86_64.tar.gz     # use your file's name
./winder
```

On Windows the `.exe` is the program itself: double-click it.

Build from source (in `tools/winder`): `cargo build --release --locked`.
Release targets (`--target T`, artifact `target/T/release/winder[.exe]`):
x86_64-pc-windows-msvc, x86_64-apple-darwin, aarch64-apple-darwin,
x86_64-unknown-linux-musl, aarch64-unknown-linux-musl (static, no libc).

## Wiring

USB-UART adapter (5 V logic) to the Pro Micro: adapter TX to winder RX (D0),
adapter RX to winder TX (D1), GND to GND. Protocol: `docs/hardware/serial-protocol.md`.

## Running

`winder` opens a browser UI (`http://127.0.0.1:8765`). If no browser can be
opened it prints the address and writes `winder-open-me.txt` beside the
program. Flags: `--port --baud --fake --true-coast --http-port --no-browser
--list-ports --help`. Subcommands: `doctor` (checklist) and
`calibrate [--lengths --measured --save]` (headless; `--measured` needs one
number per wind of the first pass, at least 3; if the second pass fails the
earlier settings are put back). `--fake` runs a
simulated winder, no hardware needed.

`doctor` checks, in order: adapter found, allowed to use it, winder answers,
firmware compatible, saved settings sane, browser can open. Each failure
lists numbered plain steps and, where possible, a "Fix it" button.

## Automatic calibration (default path, no measuring)

1. Press Start automatic calibration. The app turns Test mode on, winds 4 or
   more test lengths (longer ones are tried if the motor did not reach full
   speed), averages the coast, sets it, and checks it with one more wind. The
   check counts as verified when it lands within 20 pulses of its target
   (about 0.1 inch); the result also shows the leftover error before the fix.
2. Review the result, press Save to store it on the winder, or Cancel /
   Discard to restore the earlier values. Click button 4 on the machine to
   switch manual mode on/off.
3. Optional length check: the app winds about 36 inches; measure the paper
   and enter it. Corrections under 0.2% are ignored, over 10% are rejected.
   Apply fixes the wheel size; then re-run the automatic calibration.
   The length check does not erase the automatic calibration's result or its
   chart; it keeps its own, and one Save stores both.

Test mode has a 5 second lease; the app keeps it alive and, if the link
drops, the winder stops its own motor within 5 seconds. The app also keeps the
lease alive only while its page is open: if the page is closed (or the browser
crashes) for about 12 seconds (10 seconds plus at most 2 seconds to notice the closed
page) while Test mode is on, the app itself stops the motor
and turns Test mode off. If a test wind fails, times out or you press Stop, the
motor is stopped; a stop during several test winds cancels the remaining ones.
Save only stores the numbers you reviewed: if the winder was reset or restarted
before you pressed Save, nothing is saved and you are asked to run again.
Discard restores the earlier wheel, short, long and coast exactly as they were. Settings (wheel, cpr,
short, long, coast) can be edited by hand; "Reset to factory settings" restores
wheel 101638, cpr 2400, short 206187, long 618562, coast 13746, margin 191, bump 1.
The factory coast is the 72 inch (2 yd) estimate of the default trim mode (DEC-012), so the first
test winds are made with `coast = 13746` (the calibration winds are at least coast + 2400
pulses long, so the roll is up to speed when the relay drops); after calibration the measured
coast replaces it. The calibration never changes `margin` or `bump`.

## Chromebook (Linux / Crostini) -- UNVERIFIED on real hardware

- Turn Linux on: Settings -> Advanced -> Developers -> Linux development
  environment -> Turn on. Some school or work Chromebooks block this; ask the
  administrator.
- Share the adapter: Settings -> Advanced -> Developers -> Linux development
  environment -> Manage USB devices -> switch the adapter ON.
- Permission denied on /dev/ttyUSB* or ttyACM*: `sudo usermod -aG dialout $USER`
  (the doctor's Fix it runs this with `sudo -n`), then "Shut down Linux" and reopen
  the Terminal.
- Browser: open Chrome and type `localhost:8765` (port forwarding reaches it).
