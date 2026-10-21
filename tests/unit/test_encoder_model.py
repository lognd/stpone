"""Unit test: the REAL 40 kHz encoder ISR (src/stpalpha/hal/setup.c) against a synthetic quadrature.

The ISR is compiled on the host behind tests/unit/avr_shim and sampled the way Timer1 samples it
(once per 400 cycles, every entry late by a random 0..jitter cycles). Timing on the AVR itself is
covered by the worst-case analysis in docs/spec/L5-component-design/SUB-01-firmware-hal.md COMP-0103
and by the isr_lat_max evidence of docs/runbooks/hardware-tests.md P2 (DEC-009).
"""

from __future__ import annotations

import subprocess
from pathlib import Path

import pytest

_ROOT = Path(__file__).resolve().parents[2]
_CYCLES_PER_POLL = 400
_CPU_HZ = 16_000_000


@pytest.fixture(scope="module")
def model(tmp_path_factory: pytest.TempPathFactory) -> Path:
    exe = tmp_path_factory.mktemp("encoder") / "model"
    subprocess.run(
        [
            "gcc", "-std=gnu11", "-O1", "-Wall", "-Wno-comment", "-Wno-attributes", "-Werror",
            f"-I{_ROOT / 'tests/unit/avr_shim'}", f"-I{_ROOT / 'include'}",
            str(_ROOT / "tests/unit/encoder_model.c"), "-o", str(exe),
        ],
        check=True,
    )  # fmt: skip
    return exe


def _run(
    model: Path, rate: int, jitter: int, seed: int, script: str, *extra: str
) -> dict[str, int]:
    out = subprocess.run(
        [str(model), str(rate), str(jitter), str(seed), script, *extra],
        capture_output=True, text=True, check=True,
    ).stdout  # fmt: skip
    return {k: int(v) for k, v in (t.split("=") for t in out.split())}


@pytest.mark.parametrize(
    "rate", [3600, 8000, 20000]
)  # 90 RPM, the 8000/s brief, 500 RPM
@pytest.mark.parametrize(
    "jitter", [0, 60, 150]
)  # late entries, cycles (isr_lat_max budget 200)
def test_no_tick_is_missed_in_either_direction(
    model: Path, rate: int, jitter: int
) -> None:
    for seed in (1, 2, 3):
        got = _run(model, rate, jitter, seed, "F5000R3200F40R1800F77R90")
        assert got["count"] == 5000 - 3200 + 40 - 1800 + 77 - 90, (
            rate,
            jitter,
            seed,
            got,
        )
        assert got["errors"] == 0, (rate, jitter, seed, got)


def test_an_illegal_double_step_is_counted_as_two_steps_and_flagged(
    model: Path,
) -> None:
    fwd = _run(model, 8000, 100, 5, "F500S F500")
    assert fwd["count"] == 1002 and fwd["errors"] == 1
    rev = _run(model, 8000, 100, 5, "F900R200S R200")
    assert rev["count"] == 900 - 200 - 2 - 200 and rev["errors"] == 1


def test_the_model_is_sensitive_beyond_the_sampling_limit(model: Path) -> None:
    """Above 16e6/(400+jitter) states/s samples are lost: proves the passes above mean something."""
    limit = _CPU_HZ // (_CYCLES_PER_POLL + 150)
    assert limit > 20000  # 500 RPM on 2400 counts/rev is 20000 counts/s; margin below
    lost = _run(model, 2 * limit, 150, 1, "F20000")
    assert lost["errors"] > 0 or lost["count"] != 20000


def test_entry_latency_is_recorded_and_the_isr_cuts_the_relay_at_the_stop_count(
    model: Path,
) -> None:
    got = _run(model, 20000, 150, 9, "F5000", "3000")
    assert (
        got["relay"] == 0 and got["cut"] == 3000
    )  # the exact count, not the late main sample
    assert 39 <= got["lat_max"] <= 150 + 39
    unarmed = _run(model, 20000, 0, 9, "F5000")
    assert unarmed["relay"] == 0 and unarmed["cut"] == -1
