"""System tests: the real firmware image in simavr, driven through LATCHING panel buttons.

The panel switches latch: a push leaves the contact down, the next push lets it up, and
the pin level stays where the push left it. These tests model exactly that (each push
flips the pin and holds it) and assert DEC-011: every push is one click, from either pin
level, including a button already latched down at power-up and the pushes after a wind
that finished by itself.

Only the simavr command line is used (CI installs no libsimavr): pin and UART stimulus go
in through a VCD input file (`simavr -i`), the relay pin comes out through a VCD trace
requested by a `.mmcu` section added to a copy of the ELF, and the machine state is read
with the read-only host-link queries DUMP / PARAM on USART1, which simavr echoes on stderr.

Covers take_pending (src/stpalpha/main.cpp) and Controller::step_idle / step_wind
(include/stpalpha/controller.h). No frob:tests directive: frob's graph has no C++ symbols,
so the bind would be an unresolved TEST001 like those in test_firmware.py.
"""

from __future__ import annotations

import re
import shutil
import struct
import subprocess
from pathlib import Path

import pytest

_TOOLS = ("avr-gcc", "avr-objcopy", "cmake", "simavr")
_missing = [t for t in _TOOLS if shutil.which(t) is None]

pytestmark = pytest.mark.skipif(bool(_missing), reason=f"missing tools: {_missing}")

# simavr .mmcu tags (simavr/sim/avr/avr_mcu_section.h, simavr 1.6).
_TAG_NAME = 1
_TAG_FREQUENCY = 2
_TAG_VCD_FILENAME = 12
_TAG_VCD_PERIOD = 13
_TAG_VCD_PORTPIN = 15

# VCD input aliases -> simavr IRQ names (<ioctl>_<index>): the hal/setup.c pin map.
_BUTTON_PINS = {1: "iogB_5", 2: "iogB_4", 3: "iogE_6", 4: "iogD_7"}
_BUTTON_ALIAS = {1: "a", 2: "b", 3: "c", 4: "d"}
_ENC_A, _ENC_B, _UART = "e", "f", "u"

_PUSH_SETTLE_MS = 300  # debounce (16 ms) + at most one slow pass + the click's own pass
_QUERY_US_PER_BYTE = 120  # one byte at 115200 baud is ~87 us
_ENC_EDGE_US = 100  # 10 kHz quadrature edges, well inside the 40 kHz poll
_GREEN = re.compile(r"\x1b\[32m(.*?)\n\x1b\[0m", re.DOTALL)


def _tag(tag: int, payload: bytes) -> bytes:
    """One simavr .mmcu record: tag, length, payload."""
    return bytes([tag, len(payload)]) + payload


def _portpin(port: str, pin: int, name: str) -> bytes:
    """A VCD_PORTPIN record: trace one output pin under `name`."""
    return _tag(
        _TAG_VCD_PORTPIN,
        struct.pack("<BH", ord(port), pin) + name.encode().ljust(32, b"\0"),
    )


_MMCU = (
    _tag(_TAG_NAME, b"atmega32u4".ljust(16, b"\0"))
    + _tag(_TAG_FREQUENCY, struct.pack("<I", 16_000_000))
    + _tag(_TAG_VCD_FILENAME, b"trace.vcd".ljust(64, b"\0"))
    + _tag(_TAG_VCD_PERIOD, struct.pack("<I", 1000))
    + _portpin("B", 6, "relay")
)


class Bench:
    """A scripted run of the firmware: latching pushes, encoder pulses and DUMP checkpoints."""

    def __init__(
        self, elf: Path, work: Path, *, levels: tuple[int, int, int, int] = (0, 0, 0, 0)
    ) -> None:
        self._elf = elf
        self._work = work
        self._t_us = 0
        self._events: list[tuple[int, str]] = []
        self._level = dict(zip((1, 2, 3, 4), levels, strict=True))
        self._enc_phase = 0
        self._queries: list[str] = []
        self.replies: dict[str, dict[str, str]] = {}
        self.relay_edges: list[tuple[int, int]] = []  # (time us, new level)
        self.pushes: list[tuple[int, int]] = []  # (button, time us) of every push

    def at(self, ms: float) -> Bench:
        """Move the script clock to `ms` after power-up (never backwards)."""
        us = int(ms * 1000)
        assert us >= self._t_us, f"script clock went backwards to {ms} ms"
        self._t_us = us
        return self

    def wait(self, ms: float) -> Bench:
        """Advance the script clock by `ms`."""
        return self.at(self._t_us / 1000 + ms)

    def _flip(self, btn: int) -> None:
        """The pin edge of one push of a latching button: it flips and stays."""
        self._level[btn] ^= 1
        self._events.append((self._t_us, f"{self._level[btn]}{_BUTTON_ALIAS[btn]}"))
        self.pushes.append((btn, self._t_us))

    def push(self, btn: int) -> Bench:
        """One push of a latching button, then let it settle."""
        self._flip(btn)
        return self.wait(_PUSH_SETTLE_MS)

    def quick_pushes(self, btn: int, gap_ms: float) -> Bench:
        """Two pushes `gap_ms` apart (faster than one main-loop pass), then settle."""
        self._flip(btn)
        self.wait(gap_ms)
        return self.push(btn)

    def pulses(self, n: int) -> Bench:
        """Feed `n` quadrature edges on PD4/PC6 (each one count in 4x decoding)."""
        seq = ((1, 1), (0, 1), (0, 0), (1, 0))  # Gray order from the pulled-up idle
        for _ in range(n):
            self._enc_phase = (self._enc_phase + 1) % 4
            a, b = seq[self._enc_phase]
            self._events.append((self._t_us, f"{a}{_ENC_A}"))
            self._events.append((self._t_us, f"{b}{_ENC_B}"))
            self._t_us += _ENC_EDGE_US
        return self

    def send(self, line: str, label: str | None = None) -> Bench:
        """Type one host-link line on USART1; `label` names its reply for `self.replies`."""
        for ch in line + "\n":
            self._events.append((self._t_us, f"b{ord(ch):08b} {_UART}"))
            self._t_us += _QUERY_US_PER_BYTE
        self._queries.append(label or f"#{len(self._queries)}")
        return self.wait(80)

    def dump(self, label: str) -> Bench:
        """Checkpoint: record the DUMP reply under `label`."""
        return self.send("DUMP", label)

    def run(self, tail_ms: float = 200) -> Bench:
        """Write the stimulus, run simavr to its end, and parse replies and the relay trace."""
        self._work.mkdir(parents=True, exist_ok=True)
        (self._work / "mmcu.bin").write_bytes(_MMCU)
        sim_elf = self._work / "sim.elf"
        subprocess.run(
            [
                "avr-objcopy",
                "--add-section",
                f".mmcu={self._work / 'mmcu.bin'}",
                str(self._elf),
                str(sim_elf),
            ],
            check=True,
            capture_output=True,
        )
        end_us = self._t_us + int(tail_ms * 1000)
        names = [(_BUTTON_ALIAS[b], 1, _BUTTON_PINS[b]) for b in (1, 2, 3, 4)]
        names += [(_ENC_A, 1, "iogD_4"), (_ENC_B, 1, "iogC_6"), (_UART, 8, "uar1_0")]
        head = ["$timescale 1us $end", "$scope module bench $end"]
        head += [f"$var wire {size} {alias} {name} $end" for alias, size, name in names]
        head += ["$upscope $end", "$enddefinitions $end"]
        # Power-up levels: buttons where the script says they are latched, encoder pulled up.
        body = [
            "#1",
            *(f"{self._level_at_boot(b)}{_BUTTON_ALIAS[b]}" for b in (1, 2, 3, 4)),
        ]
        body += [f"1{_ENC_A}", f"1{_ENC_B}"]
        last = -1
        for when, value in sorted(self._events, key=lambda e: e[0]):
            stamp = max(when, 2)
            if stamp != last:
                body.append(f"#{stamp}")
                last = stamp
            body.append(value)
        body += [
            f"#{end_us}",
            f"{(1, 1, 0, 0)[self._enc_phase]}{_ENC_B}",
        ]  # sets the run length
        (self._work / "in.vcd").write_text("\n".join(head + body) + "\n")
        proc = subprocess.run(
            ["simavr", "-m", "atmega32u4", "-f", "16000000", "-i", "in.vcd", "sim.elf"],
            cwd=self._work,
            capture_output=True,
            text=True,
            timeout=300,
            check=False,
        )
        assert proc.returncode == 0, proc.stdout + proc.stderr
        lines = _uart_lines(proc.stderr)
        answers = [ln for ln in lines if ln.startswith(("OK", "ERR"))]
        assert len(answers) == len(self._queries), (self._queries, lines)
        for label, answer in zip(self._queries, answers, strict=True):
            self.replies[label] = _fields(answer)
        self.relay_edges = _relay_edges(self._work / "trace.vcd")
        return self

    def _level_at_boot(self, btn: int) -> int:
        """The power-up pin level: the final level undone by every scripted flip."""
        flips = sum(1 for b, _ in self.pushes if b == btn)
        return self._level[btn] ^ (flips & 1)

    def relay_on_count(self) -> int:
        """How many times the relay pin went high during the whole run."""
        return sum(1 for _, level in self.relay_edges if level == 1)


def _uart_lines(stderr: str) -> list[str]:
    """Reassemble USART1 lines from simavr's stderr echo (chunks of <= 256, '.' for \\n)."""
    lines: list[str] = []
    buf = ""
    for chunk in _GREEN.findall(stderr):
        buf += chunk
        if chunk.endswith("."):
            lines.append(buf.rstrip("."))
            buf = ""
    return lines


def _fields(answer: str) -> dict[str, str]:
    """`OK a=1 b=2` -> {"status": "OK", "a": "1", "b": "2"}."""
    head, *rest = answer.split()
    out = {"status": head}
    for item in rest:
        key, _, value = item.partition("=")
        out[key] = value
    return out


def _relay_edges(trace: Path) -> list[tuple[int, int]]:
    """Relay (PB6) level changes from simavr's VCD trace (10 ns timescale), in us."""
    alias = None
    edges: list[tuple[int, int]] = []
    now = 0
    for line in trace.read_text().splitlines():
        if line.startswith("$var") and line.split()[4] == "relay":
            alias = line.split()[3]
        elif line.startswith("#"):
            now = int(line[1:]) // 100
        elif alias and len(line) == 2 and line[1] == alias and line[0] in "01":
            level = int(line[0])
            if not edges or edges[-1][1] != level:
                edges.append((now, level))
    return edges


def _assert_state(
    bench: Bench, label: str, *, state: str, relay: int, manual: int = 0
) -> None:
    """One checkpoint's machine state, with the whole reply in the failure message."""
    got = bench.replies[label]
    assert (got["state"], int(got["relay"]), int(got["manual"])) == (
        state,
        relay,
        manual,
    ), (label, got)


def _toggles_between(bench: Bench, first: str, last: str) -> list[int]:
    """Debounced edges counted per button between two checkpoints (the DUMP toggles field)."""
    a = [int(x) for x in bench.replies[first]["toggles"].split(",")]
    z = [int(x) for x in bench.replies[last]["toggles"].split(",")]
    return [y - x for x, y in zip(a, z, strict=True)]


@pytest.fixture
def firmware(build_dir: Path) -> Path:
    """The production image, exactly as flashed."""
    return build_dir / "firmware.elf"


@pytest.mark.parametrize("level", [0, 1], ids=["pins-up", "pins-latched-down"])
def test_every_latching_push_is_one_action(
    firmware: Path, tmp_path: Path, level: int
) -> None:
    """Each push of each button does exactly one thing, from either power-up pin level."""
    b = Bench(firmware, tmp_path, levels=(level,) * 4)
    b.at(400).send("PARAM", "param").dump("boot")
    b.push(1).dump("btn1 start").push(1).dump("btn1 stop").push(1).dump("btn1 again")
    b.push(1).dump("btn1 stop again")
    b.push(2).dump("btn2 start").push(2).dump("btn2 stop")
    b.push(1).dump("wind for pause")
    b.push(3).dump("btn3 pause").push(3).dump("btn3 play").push(3).dump(
        "btn3 pause again"
    )
    b.push(1).dump("stop paused wind")
    b.push(4).dump("btn4 manual on").push(4).dump("btn4 manual off")
    b.run()

    short, long = b.replies["param"]["short"], b.replies["param"]["long"]
    _assert_state(
        b, "boot", state="IDLE", relay=0
    )  # a latched-down button starts nothing
    assert b.replies["boot"]["pending"] == "0,0,0,0", b.replies[
        "boot"
    ]  # boot edges flushed
    _assert_state(b, "btn1 start", state="WINDING", relay=1)
    assert b.replies["btn1 start"]["target"] == short
    _assert_state(b, "btn1 stop", state="IDLE", relay=0)
    _assert_state(b, "btn1 again", state="WINDING", relay=1)
    _assert_state(b, "btn1 stop again", state="IDLE", relay=0)
    _assert_state(b, "btn2 start", state="WINDING", relay=1)
    assert b.replies["btn2 start"]["target"] == long
    _assert_state(b, "btn2 stop", state="IDLE", relay=0)
    _assert_state(b, "wind for pause", state="WINDING", relay=1)
    _assert_state(b, "btn3 pause", state="PAUSED", relay=0)
    _assert_state(b, "btn3 play", state="WINDING", relay=1)
    _assert_state(b, "btn3 pause again", state="PAUSED", relay=0)
    _assert_state(b, "stop paused wind", state="IDLE", relay=0)
    _assert_state(b, "btn4 manual on", state="IDLE", relay=1, manual=1)
    _assert_state(b, "btn4 manual off", state="IDLE", relay=0, manual=0)
    # Four winds, one resume and one manual run: no other relay closure anywhere.
    assert b.relay_on_count() == 6, b.relay_edges
    assert _toggles_between(b, "boot", "btn4 manual off") == [6, 2, 3, 2]


@pytest.mark.parametrize("level", [0, 1], ids=["btn1-up", "btn1-latched-down"])
def test_one_push_starts_the_next_wind_after_a_finished_wind(
    firmware: Path, tmp_path: Path, level: int
) -> None:
    """A wind that ended on its own count leaves the switch latched; the next push still starts."""
    b = Bench(firmware, tmp_path, levels=(level, 0, 0, 0))
    b.at(400)
    for line in ("SET bump 0", "SET coast 0", "SET margin 0", "SET short 1000"):
        b.send(line, line)
    b.push(1).dump("start").pulses(1100).wait(_PUSH_SETTLE_MS).dump("finished")
    b.push(1).dump("next wind").push(1).dump("stopped")
    b.run()

    for line in ("SET bump 0", "SET coast 0", "SET margin 0", "SET short 1000"):
        assert b.replies[line]["status"] == "OK", (line, b.replies[line])
    _assert_state(b, "start", state="WINDING", relay=1)
    _assert_state(b, "finished", state="IDLE", relay=0)
    assert int(b.replies["finished"]["count"]) >= 1000, b.replies["finished"]
    _assert_state(b, "next wind", state="WINDING", relay=1)
    assert b.replies["next wind"]["count"] == "0", b.replies["next wind"]
    _assert_state(b, "stopped", state="IDLE", relay=0)
    assert b.relay_on_count() == 2, b.relay_edges


def test_rapid_double_pushes_are_two_clicks(firmware: Path, tmp_path: Path) -> None:
    """Two pushes inside one slow pass are two clicks: start+stop, manual on+off."""
    b = Bench(firmware, tmp_path)
    b.at(400).dump("boot").quick_pushes(1, 30).dump("btn1 double").quick_pushes(
        4, 30
    ).dump("btn4 double")
    b.run()

    _assert_state(b, "btn1 double", state="IDLE", relay=0)
    _assert_state(b, "btn4 double", state="IDLE", relay=0, manual=0)
    assert _toggles_between(b, "boot", "btn4 double") == [2, 0, 0, 2]
    assert b.relay_on_count() == 2, (
        b.relay_edges
    )  # one short wind, one short manual run


def test_manual_nudge_past_the_stop_while_paused_waits_for_play(
    firmware: Path, tmp_path: Path
) -> None:
    """Pausing, then nudging with manual past eff_target, never lets the trim bump by itself."""
    b = Bench(firmware, tmp_path)
    b.at(400)
    for line in ("SET bump 1", "SET margin 0", "SET coast 300", "SET short 1000"):
        b.send(line, line)
    b.push(1).pulses(500).push(3).dump("paused")
    b.push(4).pulses(300).wait(_PUSH_SETTLE_MS).dump("past stop")
    b.push(4).dump("manual off").wait(2000).dump("still paused")
    b.push(3).wait(1500).dump("resumed")
    b.push(1).dump("stopped")
    b.run()

    for line in ("SET bump 1", "SET margin 0", "SET coast 300", "SET short 1000"):
        assert b.replies[line]["status"] == "OK", (line, b.replies[line])
    _assert_state(b, "paused", state="PAUSED", relay=0)
    assert b.replies["past stop"]["state"] == "PAUSED", b.replies["past stop"]
    assert b.replies["past stop"]["trim"] == "1", b.replies["past stop"]
    _assert_state(b, "still paused", state="PAUSED", relay=0)
    assert b.replies["still paused"]["bumps"] == "0", b.replies["still paused"]
    # Play: the trim settles, then bumps toward the goal by itself (the operator asked for it).
    assert b.replies["resumed"]["state"] == "TRIM", b.replies["resumed"]
    assert int(b.replies["resumed"]["bumps"]) >= 1, b.replies["resumed"]
    _assert_state(b, "stopped", state="IDLE", relay=0)
    manual_off_us, play_us = b.pushes[-3][1], b.pushes[-2][1]
    closed = [t for t, lv in b.relay_edges if lv == 1]
    assert not [t for t in closed if manual_off_us < t < play_us], (
        b.relay_edges
    )  # no self-start
    assert [t for t in closed if t > play_us], b.relay_edges  # the bump after play
