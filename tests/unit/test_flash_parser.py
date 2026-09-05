"""Unit tests for the flash CLI parser."""

from __future__ import annotations

from pathlib import Path

from stpone.flash.cli import build_parser, main


def test_parser_maps_flags_to_config_fields() -> None:
    # frob:tests src/stpone/flash/cli.py::build_parser kind="unit"
    args = build_parser().parse_args(
        [
            "--target",
            "test_lcd",
            "--no-build",
            "--port",
            "/dev/ttyACM1",
            "--attach-timeout",
            "3",
        ]
    )
    assert args.target == "test_lcd"
    assert args.build is False
    assert args.port == "/dev/ttyACM1"
    assert args.attach_timeout_s == 3.0
    assert isinstance(args.pyproject, Path)


def test_main_build_only_without_build_is_a_noop(tmp_path: Path) -> None:
    # frob:tests src/stpone/flash/cli.py::main kind="unit"
    code = main(
        ["--no-build", "--build-only", "--pyproject", str(tmp_path / "none.toml")]
    )
    assert code == 0
