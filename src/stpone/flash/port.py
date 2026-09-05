from __future__ import annotations

import glob
import time
from collections.abc import Callable

from typani.result import Err, Ok, Result

from stpone.flash.errors import FlashError
from stpone.logging import get_logger

_log = get_logger(__name__)
_POLL_S = 0.5


# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0504
# frob:tests tests/unit/test_flash_port.py::test_wait_for_port_times_out kind="unit"
# frob:tests tests/unit/test_flash_port.py::test_wait_for_port_returns_first_match_sorted kind="unit"  # noqa: E501
def wait_for_port(
    pattern: str,
    timeout_s: float,
    *,
    glob_fn: Callable[[str], list[str]] = glob.glob,
    sleep: Callable[[float], None] = time.sleep,
    now: Callable[[], float] = time.monotonic,
) -> Result[str, FlashError]:
    """Poll the serial-port glob until one device matches, or time out."""
    deadline = now() + timeout_s
    _log.info("waiting up to %.0f s for a port matching %s...", timeout_s, pattern)
    while True:
        matches = glob_fn(pattern)
        if matches:
            port = min(matches)
            _log.info("found bootloader port %s", port)
            return Ok(port)
        if now() >= deadline:
            _log.error("no port matched %s within %.0f s", pattern, timeout_s)
            return Err(FlashError.PortTimeout)
        sleep(_POLL_S)
