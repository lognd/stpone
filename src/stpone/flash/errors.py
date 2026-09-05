from __future__ import annotations

from typani.error_set import ErrorSet


# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0509
class FlashError(ErrorSet):
    """Everything that can go wrong between a source tree and a flashed Pro Micro."""

    ToolMissing = "a required executable is not on PATH"
    BuildFailed = "cmake configure or build exited non-zero"
    HexMissing = "the requested image has not been built"
    BindFailed = "usbipd bind failed; it needs an elevated PowerShell (UAC prompt)"
    AttachTimeout = "the bootloader never appeared to usbipd before the deadline"
    PortTimeout = "no serial port matched the bootloader glob before the deadline"
    AvrdudeFailed = "avrdude exited non-zero"


# What the operator should do next, keyed by error (printed by the CLI).
# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0509
REMEDIES: dict[FlashError, str] = {
    FlashError.ToolMissing: (
        "Install the missing tool: avr-gcc/avr-libc/avrdude/cmake on the Linux side, "
        "usbipd-win (usbipd.exe) on the Windows side for WSL."
    ),
    FlashError.BuildFailed: (
        "Read the cmake output above; fix the source or the toolchain."
    ),
    FlashError.HexMissing: (
        "Build first (drop --no-build) or pass --target for an image that exists."
    ),
    FlashError.BindFailed: (
        "Approve the UAC prompt, or bind manually from an elevated PowerShell: "
        "usbipd bind --hardware-id 2341:0036"
    ),
    FlashError.AttachTimeout: (
        "1. Run scripts/flash.py --setup once per machine (elevated usbipd bind). "
        "2. Double-tap RST on the Pro Micro quickly (short RST to GND twice). "
        "3. Compare `usbipd.exe list` with `dmesg | tail -20`."
    ),
    FlashError.PortTimeout: (
        "The Caterina bootloader only stays up for ~8 s: double-tap RST and re-run "
        "immediately. On WSL make sure the attach step above succeeded."
    ),
    FlashError.AvrdudeFailed: (
        "Check that the port is the bootloader (not the running firmware), "
        "that nothing else holds it open, and that the board was reset recently."
    ),
}
