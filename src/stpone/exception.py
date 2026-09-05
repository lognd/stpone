from __future__ import annotations


# frob:doc docs/spec/L5-component-design/SUB-06-desktop-core.md#comp-0602
class DeveloperException(Exception):
    """Raised for programmer errors (invariant violations), never for user input."""
