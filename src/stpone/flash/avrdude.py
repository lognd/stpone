from __future__ import annotations

import shutil
from collections.abc import Callable
from pathlib import Path

from typani.result import Err, Ok, Result

from stpone.flash.config import FlashConfig
from stpone.flash.errors import FlashError
from stpone.flash.proc import Runner, run_command
from stpone.logging import get_logger

_log = get_logger(__name__)


# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0505
# frob:tests tests/unit/test_flash_build.py::test_avrdude_argv_matches_legacy_upload_script kind="unit"  # noqa: E501
def avrdude_argv(cfg: FlashConfig, port: str, image: Path) -> list[str]:
    """The exact avrdude invocation the old upload.sh used (Caterina, avr109)."""
    return [
        "avrdude",
        "-v",
        f"-p{cfg.mcu}",
        f"-c{cfg.programmer}",
        f"-P{port}",
        f"-b{cfg.baud}",
        "-D",
        f"-Uflash:w:{image}:i",
    ]


# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0505
# frob:tests tests/unit/test_flash_build.py::test_flash_image_outcomes kind="unit"
def flash_image(
    cfg: FlashConfig,
    port: str,
    image: Path,
    runner: Runner = run_command,
    *,
    which: Callable[[str], str | None] = shutil.which,
) -> Result[None, FlashError]:
    """Write one Intel HEX image through the bootloader on `port`."""
    if which("avrdude") is None:
        _log.error("avrdude not found on PATH")
        return Err(FlashError.ToolMissing)
    _log.info("flashing %s via %s", image, port)
    result = runner(avrdude_argv(cfg, port, image), capture=False)
    if result.returncode != 0:
        _log.error("avrdude failed (%d)", result.returncode)
        return Err(FlashError.AvrdudeFailed)
    _log.info("flash complete")
    return Ok(None)
