from __future__ import annotations

import math
import struct
from abc import ABC
from typing import ClassVar, final

from pydantic import RootModel, field_validator
from typing_extensions import Self

from stpone.serial.packets.generic import FixedWidth


class _FloatRoot(RootModel[float], FixedWidth, ABC):
    """
    Base class for finite fixed-width float wrappers.

    Stores a Python float, validates that it is finite, and lets subclasses
    define byte encoding/decoding.
    """

    @field_validator("root", mode="before")
    @classmethod
    def _validate_float(cls, value: object) -> float:
        if isinstance(value, bool):
            raise ValueError("`bool` is not a valid floating-point value")

        try:
            value = float(value)  # type: ignore
        except Exception as exc:
            raise ValueError(f"Expected float-compatible value, got {value!r}") from exc

        if not math.isfinite(value):
            raise ValueError("Floating-point value must be finite")

        return value


class _StructFloat(_FloatRoot):
    _struct_format: ClassVar[str]
    _byte_width: ClassVar[int]

    @classmethod
    def get_byte_width(cls) -> int:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0703
        return cls._byte_width

    @field_validator("root")
    @classmethod
    def _validate_encodable(cls, value: float) -> float:
        try:
            struct.pack(cls._struct_format, value)
        except OverflowError as exc:
            raise ValueError(
                f"{value!r} is not representable as {cls.__name__}"
            ) from exc

        return value

    def to_bytes(self) -> bytes:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0703
        return struct.pack(self._struct_format, self.root)

    @classmethod
    def from_bytes(cls, data: bytes) -> Self:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0703
        if len(data) != cls.get_byte_width():
            raise ValueError(
                f"{cls.__name__} requires exactly {cls.get_byte_width()} bytes"
            )

        return cls.model_validate(struct.unpack(cls._struct_format, data)[0])


# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0703
@final
class F16(_StructFloat):
    _struct_format: ClassVar[str] = "<e"
    _byte_width: ClassVar[int] = 2


# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0703
@final
class F32(_StructFloat):
    _struct_format: ClassVar[str] = "<f"
    _byte_width: ClassVar[int] = 4


# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0703
@final
class F64(_StructFloat):
    _struct_format: ClassVar[str] = "<d"
    _byte_width: ClassVar[int] = 8
