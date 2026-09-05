from __future__ import annotations

from abc import ABC, abstractmethod
from enum import IntEnum
from typing import Any, Optional, cast

from pydantic import BaseModel
from typani.result import Ok, Result
from typing_extensions import Self

from stpone.exception import DeveloperException
from stpone.logging import get_logger
from stpone.serial.rdt import RDTCommunication

# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
LOGGER = get_logger("serial")


# frob:waive WIRE001 reason="M2 protocol base, no M1 caller by design" \
# follow_up="T-0006"
# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
# frob:tests tests/unit/test_serial_generic.py::test_base_classes_unit kind="unit"
# frob:tests tests/integration/test_serial_stream.py::test_serializable_base_contract kind="integration"  # noqa: E501
class Serializable(ABC):
    # frob:waive WIRE001 reason="M2 protocol base, no M1 caller by design" \
    # follow_up="T-0006"
    @abstractmethod
    # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
    def to_bytes(self) -> bytes: ...

    def __bytes__(self) -> bytes:
        return self.to_bytes()

    # frob:waive WIRE001 reason="M2 protocol base, no M1 caller by design" \
    # follow_up="T-0006"
    async def to_stream(self, stream: RDTCommunication) -> Result[None, IntEnum]:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
        status = await stream.write(self.to_bytes())
        if status.is_err:
            LOGGER.error("write failed: %s", status.danger_err)
            stream.error(status.danger_err)
        return status

    # frob:waive WIRE001 reason="M2 protocol base, no M1 caller by design" \
    # follow_up="T-0006"
    @classmethod
    @abstractmethod
    # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
    async def from_stream(cls, stream: RDTCommunication) -> Optional[Self]: ...


# kind="integration"
# frob:waive WIRE001 reason="M2 protocol base, no M1 caller by design" \
# follow_up="T-0006"
# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
# frob:tests tests/unit/test_serial_generic.py::test_base_classes_unit kind="unit"
# frob:tests tests/integration/test_serial_stream.py::test_fixed_width_contract \
# kind="integration"
class FixedWidth(Serializable, ABC):
    # frob:waive WIRE001 reason="M2 protocol base, no M1 caller by design" \
    # follow_up="T-0006"
    @classmethod
    @abstractmethod
    # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
    def get_byte_width(cls) -> int: ...

    # frob:waive WIRE001 reason="M2 protocol base, no M1 caller by design" \
    # follow_up="T-0006"
    @classmethod
    @abstractmethod
    # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
    def from_bytes(cls, data: bytes) -> Self: ...

    # frob:waive WIRE001 reason="M2 protocol base, no M1 caller by design" \
    # follow_up="T-0006"
    @classmethod
    async def from_stream(cls, stream: RDTCommunication) -> Optional[Self]:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
        res = await stream.read(cls.get_byte_width())
        if res.is_err:
            LOGGER.error("%s: read failed: %s", cls.__name__, res.danger_err)
            stream.error(res.danger_err)
            return None
        return cls.from_bytes(res.danger_ok)


# frob:waive WIRE001 reason="M2 protocol base, no M1 caller by design" \
# follow_up="T-0006"
# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
# frob:tests tests/unit/test_serial_generic.py::test_constant_base_unit kind="unit"
# frob:tests tests/integration/test_serial_stream.py::test_constant_mismatch_yields_none kind="integration"  # noqa: E501
class Constant(Serializable, ABC):
    # frob:waive WIRE001 reason="M2 protocol base, no M1 caller by design" \
    # follow_up="T-0006"
    @staticmethod
    @abstractmethod
    # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
    def get_value() -> bytes: ...

    # frob:waive WIRE001 reason="M2 protocol base, no M1 caller by design" \
    # follow_up="T-0006"
    def to_bytes(self) -> bytes:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
        return self.get_value()

    # frob:waive WIRE001 reason="M2 protocol base, no M1 caller by design" \
    # follow_up="T-0006"
    @classmethod
    async def from_stream(cls, stream: RDTCommunication) -> Optional[Self]:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
        res = await stream.read(len(cls.get_value()))
        if res.is_err:
            LOGGER.error("%s: read failed: %s", cls.__name__, res.danger_err)
            stream.error(res.danger_err)
            return None

        data = res.danger_ok
        if data != cls.get_value():
            LOGGER.error(
                "%s: received unexpected bytes %r; expected %r",
                cls.__name__,
                data,
                cls.get_value(),
            )
            return None
        return cls()


# frob:waive WIRE001 reason="M2 protocol base, no M1 caller by design" \
# follow_up="T-0006"
# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
# frob:tests tests/unit/test_serial_generic.py::test_pydantic_to_stream_unit kind="unit"
# frob:tests tests/integration/test_serial_stream.py::test_composite_packet_round_trip kind="integration"  # noqa: E501
class Pydantic(BaseModel, Serializable):
    @classmethod
    def __pydantic_init_subclass__(cls, **kwargs: Any) -> None:
        super().__pydantic_init_subclass__(**kwargs)
        for name, field in cls.model_fields.items():
            if field.annotation is None:
                raise DeveloperException(
                    f"{cls.__name__}.{name} has untyped field, `{field}`."
                )
            if not (
                isinstance(field.annotation, type)
                and issubclass(field.annotation, Serializable)
            ):
                raise DeveloperException(
                    f"{cls.__name__}.{name} has field, `{field}`, "
                    "non-serializable type "
                    f"{field.annotation!r}"
                )

    # frob:waive WIRE001 reason="M2 protocol base, no M1 caller by design" \
    # follow_up="T-0006"
    def to_bytes(self) -> bytes:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
        buf = bytearray()
        for _name, value in self:  # pydantic yields fields in declaration order
            buf.extend(value.to_bytes())
        return bytes(buf)

    # frob:waive WIRE001 reason="M2 protocol base, no M1 caller by design" \
    # follow_up="T-0006"
    async def to_stream(self, stream: RDTCommunication) -> Result[None, IntEnum]:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
        for _name, value in self:
            status = await value.to_stream(stream)
            if status.is_err:
                return status
        return Ok(None)

    # frob:waive WIRE001 reason="M2 protocol base, no M1 caller by design" \
    # follow_up="T-0006"
    @classmethod
    async def from_stream(cls, stream: RDTCommunication) -> Optional[Self]:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
        model: dict[str, Serializable] = {}
        for name, field in cls.model_fields.items():
            kind = cast(type[Serializable], field.annotation)
            comp = await kind.from_stream(stream)
            if comp is None:
                LOGGER.error("%s: field %s did not decode", cls.__name__, name)
                return None
            model[name] = comp
        return cls.model_validate(model)
