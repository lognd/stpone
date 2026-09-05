"""Unit tests for the fixed-width serial packet types."""

from __future__ import annotations

import pytest
from pydantic import ValidationError

from stpone.serial.packets.floats import F16, F32, F64
from stpone.serial.packets.handshake import AckPacket, SynAckPacket, SynPacket
from stpone.serial.packets.integers import (
    I8,
    I16,
    I32,
    I64,
    U8,
    U16,
    U32,
    U64,
    Bool,
)
from stpone.serial.packets.string import FastString, String

_INT_TYPES = (U8, U16, U32, U64, I8, I16, I32, I64)


@pytest.mark.parametrize("kind", _INT_TYPES)
def test_every_integer_type_round_trips(kind: type[U8]) -> None:
    # frob:tests src/stpone/serial/packets/integers.py::_IntRoot.to_bytes kind="unit"
    # frob:tests src/stpone/serial/packets/integers.py::_IntRoot.from_bytes kind="unit"
    low, high = kind.min_value(), kind.max_value_exclusive() - 1
    for value in (low, 0, high):
        packed = kind(value).to_bytes()
        assert len(packed) == kind.get_byte_width()
        assert kind.from_bytes(packed).root == value
    with pytest.raises(ValidationError):
        kind(high + 1)
    with pytest.raises(ValidationError):
        kind(low - 1)


def test_integer_bounds_and_width() -> None:
    # frob:tests src/stpone/serial/packets/integers.py::_IntRoot.min_value kind="unit"
    # frob:tests src/stpone/serial/packets/integers.py::_IntRoot.max_value_exclusive \
    # kind="unit"
    # frob:tests src/stpone/serial/packets/integers.py::_IntRoot.get_byte_width \
    # kind="unit"
    # frob:tests src/stpone/serial/packets/integers.py::_IntRoot.validate_range \
    # kind="unit"
    assert (U8.min_value(), U8.max_value_exclusive()) == (0, 256)
    assert (I8.min_value(), I8.max_value_exclusive()) == (-128, 128)
    assert U32.get_byte_width() == 4


def test_integer_round_trip_and_range() -> None:
    # frob:tests src/stpone/serial/packets/integers.py::U8 kind="unit"
    # frob:tests src/stpone/serial/packets/integers.py::I16 kind="unit"
    assert U8(200).to_bytes() == b"\xc8"
    assert I16(-2).to_bytes() == b"\xfe\xff"
    with pytest.raises(ValueError, match="requires exactly"):
        U8.from_bytes(b"\x00\x00")


def test_wide_integer_types_are_little_endian() -> None:
    # frob:tests src/stpone/serial/packets/integers.py::U16 kind="unit"
    # frob:tests src/stpone/serial/packets/integers.py::U32 kind="unit"
    assert U16(0x1234).to_bytes() == b"\x34\x12"
    assert U32(0x12345678).to_bytes() == b"\x78\x56\x34\x12"


def test_signed_and_64_bit_types() -> None:
    # frob:tests src/stpone/serial/packets/integers.py::I8 kind="unit"
    # frob:tests src/stpone/serial/packets/integers.py::U64 kind="unit"
    assert I8(-1).to_bytes() == b"\xff"
    assert U64(1).to_bytes() == b"\x01" + b"\x00" * 7


def test_remaining_signed_types() -> None:
    # frob:tests src/stpone/serial/packets/integers.py::I32 kind="unit"
    # frob:tests src/stpone/serial/packets/integers.py::I64 kind="unit"
    assert I32(-1).to_bytes() == b"\xff" * 4
    assert I64.from_bytes(b"\xff" * 8).root == -1


def test_bool_accepts_only_zero_and_one() -> None:
    # frob:tests src/stpone/serial/packets/integers.py::Bool kind="unit"
    # frob:tests src/stpone/serial/packets/integers.py::Bool.validate_range kind="unit"
    assert Bool(1).to_bytes() == b"\x01"
    with pytest.raises(ValidationError):
        Bool(2)


@pytest.mark.parametrize("kind", (F16, F32, F64))
def test_every_float_type_round_trips(kind: type[F32]) -> None:
    # frob:tests src/stpone/serial/packets/floats.py::_StructFloat.to_bytes kind="unit"
    # frob:tests src/stpone/serial/packets/floats.py::_StructFloat.from_bytes \
    # kind="unit"
    packed = kind(1.5).to_bytes()
    assert len(packed) == kind.get_byte_width()
    assert kind.from_bytes(packed).root == 1.5
    with pytest.raises(ValidationError):
        kind(float("nan"))
    with pytest.raises(ValidationError):
        kind(float("inf"))
    with pytest.raises(ValueError, match="requires exactly"):
        kind.from_bytes(b"")


def test_float_widths() -> None:
    # frob:tests src/stpone/serial/packets/floats.py::_StructFloat.get_byte_width \
    # kind="unit"
    # frob:tests src/stpone/serial/packets/floats.py::F16 kind="unit"
    assert (F16.get_byte_width(), F32.get_byte_width(), F64.get_byte_width()) == (
        2,
        4,
        8,
    )


def test_float_overflow_and_bool_rejected() -> None:
    # frob:tests src/stpone/serial/packets/floats.py::F32 kind="unit"
    # frob:tests src/stpone/serial/packets/floats.py::F64 kind="unit"
    with pytest.raises(ValidationError):
        F32(1e40)
    with pytest.raises(ValidationError):
        F64(True)
    assert F64(2.0).to_bytes() == b"\x00\x00\x00\x00\x00\x00\x00\x40"


def test_string_is_length_prefixed() -> None:
    # frob:tests src/stpone/serial/packets/string.py::String kind="unit"
    # frob:tests src/stpone/serial/packets/string.py::String.to_bytes kind="unit"
    assert String("hi").to_bytes() == b"\x02\x00\x00\x00hi"
    assert bytes(String("")) == b"\x00\x00\x00\x00"


def test_string_length_validator_accepts_normal_text() -> None:
    # frob:tests src/stpone/serial/packets/string.py::String.validate_string_length \
    # kind="unit"
    assert String("x" * 1000).root == "x" * 1000


def test_fast_string_is_64_wide() -> None:
    # frob:tests src/stpone/serial/packets/string.py::FastString kind="unit"
    # frob:tests src/stpone/serial/packets/string.py::FastString.get_byte_width \
    # kind="unit"
    assert FastString.get_byte_width() == 64
    assert FastString("abc").to_bytes() == b"abc"


def test_fast_string_round_trip() -> None:
    # frob:tests src/stpone/serial/packets/string.py::FastString.to_bytes kind="unit"
    # frob:tests src/stpone/serial/packets/string.py::FastString.from_bytes kind="unit"
    assert FastString.from_bytes(b"abc").root == "abc"


def test_handshake_constants_are_singletons() -> None:
    # frob:tests src/stpone/serial/packets/handshake.py::SynPacket kind="unit"
    # frob:tests src/stpone/serial/packets/handshake.py::SynAckPacket kind="unit"
    assert SynPacket() is SynPacket()
    assert bytes(SynPacket()) == b"u up?"
    assert bytes(SynAckPacket()) == b"ye; wbu?"


def test_handshake_values() -> None:
    # frob:tests src/stpone/serial/packets/handshake.py::AckPacket kind="unit"
    # frob:tests src/stpone/serial/packets/handshake.py::AckPacket.get_value kind="unit"
    assert bytes(AckPacket()) == b"lol yeah"
    assert AckPacket.get_value() == b"lol yeah"


def test_syn_values() -> None:
    # frob:tests src/stpone/serial/packets/handshake.py::SynPacket.get_value kind="unit"
    # frob:tests src/stpone/serial/packets/handshake.py::SynAckPacket.get_value \
    # kind="unit"
    assert SynPacket.get_value() == b"u up?"
    assert SynAckPacket.get_value() == b"ye; wbu?"
