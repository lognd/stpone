"""Unit test: the incremental LCD repaint (include/stpalpha/panel.h, DEC-009)."""

from __future__ import annotations

import random
import subprocess
from pathlib import Path

import pytest

_ROOT = Path(__file__).resolve().parents[2]
_BUDGET = 5


@pytest.fixture(scope="module")
def driver(tmp_path_factory: pytest.TempPathFactory) -> Path:
    exe = tmp_path_factory.mktemp("panel") / "driver"
    subprocess.run(
        [
            "g++", "-std=c++17", "-O1", "-Wall", "-Wno-comment", "-Werror",
            f"-I{_ROOT / 'include'}", str(_ROOT / "tests/unit/panel_driver.cpp"),
            "-o", str(exe),
        ],
        check=True,
    )  # fmt: skip
    return exe


def _run(driver: Path, *lines: str) -> list[str]:
    out = subprocess.run(
        [str(driver)],
        input="\n".join(lines) + "\n",
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    return out.splitlines()


def _stats(line: str) -> dict[str, int]:
    return {k: int(v) for k, v in (t.split("=") for t in line.split())}


_ROWS = ("row 0 ROW-ZERO", "row 1 ROW-ONE", "row 2 ROW-TWO", "row 3 ROW-THREE")
_WIDE = (
    "row 0      WINDING...     ",
    "row 1  45% [#####--------]",
    "row 2 LEN 0012.3/0090.0 yd",
    "row 3 [1/2]STOP   [3]PAUSE",
)


def _screen(line: str) -> list[str]:
    return line.strip("|").split("||")


def _drain(n: int = 40) -> list[str]:
    return ["pump"] * n


def test_every_pump_is_bounded_and_the_panel_converges(driver: Path) -> None:
    out = _run(driver, *_ROWS, *_drain(30), "screen")
    pumps = [_stats(x) for x in out[4:34]]
    assert all(p["puts"] + p["moves"] <= _BUDGET for p in pumps), pumps
    assert pumps[-1]["sync"] == 1 and pumps[-1]["puts"] == 0
    first_sync = next(i for i, p in enumerate(pumps) if p["sync"])
    assert first_sync > 5  # a full rewrite is several passes, never one
    rows = _screen(out[-1])
    assert rows[0] == "ROW-ZERO".ljust(20)
    assert rows[1] == "ROW-ONE".ljust(20)
    assert rows[3] == "ROW-THREE".ljust(20)


def test_percent_and_length_digits_are_written_before_anything_else(
    driver: Path,
) -> None:
    out = _run(driver, *_WIDE, "pump screen")
    rows = _screen(out[-1])
    assert rows[1][:3] == " 45"  # the percent number, first
    assert rows[1][6:19] == "?" * 13  # the bar is not yet there
    assert rows[0].startswith("?") and rows[3].startswith("?")
    out = _run(driver, *_WIDE, *["pump"] * 5, "pump screen")
    rows = _screen(out[-1])
    assert rows[2][4:10] == "0012.3"  # the length digits
    assert "?" in rows[1][6:19]  # the bar is not finished


def test_a_digit_change_preempts_the_rest_of_a_full_rewrite(driver: Path) -> None:
    script = [*_WIDE, *_drain(), "invalidate", "pump", "row 2 LEN 0012.4/0090.0 yd",
              "pump", "pump", "pump", "pump screen"]  # fmt: skip
    out = _run(driver, *script)
    # within two chunks of the change (one may be a low-priority slot) the new tenths digit is
    # on the panel although the rewrite of the lower priority cells is far from finished
    rows = _screen(out[-1])
    assert rows[2][4:10] == "0012.4"
    assert _stats(out[-2])["sync"] == 0  # the heal is still running


def test_a_small_change_costs_one_cursor_move_and_the_changed_cells(
    driver: Path,
) -> None:
    out = _run(driver, *_ROWS, *_drain(), "row 2 ROW-TWX", "pump", "pump")
    first, second = _stats(out[-2]), _stats(out[-1])
    assert first == {"sync": 1, "puts": 1, "moves": 1}
    assert second["puts"] == 0 and second["moves"] == 0


def test_adjacent_changes_share_one_cursor_move(driver: Path) -> None:
    out = _run(driver, *_ROWS, *_drain(), "row 3 ROW-THREX", "row 3 ROW-THRXY", "pump")
    assert _stats(out[-1]) == {"sync": 1, "puts": 2, "moves": 1}


def test_a_pending_stop_toggle_makes_the_repaint_yield(driver: Path) -> None:
    out = _run(driver, *_ROWS, "interrupt 1", "pump", "interrupt 0", "pump")
    assert _stats(out[5]) == {"sync": 0, "puts": 0, "moves": 0}
    assert _stats(out[7])["puts"] > 0


def test_invalidate_rewrites_every_cell_and_repairs_a_corrupt_one(driver: Path) -> None:
    base = [*_ROWS, *_drain()]
    out = _run(driver, *base, "corrupt 1 3 #", "pump", "screen")
    assert "#" in out[-1]  # the shadow trusts what it sent: only a heal repairs it
    out = _run(driver, *base, "corrupt 1 3 #", "invalidate", *_drain(), "screen")
    assert "#" not in out[-1] and "ROW-ONE" in out[-1]


def test_a_bus_fault_stops_the_pump_without_marking_the_cell_shown(
    driver: Path,
) -> None:
    out = _run(driver, *_ROWS, "fault 3", "pump", "pump")
    assert _stats(out[5])["sync"] == 0
    assert _stats(out[6]) == {
        "sync": 0,
        "puts": 0,
        "moves": 0,
    }  # faulted: nothing more is sent


def _cell_class(r: int, c: int) -> int:
    if (r == 1 and c <= 2) or (r == 2 and 4 <= c <= 9):
        return 0
    if r == 1 and 6 <= c <= 18:
        return 1
    return 2


def _fuzz_script(passes: int, every: int) -> tuple[list[str], list[list[str]]]:
    """Script plus the wanted rows after each pump: a digit change every `every` passes."""
    rng = random.Random(7)
    wanted = [list(r[6:].ljust(20)) for r in _WIDE]
    lines = list(_WIDE)
    history: list[list[str]] = []
    for i in range(passes):
        if i % 60 == 30:
            lines.append(
                "invalidate"
            )  # a heal every ~60 passes: far more often than the real 2 s
        if i % every == 0:
            wanted[1][rng.randrange(0, 3)] = str(rng.randrange(10))
            wanted[2][rng.randrange(4, 10)] = str(rng.randrange(10))
            for r in (1, 2):
                lines.append(f"row {r} {''.join(wanted[r])}")
        lines.append("pump screen")
        history.append(["".join(r) for r in wanted])
    return lines, history


def _worst_ages(driver: Path, passes: int, every: int) -> dict[int, int]:
    lines, history = _fuzz_script(passes, every)
    out = _run(driver, *lines)
    stats = [_stats(x) for x in out if x.startswith("sync=")]
    screens = [_screen(x) for x in out if x.startswith("|")]
    assert len(stats) == len(screens) == passes
    assert all(
        s["puts"] + s["moves"] <= _BUDGET for s in stats
    )  # bounded work per pass
    age = {(r, c): 0 for r in range(4) for c in range(20)}
    worst = {0: 0, 1: 0, 2: 0}
    for i in range(passes):
        for r in range(4):
            for c in range(20):
                ok = screens[i][r][c] == history[i][r][c]
                age[(r, c)] = 0 if ok else age[(r, c)] + 1
                cls = _cell_class(r, c)
                worst[cls] = max(worst[cls], age[(r, c)])
    return worst


def test_fuzz_no_starvation_and_bounded_waits_under_continuous_changes(
    driver: Path,
) -> None:
    """Digit changes every 3rd pass (about 5x the real rate) plus a heal every 60 passes."""
    worst = _worst_ages(driver, 600, 3)
    # waits in passes (a pass is <= ~45 ms while repainting): documented in DEC-009
    assert worst[2] <= 32, worst  # state row, hints: guaranteed slot, never starved
    assert worst[1] <= 32, worst  # the bar
    assert worst[0] <= 8, worst  # percent and length digits
    # the real rate (a digit change roughly every 5th-10th pass) is even better
    real = _worst_ages(driver, 600, 8)
    assert real[0] <= worst[0] and real[2] <= worst[2]
