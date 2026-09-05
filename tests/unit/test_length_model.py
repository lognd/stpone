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
    length = _const(src, "kTargetLengthIn")
    circ = _f32(_f32(3.14159265) * wheel)
    assert (wheel, cpr, length) == (5.0, 2400.0, 1080.0)
    assert int(_f32(_f32(length * cpr) / circ)) == 165011
    assert round(length / 36.0, 1) == 30.0
