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

# Pseudo-target meaning "every image in CMakeLists.txt".
# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0502
ALL_TARGET = "all"
# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0502
TOOLCHAIN_FILE = Path("cmake") / "avr-gcc-toolchain.cmake"


# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0502
def hex_path(cfg: FlashConfig, target: str) -> Path:
    """Where cmake's post-build objcopy step leaves the Intel HEX for a target."""
    return cfg.build_dir / f"{target}.hex"


# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0502
def configure(
    cfg: FlashConfig,
    runner: Runner = run_command,
    *,
    which: Callable[[str], str | None] = shutil.which,
) -> Result[None, FlashError]:
    """cmake configure with the AVR toolchain file; idempotent on an existing tree."""
    cmake = which("cmake")
    if cmake is None:
        _log.error("cmake not found on PATH")
        return Err(FlashError.ToolMissing)
    argv = [
        cmake,
        "-S",
        str(cfg.source_dir),
        "-B",
        str(cfg.build_dir),
        f"-DCMAKE_TOOLCHAIN_FILE={cfg.source_dir / TOOLCHAIN_FILE}",
    ]
    _log.info("configuring %s -> %s", cfg.source_dir, cfg.build_dir)
    result = runner(argv, capture=False)
    if result.returncode != 0:
        _log.error("cmake configure failed (%d)", result.returncode)
        return Err(FlashError.BuildFailed)
    return Ok(None)


# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0502
def build(
    cfg: FlashConfig,
    target: str,
    runner: Runner = run_command,
    *,
    which: Callable[[str], str | None] = shutil.which,
) -> Result[None, FlashError]:
    """cmake --build for one image (`<target>.elf`) or every image (ALL_TARGET)."""
    cmake = which("cmake")
    if cmake is None:
        _log.error("cmake not found on PATH")
        return Err(FlashError.ToolMissing)
    argv = [cmake, "--build", str(cfg.build_dir)]
    if target != ALL_TARGET:
        argv += ["--target", f"{target}.elf"]
    _log.info("building %s in %s", target, cfg.build_dir)
    result = runner(argv, capture=False)
    if result.returncode != 0:
        _log.error("cmake build failed (%d)", result.returncode)
        return Err(FlashError.BuildFailed)
    return Ok(None)
