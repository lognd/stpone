from __future__ import annotations

import platform
import shutil
import time
from collections.abc import Callable
from enum import Enum
from pathlib import Path

from typani.result import Err, Ok, Result

from stpone.flash.config import FlashConfig
from stpone.flash.errors import FlashError
from stpone.flash.proc import Runner, run_command
from stpone.logging import get_logger

_log = get_logger(__name__)

# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0503
USBIPD = "usbipd.exe"
# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0503
POWERSHELL = "powershell.exe"
_ATTACH_RETRY_S = 1.0


# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0503
# frob:tests tests/unit/test_flash_host.py::test_detect_host_other_os kind="unit"
class HostKind(str, Enum):
    """Where we are running; decides whether the USB device needs forwarding."""

    WSL = "wsl"
    LINUX = "linux"
    OTHER = "other"


# kind="unit"
# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0503
# frob:tests tests/unit/test_flash_host.py::test_detect_host_plain_linux_and_missing_banner kind="unit"  # noqa: E501
# frob:tests tests/unit/test_flash_host.py::test_detect_host_wsl_from_proc_version \
# kind="unit"
def detect_host(
    proc_version: Path = Path("/proc/version"), system: str | None = None
) -> HostKind:
    """WSL when the kernel banner names Microsoft; plain Linux otherwise."""
    sysname = system if system is not None else platform.system()
    if sysname != "Linux":
        _log.debug("host: %s is not Linux", sysname)
        return HostKind.OTHER
    try:
        banner = proc_version.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        _log.debug("host: cannot read %s (%s); assuming plain Linux", proc_version, exc)
        return HostKind.LINUX
    kind = HostKind.WSL if "microsoft" in banner.lower() else HostKind.LINUX
    _log.debug("host: %s", kind.value)
    return kind


# kind="unit"
# kind="unit"
# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0503
# frob:tests tests/unit/test_flash_host.py::test_attach_times_out_and_lists_devices \
# kind="unit"
# frob:tests tests/unit/test_flash_host.py::test_attach_retries_until_success \
# kind="unit"
def attach_bootloader(
    cfg: FlashConfig,
    runner: Runner = run_command,
    *,
    which: Callable[[str], str | None] = shutil.which,
    sleep: Callable[[float], None] = time.sleep,
    now: Callable[[], float] = time.monotonic,
) -> Result[None, FlashError]:
    """Poll usbipd until the Caterina bootloader (by hardware id) is attached to WSL."""
    exe = which(USBIPD)
    if exe is None:
        _log.error("%s not found on PATH (is usbipd-win installed on Windows?)", USBIPD)
        return Err(FlashError.ToolMissing)
    argv = [exe, "attach", "--wsl", "--hardware-id", cfg.bootloader_hardware_id]
    deadline = now() + cfg.attach_timeout_s
    attempt = 0
    _log.info(
        "waiting up to %.0f s for bootloader %s to appear to usbipd...",
        cfg.attach_timeout_s,
        cfg.bootloader_hardware_id,
    )
    while True:
        attempt += 1
        result = runner(argv)
        if result.returncode == 0:
            _log.info("bootloader attached to WSL after %d attempt(s)", attempt)
            return Ok(None)
        if now() >= deadline:
            _log.error("usbipd attach never succeeded: %s", result.stderr.strip())
            listing = runner([exe, "list"])
            _log.error("usbipd list:\n%s", listing.stdout.strip())
            return Err(FlashError.AttachTimeout)
        _log.debug(
            "attach attempt %d failed (%d); retrying", attempt, result.returncode
        )
        sleep(_ATTACH_RETRY_S)


# kind="unit"
# kind="unit"
# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0503
# frob:tests tests/unit/test_flash_host.py::test_bind_failure_and_missing_powershell \
# kind="unit"
# frob:tests tests/unit/test_flash_host.py::test_bind_elevates_through_powershell \
# kind="unit"
def bind_bootloader(
    cfg: FlashConfig,
    runner: Runner = run_command,
    *,
    which: Callable[[str], str | None] = shutil.which,
) -> Result[None, FlashError]:
    """One-time, elevated `usbipd bind` of the bootloader hardware id via PowerShell."""
    ps = which(POWERSHELL)
    if ps is None:
        _log.error("%s not found on PATH; is this WSL?", POWERSHELL)
        return Err(FlashError.ToolMissing)
    command = (
        "$p = Start-Process -Verb RunAs -Wait -PassThru -FilePath usbipd "
        f"-ArgumentList 'bind','--hardware-id','{cfg.bootloader_hardware_id}'; "
        "exit $p.ExitCode"
    )
    _log.info("requesting an elevated usbipd bind (expect a UAC prompt)...")
    result = runner([ps, "-NoProfile", "-NonInteractive", "-Command", command])
    if result.returncode != 0:
        _log.error(
            "usbipd bind failed (%d): %s", result.returncode, result.stderr.strip()
        )
        return Err(FlashError.BindFailed)
    _log.info(
        "bootloader %s is bound; this persists across reboots",
        cfg.bootloader_hardware_id,
    )
    return Ok(None)
