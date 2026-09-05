from __future__ import annotations

import logging


# follow_up="T-0007"
# frob:doc docs/spec/L5-component-design/SUB-06-desktop-core.md#comp-0601
# frob:tests tests/unit/test_logging.py::test_below_level_filter_passes_records_below_threshold kind="unit"  # noqa: E501
# frob:waive WIRE001 reason="instantiated by dictConfig from config.toml" \
# follow_up="T-0007"
class BelowLevelFilter(logging.Filter):
    """Pass only records with levelno strictly below the configured threshold."""

    def __init__(self, below: str = "WARNING") -> None:
        super().__init__()
        # T-3277: logging.getLevelNamesMapping() is a real dict lookup, not
        # a runtime-resolved attribute indirection (OPAQUE001's
        # python:runtime:getattr-dynamic-name pattern) -- `getattr(logging,
        # below.upper(), ...)` reads as a dynamic capability probe even
        # though `below` is always one of the fixed stdlib level names.
        self._below = logging.getLevelNamesMapping().get(below.upper(), logging.WARNING)

    # follow_up="T-0007"
    # frob:waive WIRE001 reason="instantiated by dictConfig from config.toml" \
    # follow_up="T-0007"
    def filter(self, record: logging.LogRecord) -> bool:
        # frob:doc docs/spec/L5-component-design/SUB-06-desktop-core.md#comp-0601
        return record.levelno < self._below
