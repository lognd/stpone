"""Unit tests for the FlashApp orchestration (no real tools involved)."""

from __future__ import annotations

from pathlib import Path

import pytest
from typani.result import Ok

from stpone.flash.app import FlashApp
from stpone.flash.config import FlashConfig
from stpone.flash.errors import FlashError
from stpone.flash.host import HostKind
from tests.conftest import FakeRunner


@pytest.fixture(autouse=True)
def _no_sleep(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("stpone.flash.app.time.sleep", lambda s: None)


def _app(
    cfg: FlashConfig, runner: FakeRunner, host: HostKind | None = None
) -> FlashApp:
    # Every tool "exists" by name so the fake runner sees bare argv[0] values.
    return FlashApp(cfg, runner=runner, host=host, which=lambda name: name)


def test_setup_only_binds() -> None:
    # frob:tests src/stpone/flash/app.py::FlashApp.__call__ kind="unit"
    runner = FakeRunner()
    assert _app(FlashConfig(setup=True), runner)().is_ok
    assert len(runner.calls) == 1
    assert runner.calls[0][0] == "powershell.exe"


def test_build_only_configures_and_builds(tmp_path: Path) -> None:
    # frob:tests src/stpone/flash/app.py::FlashApp.__call__ kind="unit"
    runner = FakeRunner()
    cfg = FlashConfig(
        source_dir=tmp_path, build_dir=tmp_path / "b", build_only=True, target="all"
    )
    assert _app(cfg, runner)().is_ok
    assert [c[:2] for c in runner.calls] == [["cmake", "-S"], ["cmake", "--build"]]


def test_flash_on_linux_skips_attach(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    # frob:tests src/stpone/flash/app.py::FlashApp.__call__ kind="unit"
    (tmp_path / "firmware.hex").write_text(":00000001FF\n")
    monkeypatch.setattr(
        "stpone.flash.app.wait_for_port", lambda g, t: Ok("/dev/ttyACM0")
    )
    runner = FakeRunner()
    cfg = FlashConfig(build=False, build_dir=tmp_path)
    assert _app(cfg, runner, HostKind.LINUX)().is_ok
    assert [c[0] for c in runner.calls] == ["avrdude"]
    assert f"-Uflash:w:{tmp_path / 'firmware.hex'}:i" in runner.calls[0]


def test_flash_on_wsl_attaches_first(tmp_path: Path) -> None:
    # frob:tests src/stpone/flash/app.py::FlashApp.__call__ kind="unit"
    (tmp_path / "firmware.hex").write_text(":00000001FF\n")
    runner = FakeRunner()
    cfg = FlashConfig(build=False, build_dir=tmp_path, port="/dev/ttyACM3")
    assert _app(cfg, runner, HostKind.WSL)().is_ok
    assert [c[0] for c in runner.calls] == ["usbipd.exe", "avrdude"]
    assert "-P/dev/ttyACM3" in runner.calls[1]


def test_missing_image_is_an_error(tmp_path: Path) -> None:
    # frob:tests src/stpone/flash/app.py::FlashApp.__call__ kind="unit"
    cfg = FlashConfig(build=False, build_dir=tmp_path)
    result = _app(cfg, FakeRunner(), HostKind.LINUX)()
    assert result.danger_err is FlashError.HexMissing


def test_build_failure_short_circuits(tmp_path: Path) -> None:
    # frob:tests src/stpone/flash/app.py::FlashApp.__call__ kind="unit"
    runner = FakeRunner([0, 3])
    cfg = FlashConfig(source_dir=tmp_path, build_dir=tmp_path)
    result = _app(cfg, runner, HostKind.LINUX)()
    assert result.danger_err is FlashError.BuildFailed
    assert len(runner.calls) == 2
