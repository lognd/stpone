//! Job tests against the simulated firmware (short timings; real threads).

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use super::*;
use crate::client::Transport;
use crate::fake::FakeDevice;
use crate::protocol::FACTORY;

/// Fake device reachable by both the client and the test, with a cable that can be cut.
struct Tap {
    dev: Arc<Mutex<FakeDevice>>,
    cut: Arc<AtomicBool>,
}

impl Transport for Tap {
    fn write_line(&mut self, line: &str) -> Res<()> {
        if self.cut.load(Ordering::SeqCst) {
            return Err(CalError::Io("cable cut".into()));
        }
        self.dev.lock().unwrap().write_line(line)
    }
    fn read_line(&mut self, timeout: Duration) -> Res<Option<String>> {
        if self.cut.load(Ordering::SeqCst) {
            return Err(CalError::Io("cable cut".into()));
        }
        self.dev.lock().unwrap().read_line(timeout)
    }
}

struct Rig {
    ctx: Ctx,
    dev: Arc<Mutex<FakeDevice>>,
    cut: Arc<AtomicBool>,
}

fn fast_dev(true_coast: i64) -> FakeDevice {
    let mut d = FakeDevice::new(true_coast);
    d.speed = 100_000.0;
    d.settle = Duration::from_millis(50);
    d.ramp = Duration::from_millis(200);
    d
}

fn rig(d: FakeDevice) -> Rig {
    let dev = Arc::new(Mutex::new(d));
    let cut = Arc::new(AtomicBool::new(false));
    let client = Client::new(Box::new(Tap {
        dev: dev.clone(),
        cut: cut.clone(),
    }));
    let ctx = Ctx {
        client: Arc::new(Mutex::new(client)),
        auto: Auto::new(),
        runs: Runs::default(),
        notify: Arc::new(|| {}),
        cfg: Cfg {
            base_pulses: 80_000,
            pause: Duration::from_millis(100),
            run_timeout: Duration::from_secs(20),
            human_timeout: Duration::from_secs(20),
            max_attempts: 8,
            tail_ms: 150,
        },
    };
    Rig { ctx, dev, cut }
}

fn wait_for(ctx: &Ctx, what: &str, f: impl Fn(&AutoView) -> bool) -> AutoView {
    let end = Instant::now() + Duration::from_secs(30);
    loop {
        let v = ctx.auto.view();
        if f(&v) {
            return v;
        }
        assert!(
            Instant::now() < end,
            "timed out waiting for {what}; at {:?}",
            v.phase
        );
        std::thread::sleep(Duration::from_millis(20));
    }
}

fn device_quiet(r: &Rig) {
    let mut d = r.dev.lock().unwrap();
    let _ = d.read_line(Duration::from_millis(5));
    assert!(!d.relay_on(), "motor must be off");
    assert!(!d.in_debug(), "Test mode must be off");
}

#[test]
fn full_auto_success_then_save() {
    let r = rig(fast_dev(3000));
    let h = start_auto(&r.ctx).unwrap();
    h.join().unwrap();
    let v = r.ctx.auto.view();
    assert_eq!(v.phase, Phase::ReadyToSave, "{v:?}");
    let res = v.result.unwrap();
    assert!((res.coast_after - 3000).abs() <= 300, "{res:?}");
    assert!(res.residual_pulses.unwrap().abs() <= 400, "{res:?}");
    // The Rust side decides the verdict, from the stated tolerance.
    assert_eq!(res.verify_residual_pulses, res.residual_pulses);
    assert_eq!(
        res.verified,
        res.residual_pulses.unwrap().abs() <= VERIFY_TOLERANCE_PULSES,
        "{res:?}"
    );
    // The old coast was the factory 2291, so the test winds overshot by about the true
    // follow-through minus it.
    assert!(
        (res.residual_before_pulses.unwrap() - (3000 - FACTORY.coast)).abs() <= 300,
        "{res:?}"
    );
    let runs = r.ctx.runs.lock().unwrap().clone();
    assert!(runs.last().unwrap().verify);
    assert!(runs.iter().filter(|x| x.counts()).count() >= 4);
    device_quiet(&r);
    let (_, series, marks) = r.ctx.auto.render();
    assert!(!series.as_array().unwrap().is_empty() && !marks.as_array().unwrap().is_empty());
    save(&r.ctx).unwrap();
    let v = r.ctx.auto.view();
    assert_eq!(v.phase, Phase::Done);
    assert!(v.result.unwrap().saved);
    assert!(matches!(save(&r.ctx), Err(CalError::Refused(_))));
}

#[test]
fn second_start_is_refused_while_busy() {
    let r = rig(fast_dev(3000));
    let h = start_auto(&r.ctx).unwrap();
    assert!(matches!(start_auto(&r.ctx), Err(CalError::Refused(_))));
    assert!(matches!(
        start_length_check(&r.ctx),
        Err(CalError::Refused(_))
    ));
    r.ctx.auto.request_cancel().unwrap();
    h.join().unwrap();
    assert_eq!(r.ctx.auto.view().phase, Phase::Cancelled);
}

#[test]
fn run_not_up_to_speed_is_redone_longer() {
    let mut d = fast_dev(3000);
    d.ramp = Duration::from_millis(600);
    let mut r = rig(d);
    r.ctx.cfg.base_pulses = 40_000;
    start_auto(&r.ctx).unwrap().join().unwrap();
    let v = r.ctx.auto.view();
    assert_eq!(v.phase, Phase::ReadyToSave, "{v:?}");
    let runs = r.ctx.runs.lock().unwrap().clone();
    assert!(runs.iter().any(|x| x.speed_ok == Some(false)), "{runs:?}");
    assert!(runs.iter().filter(|x| x.counts()).count() >= 3);
}

#[test]
fn disconnect_mid_run_fails_and_the_machine_stops_itself() {
    let mut d = fast_dev(3000);
    d.lease = Duration::from_millis(400);
    let r = rig(d);
    let h = start_auto(&r.ctx).unwrap();
    wait_for(&r.ctx, "running", |v| v.phase == Phase::Running);
    r.cut.store(true, Ordering::SeqCst);
    h.join().unwrap();
    let v = r.ctx.auto.view();
    assert_eq!(v.phase, Phase::Failed);
    assert!(v.reason.unwrap().contains("5 seconds"));
    std::thread::sleep(Duration::from_millis(700));
    device_quiet(&r);
}

#[test]
fn cancel_mid_run_stops_the_motor() {
    let r = rig(fast_dev(3000));
    let h = start_auto(&r.ctx).unwrap();
    wait_for(&r.ctx, "running", |v| v.phase == Phase::Running);
    r.ctx.auto.request_cancel().unwrap();
    h.join().unwrap();
    assert_eq!(r.ctx.auto.view().phase, Phase::Cancelled);
    device_quiet(&r);
}

#[test]
fn machine_ending_test_mode_fails_the_job() {
    let mut d = fast_dev(3000);
    d.lease = Duration::from_millis(300);
    let r = rig(d);
    r.ctx.cl().keepalive_every = Duration::from_secs(10);
    let mut r = r;
    r.ctx.cfg.pause = Duration::from_secs(1);
    start_auto(&r.ctx).unwrap().join().unwrap();
    let v = r.ctx.auto.view();
    assert_eq!(v.phase, Phase::Failed, "{v:?}");
    assert!(v.reason.unwrap().to_lowercase().contains("test mode"));
}

fn to_need_measure(r: &Rig) -> (JoinHandle<()>, f64) {
    let h = start_length_check(&r.ctx).unwrap();
    let v = wait_for(&r.ctx, "need_measure", |v| v.phase == Phase::NeedMeasure);
    (h, v.length_check.unwrap().expected_in.unwrap())
}

#[test]
fn keepalive_holds_test_mode_while_waiting_for_a_measurement() {
    let mut d = fast_dev(0);
    d.lease = Duration::from_millis(600);
    let r = rig(d);
    r.ctx.cl().keepalive_every = Duration::from_millis(100);
    let (h, _) = to_need_measure(&r);
    std::thread::sleep(Duration::from_millis(1500));
    assert_eq!(r.ctx.auto.view().phase, Phase::NeedMeasure);
    assert!(r.dev.lock().unwrap().in_debug());
    r.ctx.auto.offer_skip().unwrap();
    h.join().unwrap();
    assert_eq!(r.ctx.auto.view().phase, Phase::Done);
    device_quiet(&r);
}

#[test]
fn exact_measurement_changes_nothing() {
    let r = rig(fast_dev(0));
    let before = r.ctx.cl().param().unwrap();
    let (h, expected) = to_need_measure(&r);
    r.ctx.auto.offer_measure(expected).unwrap();
    h.join().unwrap();
    let v = r.ctx.auto.view();
    assert_eq!(v.phase, Phase::Done, "{v:?}");
    let note = v.length_check.unwrap().result.unwrap().note.unwrap();
    assert!(note.contains("no change"), "{note}");
    assert!(v.result.is_none(), "no main test ran, so no main result");
    assert_eq!(r.ctx.cl().param().unwrap().wheel_um, before.wheel_um);
}

#[test]
fn two_percent_error_is_proposed_and_applied() {
    let r = rig(fast_dev(0));
    let before = r.ctx.cl().param().unwrap();
    let (h, expected) = to_need_measure(&r);
    r.ctx.auto.offer_measure(expected * 1.02).unwrap();
    let v = wait_for(&r.ctx, "proposal", |v| {
        v.length_check
            .as_ref()
            .is_some_and(|l| l.proposal.is_some())
    });
    assert!((v.length_check.unwrap().proposal.unwrap().percent - 2.0).abs() < 0.2);
    r.ctx.auto.offer_apply().unwrap();
    h.join().unwrap();
    let v = r.ctx.auto.view();
    assert_eq!(v.phase, Phase::ReadyToSave, "{v:?}");
    let after = r.ctx.cl().param().unwrap();
    let ratio = after.wheel_um as f64 / before.wheel_um as f64;
    assert!((ratio - 1.02).abs() < 0.003, "ratio {ratio}");
    assert_eq!(after.coast, before.coast);
    // Discarding restores the earlier wheel size.
    r.ctx.cl().debug(true).unwrap();
    discard(&r.ctx).unwrap();
    assert_eq!(r.ctx.cl().param().unwrap().wheel_um, before.wheel_um);
}

#[test]
fn absurd_measurement_is_rejected_and_skip_cancel_work() {
    let r = rig(fast_dev(0));
    let (h, expected) = to_need_measure(&r);
    assert!(matches!(
        r.ctx.auto.offer_measure(expected * 3.0),
        Err(CalError::Refused(_))
    ));
    assert!(matches!(
        r.ctx.auto.offer_apply(),
        Err(CalError::Refused(_))
    ));
    r.ctx.auto.request_cancel().unwrap();
    h.join().unwrap();
    assert_eq!(r.ctx.auto.view().phase, Phase::Cancelled);
    assert!(matches!(r.ctx.auto.offer_skip(), Err(CalError::Refused(_))));
    device_quiet(&r);
}

use crate::calibrate::NoObserver;

fn debug_on(r: &Rig) {
    r.ctx.cl().debug(true).unwrap();
}

fn motor_off(r: &Rig) {
    let mut d = r.dev.lock().unwrap();
    let _ = d.read_line(Duration::from_millis(5));
    assert!(!d.relay_on(), "motor must be off");
}

#[test]
fn timed_out_wind_aborts_and_leaves_the_relay_off() {
    let mut d = fast_dev(0);
    d.speed = 10.0; // never gets anywhere near the target
    let r = rig(d);
    debug_on(&r);
    let out = run_once_with(
        &r.ctx.client,
        1_000_000,
        Duration::from_millis(400),
        &mut NoObserver,
    );
    assert_eq!(out, Err(CalError::Timeout));
    motor_off(&r);
}

#[test]
fn wind_ended_by_abort_is_an_error_not_a_sample() {
    let mut d = fast_dev(0);
    d.speed = 10_000.0;
    let r = rig(d);
    debug_on(&r);
    let dev = r.dev.clone();
    let t = std::thread::spawn(move || {
        std::thread::sleep(Duration::from_millis(300));
        dev.lock().unwrap().write_line("ABORT").unwrap();
    });
    let out = run_once_with(
        &r.ctx.client,
        1_000_000,
        Duration::from_secs(10),
        &mut NoObserver,
    );
    t.join().unwrap();
    assert_eq!(out, Err(CalError::Cancelled));
    motor_off(&r);
}

#[test]
fn backwards_counting_encoder_is_refused() {
    let mut d = fast_dev(500);
    d.reversed = true; // A and B swapped: GET count runs negative while winding
    let r = rig(d);
    debug_on(&r);
    let out = run_once_with(&r.ctx.client, 5000, Duration::from_secs(5), &mut NoObserver);
    assert!(matches!(out, Err(CalError::Refused(_))), "{out:?}");
}

#[test]
fn length_check_keeps_the_main_result_and_plot() {
    let r = rig(fast_dev(3000));
    start_auto(&r.ctx).unwrap().join().unwrap();
    let main = r.ctx.auto.view();
    assert_eq!(main.phase, Phase::ReadyToSave);
    let main_result = main.result.clone().unwrap();
    let (_, main_series, main_marks) = r.ctx.auto.render();
    let runs_before = r.ctx.runs.lock().unwrap().len();
    // Start the optional check while the main result is still unsaved.
    let (h, _) = to_need_measure(&r);
    r.ctx.auto.offer_skip().unwrap();
    h.join().unwrap();
    let v = r.ctx.auto.view();
    // The main result survives and can still be saved.
    assert_eq!(v.phase, Phase::ReadyToSave, "{v:?}");
    let kept = v.result.clone().unwrap();
    assert_eq!(kept.coast_after, main_result.coast_after);
    assert_eq!(kept.verified, main_result.verified);
    let (_, series, marks) = r.ctx.auto.render();
    assert_eq!(series, main_series, "top-level series is the main test's");
    assert_eq!(marks, main_marks);
    assert_eq!(r.ctx.runs.lock().unwrap().len(), runs_before);
    // The check has its own plot and result.
    let lc = v.length_check.unwrap();
    assert!(!lc.series.is_empty() && !lc.marks.is_empty());
    assert!(lc.result.unwrap().note.unwrap().contains("Skipped"));
    save(&r.ctx).unwrap();
    assert!(r.ctx.auto.view().result.unwrap().saved);
}

#[test]
fn save_refuses_when_the_machine_no_longer_holds_the_reviewed_numbers() {
    let r = rig(fast_dev(3000));
    start_auto(&r.ctx).unwrap().join().unwrap();
    assert_eq!(r.ctx.auto.view().phase, Phase::ReadyToSave);
    // The machine was reset before the operator pressed Save.
    r.ctx.cl().command(&Cmd::Defaults).unwrap();
    assert!(matches!(save(&r.ctx), Err(CalError::Refused(_))));
    let v = r.ctx.auto.view();
    assert_eq!(v.phase, Phase::Failed);
    assert!(!v.result.unwrap().saved);
    assert!(matches!(save(&r.ctx), Err(CalError::Refused(_))));
}

#[test]
fn discard_restores_hand_set_lengths_not_re_derived_ones() {
    let r = rig(fast_dev(0));
    r.ctx.cl().set("short", 210_000).unwrap();
    let before = r.ctx.cl().param().unwrap();
    assert_eq!(before.short, 210_000);
    let (h, expected) = to_need_measure(&r);
    r.ctx.auto.offer_measure(expected * 1.02).unwrap();
    wait_for(&r.ctx, "proposal", |v| {
        v.length_check
            .as_ref()
            .is_some_and(|l| l.proposal.is_some())
    });
    r.ctx.auto.offer_apply().unwrap();
    h.join().unwrap();
    let changed = r.ctx.cl().param().unwrap();
    assert_ne!(
        changed.short, 210_000,
        "the wheel SET re-derives the lengths"
    );
    discard(&r.ctx).unwrap();
    assert_eq!(r.ctx.cl().param().unwrap(), before);
}

#[test]
fn manual_slot_and_job_slot_exclude_each_other() {
    let r = rig(fast_dev(0));
    r.ctx.auto.claim_manual().unwrap();
    assert!(matches!(
        r.ctx.auto.claim_manual(),
        Err(CalError::Refused(_))
    ));
    assert!(matches!(start_auto(&r.ctx), Err(CalError::Refused(_))));
    assert!(matches!(
        start_length_check(&r.ctx),
        Err(CalError::Refused(_))
    ));
    assert!(r.ctx.auto.busy());
    r.ctx.auto.release_manual();
    assert!(!r.ctx.auto.busy());
    let h = start_auto(&r.ctx).unwrap();
    assert!(matches!(
        r.ctx.auto.claim_manual(),
        Err(CalError::Refused(_))
    ));
    r.ctx.auto.request_cancel().unwrap();
    h.join().unwrap();
}
