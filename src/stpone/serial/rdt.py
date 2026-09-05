from __future__ import annotations

from abc import ABC, abstractmethod
from enum import IntEnum
from typing import Generic, TypeVar

from stpone.common import Result

# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0706
RDTStatus = TypeVar("RDTStatus", bound=IntEnum)


# kind="unit"
# kind="integration"
# follow_up="T-0006"
# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0706
# frob:tests tests/unit/test_serial_generic.py::test_transport_error_and_status_unit \
# kind="unit"
# frob:tests tests/integration/test_serial_stream.py::test_handshake_round_trip \
# kind="integration"
# frob:waive WIRE001 reason="M2 transport contract, no M1 implementation" \
# follow_up="T-0006"
class RDTCommunication(Generic[RDTStatus], ABC):
    # follow_up="T-0006"
    @abstractmethod
    # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0706
    async def read(self, length: int) -> Result[bytes, RDTStatus]: ...

    # follow_up="T-0006"
    @abstractmethod
    # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0706
    async def write(self, data: bytes) -> Result[None, RDTStatus]: ...

    # follow_up="T-0006"
    @abstractmethod
    # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0706
    def error(self, code: RDTStatus) -> None: ...
