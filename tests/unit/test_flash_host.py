"""Unit tests for host detection and usbipd handling."""

from __future__ import annotations

from pathlib import Path

from stpone.flash.config import FlashConfig
from stpone.flash.errors import FlashError
from stpone.flash.host import (
    HostKind,
    attach_bootloader,
    bind_bootloader,
    detect_host,
)
from tests.conftest import FakeRunner


def test_detect_host_wsl_from_proc_version(tmp_path: Path) -> None:
    # frob:tests src/stpone/flash/host.py::detect_host kind="unit"
    banner = tmp_path / "version"
    banner.write_text("Linux version 6.6.87.1-microsoft-standard-WSL2 (gcc ...)")
    assert detect_host(banner, system="Linux") is HostKind.WSL


def test_detect_host_plain_linux_and_missing_banner(tmp_path: Path) -> None:
    # frob:tests src/stpone/flash/host.py::detect_host kind="unit"
    banner = tmp_path / "version"
    banner.write_text("Linux version 6.8.0-45-generic (buildd@lcy02) ...")
    assert detect_host(banner, system="Linux") is HostKind.LINUX
    assert detect_host(tmp_path / "nope", system="Linux") is HostKind.LINUX


def test_detect_host_other_os() -> None:
    # frob:tests src/stpone/flash/host.py::detect_host kind="unit"
    assert detect_host(system="Windows") is HostKind.OTHER


def test_attach_retries_until_success() -> None:
    # frob:tests src/stpone/flash/host.py::attach_bootloader kind="unit"
    runner = FakeRunner([1, 1, 0])
    clock = iter([0.0, 0.0, 1.0, 2.0])
    result = attach_bootloader(
        FlashConfig(attach_timeout_s=15.0),
        runner,
        which=lambda name: "C:/usbipd.exe",
        sleep=lambda s: None,
        now=lambda: next(clock),
    )
    assert result.is_ok
    assert len(runner.calls) == 3
    assert runner.calls[0] == [
        "C:/usbipd.exe",
        "attach",
        "--wsl",
        "--hardware-id",
        "2341:0036",
    ]


def test_attach_times_out_and_lists_devices() -> None:
    # frob:tests src/stpone/flash/host.py::attach_bootloader kind="unit"
    runner = FakeRunner([1, 1, 0])
    clock = iter([0.0, 5.0, 20.0])
    result = attach_bootloader(
        FlashConfig(attach_timeout_s=15.0),
        runner,
        which=lambda name: "usbipd.exe",
        sleep=lambda s: None,
        now=lambda: next(clock),
    )
    assert result.is_err
    assert result.danger_err is FlashError.AttachTimeout
    assert runner.calls[-1] == ["usbipd.exe", "list"]


def test_attach_requires_usbipd() -> None:
    # frob:tests src/stpone/flash/host.py::attach_bootloader kind="unit"
    result = attach_bootloader(FlashConfig(), FakeRunner(), which=lambda name: None)
    assert result.danger_err is FlashError.ToolMissing


def test_bind_elevates_through_powershell() -> None:
    # frob:tests src/stpone/flash/host.py::bind_bootloader kind="unit"
    runner = FakeRunner([0])
    result = bind_bootloader(FlashConfig(), runner, which=lambda name: "powershell.exe")
    assert result.is_ok
    argv = runner.calls[0]
    assert argv[:2] == ["powershell.exe", "-NoProfile"]
    assert "Start-Process -Verb RunAs" in argv[-1]
    assert "'bind','--hardware-id','2341:0036'" in argv[-1]


def test_bind_failure_and_missing_powershell() -> None:
    # frob:tests src/stpone/flash/host.py::bind_bootloader kind="unit"
    failed = bind_bootloader(FlashConfig(), FakeRunner([1]), which=lambda n: "ps.exe")
    assert failed.danger_err is FlashError.BindFailed
    missing = bind_bootloader(FlashConfig(), FakeRunner(), which=lambda n: None)
    assert missing.danger_err is FlashError.ToolMissing
