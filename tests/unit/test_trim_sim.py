"""Physics-model fuzz of the default bump (trim) mode (tests/unit/trim_sim.cpp, DEC-010, DEC-012)."""

from __future__ import annotations

import re
import shutil
import subprocess
from pathlib import Path

import pytest

_ROOT = Path(__file__).resolve().parents[2]

# DEC-012: the assumed full-speed coast is 72 in (kDefaultCoastPulses); 191 pulses ~= 1 in.
_ASSUMED_COAST = 13746
_INCH = 191
# The model jitters every true coast by up to +30 %.
_JITTER = 1.3
# The largest true coast the random model draws (2 x the assumption): every overshoot past the
# goal is bounded by one jittered coast, whatever the bump sizes.
_MAX_COAST = 2 * _ASSUMED_COAST
_COAST_BOUND = int(_MAX_COAST * _JITTER) + 400
# DEC-012 trim target: a fault-free wind that needed bumps lands within ~3 in; 800 pulses (~4.2 in)
# is the gate, the 20-seed model maximum is 574.
_TRIM_OVER_MAX = 800
# controller.h kMaxBumps and kTrimMaxMs (DEC-012).
_MAX_BUMPS = 24
_TRIM_LIMIT_S = 45.1


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


def _run(sim_binary: Path, *args: int) -> dict[str, float]:
    """Run the simulation, require every in-model invariant to hold, return its STATS line."""
    done = subprocess.run(
        [str(sim_binary), *map(str, args)], capture_output=True, text=True, check=False
    )
    assert done.returncode == 0, done.stdout
    return {
        k: float(v)
        for k, v in re.findall(r"(\w+)=([\d.]+)", done.stdout.splitlines()[-1])
    }


def _assert_trim_bounds(stats: dict[str, float]) -> None:
    """Shared DEC-012 bounds: bump-ended overshoot, bump count and fault-free trim time."""
    assert stats["trim_max"] <= _TRIM_OVER_MAX, stats
    assert stats["max_bumps"] <= _MAX_BUMPS and stats["ff_max_bumps"] <= _MAX_BUMPS, (
        stats
    )
    # pause-fault winds add the operator's pause to wall time; the firmware bound excludes it and
    # the simulation checks it per wind for every non-pause wind
    assert stats["ff_max_trim_s"] <= _TRIM_LIMIT_S, stats


@pytest.mark.parametrize("seed", range(1, 21))
def test_every_completed_wind_ends_at_or_over_the_goal_and_failures_are_loud(
    sim_binary: Path, seed: int
) -> None:
    stats = _run(sim_binary, seed, 60)
    assert int(stats["pass"]) + int(stats["short"]) + int(stats["stopped"]) == 60
    # overshoot past the goal: bounded by one true coast even outside the 72 in assumption
    assert stats["max_over"] <= _COAST_BOUND
    # inside the assumption (true coast <= 72 in) the model stays within one jittered coast
    assert stats["max_over_assumed"] <= _ASSUMED_COAST * _JITTER
    _assert_trim_bounds(stats)


# 0, 6, 12, 36, 72, 96 and 144 in (2 x assumed): the DEC-012 design range is 0.5-8 ft.
@pytest.mark.parametrize("coast", [0, 1146, 2291, 6873, 13746, 18328, 27492])
@pytest.mark.parametrize("seed", range(1, 9))
def test_fixed_true_coast_lands_close_without_learning_across_winds(
    sim_binary: Path, coast: int, seed: int
) -> None:
    stats = _run(sim_binary, seed, 60, coast)
    _assert_trim_bounds(stats)
    # a fault-free PASS overshoots by the main-run excess of a jittered true coast over the
    # assumption (unavoidable, the main cut is fixed) plus at most the trim bound
    excess = max(0.0, coast * _JITTER - _ASSUMED_COAST)
    assert stats["ff_over_max"] <= excess + _TRIM_OVER_MAX, stats
    if coast * _JITTER < _ASSUMED_COAST:
        # inside the assumption every fault-free PASS ends within ~4 in of the goal
        assert stats["ff_over_max"] <= _TRIM_OVER_MAX, stats
