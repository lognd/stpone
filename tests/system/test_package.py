"""System tests: the package installs and imports."""

from __future__ import annotations

import ast
import importlib
from pathlib import Path


def test_package_imports():
    """The top-level package must be importable with no errors."""
    importlib.import_module("stpone")


def test_no_syntax_errors_in_src():
    """All source files must parse without syntax errors."""
    src_root = Path(__file__).parent.parent.parent / "src" / "stpone"
    errors = []
    for py_file in src_root.rglob("*.py"):
        try:
            ast.parse(py_file.read_bytes())
        except SyntaxError as exc:
            errors.append(f"{py_file}: {exc}")

    assert not errors, "Syntax errors found:\n" + "\n".join(errors)
