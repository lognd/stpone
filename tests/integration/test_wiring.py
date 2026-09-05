"""Integration tests: the desktop-core packages wire together."""

from __future__ import annotations

import logging

import pytest
from typani.result import Ok

import stpone
from stpone.exception import DeveloperException
from stpone.logging import error_ctx, get_logger


def test_logging_and_exception_wiring(caplog: pytest.LogCaptureFixture) -> None:
    # frob:tests src/stpone/logging kind="integration"
    # frob:tests src/stpone/exception.py kind="integration"
    log = get_logger("stpone.tests.wiring")
    with caplog.at_level(logging.ERROR, logger="stpone.tests.wiring"):
        with pytest.raises(DeveloperException):
            with error_ctx(log, err=DeveloperException):
                raise DeveloperException("bug")
    assert "bug" in caplog.text
    assert issubclass(DeveloperException, Exception)


def test_package_exports_typani_vocabulary() -> None:
    # frob:tests src/stpone/__init__.py kind="integration"
    # frob:tests src/stpone/common kind="integration"
    assert stpone.Ok is Ok
    assert stpone.Ok(1).is_ok
    assert stpone.SynPacket() is stpone.SynPacket()


def test_flash_package_wires() -> None:
    # frob:tests src/stpone/flash kind="integration"
    from stpone.flash import FlashApp, FlashConfig, FlashError

    assert FlashApp(FlashConfig(build=False, build_only=True))().is_ok
    assert FlashError.HexMissing.description
