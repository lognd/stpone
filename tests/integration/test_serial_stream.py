"""Integration tests: packets through an in-memory RDT transport."""

from __future__ import annotations

import pytest

from stpone.exception import DeveloperException
from stpone.serial.packets.generic import (
    Constant,
    FixedWidth,
    Pydantic,
    Serializable,
)
from stpone.serial.packets.handshake import AckPacket, SynAckPacket, SynPacket
from stpone.serial.packets.integers import U8, U16
from stpone.serial.packets.string import String
from tests.conftest import MemoryStream, Status


class Telemetry(Pydantic):
    count: U16
    state: U8


async def test_handshake_round_trip() -> None:
    # frob:tests src/stpone/serial kind="integration"
    # frob:tests src/stpone/serial/rdt.py::RDTCommunication kind="integration"
    stream = MemoryStream()
    for packet in (SynPacket(), SynAckPacket(), AckPacket()):
        assert (await packet.to_stream(stream)).is_ok
    assert await SynPacket.from_stream(stream) is SynPacket()
    assert await SynAckPacket.from_stream(stream) is SynAckPacket()
    assert await AckPacket.from_stream(stream) is AckPacket()
    assert stream.buffer == b""
    assert stream.errors == []


async def test_fixed_width_and_string_from_stream() -> None:
    # frob:tests src/stpone/serial/packets/generic.py::FixedWidth.from_stream kind="integration"
    # frob:tests src/stpone/serial/packets/string.py::String.from_stream kind="integration"
    stream = MemoryStream(U16(513).to_bytes() + String("hey").to_bytes())
    number = await U16.from_stream(stream)
    text = await String.from_stream(stream)
    assert number is not None and number.root == 513
    assert text is not None and text.root == "hey"
    assert await U16.from_stream(stream) is None
    assert stream.errors == [Status.SHORT_READ]


async def test_constant_mismatch_yields_none() -> None:
    # frob:tests src/stpone/serial/packets/generic.py::Constant.from_stream kind="integration"
    # frob:tests src/stpone/serial/packets/generic.py::Constant kind="integration"
    assert await SynPacket.from_stream(MemoryStream(b"nope!")) is None
    assert await SynPacket.from_stream(MemoryStream(b"")) is None


async def test_composite_packet_round_trip() -> None:
    # frob:tests src/stpone/serial/packets/generic.py::Pydantic kind="integration"
    # frob:tests src/stpone/serial/packets/generic.py::Pydantic.from_stream kind="integration"
    stream = MemoryStream()
    packet = Telemetry(count=U16(4096), state=U8(1))
    assert packet.to_bytes() == b"\x00\x10\x01"
    assert (await packet.to_stream(stream)).is_ok
    decoded = await Telemetry.from_stream(stream)
    assert decoded == packet
    assert await Telemetry.from_stream(stream) is None


async def test_composite_to_stream_and_bytes() -> None:
    # frob:tests src/stpone/serial/packets/generic.py::Pydantic.to_stream kind="integration"
    # frob:tests src/stpone/serial/packets/generic.py::Pydantic.to_bytes kind="integration"
    stream = MemoryStream()
    packet = Telemetry(count=U16(1), state=U8(2))
    assert (await packet.to_stream(stream)).is_ok
    assert bytes(stream.buffer) == packet.to_bytes()


def test_non_serializable_field_is_a_developer_error() -> None:
    # frob:tests src/stpone/exception.py kind="integration"
    # frob:tests src/stpone/exception.py::DeveloperException kind="integration"
    with pytest.raises(DeveloperException):

        class Broken(Pydantic):
            count: int


async def test_serializable_base_contract() -> None:
    # frob:tests src/stpone/serial/packets/generic.py::Serializable.to_stream kind="integration"
    # frob:tests src/stpone/serial/packets/generic.py::Serializable kind="integration"
    stream = MemoryStream()
    assert (await U8(7).to_stream(stream)).is_ok
    assert bytes(U8(7)) == b"\x07"
    assert issubclass(FixedWidth, Serializable) and issubclass(Constant, Serializable)


async def test_fixed_width_contract() -> None:
    # frob:tests src/stpone/serial/packets/generic.py::FixedWidth kind="integration"
    # frob:tests src/stpone/serial/packets/generic.py::FixedWidth.get_byte_width kind="integration"
    assert U8.get_byte_width() == 1
    assert U8.from_bytes(b"\x09").root == 9


async def test_transport_contract_methods() -> None:
    # frob:tests src/stpone/serial/rdt.py::RDTCommunication.read kind="integration"
    # frob:tests src/stpone/serial/rdt.py::RDTCommunication.write kind="integration"
    stream = MemoryStream()
    assert (await stream.write(b"ab")).is_ok
    assert (await stream.read(2)).danger_ok == b"ab"


def test_transport_error_hook() -> None:
    # frob:tests src/stpone/serial/rdt.py::RDTCommunication.error kind="integration"
    # frob:tests src/stpone/serial/rdt.py::RDTStatus kind="integration"
    stream = MemoryStream()
    stream.error(Status.CLOSED)
    assert stream.errors == [Status.CLOSED]
