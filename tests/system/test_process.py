"""System tests for the process itself: trace closure, waiver evidence, ignore rules."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / "docs" / "spec"
_ARTIFACT = ("REQ", "SPEC", "SYS", "SUB", "COMP")
_TEST = ("CT", "CTP", "SIT", "SUBT", "UT")
_RUNNABLE = re.compile(r"tests/[A-Za-z0-9_/]+\.py::[A-Za-z0-9_]+")
_ID = re.compile(r"\b(REQ|SPEC|SYS|SUB|COMP|CT|CTP|SIT|SUBT|UT)-([0-9]{2,4})\b")


def _spec_lines() -> list[tuple[Path, str]]:
    """Every table row line of every spec document, with its file."""
    lines: list[tuple[Path, str]] = []
    for md in sorted(SPEC.rglob("*.md")):
        lines.extend(
            (md, ln) for ln in md.read_text().splitlines() if ln.startswith("| ")
        )
    return lines


def _rows() -> list[tuple[str, str, list[str], Path]]:
    """Every trace-table row as (id, kind, referenced ids, file)."""
    rows = []
    for md, line in _spec_lines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        head = _ID.fullmatch(cells[0])
        if head is None:
            continue
        refs = [m.group(0) for m in _ID.finditer(line) if m.group(0) != cells[0]]
        rows.append((cells[0], head.group(1), refs, md))
    return rows


def _collected_node_ids() -> set[str]:
    proc = subprocess.run(
        [sys.executable, "-m", "pytest", "--collect-only", "-q", "-o", "addopts="],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    assert proc.returncode == 0, proc.stdout + proc.stderr
    return {ln.strip() for ln in proc.stdout.splitlines() if "::" in ln}


def test_trace_tables_are_closed() -> None:
    # frob:tests docs/spec kind="e2e"
    rows = _rows()
    ids = {r[0] for r in rows}
    assert len(ids) == len(rows), "duplicate ids in docs/spec"
    problems = []
    for ident, kind, refs, md in rows:
        for ref in refs:
            if ref not in ids:
                problems.append(f"{md.name}: {ident} references missing {ref}")
        if kind in _ARTIFACT and not any(r.split("-")[0] in _TEST for r in refs):
            problems.append(f"{md.name}: {ident} has no verifier")
        if kind in _TEST and not any(r.split("-")[0] in _ARTIFACT for r in refs):
            problems.append(f"{md.name}: {ident} verifies nothing")
    assert not problems, "\n".join(problems)


def test_runnables_collect() -> None:
    # frob:tests docs/spec kind="e2e"
    collected = _collected_node_ids()
    wanted = set()
    for _, line in _spec_lines():
        if "manual" in line:
            continue
        wanted.update(_RUNNABLE.findall(line))
    missing = sorted(w for w in wanted if not any(c.startswith(w) for c in collected))
    assert not missing, "runnables that do not collect:\n" + "\n".join(missing)


def test_waivers_name_their_evidence() -> None:
    # frob:tests src/stpalpha/hal kind="integration"
    waivers = []
    for path in list((ROOT / "src" / "stpalpha").rglob("*.c*")) + list(
        (ROOT / "include").rglob("*.h")
    ):
        for line in path.read_text().splitlines():
            if "frob:waive TEST001" in line:
                waivers.append((path.name, line))
    assert waivers, "expected TEST001 waivers on AVR-only symbols"
    for name, line in waivers:
        assert "hardware-tests.md" in line, (name, line)


def test_froblems_is_gitignored() -> None:
    # frob:tests .gitignore kind="e2e"
    for path in ("FROBLEMS.md", ".frob/x", "build/x"):
        proc = subprocess.run(
            ["git", "check-ignore", "-q", path], cwd=ROOT, check=False
        )
        assert proc.returncode == 0, f"{path} is not gitignored"
