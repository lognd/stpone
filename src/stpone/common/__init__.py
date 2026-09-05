"""Shared vocabulary: typani's Result/Option family re-exported under one roof."""

from __future__ import annotations

from typani.result import Err, Ok, Result
from typani.unreachable import Unreachable

__all__ = ["Err", "Ok", "Result", "Unreachable"]
