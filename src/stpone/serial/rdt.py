from __future__ import annotations

# frob:waive WIRE001 reason="M2 transport contract with no M1 implementation by design (SUB-07 carry-over)" follow_up="T-0006"
from abc import ABC, abstractmethod
from enum import IntEnum
from typing import Generic, TypeVar

from stpone.common import Result

# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0706
RDTStatus = TypeVar("RDTStatus", bound=IntEnum)


# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0706
class RDTCommunication(Generic[RDTStatus], ABC):
    @abstractmethod
    # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0706
    async def read(self, length: int) -> Result[bytes, RDTStatus]: ...

    @abstractmethod
    # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0706
    async def write(self, data: bytes) -> Result[None, RDTStatus]: ...

    @abstractmethod
    # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0706
    def error(self, code: RDTStatus) -> None: ...
