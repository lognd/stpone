from __future__ import annotations

from typing import ClassVar, final

from pydantic import RootModel, field_validator
from typing_extensions import Self

from stpone.serial.packets.generic import FixedWidth


class _IntRoot(RootModel[int], FixedWidth):
    _byte_width: ClassVar[int]
    _signed: ClassVar[bool]

    @classmethod
    def get_byte_width(cls) -> int:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0702
        return cls._byte_width

    @classmethod
    def min_value(cls) -> int:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0702
        bits = cls._byte_width * 8
        return -(1 << (bits - 1)) if cls._signed else 0

    @classmethod
    def max_value_exclusive(cls) -> int:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0702
        bits = cls._byte_width * 8
        return 1 << (bits - 1) if cls._signed else 1 << bits

    @field_validator("root")
    @classmethod
    def validate_range(cls, value: int) -> int:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0702
        if not (cls.min_value() <= value < cls.max_value_exclusive()):
            raise ValueError(
                f"{value} is outside {cls.__name__} range "
                f"[{cls.min_value()}, {cls.max_value_exclusive()})"
            )

        return value

    def to_bytes(self) -> bytes:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0702
        return self.root.to_bytes(
            length=type(self)._byte_width,
            byteorder="little",
            signed=type(self)._signed,
        )

    @classmethod
    def from_bytes(cls, data: bytes) -> Self:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0702
        if len(data) != cls._byte_width:
            raise ValueError(
                f"{cls.__name__} requires exactly {cls._byte_width} bytes; "
                f"got {len(data)}"
            )

        return cls.model_validate(
            int.from_bytes(data, byteorder="little", signed=cls._signed)
        )


# kind="unit"
# frob:waive WIRE001 reason="M2 wire type, no M1 caller by design" follow_up="T-0006"
# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0702
# frob:tests tests/unit/test_serial_packets.py::test_integer_round_trip_and_range \
# kind="unit"
@final
class U8(_IntRoot):
    _byte_width: ClassVar[int] = 1
    _signed: ClassVar[bool] = False


# kind="unit"
# frob:waive WIRE001 reason="M2 wire type, no M1 caller by design" follow_up="T-0006"
# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0702
# frob:tests tests/unit/test_serial_packets.py::test_bool_accepts_only_zero_and_one \
# kind="unit"
@final
class Bool(_IntRoot):
    # frob:waive WIRE001 reason="M2 wire type, no M1 caller by design" \
    # follow_up="T-0006"
    @field_validator("root")
    @classmethod
    def validate_range(cls, value: int) -> int:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0702
        if value != 0 and value != 1:
            raise ValueError(
                f"{value} is outside {cls.__name__} range [0, 1] ~ {{true, false}}."
            )
        return value

    _byte_width: ClassVar[int] = 1
    _signed: ClassVar[bool] = False


# frob:waive WIRE001 reason="M2 wire type, no M1 caller by design" follow_up="T-0006"
# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0702
# frob:tests tests/unit/test_serial_packets.py::test_wide_integer_types_are_little_endian kind="unit"  # noqa: E501
@final
class U16(_IntRoot):
    _byte_width: ClassVar[int] = 2
    _signed: ClassVar[bool] = False


# frob:waive WIRE001 reason="M2 wire type, no M1 caller by design" follow_up="T-0006"
# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0702
# frob:tests tests/unit/test_serial_packets.py::test_wide_integer_types_are_little_endian kind="unit"  # noqa: E501
@final
class U32(_IntRoot):
    _byte_width: ClassVar[int] = 4
    _signed: ClassVar[bool] = False


# frob:waive WIRE001 reason="M2 wire type, no M1 caller by design" follow_up="T-0006"
# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0702
# frob:tests tests/unit/test_serial_packets.py::test_signed_and_64_bit_types kind="unit"
@final
class U64(_IntRoot):
    _byte_width: ClassVar[int] = 8
    _signed: ClassVar[bool] = False


# frob:waive WIRE001 reason="M2 wire type, no M1 caller by design" follow_up="T-0006"
# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0702
# frob:tests tests/unit/test_serial_packets.py::test_signed_and_64_bit_types kind="unit"
@final
class I8(_IntRoot):
    _byte_width: ClassVar[int] = 1
    _signed: ClassVar[bool] = True


# kind="unit"
# frob:waive WIRE001 reason="M2 wire type, no M1 caller by design" follow_up="T-0006"
# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0702
# frob:tests tests/unit/test_serial_packets.py::test_integer_round_trip_and_range \
# kind="unit"
@final
class I16(_IntRoot):
    _byte_width: ClassVar[int] = 2
    _signed: ClassVar[bool] = True


# frob:waive WIRE001 reason="M2 wire type, no M1 caller by design" follow_up="T-0006"
# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0702
# frob:tests tests/unit/test_serial_packets.py::test_remaining_signed_types kind="unit"
@final
class I32(_IntRoot):
    _byte_width: ClassVar[int] = 4
    _signed: ClassVar[bool] = True


# frob:waive WIRE001 reason="M2 wire type, no M1 caller by design" follow_up="T-0006"
# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0702
# frob:tests tests/unit/test_serial_packets.py::test_remaining_signed_types kind="unit"
@final
class I64(_IntRoot):
    _byte_width: ClassVar[int] = 8
    _signed: ClassVar[bool] = True
