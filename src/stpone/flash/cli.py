from __future__ import annotations

import argparse
from collections.abc import Sequence
from pathlib import Path

from dotenv import load_dotenv

from stpone.flash.app import FlashApp
from stpone.flash.build import ALL_TARGET
from stpone.flash.config import FlashConfig
from stpone.flash.errors import REMEDIES
from stpone.logging import get_logger

_log = get_logger(__name__)

# scripts/flash.py may be run from any cwd; the repo root is three levels up.
_REPO_PYPROJECT = Path(__file__).resolve().parents[3] / "pyproject.toml"


# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0507
def build_parser() -> argparse.ArgumentParser:
    """CLI for scripts/flash.py; every option defaults to None so pyproject wins."""
    p = argparse.ArgumentParser(
        prog="flash.py",
        description=(
            "Build the firmware, hand the Pro Micro bootloader to WSL (no-op on "
            "plain Linux), and flash it with avrdude. Double-tap RST when asked."
        ),
    )
    p.add_argument(
        "--target",
        default=None,
        help=(
            "image to build/flash (cmake target without .elf; "
            f"'{ALL_TARGET}' with --build-only)"
        ),
    )
    p.add_argument("--build-dir", dest="build_dir", type=Path, default=None)
    p.add_argument("--port", default=None, help="serial port; skips the port wait")
    p.add_argument(
        "--no-build",
        dest="build",
        action="store_false",
        default=None,
        help="flash the existing image without rebuilding",
    )
    p.add_argument(
        "--build-only",
        dest="build_only",
        action="store_true",
        default=None,
        help="stop after the build (CI and simulator use)",
    )
    p.add_argument(
        "--setup",
        action="store_true",
        default=None,
        help="one-time elevated usbipd bind of the bootloader (WSL only)",
    )
    p.add_argument(
        "--attach-timeout", dest="attach_timeout_s", type=float, default=None
    )
    p.add_argument("--port-timeout", dest="port_timeout_s", type=float, default=None)
    p.add_argument(
        "--pyproject",
        type=Path,
        default=_REPO_PYPROJECT,
        help="pyproject.toml holding [tool.stpone.flash] (default: this repo's)",
    )
    return p


# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0507
def main(argv: Sequence[str] | None = None) -> int:
    """Entry point for scripts/flash.py; returns the process exit code."""
    load_dotenv()
    parser = build_parser()
    args = parser.parse_args(argv)
    cfg = FlashConfig.from_external(args, args.pyproject)
    if cfg.target == ALL_TARGET and not cfg.build_only:
        parser.error(f"--target {ALL_TARGET} only makes sense with --build-only")
    result = FlashApp(cfg)()
    if result.is_err:
        err = result.danger_err
        _log.error("flash failed: %s", err)
        _log.error("next step: %s", REMEDIES[err])
        return 1
    return 0
