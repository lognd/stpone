from __future__ import annotations

import logging


# frob:waive WIRE001 reason="instantiated by dictConfig from config.toml" \
# follow_up="T-0007"
# frob:doc docs/spec/L5-component-design/SUB-06-desktop-core.md#comp-0601
# frob:tests tests/unit/test_logging.py::test_simple_formatter_prefixes_level_at_warning_and_above kind="unit"  # noqa: E501
class SimpleFormatter(logging.Formatter):
    """Plain message for INFO/DEBUG; prefixes level name for WARNING and above."""

    def __init__(self, show_level: bool = False) -> None:
        super().__init__()
        self._show_level = show_level

    # frob:waive WIRE001 reason="instantiated by dictConfig from config.toml" \
    # follow_up="T-0007"
    def format(self, record: logging.LogRecord) -> str:
        # frob:doc docs/spec/L5-component-design/SUB-06-desktop-core.md#comp-0601
        msg = record.getMessage()
        if self._show_level or record.levelno >= logging.WARNING:
            return f"{record.levelname}: {msg}"
        return msg
