"""System tests: build every AVR image through scripts/flash.py and run one in simavr.

These are the executable evidence that the C HAL port (src/stpalpha/hal/setup.c)
boots and that its ISRs are wired into the vector table by avr-libc.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

_TOOLS = ("avr-gcc", "avr-objdump", "cmake", "simavr")
_missing = [t for t in _TOOLS if shutil.which(t) is None]

pytestmark = pytest.mark.skipif(bool(_missing), reason=f"missing tools: {_missing}")


@pytest.fixture(scope="module")
def build_dir(tmp_path_factory: pytest.TempPathFactory) -> Path:
    root = Path(__file__).resolve().parents[2]
    out = tmp_path_factory.mktemp("avr-build")
    proc = subprocess.run(
        [
            sys.executable,
            str(root / "scripts" / "flash.py"),
            "--build-only",
            "--target",
            "all",
            "--build-dir",
            str(out),
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    assert proc.returncode == 0, proc.stdout + proc.stderr
    return out


def test_every_image_builds(build_dir: Path) -> None:
    # frob:tests src/stpalpha/hal kind="integration"
    # frob:tests src/stpalpha/main.cpp kind="integration"
    for name in (
        "firmware",
        "hello_world",
        "test_buttons_leds",
        "test_buzzer",
        "test_encoder",
        "test_lcd",
        "test_leds_pins",
        "test_timer0",
    ):
        assert (build_dir / f"{name}.hex").exists(), name


def _vector_table(elf: Path) -> str:
    dump = subprocess.run(
        ["avr-objdump", "-d", "-j", ".text", str(elf)],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    start = dump.index("<__vectors>:")
    return dump[start : dump.index("\n\n", start)]


def test_firmware_vector_table_binds_timer_isrs(build_dir: Path) -> None:
    # frob:tests src/stpalpha/hal/setup.c kind="e2e"
    # frob:tests include/stpalpha/machine.h kind="integration"
    table = _vector_table(build_dir / "firmware.elf")
    lines = table.splitlines()
    # Slot n lives at byte 4n: 0x44 = TIMER1_COMPA (17), 0x54 = TIMER0_COMPA (21).
    assert "<__vector_17>" in next(ln for ln in lines if ln.strip().startswith("44:"))
    assert "<__vector_21>" in next(ln for ln in lines if ln.strip().startswith("54:"))
    # Every other slot falls through to the BADISR handler, never to reset.
    others = [ln for ln in lines[2:] if not ln.strip().startswith(("44:", "54:"))]
    assert all("<__bad_interrupt>" in ln for ln in others), others


def test_hal_init_runs_before_main(build_dir: Path) -> None:
    # frob:tests src/stpalpha/hal/setup.c::hal_init kind="e2e"
    # frob:tests scripts/flash.py kind="e2e"
    dump = subprocess.run(
        ["avr-objdump", "-d", str(build_dir / "firmware.elf")],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    hook = dump.index("<hal_init_hook>:")
    main_call = dump.index("<main>", hook)
    assert "<hal_init>" in dump[hook:main_call]


def test_hello_world_prints_on_usart1_in_simavr(build_dir: Path) -> None:
    # frob:tests tests/avr/hello_world.cpp kind="e2e"
    proc = subprocess.run(
        [
            "timeout",
            "3",
            "simavr",
            "-m",
            "atmega32u4",
            "-f",
            "16000000",
            str(build_dir / "hello_world.elf"),
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    # simavr echoes the simulated UART on stderr, not stdout.
    assert "Hello world from ATmega32U4!" in proc.stderr
