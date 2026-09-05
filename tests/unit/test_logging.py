"""Unit tests for the logging setup."""

from __future__ import annotations

import logging

import pytest

from stpone.logging.filter import BelowLevelFilter
from stpone.logging.formatter import SimpleFormatter
from stpone.logging.logger import error_ctx, get_logger


def _record(level: int) -> logging.LogRecord:
    return logging.LogRecord("x", level, __file__, 1, "hello", None, None)


def test_below_level_filter_passes_records_below_threshold() -> None:
    # frob:tests src/stpone/logging/filter.py::BelowLevelFilter.filter kind="unit"
    # frob:tests src/stpone/logging/filter.py::BelowLevelFilter kind="unit"
    below = BelowLevelFilter(below="WARNING")
    assert below.filter(_record(logging.INFO)) is True
    assert below.filter(_record(logging.WARNING)) is False


def test_simple_formatter_prefixes_level_at_warning_and_above() -> None:
    # frob:tests src/stpone/logging/formatter.py::SimpleFormatter.format kind="unit"
    # frob:tests src/stpone/logging/formatter.py::SimpleFormatter kind="unit"
    fmt = SimpleFormatter()
    assert fmt.format(_record(logging.INFO)) == "hello"
    assert fmt.format(_record(logging.WARNING)) == "WARNING: hello"


def test_get_logger_returns_a_configured_logger() -> None:
    # frob:tests src/stpone/logging/logger.py::get_logger kind="unit"
    log = get_logger(__name__)
    assert isinstance(log, logging.Logger)


def test_error_ctx_logs_and_reraises(caplog: pytest.LogCaptureFixture) -> None:
    # frob:tests src/stpone/logging/logger.py::error_ctx kind="unit"
    log = get_logger("stpone.tests.error_ctx")
    with caplog.at_level(logging.ERROR, logger="stpone.tests.error_ctx"):
        with pytest.raises(ValueError, match="boom"):
            with error_ctx(log, err=ValueError):
                raise ValueError("boom")
    assert "boom" in caplog.text
    with pytest.raises(KeyError):
        with error_ctx(log, err=ValueError):
            raise KeyError("not logged")
