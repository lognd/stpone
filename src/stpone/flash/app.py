from __future__ import annotations

import shutil
import time
from collections.abc import Callable

from typani.result import Err, Ok, Result

from stpone.flash.avrdude import flash_image
from stpone.flash.build import ALL_TARGET, build, configure, hex_path
from stpone.flash.config import FlashConfig
from stpone.flash.errors import FlashError
from stpone.flash.host import HostKind, attach_bootloader, bind_bootloader, detect_host
from stpone.flash.port import wait_for_port
from stpone.flash.proc import Runner, run_command
from stpone.logging import get_logger

_log = get_logger(__name__)

# The old upload.sh slept briefly after the port appeared so the bootloader
# finished enumerating before avrdude opened it.
_SETTLE_S = 0.5


# frob:doc docs/spec/L5-component-design/SUB-05-flash-tool.md#comp-0506
class FlashApp:
    """Config in, flashed board out: build, attach (WSL), wait for port, avrdude."""

    def __init__(
        self,
        cfg: FlashConfig,
        *,
        runner: Runner = run_command,
        host: HostKind | None = None,
        which: Callable[[str], str | None] = shutil.which,
    ) -> None:
        self._cfg = cfg
        self._runner = runner
        self._host = host
        self._which = which

    def __call__(self) -> Result[None, FlashError]:
        cfg = self._cfg
        if cfg.setup:
            _log.info("one-time setup: binding the bootloader with usbipd")
            return bind_bootloader(cfg, self._runner, which=self._which)

        if cfg.build:
            configured = configure(cfg, self._runner, which=self._which)
            if configured.is_err:
                return configured
            built = build(cfg, cfg.target, self._runner, which=self._which)
            if built.is_err:
                return built
        else:
            _log.info("skipping build (--no-build)")

        if cfg.build_only:
            _log.info("build-only run finished")
            return Ok(None)

        image = hex_path(cfg, cfg.target)
        if cfg.target == ALL_TARGET or not image.exists():
            _log.error("no image to flash at %s", image)
            return Err(FlashError.HexMissing)

        host = self._host if self._host is not None else detect_host()
        _log.info("Double-tap RST on the Pro Micro now.")
        if host is HostKind.WSL:
            attached = attach_bootloader(cfg, self._runner, which=self._which)
            if attached.is_err:
                return attached
        else:
            _log.info("host is %s, not WSL: no USB forwarding needed", host.value)

        if cfg.port is not None:
            port = cfg.port
            _log.info("using port %s from the command line", port)
        else:
            found = wait_for_port(cfg.port_glob, cfg.port_timeout_s)
            if found.is_err:
                return Err(found.danger_err)
            port = found.danger_ok
            time.sleep(_SETTLE_S)

        return flash_image(cfg, port, image, self._runner, which=self._which)
