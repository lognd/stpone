"""Integration tests: the CLI wires parser, config, and app together."""

from __future__ import annotations

from pathlib import Path

import pytest

from stpone.flash.cli import build_parser, main


def test_help_exits_zero() -> None:
    # frob:tests src/stpone/flash/cli.py::build_parser kind="integration"
    with pytest.raises(SystemExit) as exc:
        main(["--help"])
    assert exc.value.code == 0


def test_parser_leaves_unset_options_as_none() -> None:
    # frob:tests src/stpone/flash/cli.py::build_parser kind="integration"
    args = build_parser().parse_args([])
    assert args.target is None
    assert args.build is None
    assert args.build_only is None


def test_all_target_requires_build_only() -> None:
    # frob:tests src/stpone/flash/cli.py::main kind="integration"
    with pytest.raises(SystemExit) as exc:
        main(["--target", "all"])
    assert exc.value.code == 2


def test_main_reports_missing_image(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    # frob:tests src/stpone/flash/cli.py::main kind="integration"
    monkeypatch.setattr(
        "stpone.flash.app.detect_host",
        lambda: __import__("stpone.flash.host", fromlist=["HostKind"]).HostKind.LINUX,
    )
    code = main(
        [
            "--no-build",
            "--build-dir",
            str(tmp_path),
            "--pyproject",
            str(tmp_path / "none.toml"),
        ]
    )
    assert code == 1
