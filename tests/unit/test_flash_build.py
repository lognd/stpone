"""Unit tests for the cmake and avrdude wrappers."""

from __future__ import annotations

from pathlib import Path

from stpone.flash.avrdude import avrdude_argv, flash_image
from stpone.flash.build import ALL_TARGET, build, configure, hex_path
from stpone.flash.config import FlashConfig
from stpone.flash.errors import FlashError
from tests.conftest import FakeRunner


def test_hex_path_follows_cmake_layout() -> None:
    # frob:tests src/stpone/flash/build.py::hex_path kind="unit"
    cfg = FlashConfig(build_dir=Path("out"))
    assert hex_path(cfg, "test_lcd") == Path("out/test_lcd.hex")


def test_configure_argv_uses_toolchain_file() -> None:
    # frob:tests src/stpone/flash/build.py::configure kind="unit"
    runner = FakeRunner()
    cfg = FlashConfig(source_dir=Path("/repo"), build_dir=Path("/repo/build"))
    assert configure(cfg, runner, which=lambda n: "cmake").is_ok
    assert runner.calls[0] == [
        "cmake",
        "-S",
        "/repo",
        "-B",
        "/repo/build",
        "-DCMAKE_TOOLCHAIN_FILE=/repo/cmake/avr-gcc-toolchain.cmake",
    ]


def test_build_target_and_all() -> None:
    # frob:tests src/stpone/flash/build.py::build kind="unit"
    runner = FakeRunner()
    cfg = FlashConfig(build_dir=Path("build"))
    assert build(cfg, "firmware", runner, which=lambda n: "cmake").is_ok
    assert build(cfg, ALL_TARGET, runner, which=lambda n: "cmake").is_ok
    assert runner.calls[0] == ["cmake", "--build", "build", "--target", "firmware.elf"]
    assert runner.calls[1] == ["cmake", "--build", "build"]


def test_build_errors() -> None:
    # frob:tests src/stpone/flash/build.py::build kind="unit"
    # frob:tests src/stpone/flash/build.py::configure kind="unit"
    cfg = FlashConfig()
    assert (
        configure(cfg, FakeRunner(), which=lambda n: None).danger_err
        is FlashError.ToolMissing
    )
    assert (
        build(cfg, "x", FakeRunner(), which=lambda n: None).danger_err
        is FlashError.ToolMissing
    )
    assert (
        configure(cfg, FakeRunner([2]), which=lambda n: "cmake").danger_err
        is FlashError.BuildFailed
    )
    assert (
        build(cfg, "x", FakeRunner([2]), which=lambda n: "cmake").danger_err
        is FlashError.BuildFailed
    )


def test_avrdude_argv_matches_legacy_upload_script() -> None:
    # frob:tests src/stpone/flash/avrdude.py::avrdude_argv kind="unit"
    argv = avrdude_argv(FlashConfig(), "/dev/ttyACM0", Path("build/firmware.hex"))
    assert argv == [
        "avrdude",
        "-v",
        "-patmega32u4",
        "-cavr109",
        "-P/dev/ttyACM0",
        "-b57600",
        "-D",
        "-Uflash:w:build/firmware.hex:i",
    ]


def test_flash_image_outcomes() -> None:
    # frob:tests src/stpone/flash/avrdude.py::flash_image kind="unit"
    cfg = FlashConfig()
    image = Path("build/firmware.hex")
    ok = flash_image(
        cfg, "/dev/ttyACM0", image, FakeRunner(), which=lambda n: "avrdude"
    )
    assert ok.is_ok
    bad = flash_image(
        cfg, "/dev/ttyACM0", image, FakeRunner([1]), which=lambda n: "avrdude"
    )
    assert bad.danger_err is FlashError.AvrdudeFailed
    none = flash_image(cfg, "/dev/ttyACM0", image, FakeRunner(), which=lambda n: None)
    assert none.danger_err is FlashError.ToolMissing
