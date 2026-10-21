"""Host fuzz of the panel/host state machine (include/stpalpha/controller.h)."""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest

_ROOT = Path(__file__).resolve().parents[2]
_DRIVER = _ROOT / "tests" / "unit" / "controller_fuzz.cpp"


@pytest.fixture(scope="module")
def fuzz_binary(tmp_path_factory: pytest.TempPathFactory) -> Path:
    """Compile the fuzz driver once with the host g++ (skip if absent)."""
    gxx = shutil.which("g++")
    if gxx is None:
        pytest.skip("g++ not on PATH")
    out = tmp_path_factory.mktemp("fuzz") / "controller_fuzz"
    cmd = [
        gxx,
        "-std=c++17",
        "-O1",
        "-Wall",
        "-Wno-comment",
        "-Werror",
        f"-I{_ROOT / 'include'}",
        str(_DRIVER),
        "-o",
        str(out),
    ]
    done = subprocess.run(cmd, capture_output=True, text=True, check=False)
    assert done.returncode == 0, done.stderr
    return out


@pytest.mark.parametrize("seed", range(1, 41))
def test_controller_invariants_hold_for_random_sequences(
    fuzz_binary: Path, seed: int
) -> None:
    # frob:tests include/stpalpha/controller.h::Controller kind="unit"
    done = subprocess.run(
        [str(fuzz_binary), str(seed)], capture_output=True, text=True, check=False
    )
    assert done.returncode == 0, done.stdout
