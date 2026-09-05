from __future__ import annotations


# frob:doc docs/spec/L5-component-design/SUB-06-desktop-core.md#comp-0602
# frob:tests tests/integration/test_serial_stream.py::test_non_serializable_field_is_a_developer_error kind="integration"  # noqa: E501
class DeveloperException(Exception):
    """Raised for programmer errors (invariant violations), never for user input."""
