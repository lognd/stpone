"""Unit tests for the bootloader port wait."""

from __future__ import annotations

from stpone.flash.errors import FlashError
from stpone.flash.port import wait_for_port


def test_wait_for_port_returns_first_match_sorted() -> None:
    # frob:tests src/stpone/flash/port.py::wait_for_port kind="unit"
    scans = iter([[], ["/dev/ttyACM1", "/dev/ttyACM0"]])
    result = wait_for_port(
        "/dev/ttyACM*",
        10.0,
        glob_fn=lambda p: next(scans),
        sleep=lambda s: None,
        now=lambda: 0.0,
    )
    assert result.danger_ok == "/dev/ttyACM0"


def test_wait_for_port_times_out() -> None:
    # frob:tests src/stpone/flash/port.py::wait_for_port kind="unit"
    clock = iter([0.0, 4.0, 11.0])
    result = wait_for_port(
        "/dev/ttyACM*",
        10.0,
        glob_fn=lambda p: [],
        sleep=lambda s: None,
        now=lambda: next(clock),
    )
    assert result.danger_err is FlashError.PortTimeout
