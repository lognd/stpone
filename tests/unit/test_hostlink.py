"""Unit test: the host-link interpreter (docs/hardware/serial-protocol.md)."""

from __future__ import annotations

import subprocess
from pathlib import Path

import pytest

_ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def driver(tmp_path_factory: pytest.TempPathFactory) -> Path:
    exe = tmp_path_factory.mktemp("hostlink") / "driver"
    subprocess.run(
        [
            "g++", "-std=c++17", "-O1", "-Wall", "-Wno-comment", "-Werror",
            f"-I{_ROOT / 'include'}", str(_ROOT / "tests/unit/hostlink_driver.cpp"),
            "-o", str(exe),
        ],
        check=True,
    )  # fmt: skip
    return exe


def _run(driver: Path, *lines: str) -> list[tuple[str, int]]:
    out = subprocess.run(
        [str(driver)],
        input="\n".join(lines) + "\n",
        capture_output=True,
        text=True,
        check=True,
    ).stdout.splitlines()
    replies: list[tuple[str, int]] = []
    for text in out:
        if text.startswith("#req="):
            replies[-1] = (replies[-1][0], int(text[5:]))
        else:
            replies.append((text, 0))
    return replies


def test_hello_get_param_defaults(driver: Path) -> None:
    # frob:tests include/stpalpha/hostlink.h::HostLink kind="unit"
    r = [x[0] for x in _run(driver, "HELLO", "GET", "PARAM")]
    assert r[0] == "OK stpone proto=1 fw=6"
    assert r[1] == "OK state=IDLE count=-42 speed=5170 errors=3 relay=0 btn=1010 pass=-"
    assert r[2] == (
        "OK wheel_um=101638 cpr=2400 short=206187 long=618562 coast=2291 margin=191 bump=1"
    )


def test_dump_lists_every_internal(driver: Path) -> None:
    # frob:tests include/stpalpha/hostlink.h::HostLink kind="unit"
    ((text, _),) = _run(driver, "DUMP")
    for field in (
        "state=IDLE", "prev=-", "count=-42", "speed=5170", "errors=3", "relay=0", "manual=0",
        "target=206187", "eff_target=204087", "coast=2291", "margin=191", "bump=1", "bumps=0", "trim=0", "pass=-", "btn=1010", "toggles=7,0,0,0",
        "pending=0,0,1,0", "enc_dir=-1", "uptime_ms=1234", "run=0", "stack_free=321", "pass_max_ms=47", "isr_lat_max=33",
    ):  # fmt: skip
        assert field in text.split(), field


def test_set_validates_and_applies(driver: Path) -> None:
    # frob:tests include/stpalpha/hostlink.h::HostLink kind="unit"
    r = [
        x[0]
        for x in _run(
            driver, "SET coast 500", "DUMP", "SET coast 999999999", "SET short 5", "SET wheel_um 1",
            "SET nope 1", "SET coast", "SET coast -3", "DEFAULTS", "PARAM",
        )
    ]  # fmt: skip
    assert r[0] == "OK"
    assert "eff_target=205878" in r[1]  # target + margin - coast
    assert r[2:8] == ["ERR bad_arg"] * 6
    assert r[8] == "OK"
    assert "coast=2291 margin=191 bump=1" in r[9]


def test_margin_and_bump_set_validate_and_default(driver: Path) -> None:
    r = [
        x[0]
        for x in _run(
            driver, "SET margin 0", "SET margin 2000", "SET margin 2001", "SET bump 0",
            "SET bump 2", "SET bump 1", "PARAM", "SET short 3999000", "DEFAULTS",
            "SET short 3999000", "SET margin 2000", "SET margin 800", "SET margin 801",
            "DEFAULTS", "PARAM", "SET margin", "SET bump -1",
        )
    ]  # fmt: skip
    assert r[:2] == ["OK", "OK"] and r[2] == "ERR bad_arg"
    assert r[3] == "OK" and r[4] == "ERR bad_arg" and r[5] == "OK"
    assert r[6].endswith("coast=2291 margin=2000 bump=1")
    # the goal (largest target + margin) must stay within kMaxPulses = 4000000
    assert r[7] == "ERR bad_arg" and r[8] == "OK"
    assert r[9] == "OK" and r[10] == "ERR bad_arg" and r[11] == "OK" and r[12] == "OK"
    assert r[13] == "OK" and r[14].endswith("coast=2291 margin=191 bump=1")
    assert r[15:] == ["ERR bad_arg", "ERR bad_arg"]


def test_save_load_zero_raise_requests(driver: Path) -> None:
    # frob:tests include/stpalpha/hostlink.h::HostLink kind="unit"
    r = _run(driver, "SAVE", "LOAD", "ZERO")
    assert r == [("OK", 1), ("OK", 2), ("OK", 4)]


def test_debug_rules(driver: Path) -> None:
    # frob:tests include/stpalpha/hostlink.h::HostLink kind="unit"
    r = [
        x[0]
        for x in _run(
            driver, "RELAY 1", "RUN 5000", "STREAM 1", "DEBUG 1", "RELAY 1", "GET", "STREAM 1",
            "RUN 0", "RUN 5000", "RELAY 0", "ABORT", "ABORT", "DEBUG 0", "RELAY 1", "BOGUS", "",
        )
    ]  # fmt: skip
    assert r[:3] == ["ERR bad_state"] * 3
    assert r[3] == "OK" and r[4] == "OK"
    assert r[5].startswith("OK state=DEBUG") and "relay=1" in r[5]
    assert r[6] == "OK"
    assert r[7] == "ERR bad_arg"
    assert r[8] == "OK"  # run started
    assert r[9] == "ERR bad_state"  # relay is owned by the run
    assert r[10] == "OK" and r[11] == "OK"  # abort is a flag until the next pass
    assert r[12] == "OK" and r[13] == "ERR bad_state"
    assert r[14] == "ERR bad_command" and r[15] == "ERR bad_command"


def test_panel_wind_blocks_changes(driver: Path) -> None:
    # frob:tests include/stpalpha/hostlink.h::HostLink kind="unit"
    r = [
        x[0]
        for x in _run(driver, "@wind", "SET coast 1", "DEBUG 1", "ZERO", "RUN 5000")
    ]
    assert r == [
        "@ok",
        "ERR bad_state",
        "ERR bad_state",
        "ERR bad_state",
        "ERR bad_state",
    ]


def test_strict_numeric_parsing(driver: Path) -> None:
    r = [
        x[0]
        for x in _run(
            driver, "SET short 206187abc", "SET coast 4294968296", "SET coast 0x10", "SET coast +5",
            "DEBUG 1x", "DEBUG 11", "RELAY 1junk", "DEBUG 1", "RELAY 01", "RUN 100abc", "RUN",
            "RUN 99999999999", "RUN 4000001", "GET a b c",
        )
    ]  # fmt: skip
    assert r[:3] == ["ERR bad_arg"] * 3 and r[3] == "ERR bad_arg"
    assert r[4] == "ERR bad_command" and r[5] == "ERR bad_command"
    assert r[6] == "ERR bad_command"
    assert r[7] == "OK"
    assert r[8:10] == ["ERR bad_command", "ERR bad_arg"]
    assert r[10] == "ERR bad_command"  # RUN needs an argument
    assert r[11:13] == ["ERR bad_arg"] * 2
    assert r[13] == "ERR bad_command"


def test_run_must_exceed_coast(driver: Path) -> None:
    r = [
        x[0]
        for x in _run(
            driver, "SET coast 400", "DEBUG 1", "RUN 400", "RUN 300", "RUN 401"
        )
    ]
    assert r == ["OK", "OK", "ERR bad_arg", "ERR bad_arg", "OK"]


def test_params_locked_while_running(driver: Path) -> None:
    r = [
        x[0]
        for x in _run(
            driver, "@time 0", "DEBUG 1", "RUN 5000", "SET coast 10", "LOAD", "DEFAULTS", "ZERO",
            "ABORT", "@step 0 5000 0", "SET coast 10", "LOAD", "DEFAULTS",
        )
    ]  # fmt: skip
    assert r[1:3] == ["OK", "OK"]
    assert r[3:7] == ["ERR bad_state"] * 4
    assert r[7] == "OK" and r[8].startswith("@debug=1")
    assert r[9:12] == ["OK", "OK", "OK"]


def test_set_wheel_and_cpr_rederive_targets(driver: Path) -> None:
    r = [x[0] for x in _run(driver, "SET wheel_um 101638", "PARAM", "SET wheel_um 101000", "PARAM",
                            "SET short 150000", "PARAM", "SET cpr 4800", "PARAM")]  # fmt: skip
    assert (
        r[1]
        == "OK wheel_um=101638 cpr=2400 short=206188 long=618563 coast=2291 margin=191 bump=1"
    )
    # 1080 in * 25400 um/in * 2400 / (pi * 101000 um) = 207490.14
    assert (
        r[3]
        == "OK wheel_um=101000 cpr=2400 short=207490 long=622470 coast=2291 margin=191 bump=1"
    )
    assert r[5].startswith("OK wheel_um=101000 cpr=2400 short=150000 long=622470")
    assert r[7].startswith("OK wheel_um=101000 cpr=4800 short=414980 long=1244941")


def test_debug_host_silence_times_out(driver: Path) -> None:
    r = [
        x[0]
        for x in _run(
            driver, "@time 1000", "DEBUG 1", "RELAY 1", "@step 6000 0 0", "@step 6001 0 0",
            "@time 6001", "DEBUG 1", "@step 11001 0 0", "@step 11002 0 0",
        )
    ]  # fmt: skip
    assert r[:3] == ["@ok", "OK", "OK"]
    assert r[3].startswith(
        "@debug=1 relay=1"
    )  # exactly 5000 ms of silence is still allowed
    assert "debug=0 relay=0" in r[4] and "exit=1" in r[4] and "reset=1" in r[4]
    assert (
        r[5] == "@ok" and r[6] == "OK"
    )  # a fresh session starts a fresh silence window
    assert r[7].startswith("@debug=1")
    assert "debug=0" in r[8] and "exit=1" in r[8]


def test_activity_keeps_debug_alive(driver: Path) -> None:
    r = [
        x[0]
        for x in _run(
            driver, "@time 0", "DEBUG 1", "RUN 5000", "@time 4000", "GET", "@step 8000 100 0",
            "@time 8000", "STREAM 1", "@step 12000 100 0", "@step 13001 100 0",
        )
    ]  # fmt: skip
    # GET at 4000 and STREAM at 8000 count as activity, so 8000 and 12000 are alive
    assert r[5].startswith("@debug=1 relay=1 run=1")
    assert r[8].startswith("@debug=1 relay=1 run=1")
    # 13001 - 8000 > 5000: run interrupted, debug exited
    assert "debug=0" in r[9] and "run_stopped=1" in r[9] and "exit=1" in r[9]


def test_panel_stop_ends_debug_motor(driver: Path) -> None:
    r = [
        x[0]
        for x in _run(
            driver, "@time 0", "DEBUG 1", "@step 0 0 15", "RELAY 1", "@step 0 0 15", "DEBUG 1",
            "RUN 5000", "@step 0 10 1",
        )
    ]  # fmt: skip
    assert r[2].startswith("@debug=1 relay=0")  # relay off: panel buttons stay ignored
    assert "debug=0 relay=0" in r[4] and "exit=2" in r[4]
    assert "debug=0 relay=0 run=0 run_stopped=1" in r[7] and "exit=2" in r[7]


def test_leave_debug_gives_pristine_idle_and_reports_run(driver: Path) -> None:
    r = [
        x[0]
        for x in _run(
            driver, "@time 0", "DEBUG 1", "RUN 5000", "DEBUG 0", "@step 0 777 0", "@step 0 777 0"
        )
    ]  # fmt: skip
    assert r[1:4] == ["OK", "OK", "OK"]
    assert (
        "debug=0 relay=0 run=0 run_stopped=1 at_stop=777" in r[4] and "reset=1" in r[4]
    )
    assert "run_stopped=0" in r[5] and "reset=0" in r[5]


def test_bargraph_tracks_progress_while_winding_and_paused(driver: Path) -> None:
    # @wind starts BTN1 (target 206187); BTN3 (4) pauses
    r = [
        x[0]
        for x in _run(
            driver, *_LEGACY, "@wind", "@step 0 0 0", "@step 0 51547 0", "@step 0 103094 0",
            "@step 0 154641 0", "@step 0 103094 4", "@step 0 103094 0",
        )
    ]  # fmt: skip
    leds = [int(x.split("leds=")[1].split()[0]) for x in r[4:]]
    assert leds == [0, 1, 3, 7, 3, 3]


def test_get_and_dump_put_speed_right_after_count(driver: Path) -> None:
    get, dump = (x[0].split() for x in _run(driver, "GET", "DUMP"))
    assert get[2:4] == ["count=-42", "speed=5170"]
    assert dump[3:6] == ["count=-42", "speed=5170", "errors=3"]


def _speeds(driver: Path, *lines: str) -> list[int]:
    return [int(x[0].split("=")[1]) for x in _run(driver, *lines)]


def test_speed_meter_samples_a_fixed_100ms_window(driver: Path) -> None:
    got = _speeds(
        driver,
        "@meter 50 100",    # window not complete: still 0
        "@meter 100 400",   # 400 pulses in 100 ms
        "@meter 150 500",   # mid-window: unchanged
        "@meter 200 500",   # 100 pulses in 100 ms
        "@meter 300 -100",  # reverse through zero: |delta| = 600
        "@meter 700 -100",  # stopped; the window is 400 ms long
        "@rebase 710 0",    # a ZERO must not read as motion
        "@meter 810 50",
        "@meter 910 9000000",  # a count jump is not motion
    )  # fmt: skip
    assert got == [0, 4000, 4000, 1000, 6000, 0, 0, 500, 0]


def test_speed_meter_divides_by_the_real_elapsed_time(driver: Path) -> None:
    assert _speeds(driver, "@meter 250 500") == [2000]


def _results(driver: Path, *lines: str) -> list[dict[str, int]]:
    out = []
    for text, _ in _run(driver, *lines):
        if text.startswith("@debug="):
            out.append({k: int(v) for k, v in (t.split("=") for t in text[1:].split())})
        else:
            out.append({})
    return out


_LEGACY = ("SET coast 0", "SET margin 0", "SET bump 0")  # the single cut at the target


def test_result_settles_then_done_then_expires(driver: Path) -> None:
    r = _results(
        driver,
        *_LEGACY,
        "@wind",
        "@step 1000 206187 0",  # target reached: count kept, SETTLING
        "@step 1100 206300 0",  # coasting
        "@step 1599 206300 0",  # 499 ms still: SETTLING
        "@step 1600 206300 0",  # 500 ms still: DONE
        "@step 60999 206300 0",
        "@step 61000 206300 0",  # 60 s after the stop: gone, count zeroed
    )
    r = r[3:]
    assert r[1]["stopped"] == 1 and r[1]["result"] == 1 and r[1]["reset"] == 0
    assert r[2]["result"] == 1
    assert r[3]["result"] == 1
    assert r[4]["result"] == 2 and r[4]["changed"] == 1
    assert r[5]["result"] == 2
    assert r[6]["result"] == 0 and r[6]["reset"] == 1 and r[6]["relay"] == 0


def test_result_restarts_when_the_count_moves_again(driver: Path) -> None:
    r = _results(
        driver,
        *_LEGACY, "@wind", "@step 0 206187 0", "@step 600 206200 0", "@step 1100 206200 0",
        "@step 1200 206250 0", "@step 1700 206250 0",
    )  # fmt: skip
    assert [x["result"] for x in r[4:]] == [1, 1, 2, 1, 2]


def test_result_clears_on_manual_and_new_wind_and_debug(driver: Path) -> None:
    stop = (*_LEGACY, "@wind", "@step 0 206187 0", "@step 600 206300 0")
    manual = _results(driver, *stop, "@step 700 206300 8")[-1]
    assert manual["result"] == 0 and manual["relay"] == 1 and manual["reset"] == 1
    wind = _results(driver, *stop, "@step 700 206300 1")[-1]
    assert wind["result"] == 0 and wind["relay"] == 1 and wind["reset"] == 1
    dbg = _results(driver, "@time 0", *stop, "DEBUG 1", "@step 700 206300 0")
    assert dbg[-1]["debug"] == 1 and dbg[-1]["result"] == 0 and dbg[-1]["reset"] == 1
    # the relay is off and nothing is running while the result is shown
    shown = _results(driver, *stop)[-1]
    assert shown["relay"] == 0 and shown["debug"] == 0 and shown["run"] == 0


def test_dump_reply_fits_the_firmware_reply_buffer_at_its_widest(driver: Path) -> None:
    r = [x[0] for x in _run(driver, "@worst", "DEBUG 1", "DUMP", "STREAM 1")]
    assert (
        len(r[2]) < 335
    )  # main.cpp g_reply is 352 bytes; the widest real line is ~325
    assert r[2].endswith("isr_lat_max=65535 margin=191 bump=1 bumps=0 trim=0 pass=-")


def test_the_5s_lease_and_speed_hold_with_a_long_780ms_lcd_pass(driver: Path) -> None:
    """Passes of 780 ms (the old full-panel rewrite) must not distort the lease or the speed."""
    steps = [f"@step {780 * i} 0 0" for i in range(1, 9)]
    r = [x[0] for x in _run(driver, "@time 0", "DEBUG 1", "RELAY 1", *steps)]
    alive = [x.startswith("@debug=1") for x in r[3:]]
    # last host line at t=0: still alive at 4680 ms (6 passes), expired by the 7th (5460 ms)
    assert alive == [True] * 6 + [False, False]
    # 3600 pulses/s (90 RPM) over a 780 ms pass is still read as 3600, not a 3x spike
    got = _speeds(driver, "@meter 780 2808", "@meter 1560 5616")
    assert got == [3600, 3600]


def test_pass_meter_tracks_the_longest_pass_and_clears(driver: Path) -> None:
    r = [x[0] for x in _run(driver, "@pass 100", "@pass 130", "@pass 133", "@pass 300", "@pass 310",
                            "@passclear", "@pass 320")]  # fmt: skip
    assert r == ["@passmax=0", "@passmax=30", "@passmax=30", "@passmax=167", "@passmax=167",
                 "@passmax=0", "@passmax=10"]  # fmt: skip


# ---- default bump (trim) mode, directed scenarios (DEC-010) --------------------------------
# Defaults: target 206187, margin 191 -> goal 206378, coast 2291 -> main cut at 204087.
_NOSTOP = 0xFFFFFFFF
_GOAL = 206378
_MAIN_CUT = 204087


def _settled_into_first_bump(*extra: str) -> list[str]:
    """@wind, the main cut, 500 ms of quiet at 204100: the first bump starts on the last line."""
    return [
        "@wind",
        f"@step 0 {_MAIN_CUT} 0",
        "@step 100 204100 0",
        "@step 600 204100 0",
        *extra,
    ]


def test_trim_main_cut_settles_then_bumps_half_the_gap(driver: Path) -> None:
    r = _results(driver, *_settled_into_first_bump())
    cut, moving, bump = r[1], r[2], r[3]
    assert (
        cut["state"] == 3
        and cut["trim"] == 1
        and cut["relay"] == 0
        and cut["stopped"] == 0
    )
    assert cut["stop_at"] == _NOSTOP and cut["outcome"] == 0
    assert moving["relay"] == 0 and moving["stop_at"] == _NOSTOP
    # remaining = 206378 - 204100 = 2278 > the final window: the first bump is remaining / 2
    assert (
        bump["relay"] == 1 and bump["bumps"] == 1 and bump["stop_at"] == 204100 + 1139
    )


def test_trim_bump_is_cut_by_the_isr_then_settles_again(driver: Path) -> None:
    r = _results(
        driver, *_settled_into_first_bump(), "@step 700 204500 0", "@step 800 205239 0",
        "@step 1000 205800 0", "@step 1500 205800 0",
    )  # fmt: skip
    mid, cut, moving, settled = r[4], r[5], r[6], r[7]
    assert mid["relay"] == 1 and mid["stop_at"] == 205239
    assert cut["relay"] == 0 and cut["stop_at"] == _NOSTOP and cut["bumps"] == 1
    assert moving["relay"] == 0
    # learned: the bump coasted 205800 - 204100 - 1139 = 561; remaining 578 -> b = 578 - 561 = 17
    assert (
        settled["relay"] == 1
        and settled["bumps"] == 2
        and settled["stop_at"] == 205800 + 17
    )


def test_trim_final_window_arms_the_goal_and_passes_only_at_or_over_it(
    driver: Path,
) -> None:
    near = _GOAL - 300  # inside the 382 pulse final window
    r = _results(
        driver, "@wind", f"@step 0 {_MAIN_CUT} 0", f"@step 100 {near} 0", f"@step 600 {near} 0",
        f"@step 700 {_GOAL} 0", f"@step 1300 {_GOAL + 40} 0", f"@step 1900 {_GOAL + 40} 0",
    )  # fmt: skip
    final, cut = r[3], r[4]
    assert final["relay"] == 1 and final["stop_at"] == _GOAL and final["bumps"] == 1
    assert cut["relay"] == 0 and cut["stop_at"] == _NOSTOP
    done = r[6]
    assert (
        done["stopped"] == 1 and done["outcome"] == 1 and done["beep"] == 1
    )  # PASS, START
    assert done["result"] == 2 and done["relay"] == 0 and done["stop_at"] == _NOSTOP


def test_trim_main_overshoot_past_the_goal_passes_after_settling(driver: Path) -> None:
    r = _results(
        driver, "@wind", f"@step 0 {_MAIN_CUT} 0", f"@step 100 {_GOAL + 900} 0",
        f"@step 700 {_GOAL + 900} 0",
    )  # fmt: skip
    assert r[3]["stopped"] == 1 and r[3]["outcome"] == 1 and r[3]["bumps"] == 0


def test_trim_pause_resume_keeps_the_trim_and_caps_manual_at_the_goal(
    driver: Path,
) -> None:
    r = _results(
        driver, *_settled_into_first_bump(), "@step 700 204500 4", "@step 800 204500 8",
        "@step 900 204500 8", "@step 1000 204500 4", "@step 1100 204500 0", "@step 1700 204500 0",
    )  # fmt: skip
    paused, manual_on, manual_off, resumed, settling, again = r[4:]
    assert paused["state"] == 2 and paused["trim"] == 1 and paused["relay"] == 0
    assert paused["stop_at"] == _NOSTOP  # disarmed while paused
    assert (
        manual_on["relay"] == 1
        and manual_on["stop_at"] == _GOAL
        and manual_on["manual"] == 1
    )
    assert manual_off["relay"] == 0 and manual_off["stop_at"] == _NOSTOP
    assert resumed["state"] == 3 and resumed["trim"] == 1 and resumed["relay"] == 0
    assert settling["relay"] == 0 and settling["stopped"] == 0
    # the trim resumed where it was: 500 ms of quiet later the next bump starts (bumps 2)
    assert again["relay"] == 1 and again["bumps"] == 2


def test_trim_operator_stop_is_short_loud_and_held(driver: Path) -> None:
    r = _results(
        driver, *_settled_into_first_bump(), "@step 700 204500 1", "@step 61000 204500 0",
        "@step 62000 204500 0", "@step 63000 204500 4",
    )  # fmt: skip
    stop, still, held, cleared = r[4], r[5], r[6], r[7]
    assert stop["stopped"] == 1 and stop["relay"] == 0 and stop["stop_at"] == _NOSTOP
    assert (
        stop["outcome"] == 3 and stop["beep"] == 2 and stop["state"] == 0
    )  # STOPPED, CANCEL
    assert (
        still["result"] != 0 and held["result"] != 0
    )  # held far past the 60 s auto-clear
    assert (
        cleared["result"] == 0 and cleared["outcome"] == 3
    )  # a button press clears the screen


def test_trim_cannot_start_a_new_wind_from_the_trim(driver: Path) -> None:
    r = _results(driver, "@wind", f"@step 0 {_MAIN_CUT} 0", "@step 10 204100 2")
    assert r[2]["stopped"] == 1 and r[2]["state"] == 0 and r[2]["relay"] == 0


def test_trim_stall_twice_ends_short_with_the_alarm(driver: Path) -> None:
    stalled = [f"@step {700 + 100 * k} 204100 0" for k in range(0, 40)]
    r = _results(driver, *_settled_into_first_bump(), *stalled)
    first = next(
        i for i, x in enumerate(r[4:], 4) if x["relay"] == 0 and x["bumps"] == 1
    )
    # 1500 ms on with no count change: cut at the first pass past 1500 ms (stall 1), settle,
    # bump again, stall 2 ends the wind
    end = next(x for x in r[4:] if x["stopped"] == 1)
    assert first > 4 and end["outcome"] == 2 and end["beep"] == 3 and end["bumps"] == 2
    assert end["alarm"] == 1 and end["relay"] == 0


def test_trim_limit_30s_ends_short(driver: Path) -> None:
    # the count creeps 1 pulse per 100 ms: no stall, but the 30 s limit still ends the trim
    creep = [f"@step {700 + 100 * k} {204100 + k} 0" for k in range(0, 320)]
    end = next(
        x
        for x in _results(driver, *_settled_into_first_bump(), *creep)
        if x.get("stopped")
    )
    assert end["outcome"] == 2 and end["beep"] == 3


def test_legacy_bump_zero_cuts_once_at_the_goal_minus_coast(driver: Path) -> None:
    r = _results(
        driver,
        "SET bump 0",
        "@wind",
        f"@step 0 {_MAIN_CUT - 1} 0",
        f"@step 1 {_MAIN_CUT} 0",
    )
    assert r[2]["stop_at"] == _MAIN_CUT and r[2]["relay"] == 1
    assert (
        r[3]["stopped"] == 1
        and r[3]["outcome"] == 0
        and r[3]["beep"] == 1
        and r[3]["result"] == 1
    )


def _image(driver: Path, *lines: str) -> dict[str, int]:
    text = _run(driver, *lines)[-1][0]
    return {k: int(v) for k, v in (t.split("=") for t in text[1:].split())}


def test_eeprom_image_round_trips_margin_bump_and_the_new_default_coast(
    driver: Path,
) -> None:
    first = _image(
        driver, "@image"
    )  # defaults; the host pads to 36, the AVR image is 34 bytes
    assert first["image"] == 0 and first["same"] == 1 and first["size"] >= 34
    got = _image(driver, "SET coast 1234", "SET margin 777", "SET bump 0", "@image")
    assert got["image"] == 0 and got["same"] == 1


def test_eeprom_image_old_corrupt_or_foreign_is_refused(driver: Path) -> None:
    assert (
        _image(driver, "@imagev5")["image"] == 1
    )  # OLD_VERSION: fall back to defaults
    assert _image(driver, "@imagemagic")["image"] == 2  # BAD_MAGIC
    assert _image(driver, "@imagecrc")["image"] == 3  # BAD_CRC
