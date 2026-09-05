from __future__ import annotations

import logging
import logging.config
import tomllib
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path

_CONFIG_PATH = Path(__file__).parent / "config.toml"
_initialized = False


def _init() -> None:
    global _initialized
    if _initialized:
        return
    with _CONFIG_PATH.open("rb") as f:
        cfg = tomllib.load(f)
    logging.config.dictConfig(cfg)
    _initialized = True


# frob:doc docs/spec/L5-component-design/SUB-06-desktop-core.md#comp-0601
def get_logger(name: str) -> logging.Logger:
    """Return a configured logger; call with __name__ from each module."""
    _init()
    return logging.getLogger(name)


# frob:doc docs/spec/L5-component-design/SUB-06-desktop-core.md#comp-0601
@contextmanager
def error_ctx(
    logger: logging.Logger,
    *,
    err: type[Exception] | tuple[type[Exception], ...] = Exception,
) -> Iterator[None]:
    """Log any matching exception raised in the block, then re-raise it."""
    try:
        yield
    except err as e:
        logger.error(e)
        raise
