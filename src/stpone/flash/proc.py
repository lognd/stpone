from __future__ import annotations

# frob:waive WIRE001 reason="Runner is a typing Protocol referenced from annotations in every flash module -- a type not a call"
import subprocess
from collections.abc import Sequence
from pathlib import Path
from typing import Protocol

from stpone.logging import get_logger

_log = get_logger(__name__)


# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0508
class Runner(Protocol):
    """Anything that runs an argv and returns a CompletedProcess (real or fake)."""

    def __call__(
        self,
        argv: Sequence[str],
        *,
        cwd: Path | None = None,
        capture: bool = True,
    ) -> subprocess.CompletedProcess[str]: ...


# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0508
def run_command(
    argv: Sequence[str],
    *,
    cwd: Path | None = None,
    capture: bool = True,
) -> subprocess.CompletedProcess[str]:
    """Run one external tool with argv logging; never raises on a non-zero exit."""
    _log.debug("run: %s (cwd=%s, capture=%s)", " ".join(argv), cwd, capture)
    result = subprocess.run(
        list(argv), cwd=cwd, capture_output=capture, text=True, check=False
    )
    _log.debug("exit %d: %s", result.returncode, argv[0])
    if capture and result.returncode != 0:
        _log.debug("stderr: %s", result.stderr.strip())
    return result
