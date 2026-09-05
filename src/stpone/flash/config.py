from __future__ import annotations

import argparse
import tomllib
from pathlib import Path
from typing import Any

from pydantic import BaseModel

from stpone.logging import get_logger

_log = get_logger(__name__)

# Config fields the CLI may override; None on the namespace means "unset".
# (Also what frob's FLAGCOV001 gate checks the parser against.)
# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0501
CLI_FIELDS: frozenset[str] = frozenset(
    {
        "build_dir",
        "target",
        "port",
        "build",
        "build_only",
        "setup",
        "attach_timeout_s",
        "port_timeout_s",
    }
)


# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0501
class FlashConfig(BaseModel):
    """Settings for one flash run: pyproject [tool.stpone.flash] defaults, CLI wins."""

    model_config = {"frozen": True}

    source_dir: Path = Path(".")
    build_dir: Path = Path("build")
    target: str = "firmware"
    mcu: str = "atmega32u4"
    programmer: str = "avr109"
    baud: int = 57600
    bootloader_hardware_id: str = "2341:0036"
    port_glob: str = "/dev/ttyACM*"
    attach_timeout_s: float = 15.0
    port_timeout_s: float = 10.0
    port: str | None = None
    build: bool = True
    build_only: bool = False
    setup: bool = False

    @classmethod
    def from_external(
        cls, args: argparse.Namespace, pyproject: Path | None = None
    ) -> FlashConfig:
        """Merge the pyproject table with the parsed CLI args (CLI wins)."""
        # frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0501
        merged: dict[str, Any] = {}
        resolved = pyproject if pyproject is not None else Path("pyproject.toml")
        if resolved.exists():
            with resolved.open("rb") as f:
                data = tomllib.load(f)
            merged.update(data.get("tool", {}).get("stpone", {}).get("flash", {}))
            merged["source_dir"] = resolved.resolve().parent
            _log.debug("loaded [tool.stpone.flash] from %s", resolved)
        else:
            _log.debug("no pyproject at %s; using built-in defaults", resolved)
        for name in sorted(CLI_FIELDS):
            value = getattr(args, name, None)
            if value is not None:
                merged[name] = value
        cfg = cls(**merged)
        _log.debug("flash config: %s", cfg.model_dump())
        return cfg
