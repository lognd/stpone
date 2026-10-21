"""Physics-model fuzz of the default bump (trim) mode (tests/unit/trim_sim.cpp, DEC-010)."""

from __future__ import annotations

import re
import shutil
import subprocess
from pathlib import Path

import pytest

_ROOT = Path(__file__).resolve().parents[2]

# The largest true full-speed coast the model draws (2 x the 12 in assumption) with +30 % jitter:
# every overshoot past the goal is bounded by one coast, whatever the bump sizes.
_MAX_COAST = 2 * 2291
_COAST_BOUND = int(_MAX_COAST * 1.3) + 400


@pytest.fixture(scope="module")
def sim_binary(tmp_path_factory: pytest.TempPathFactory) -> Path:
    """Compile the trim simulation once with the host g++ (skip if absent)."""
    gxx = shutil.which("g++")
    if gxx is None:
        pytest.skip("g++ not on PATH")
    out = tmp_path_factory.mktemp("trim") / "trim_sim"
    cmd = [gxx, "-std=c++17", "-O2", "-Wall", "-Wno-comment", "-Werror",
           f"-I{_ROOT / 'include'}", str(_ROOT / "tests/unit/trim_sim.cpp"), "-o", str(out)]  # fmt: skip
    done = subprocess.run(cmd, capture_output=True, text=True, check=False)
    assert done.returncode == 0, done.stderr
    return out


@pytest.mark.parametrize("seed", range(1, 21))
def test_every_completed_wind_ends_at_or_over_the_goal_and_failures_are_loud(
    sim_binary: Path, seed: int
) -> None:
    done = subprocess.run(
        [str(sim_binary), str(seed), "60"], capture_output=True, text=True, check=False
    )
    assert done.returncode == 0, done.stdout
    stats = dict(re.findall(r"(\w+)=([\d.]+)", done.stdout.splitlines()[-1]))
    assert int(stats["pass"]) + int(stats["short"]) + int(stats["stopped"]) == 60
    # overshoot past the goal: bounded by one true coast even outside the 12 in assumption
    assert float(stats["max_over"]) <= _COAST_BOUND
    # inside the assumption (true coast <= 2291) the model stays within one full coast of the goal
    assert float(stats["max_over_assumed"]) <= 2291 * 1.3
    assert int(stats["max_bumps"]) <= 16 and float(stats["max_trim_s"]) <= 30.1
