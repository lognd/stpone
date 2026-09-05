"""Unit tests for the Serializable hierarchy and the transport contract."""

from __future__ import annotations

from stpone.serial.packets.handshake import SynPacket
from stpone.serial.packets.integers import U8, U16
from tests.conftest import MemoryStream, Status


def test_constant_bytes() -> None:
    # frob:tests src/stpone/serial/packets/generic.py::Constant.get_value kind="unit"
    # frob:tests src/stpone/serial/packets/generic.py::Constant.to_bytes kind="unit"
    assert SynPacket.get_value() == b"u up?"
    assert SynPacket().to_bytes() == b"u up?"


def test_fixed_width_bytes() -> None:
    # frob:tests src/stpone/serial/packets/generic.py::FixedWidth.from_bytes kind="unit"
    # frob:tests src/stpone/serial/packets/generic.py::FixedWidth.get_byte_width kind="unit"
    assert U16.get_byte_width() == 2
    assert U16.from_bytes(b"\x01\x02").root == 0x0201


def test_serializable_bytes_protocol() -> None:
    # frob:tests src/stpone/serial/packets/generic.py::Serializable.to_bytes kind="unit"
    # frob:tests src/stpone/serial/packets/generic.py::Pydantic.to_bytes kind="unit"
    from tests.integration.test_serial_stream import Telemetry

    assert bytes(U8(5)) == b"\x05"
    assert Telemetry(count=U16(2), state=U8(3)).to_bytes() == b"\x02\x00\x03"


async def test_stream_methods_unit() -> None:
    # frob:tests src/stpone/serial/packets/generic.py::Serializable.to_stream kind="unit"
    # frob:tests src/stpone/serial/packets/generic.py::FixedWidth.from_stream kind="unit"
    stream = MemoryStream()
    assert (await U8(9).to_stream(stream)).is_ok
    decoded = await U8.from_stream(stream)
    assert decoded is not None and decoded.root == 9


async def test_constant_and_pydantic_streams_unit() -> None:
    # frob:tests src/stpone/serial/packets/generic.py::Constant.from_stream kind="unit"
    # frob:tests src/stpone/serial/packets/generic.py::Pydantic.from_stream kind="unit"
    from tests.integration.test_serial_stream import Telemetry

    stream = MemoryStream(b"u up?" + b"\x02\x00\x03")
    assert await SynPacket.from_stream(stream) is SynPacket()
    packet = await Telemetry.from_stream(stream)
    assert packet is not None and packet.count.root == 2


async def test_pydantic_to_stream_unit() -> None:
    # frob:tests src/stpone/serial/packets/generic.py::Pydantic.to_stream kind="unit"
    # frob:tests src/stpone/serial/packets/generic.py::Pydantic kind="unit"
    from tests.integration.test_serial_stream import Telemetry

    stream = MemoryStream()
    assert (await Telemetry(count=U16(1), state=U8(1)).to_stream(stream)).is_ok
    assert bytes(stream.buffer) == b"\x01\x00\x01"


async def test_transport_contract_unit() -> None:
    # frob:tests src/stpone/serial/rdt.py::RDTCommunication.read kind="unit"
    # frob:tests src/stpone/serial/rdt.py::RDTCommunication.write kind="unit"
    stream = MemoryStream()
    assert (await stream.write(b"xy")).is_ok
    assert (await stream.read(2)).danger_ok == b"xy"
    assert (await stream.read(1)).danger_err is Status.SHORT_READ


def test_transport_error_and_status_unit() -> None:
    # frob:tests src/stpone/serial/rdt.py::RDTCommunication.error kind="unit"
    # frob:tests src/stpone/serial/rdt.py::RDTCommunication kind="unit"
    stream = MemoryStream()
    stream.error(Status.CLOSED)
    assert stream.errors == [Status.CLOSED]


def test_base_classes_unit() -> None:
    # frob:tests src/stpone/serial/packets/generic.py::Serializable kind="unit"
    # frob:tests src/stpone/serial/packets/generic.py::FixedWidth kind="unit"
    from stpone.serial.packets.generic import Constant, FixedWidth, Serializable

    assert issubclass(FixedWidth, Serializable)
    assert issubclass(Constant, Serializable)


def test_constant_base_unit() -> None:
    # frob:tests src/stpone/serial/packets/generic.py::Constant kind="unit"
    # frob:tests src/stpone/serial/rdt.py::RDTStatus kind="unit"
    assert isinstance(SynPacket(), SynPacket)
    assert Status.CLOSED.value == 2
