"""Shared system-test fixtures: one build of every AVR image per session."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="session")
def build_dir(tmp_path_factory: pytest.TempPathFactory) -> Path:
    """Build every image through scripts/flash.py once and return the build directory."""
    out = tmp_path_factory.mktemp("avr-build")
    proc = subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts" / "flash.py"),
            "--build-only",
            "--target",
            "all",
            "--build-dir",
            str(out),
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    assert proc.returncode == 0, proc.stdout + proc.stderr
    return out
