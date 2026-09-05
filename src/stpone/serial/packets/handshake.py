from __future__ import annotations

from typing import final

from typani.singleton import singleton

from stpone.serial.packets.generic import Constant


# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0705
@final
@singleton
class SynPacket(Constant):
    @staticmethod
    def get_value() -> bytes:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0705
        return b"u up?"


# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0705
@final
@singleton
class SynAckPacket(Constant):
    @staticmethod
    def get_value() -> bytes:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0705
        return b"ye; wbu?"


# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0705
@final
@singleton
class AckPacket(Constant):
    @staticmethod
    def get_value() -> bytes:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0705
        return b"lol yeah"
