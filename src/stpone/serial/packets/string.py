from __future__ import annotations

from typing import Optional

from pydantic import PrivateAttr, RootModel, model_validator
from typing_extensions import Self

from stpone.serial.packets.generic import FixedWidth, Serializable
from stpone.serial.rdt import RDTCommunication


# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0704
# frob:tests tests/unit/test_serial_packets.py::test_fast_string_round_trip kind="unit"
# frob:tests tests/unit/test_serial_packets.py::test_fast_string_is_64_wide kind="unit"
# frob:waive WIRE001 reason="M2 wire type, no M1 caller by design" follow_up="T-0006"
class FastString(RootModel[str], FixedWidth):
    # follow_up="T-0006"
    @classmethod
    def get_byte_width(cls) -> int:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0704
        return 64

    # follow_up="T-0006"
    @classmethod
    def from_bytes(cls, data: bytes) -> Self:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0704
        return cls.model_validate(data.decode("utf-8", errors="ignore"))

    # follow_up="T-0006"
    def to_bytes(self) -> bytes:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0704
        return self.root.encode("utf-8", errors="ignore")


# kind="unit"
# frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0704
# frob:tests tests/unit/test_serial_packets.py::test_string_is_length_prefixed \
# kind="unit"
class String(RootModel[str], Serializable):
    _byte_cache: Optional[bytes] = PrivateAttr(default=None)

    @property
    def _byte_repr(self) -> bytes:
        if self._byte_cache is None:
            self._byte_cache = self.root.encode(encoding="utf-8", errors="ignore")
        return self._byte_cache

    # follow_up="T-0006"
    @model_validator(mode="after")
    def validate_string_length(self) -> Self:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0704
        if len(self._byte_repr) >= (1 << 32):
            raise ValueError(
                f'String, "{self.root[:10]}...{self.root[-10:]}", is too long '
                f"(`{len(self.root)}` >= 2**32 characters)."
            )
        return self

    # follow_up="T-0006"
    def to_bytes(self) -> bytes:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0704
        return (
            len(self.root).to_bytes(length=4, byteorder="little", signed=False)
            + self._byte_repr
        )

    # follow_up="T-0006"
    @classmethod
    async def from_stream(cls, stream: RDTCommunication) -> Optional[Self]:
        # frob:doc docs/spec/L5-component-design/SUB-07-serial-protocol.md#comp-0704
        res = await stream.read(4)
        if res.is_err:
            stream.error(res.danger_err)
            return None

        length = int.from_bytes(res.danger_ok, byteorder="little", signed=False)
        res = await stream.read(length)
        if res.is_err:
            stream.error(res.danger_err)
            return None

        return cls.model_validate(res.danger_ok.decode("utf-8"))
