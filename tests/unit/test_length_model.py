"""Unit test: the length model of SPEC-001 matches the firmware constants."""

from __future__ import annotations

import re
import struct
from pathlib import Path

_MAIN = Path(__file__).resolve().parents[2] / "src" / "stpalpha" / "main.cpp"


def _f32(x: float) -> float:
    return struct.unpack("f", struct.pack("f", x))[0]


def _const(src: str, name: str) -> float:
    match = re.search(name + r"\s*=\s*([0-9.]+)f", src)
    assert match is not None, name
    return _f32(float(match.group(1)))


def test_target_pulses_match_firmware_constants() -> None:
    # frob:tests src/stpalpha/main.cpp::main kind="unit"
    # frob:tests include/stpalpha/machine.h kind="integration"
    src = _MAIN.read_text()
    wheel = _const(src, "kWheelDiamIn")
    cpr = _const(src, "kCountsPerRev")
    short_len = _const(src, "kShortLengthIn")
    long_len = _const(src, "kLongLengthIn")
    circ = _f32(_f32(3.14159265) * wheel)
    assert (wheel, cpr) == (_f32(4.0015), 2400.0)
    assert (short_len, long_len) == (1080.0, 3240.0)
    assert int(_f32(_f32(short_len * cpr) / circ)) == 206187
    assert int(_f32(_f32(long_len * cpr) / circ)) == 618562
    assert round(short_len / 36.0, 1) == 30.0
    assert round(long_len / 36.0, 1) == 90.0
