"""Unit tests for the process runner and the error table."""

from __future__ import annotations

import sys

from stpone.flash.errors import REMEDIES, FlashError
from stpone.flash.proc import run_command


def test_run_command_never_raises() -> None:
    # frob:tests src/stpone/flash/proc.py::run_command kind="unit"
    # frob:tests src/stpone/flash/proc.py::Runner kind="unit"
    ok = run_command([sys.executable, "-c", "print('hi')"])
    assert ok.returncode == 0
    assert ok.stdout.strip() == "hi"
    bad = run_command([sys.executable, "-c", "import sys; sys.exit(3)"])
    assert bad.returncode == 3


def test_every_error_has_a_remedy() -> None:
    # frob:tests src/stpone/flash/errors.py::FlashError kind="unit"
    # frob:tests src/stpone/flash/errors.py::REMEDIES kind="unit"
    for member in FlashError:
        assert member in REMEDIES, member
        assert REMEDIES[member]
