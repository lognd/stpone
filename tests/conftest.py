"""Shared fixtures for every test level."""

from __future__ import annotations

import subprocess
from collections.abc import Sequence
from enum import IntEnum
from pathlib import Path

import pytest
from typani.result import Err, Ok, Result

from stpone.serial.rdt import RDTCommunication


class FakeRunner:
    """Records argv calls and replays scripted exit codes (default: success)."""

    def __init__(self, returncodes: Sequence[int] = ()) -> None:
        self.calls: list[list[str]] = []
        self._codes = list(returncodes)

    def __call__(
        self,
        argv: Sequence[str],
        *,
        cwd: Path | None = None,
        capture: bool = True,
    ) -> subprocess.CompletedProcess[str]:
        self.calls.append(list(argv))
        code = self._codes.pop(0) if self._codes else 0
        return subprocess.CompletedProcess(list(argv), code, stdout="", stderr="")


class Status(IntEnum):
    """Transport status codes for the in-memory stream."""

    SHORT_READ = 1
    CLOSED = 2


class MemoryStream(RDTCommunication[Status]):
    """A byte pipe: writes append, reads consume; short reads are errors."""

    def __init__(self, data: bytes = b"") -> None:
        self.buffer = bytearray(data)
        self.errors: list[Status] = []

    async def read(self, length: int) -> Result[bytes, Status]:
        if len(self.buffer) < length:
            return Err(Status.SHORT_READ)
        chunk = bytes(self.buffer[:length])
        del self.buffer[:length]
        return Ok(chunk)

    async def write(self, data: bytes) -> Result[None, Status]:
        self.buffer.extend(data)
        return Ok(None)

    def error(self, code: Status) -> None:
        self.errors.append(code)


@pytest.fixture
def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]
