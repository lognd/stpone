from __future__ import annotations

from typing import final

from typani.singleton import singleton

from stpone.serial.packets.generic import Constant


# tests/unit/test_serial_packets.py::test_handshake_constants_are_singletons kind="unit"
# follow_up="T-0006"
# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0705
# frob:tests tests/unit/test_serial_packets.py::test_syn_values kind="unit"
# frob:tests \
# tests/unit/test_serial_packets.py::test_handshake_constants_are_singletons kind="unit"
# frob:waive WIRE001 reason="M2 handshake packet, no M1 caller by design" \
# follow_up="T-0006"
@final
@singleton
class SynPacket(Constant):
    # follow_up="T-0006"
    @staticmethod
    def get_value() -> bytes:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0705
        return b"u up?"


# tests/unit/test_serial_packets.py::test_handshake_constants_are_singletons kind="unit"
# follow_up="T-0006"
# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0705
# frob:tests \
# tests/unit/test_serial_packets.py::test_handshake_constants_are_singletons kind="unit"
# frob:waive WIRE001 reason="M2 handshake packet, no M1 caller by design" \
# follow_up="T-0006"
@final
@singleton
class SynAckPacket(Constant):
    # follow_up="T-0006"
    @staticmethod
    def get_value() -> bytes:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0705
        return b"ye; wbu?"


# follow_up="T-0006"
# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0705
# frob:tests tests/unit/test_serial_packets.py::test_handshake_values kind="unit"
# frob:waive WIRE001 reason="M2 handshake packet, no M1 caller by design" \
# follow_up="T-0006"
@final
@singleton
class AckPacket(Constant):
    # follow_up="T-0006"
    @staticmethod
    def get_value() -> bytes:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0705
        return b"lol yeah"
