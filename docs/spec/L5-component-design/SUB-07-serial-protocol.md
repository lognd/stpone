# L5 -- SUB-07 serial-protocol

Satisfies SUB-07. Owned code: `src/stpone/serial/**`. Carried over from
`stp-upgrade` for M1 with the bug fixes listed under comp-0701; the
concrete transport and the monitor application are M2 design work.

## Components

| ID | Component | Path | Responsibility | satisfies | verified-by | Milestone |
|---|---|---|---|---|---|---|
| COMP-0701 | serializable hierarchy | src/stpone/serial/packets/generic.py | `Serializable` (to_bytes/to_stream/from_stream), `FixedWidth` (byte width + from_bytes), `Constant` (fixed byte string), `Pydantic` (composite model, fields written in order). | SUB-07, SYS-020 | UT-0701 | M1 |
| COMP-0702 | integers | src/stpone/serial/packets/integers.py | `U8..U64`, `I8..I64`, `Bool`: range-validated little-endian ints. | SUB-07, SPEC-030 | UT-0702 | M1 |
| COMP-0703 | floats | src/stpone/serial/packets/floats.py | `F16`, `F32`, `F64`: finite, struct-packed little-endian. | SUB-07, SPEC-030 | UT-0703 | M1 |
| COMP-0704 | strings | src/stpone/serial/packets/string.py | `String` (u32 length prefix + UTF-8), `FastString` (64 bytes). | SUB-07, SPEC-030 | UT-0704 | M1 |
| COMP-0705 | handshake | src/stpone/serial/packets/handshake.py | `SynPacket`, `SynAckPacket`, `AckPacket` singletons with the exact bytes. | SUB-07, SPEC-030 | UT-0705 | M1 |
| COMP-0706 | transport contract | src/stpone/serial/rdt.py | `RDTCommunication[Status]`: `read`, `write` -> `Result`, `error(code)`. | SUB-07, SYS-021 | UT-0706 | M1 |

### comp-0701
<!-- frob:describes src/stpone/serial/packets/generic.py::LOGGER -->
<!-- frob:describes src/stpone/serial/packets/generic.py::Serializable -->
<!-- frob:describes src/stpone/serial/packets/generic.py::Serializable.to_bytes -->
<!-- frob:describes src/stpone/serial/packets/generic.py::Serializable.to_stream -->
<!-- frob:describes src/stpone/serial/packets/generic.py::Serializable.from_stream -->
<!-- frob:describes src/stpone/serial/packets/generic.py::FixedWidth -->
<!-- frob:describes src/stpone/serial/packets/generic.py::FixedWidth.get_byte_width -->
<!-- frob:describes src/stpone/serial/packets/generic.py::FixedWidth.from_bytes -->
<!-- frob:describes src/stpone/serial/packets/generic.py::FixedWidth.from_stream -->
<!-- frob:describes src/stpone/serial/packets/generic.py::Constant -->
<!-- frob:describes src/stpone/serial/packets/generic.py::Constant.get_value -->
<!-- frob:describes src/stpone/serial/packets/generic.py::Constant.to_bytes -->
<!-- frob:describes src/stpone/serial/packets/generic.py::Constant.from_stream -->
<!-- frob:describes src/stpone/serial/packets/generic.py::Pydantic -->
<!-- frob:describes src/stpone/serial/packets/generic.py::Pydantic.to_bytes -->
<!-- frob:describes src/stpone/serial/packets/generic.py::Pydantic.to_stream -->
<!-- frob:describes src/stpone/serial/packets/generic.py::Pydantic.from_stream -->

Bug fixes against the carried-over code, each pinned by UT-0701: the
subclass hook tested `isinstance(Serializable, annotation)` (always
false, so no composite packet could be declared) and now tests
`issubclass`; `FixedWidth.from_stream`/`Constant.from_stream` unwrapped
the error twice; `to_stream` returned nothing but was checked as a
`Result`; `Pydantic.from_stream` treated an `Optional` as a `Result`;
`Constant.from_stream` tried to raise a pydantic `ValidationError` with
a bare string. Mismatched constants now log and yield `None`.

### comp-0702
<!-- frob:describes src/stpone/serial/packets/integers.py::_IntRoot.get_byte_width -->
<!-- frob:describes src/stpone/serial/packets/integers.py::_IntRoot.min_value -->
<!-- frob:describes src/stpone/serial/packets/integers.py::_IntRoot.max_value_exclusive -->
<!-- frob:describes src/stpone/serial/packets/integers.py::_IntRoot.validate_range -->
<!-- frob:describes src/stpone/serial/packets/integers.py::_IntRoot.to_bytes -->
<!-- frob:describes src/stpone/serial/packets/integers.py::_IntRoot.from_bytes -->
<!-- frob:describes src/stpone/serial/packets/integers.py::U8 -->
<!-- frob:describes src/stpone/serial/packets/integers.py::Bool -->
<!-- frob:describes src/stpone/serial/packets/integers.py::Bool.validate_range -->
<!-- frob:describes src/stpone/serial/packets/integers.py::U16 -->
<!-- frob:describes src/stpone/serial/packets/integers.py::U32 -->
<!-- frob:describes src/stpone/serial/packets/integers.py::U64 -->
<!-- frob:describes src/stpone/serial/packets/integers.py::I8 -->
<!-- frob:describes src/stpone/serial/packets/integers.py::I16 -->
<!-- frob:describes src/stpone/serial/packets/integers.py::I32 -->
<!-- frob:describes src/stpone/serial/packets/integers.py::I64 -->

### comp-0703
<!-- frob:describes src/stpone/serial/packets/floats.py::_StructFloat.get_byte_width -->
<!-- frob:describes src/stpone/serial/packets/floats.py::_StructFloat.to_bytes -->
<!-- frob:describes src/stpone/serial/packets/floats.py::_StructFloat.from_bytes -->
<!-- frob:describes src/stpone/serial/packets/floats.py::F16 -->
<!-- frob:describes src/stpone/serial/packets/floats.py::F32 -->
<!-- frob:describes src/stpone/serial/packets/floats.py::F64 -->

### comp-0704
<!-- frob:describes src/stpone/serial/packets/string.py::FastString -->
<!-- frob:describes src/stpone/serial/packets/string.py::FastString.get_byte_width -->
<!-- frob:describes src/stpone/serial/packets/string.py::FastString.from_bytes -->
<!-- frob:describes src/stpone/serial/packets/string.py::FastString.to_bytes -->
<!-- frob:describes src/stpone/serial/packets/string.py::String -->
<!-- frob:describes src/stpone/serial/packets/string.py::String.validate_string_length -->
<!-- frob:describes src/stpone/serial/packets/string.py::String.to_bytes -->
<!-- frob:describes src/stpone/serial/packets/string.py::String.from_stream -->

### comp-0705
<!-- frob:describes src/stpone/serial/packets/handshake.py::SynPacket -->
<!-- frob:describes src/stpone/serial/packets/handshake.py::SynPacket.get_value -->
<!-- frob:describes src/stpone/serial/packets/handshake.py::SynAckPacket -->
<!-- frob:describes src/stpone/serial/packets/handshake.py::SynAckPacket.get_value -->
<!-- frob:describes src/stpone/serial/packets/handshake.py::AckPacket -->
<!-- frob:describes src/stpone/serial/packets/handshake.py::AckPacket.get_value -->

### comp-0706
<!-- frob:describes src/stpone/serial/rdt.py::RDTStatus -->
<!-- frob:describes src/stpone/serial/rdt.py::RDTCommunication -->
<!-- frob:describes src/stpone/serial/rdt.py::RDTCommunication.read -->
<!-- frob:describes src/stpone/serial/rdt.py::RDTCommunication.write -->
<!-- frob:describes src/stpone/serial/rdt.py::RDTCommunication.error -->

## Unit test plan

| ID | Case | verifies | runnable |
|---|---|---|---|
| UT-0701 | Composite packet round-trips through an in-memory transport; a non-serializable field raises `DeveloperException`; short reads yield `None` plus one `error()` call. | COMP-0701 | tests/integration/test_serial_stream.py::test_composite_packet_round_trip |
| UT-0702 | Every integer type round-trips at its bounds and rejects one past them; `Bool` accepts only 0/1. | COMP-0702 | tests/unit/test_serial_packets.py::test_every_integer_type_round_trips |
| UT-0703 | Every float type round-trips and rejects NaN/inf. | COMP-0703 | tests/unit/test_serial_packets.py::test_every_float_type_round_trips |
| UT-0704 | `String` is length-prefixed and streams back; `FastString` is 64 wide. | COMP-0704 | tests/unit/test_serial_packets.py::test_string_is_length_prefixed |
| UT-0705 | The three constants are singletons with the exact bytes. | COMP-0705 | tests/unit/test_serial_packets.py::test_handshake_constants_are_singletons |
| UT-0706 | The in-memory transport satisfies the contract and the handshake round-trips. | COMP-0706 | tests/integration/test_serial_stream.py::test_handshake_round_trip |
