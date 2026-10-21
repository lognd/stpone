"""System test: the panel and host-link text formats identically under avr-libc and glibc.

The host unit tests compile include/stpalpha/{render,length,hostlink}.h with g++/glibc, where a
`%*d` or a NULL `%s` works; avr-libc's printf drops the rest of the output (DEC-008, DEC-009).
tests/avr/test_render_fmt.cpp runs the very same case table on the ATmega32U4 in simavr and this
test requires its USART1 output to equal the host run line for line.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

_ROOT = Path(__file__).resolve().parents[2]
_TOOLS = ("avr-gcc", "cmake", "simavr", "g++")
_missing = [t for t in _TOOLS if shutil.which(t) is None]

pytestmark = pytest.mark.skipif(bool(_missing), reason=f"missing tools: {_missing}")


@pytest.fixture(scope="module")
def host_lines(tmp_path_factory: pytest.TempPathFactory) -> list[str]:
    exe = tmp_path_factory.mktemp("render") / "driver"
    subprocess.run(
        [
            "g++", "-std=c++17", "-O1", "-Wall", "-Wno-comment", "-Werror",
            f"-I{_ROOT / 'include'}", str(_ROOT / "tests/unit/render_driver.cpp"),
            "-o", str(exe),
        ],
        check=True,
    )  # fmt: skip
    return subprocess.run(
        [str(exe)], capture_output=True, text=True, check=True
    ).stdout.splitlines()


@pytest.fixture(scope="module")
def avr_lines(tmp_path_factory: pytest.TempPathFactory) -> list[str]:
    out = tmp_path_factory.mktemp("avr-render")
    proc = subprocess.run(
        [
            sys.executable, str(_ROOT / "scripts" / "flash.py"), "--build-only",
            "--target", "test_render_fmt", "--build-dir", str(out),
        ],
        capture_output=True,
        text=True,
        check=False,
    )  # fmt: skip
    assert proc.returncode == 0, proc.stdout + proc.stderr
    sim = subprocess.run(
        ["timeout", "5", "simavr", "-m", "atmega32u4", "-f", "16000000",
         str(out / "test_render_fmt.elf")],
        capture_output=True,
        text=True,
        check=False,
    )  # fmt: skip
    # simavr colours each echoed line and shows the newline it flushed on as a trailing ".".
    text = re.sub(r"\x1b\[[0-9;]*m", "", sim.stderr)
    assert "@done" in text, text[-2000:]
    lines = text[: text.index("@done")].splitlines()
    return [ln[:-1] if ln.endswith(".") else ln for ln in lines]


def test_avr_libc_formats_every_screen_and_reply_like_glibc(
    host_lines: list[str], avr_lines: list[str]
) -> None:
    assert len(host_lines) > 40
    assert avr_lines[-len(host_lines) :] == host_lines


def test_no_printf_width_star_or_hh_modifiers_in_firmware_sources() -> None:
    """avr-libc's vfprintf has no `*` width/precision and no `hh`/`j`/`z`/`t` modifiers."""
    bad = re.compile(r"%[-+ #0]*(\*|\d*\.\*|\d*hh|\d*[jzt][dium])")
    offenders = []
    for path in [
        *(_ROOT / "include").rglob("*.h"),
        *(_ROOT / "src" / "stpalpha").rglob("*.[ch]*"),
    ]:
        for n, line in enumerate(path.read_text().splitlines(), 1):
            if '"' in line and bad.search(line):
                offenders.append(f"{path.relative_to(_ROOT)}:{n}: {line.strip()}")
    assert not offenders, offenders


def test_trim_and_failed_wind_screens_have_the_documented_rows(
    host_lines: list[str],
) -> None:
    """DEC-010: TRIMMING on row 0; a failed wind never uses the DONE wording."""

    def rows(line: str) -> list[str]:
        return [r.rstrip("~") for r in line.split("|")[1:]]

    trim = next(rows(x) for x in host_lines if x.startswith("trim|"))
    assert trim[0] == "     TRIMMING...    "
    assert trim[1].startswith(" 99% [")  # the bar and percent use the nominal target
    assert trim[3] == "[1/2]STOP   [3]PAUSE"
    short = [rows(x) for x in host_lines if x.startswith("short|")]
    stopped = [rows(x) for x in host_lines if x.startswith("stopped|")]
    assert short and stopped
    for r in short + stopped:
        assert r[0] == " !! UNDER TARGET !! "
        assert "DONE" not in "".join(r) and "on target" not in "".join(r)
    # count 203187 (3000 under the target) -> goal 206378 is 3191 pulses = 16.7 in away
    assert short[1][1].startswith("SHORT -16.7 in") and stopped[1][1].startswith(
        "STOPPED -16.7 in"
    )
    assert short[1][2].startswith("LEN 29.6/30.0 yd")
    # a settling failed wind still reads like any settling wind; a PASS keeps the DONE wording
    settling = next(rows(x) for x in host_lines if x.startswith("short-s|"))
    assert settling[0] == " -= PAPER WINDER =- " and settling[1].startswith("NOW ")
    passed = next(rows(x) for x in host_lines if x.startswith("pass|"))
    assert passed[1].startswith("DONE ") and passed[0] == " -= PAPER WINDER =- "
