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


# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
class Serializable(ABC):
    @abstractmethod
                                 # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
    def to_bytes(self) -> bytes: ...

    def __bytes__(self) -> bytes:
        return self.to_bytes()

    async def to_stream(self, stream: RDTCommunication) -> Result[None, IntEnum]:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
        status = await stream.write(self.to_bytes())
        if status.is_err:
            LOGGER.error("write failed: %s", status.danger_err)
            stream.error(status.danger_err)
        return status

    @classmethod
    @abstractmethod
                                                                            # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
    async def from_stream(cls, stream: RDTCommunication) -> Optional[Self]: ...


# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
class FixedWidth(Serializable, ABC):
    @classmethod
    @abstractmethod
                                    # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
    def get_byte_width(cls) -> int: ...

    @classmethod
    @abstractmethod
                                              # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
    def from_bytes(cls, data: bytes) -> Self: ...

    @classmethod
    async def from_stream(cls, stream: RDTCommunication) -> Optional[Self]:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
        res = await stream.read(cls.get_byte_width())
        if res.is_err:
            LOGGER.error("%s: read failed: %s", cls.__name__, res.danger_err)
            stream.error(res.danger_err)
            return None
        return cls.from_bytes(res.danger_ok)


# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
class Constant(Serializable, ABC):
    @staticmethod
    @abstractmethod
                              # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
    def get_value() -> bytes: ...

    def to_bytes(self) -> bytes:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
        return self.get_value()

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


# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
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

    def to_bytes(self) -> bytes:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
        buf = bytearray()
        for attr in type(self).model_fields:
            buf.extend(getattr(self, attr).to_bytes())
        return bytes(buf)

    async def to_stream(self, stream: RDTCommunication) -> Result[None, IntEnum]:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0701
        for attr in type(self).model_fields:
            status = await getattr(self, attr).to_stream(stream)
            if status.is_err:
                return status
        return Ok(None)

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
