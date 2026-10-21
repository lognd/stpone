"""Unit test: the single paper-length conversion and result rows (include/stpalpha/length.h)."""

from __future__ import annotations

import math
import subprocess
from pathlib import Path

import pytest

_ROOT = Path(__file__).resolve().parents[2]
_DEFAULT = (101638, 2400, 206187)  # wheel_um, cpr, 30 yd in pulses


@pytest.fixture(scope="module")
def driver(tmp_path_factory: pytest.TempPathFactory) -> Path:
    exe = tmp_path_factory.mktemp("length") / "driver"
    subprocess.run(
        [
            "g++", "-std=c++17", "-O1", "-Wall", "-Wno-comment", "-Werror",
            f"-I{_ROOT / 'include'}", str(_ROOT / "tests/unit/length_driver.cpp"),
            "-o", str(exe),
        ],
        check=True,
    )  # fmt: skip
    return exe


def _rows(driver: Path, *cases: tuple[int, int, int, int]) -> list[list[str]]:
    text = "".join(" ".join(map(str, c)) + "\n" for c in cases)
    out = subprocess.run(
        [str(driver)], input=text, capture_output=True, text=True, check=True
    ).stdout
    return [line.split("|") for line in out.splitlines()]


def test_tenths_match_a_float_model_and_round_to_nearest(driver: Path) -> None:
    wheel, cpr, short = _DEFAULT
    pulses = [0, 1, 1000, short - 1, short, short + 1000, 3 * short]
    rows = _rows(driver, *[(p, wheel, cpr, short) for p in pulses])
    for p, row in zip(pulses, rows, strict=True):
        yards = p * math.pi * wheel / 25400 / cpr / 36
        assert abs(int(row[0]) - yards * 10) <= 0.51, (p, row)
    assert (
        rows[4][0] == "300"
    )  # 30.0 yd exactly at the target (truncation would say 29.9)


def test_len_field_is_four_digits_and_saturates_without_wrapping(driver: Path) -> None:
    wheel, cpr, short = _DEFAULT
    rows = _rows(driver, (short, wheel, cpr, short), (short * 40, wheel, cpr, short),
                 (4000000000, wheel, cpr, short), (5, wheel, 0, short))  # fmt: skip
    assert rows[0][1] == "  30.0"
    assert rows[1][1] == "1200.0"  # well past the target: keeps counting
    assert rows[2][0] == "99999" and rows[2][1] == "9999.9"  # saturates, never wraps
    assert rows[3][0] == "99999"  # a zero cpr cannot divide by zero


def test_result_rows_over_short_on_target_and_settling(driver: Path) -> None:
    wheel, cpr, short = _DEFAULT
    one_tenth = round(short / 300)  # pulses in 0.1 yd (3.6 in)
    rows = _rows(
        driver,
        (short + one_tenth, wheel, cpr, short),
        (short - 2 * one_tenth, wheel, cpr, short),
        (short, wheel, cpr, short),
    )  # fmt: skip
    # Under a yard the miss is in inches (tenths), not in whole tenths of a yard.
    assert rows[0][2:4] == ["DONE 30.1/30.0 yd".ljust(20), "+3.6 in over".ljust(20)]
    assert rows[1][2:4] == ["DONE 29.8/30.0 yd".ljust(20), "-7.2 in short".ljust(20)]
    assert rows[2][2:4] == ["DONE 30.0/30.0 yd".ljust(20), "on target".ljust(20)]
    assert rows[0][4:6] == ["NOW  30.1/30.0 yd".ljust(20), "settling...".ljust(20)]
    assert all(len(cell) == 20 for row in rows for cell in row[2:])


def test_miss_switches_to_yards_at_one_yard_and_never_prints_empty_numbers(
    driver: Path,
) -> None:
    wheel, cpr, short = _DEFAULT
    per_in = cpr / (math.pi * wheel / 25400)  # pulses per inch of paper
    cases = [
        (short + round(35.9 * per_in), "+35.9 in over"),
        (short + round(46.8 * per_in), "+1.3 yd over"),  # 46.8 in = 1.3 yd
        (short - round(12.4 * per_in), "-12.4 in short"),
    ]
    rows = _rows(
        driver,
        *[(p, wheel, cpr, short) for p, _ in cases[:3]],
        (short - 3, wheel, cpr, short),
    )
    for (_, want), row in zip(cases[:3], rows, strict=False):
        assert row[3] == want.ljust(20), row
    assert rows[3][3] == "on target".ljust(
        20
    )  # under half a tenth of an inch is on target
    for row in rows:
        # The AVR once printed " / yd" (empty numbers) because printf lacked %*d.
        assert any(ch.isdigit() for ch in row[1]) and any(
            ch.isdigit() for ch in row[2]
        ), row


def test_progress_row_shows_percent_and_a_bar(driver: Path) -> None:
    wheel, cpr, short = _DEFAULT
    rows = _rows(driver, (0, wheel, cpr, short), ((short + 1) // 2, wheel, cpr, short),
                 (short, wheel, cpr, short), (short * 2, wheel, cpr, short))  # fmt: skip
    assert rows[0][6] == "  0% [-------------]"
    assert rows[1][6] == " 50% [######-------]"
    assert rows[2][6] == "100% [#############]"
    assert rows[3][6] == "100% [#############]"  # past the target stays at 100
    assert all(len(r[6]) == 20 for r in rows)
