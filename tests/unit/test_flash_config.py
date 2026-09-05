"""Unit tests for FlashConfig merging."""

from __future__ import annotations

import argparse
from pathlib import Path

from stpone.flash.config import FlashConfig


def _ns(**kw: object) -> argparse.Namespace:
    return argparse.Namespace(**kw)


def test_from_external_uses_defaults_without_pyproject(tmp_path: Path) -> None:
    # frob:tests src/stpone/flash/config.py::FlashConfig.from_external kind="unit"
    cfg = FlashConfig.from_external(_ns(), tmp_path / "missing.toml")
    assert cfg.target == "firmware"
    assert cfg.mcu == "atmega32u4"
    assert cfg.build is True


def test_from_external_reads_tool_table_and_cli_wins(tmp_path: Path) -> None:
    # frob:tests src/stpone/flash/config.py::FlashConfig.from_external kind="unit"
    pyproject = tmp_path / "pyproject.toml"
    pyproject.write_text(
        '[tool.stpone.flash]\ntarget = "test_lcd"\nbaud = 1200\nbuild_dir = "out"\n'
    )
    cfg = FlashConfig.from_external(
        _ns(target="firmware", port="/dev/ttyACM9", build=None), pyproject
    )
    assert cfg.target == "firmware"
    assert cfg.baud == 1200
    assert cfg.build_dir == Path("out")
    assert cfg.port == "/dev/ttyACM9"
    assert cfg.source_dir == tmp_path.resolve()


def test_repo_pyproject_table_is_valid(repo_root: Path) -> None:
    # frob:tests src/stpone/flash/config.py::FlashConfig kind="unit"
    cfg = FlashConfig.from_external(_ns(), repo_root / "pyproject.toml")
    assert cfg.bootloader_hardware_id == "2341:0036"
    assert cfg.programmer == "avr109"
    assert cfg.source_dir == repo_root
