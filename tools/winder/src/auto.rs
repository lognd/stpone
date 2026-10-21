//! Automatic calibration jobs: the hands-off coast tuning and the optional length check.

use std::collections::{HashSet, VecDeque};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Condvar, Mutex, MutexGuard};
use std::thread::{self, sleep, JoinHandle};
use std::time::{Duration, Instant};

use serde::Serialize;
use serde_json::{json, Value};

use crate::calibrate::{
    coast_stats, inches_for_pulses, pulses_for_inches, residual, restore_params, run_once_with,
    wheel_after_check, Observer, RunRecord, Sample, MIN_VALID_RUNS, TAIL_MS,
};
use crate::client::{Client, Shared};
use crate::error::{CalError, Res};
use crate::protocol::{Cmd, Event, Params, RUN_MAX};

/// Runs shared between the job and the UI model.
pub type Runs = Arc<Mutex<Vec<RunRecord>>>;
/// Called after every visible change so the UI can refresh.
pub type Notify = Arc<dyn Fn() + Send + Sync>;

const SERIES_CAP: usize = 600;
const MARKS_CAP: usize = 100;
/// Valid winds wanted before the coast is computed.
const NEEDED: usize = 4;
/// Length of the optional length-check wind, in inches.
const CHECK_INCHES: f64 = 36.0;
/// Corrections smaller than this (percent) are not worth applying.
const NOT_WORTH_PCT: f64 = 0.2;
/// Largest correction (percent) accepted as a believable measurement.
const MAX_CORRECTION_PCT: f64 = 10.0;
/// The new coast counts as verified when the check wind lands within this many pulses
/// of its target (at 2400 counts per turn of a 4 inch wheel, 20 pulses is about 0.1 inch).
pub const VERIFY_TOLERANCE_PULSES: i64 = 20;

/// Run-up (pulses, one turn of the encoder) a debug test wind needs beyond the coast: the firmware
/// cuts a RUN at `length - coast`, so a length barely above the coast would never reach full speed
/// (the factory coast is 2291 pulses, DEC-010).
const RUN_UP_PULSES: i64 = 2400;

/// Shortest test wind worth making with this coast.
fn min_test_len(coast: i64) -> i64 {
    coast + RUN_UP_PULSES
}

/// Where a job is, as the browser sees it.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Phase {
    Idle,
    Preparing,
    Running,
    Settling,
    NeedMeasure,
    Computing,
    Verifying,
    ReadyToSave,
    Done,
    Failed,
    Cancelled,
}

impl Phase {
    /// True while a job thread is working (the poller stays off the wire).
    pub fn busy(self) -> bool {
        matches!(
            self,
            Phase::Preparing
                | Phase::Running
                | Phase::Settling
                | Phase::NeedMeasure
                | Phase::Computing
                | Phase::Verifying
        )
    }
}

/// Correction proposed after a length measurement.
#[derive(Clone, Debug, Serialize)]
pub struct Proposal {
    pub measured_in: f64,
    pub new_wheel_um: i64,
    pub percent: f64,
}

/// The length-check step: what the machine thinks it fed, and what the user reported.
#[derive(Clone, Debug, Serialize)]
pub struct LengthCheck {
    pub expected_in: Option<f64>,
    pub pulses: i64,
    pub proposal: Option<Proposal>,
    /// This check's own outcome; the main test's `auto.result` is left alone.
    pub result: Option<AutoResult>,
    /// This check's own plot (the top-level `series`/`marks` stay the main test's).
    pub series: VecDeque<SeriesPt>,
    pub marks: VecDeque<Mark>,
}

impl LengthCheck {
    fn empty() -> LengthCheck {
        LengthCheck {
            expected_in: None,
            pulses: 0,
            proposal: None,
            result: None,
            series: VecDeque::new(),
            marks: VecDeque::new(),
        }
    }
}

/// What a finished job changed.
#[derive(Clone, Debug, Default, Serialize)]
pub struct AutoResult {
    pub coast_before: i64,
    pub coast_after: i64,
    pub wheel_um_before: i64,
    pub wheel_um_after: i64,
    pub spread_pct: Option<f64>,
    /// Signed miss (final - target) of the check wind with the new coast; None when no
    /// check wind reached full speed. Same value as `verify_residual_pulses`.
    pub residual_pulses: Option<i64>,
    /// Same as `residual_pulses` (the explicit name).
    pub verify_residual_pulses: Option<i64>,
    /// Mean signed miss of the test winds made with the OLD coast (the "before" bar).
    pub residual_before_pulses: Option<i64>,
    /// True only when a check wind ran and |miss| <= `VERIFY_TOLERANCE_PULSES`.
    pub verified: bool,
    pub saved: bool,
    pub changed: bool,
    pub note: Option<String>,
}

/// The `auto` object of the UI state.
#[derive(Clone, Debug, Serialize)]
pub struct AutoView {
    pub phase: Phase,
    pub step: usize,
    pub of: usize,
    pub attempt: usize,
    pub valid_runs: usize,
    pub needed: usize,
    pub message: String,
    pub reason: Option<String>,
    pub result: Option<AutoResult>,
    pub length_check: Option<LengthCheck>,
}

impl AutoView {
    fn idle() -> AutoView {
        AutoView {
            phase: Phase::Idle,
            step: 0,
            of: 0,
            attempt: 0,
            valid_runs: 0,
            needed: NEEDED,
            message: String::new(),
            reason: None,
            result: None,
            length_check: None,
        }
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct SeriesPt {
    t_ms: u64,
    count: i64,
    speed: Option<u32>,
    relay: u8,
    run: u8,
}

#[derive(Clone, Debug, Serialize)]
pub struct Mark {
    t_ms: u64,
    kind: String,
    count: i64,
}

#[derive(Clone, Copy, Debug)]
enum Human {
    Measured(f64),
    Apply,
    Skip,
}

struct AutoState {
    view: AutoView,
    series: VecDeque<SeriesPt>,
    marks: VecDeque<Mark>,
    epoch: Instant,
    human: Option<Human>,
    /// Parameters before the first unsaved change, to undo a discarded result.
    undo: Option<Params>,
    /// (wheel_um, coast) the machine must still hold for the pending Save to be the reviewed result.
    unsaved: Option<(i64, i64)>,
    /// Wheel size the length check ran with.
    lc_wheel: Option<i64>,
    /// Plot origin of the length check.
    lc_epoch: Instant,
    /// A manual test-wind run owns the wire (no job thread, so no phase).
    manual: bool,
}

/// Shared job state: what the UI reads and what the endpoints poke.
pub struct Auto {
    st: Mutex<AutoState>,
    cv: Condvar,
    cancel: AtomicBool,
}

impl Auto {
    /// A fresh, idle job slot.
    pub fn new() -> Arc<Auto> {
        Arc::new(Auto {
            st: Mutex::new(AutoState {
                view: AutoView::idle(),
                series: VecDeque::new(),
                marks: VecDeque::new(),
                epoch: Instant::now(),
                human: None,
                undo: None,
                unsaved: None,
                lc_wheel: None,
                lc_epoch: Instant::now(),
                manual: false,
            }),
            cv: Condvar::new(),
            cancel: AtomicBool::new(false),
        })
    }

    fn lock(&self) -> MutexGuard<'_, AutoState> {
        self.st.lock().expect("auto lock poisoned")
    }

    /// A copy of the current view.
    pub fn view(&self) -> AutoView {
        self.lock().view.clone()
    }

    /// True while a job thread or a manual run is working: one slot for both.
    pub fn busy(&self) -> bool {
        let s = self.lock();
        s.view.phase.busy() || s.manual
    }

    /// True while a manual test-wind run holds the slot.
    pub fn manual_active(&self) -> bool {
        self.lock().manual
    }

    /// Take the single job slot for a manual run (check and set under one lock).
    pub fn claim_manual(&self) -> Res<()> {
        let mut s = self.lock();
        if s.view.phase.busy() || s.manual {
            return Err(CalError::Refused(
                "A calibration or test wind is already running. Wait for it to finish or press Stop first.".into(),
            ));
        }
        s.manual = true;
        self.cancel.store(false, Ordering::SeqCst);
        log::info!("manual run claimed the job slot");
        Ok(())
    }

    /// Give the manual run's slot back.
    pub fn release_manual(&self) {
        self.lock().manual = false;
        log::info!("manual run released the job slot");
    }

    /// True once Stop/Cancel was pressed for the running job or manual run.
    pub fn cancel_requested(&self) -> bool {
        self.cancel.load(Ordering::SeqCst)
    }

    /// (step, of) for the banner while a job runs.
    pub fn steps(&self) -> Option<(usize, usize)> {
        let s = self.lock();
        s.view
            .phase
            .busy()
            .then(|| (s.view.step.max(1), s.view.of.max(1)))
    }

    /// Start a fresh main plot (manual runs use this too).
    pub fn reset_series(&self) {
        let mut s = self.lock();
        s.series.clear();
        s.marks.clear();
        s.epoch = Instant::now();
    }

    /// JSON for `auto`, `series` and `marks`.
    pub fn render(&self) -> (Value, Value, Value) {
        let s = self.lock();
        (json!(s.view), json!(s.series), json!(s.marks))
    }

    /// Claim the slot for a job. A length check keeps the main test's result, plot and
    /// undo record (the optional step comes after it); a main test starts everything afresh.
    fn begin(&self, step_of: usize, message: &str, length_check: bool) -> Res<()> {
        let mut s = self.lock();
        if s.view.phase.busy() || s.manual {
            return Err(CalError::Refused(
                "A calibration is already running. Wait for it to finish or press Cancel.".into(),
            ));
        }
        if length_check {
            s.view.phase = Phase::Preparing;
            s.view.reason = None;
            s.view.length_check = Some(LengthCheck::empty());
            s.lc_wheel = None;
            s.lc_epoch = Instant::now();
        } else {
            s.view = AutoView::idle();
            s.view.phase = Phase::Preparing;
            s.undo = None;
            s.unsaved = None;
            s.lc_wheel = None;
            s.series.clear();
            s.marks.clear();
            s.epoch = Instant::now();
        }
        s.view.of = step_of;
        s.view.step = 1;
        s.view.message = message.into();
        s.human = None;
        self.cancel.store(false, Ordering::SeqCst);
        Ok(())
    }

    /// Ask the running job to stop; it winds down and ends in `cancelled`.
    pub fn request_cancel(&self) -> Res<()> {
        if !self.busy() {
            return Err(CalError::Refused(
                "Nothing is running, so there is nothing to cancel.".into(),
            ));
        }
        log::warn!("cancel requested");
        self.cancel.store(true, Ordering::SeqCst);
        self.cv.notify_all();
        Ok(())
    }

    fn need_measure_state(&self) -> Res<MutexGuard<'_, AutoState>> {
        let s = self.lock();
        if s.view.phase == Phase::NeedMeasure {
            Ok(s)
        } else {
            Err(CalError::Refused(
                "The winder is not waiting for a measurement right now.".into(),
            ))
        }
    }

    /// The user measured the paper: validate it and hand it to the job.
    pub fn offer_measure(&self, inches: f64) -> Res<()> {
        let mut s = self.need_measure_state()?;
        let expected = s.view.length_check.as_ref().and_then(|l| l.expected_in);
        let wheel = s.lc_wheel;
        let (Some(expected), Some(wheel)) = (expected, wheel) else {
            return Err(CalError::Refused(
                "The test wind has not finished yet.".into(),
            ));
        };
        let new = wheel_after_check(wheel, expected, inches)?;
        let pct = 100.0 * (new as f64 / wheel as f64 - 1.0);
        if pct.abs() > MAX_CORRECTION_PCT {
            return Err(CalError::Refused(format!(
                "That does not look right: the winder expected about {expected:.1} inches, but you entered {inches:.1}. Measure again from where the paper started."
            )));
        }
        s.human = Some(Human::Measured(inches));
        self.cv.notify_all();
        Ok(())
    }

    /// The user confirmed the proposed wheel correction.
    pub fn offer_apply(&self) -> Res<()> {
        let mut s = self.need_measure_state()?;
        if s.view
            .length_check
            .as_ref()
            .and_then(|l| l.proposal.as_ref())
            .is_none()
        {
            return Err(CalError::Refused("Enter the measured length first.".into()));
        }
        s.human = Some(Human::Apply);
        self.cv.notify_all();
        Ok(())
    }

    /// The user skipped the measurement; nothing changes.
    pub fn offer_skip(&self) -> Res<()> {
        let mut s = self.need_measure_state()?;
        s.human = Some(Human::Skip);
        self.cv.notify_all();
        Ok(())
    }
}

/// Tunables; tests shorten them.
#[derive(Clone, Debug)]
pub struct Cfg {
    pub base_pulses: i64,
    pub pause: Duration,
    pub run_timeout: Duration,
    pub human_timeout: Duration,
    pub max_attempts: usize,
    pub tail_ms: u64,
}

impl Default for Cfg {
    fn default() -> Cfg {
        Cfg {
            base_pulses: 50_000,
            pause: Duration::from_secs(3),
            run_timeout: Duration::from_secs(120),
            human_timeout: Duration::from_secs(30 * 60),
            max_attempts: 8,
            tail_ms: TAIL_MS,
        }
    }
}

/// Everything a job thread needs.
#[derive(Clone)]
pub struct Ctx {
    pub client: Shared,
    pub auto: Arc<Auto>,
    pub runs: Runs,
    pub notify: Notify,
    pub cfg: Cfg,
}

impl Ctx {
    fn upd(&self, f: impl FnOnce(&mut AutoState)) {
        f(&mut self.auto.lock());
        (self.notify)();
    }

    fn phase(&self, p: Phase, msg: &str) {
        log::info!("auto: {p:?}: {msg}");
        self.upd(|s| {
            s.view.phase = p;
            s.view.message = msg.into();
        });
    }

    fn cl(&self) -> MutexGuard<'_, Client> {
        self.client.lock().expect("client lock poisoned")
    }

    fn cancelled(&self) -> bool {
        self.auto.cancel_requested()
    }

    /// Keep the lease alive and notice the machine ending Test mode.
    fn hold(&self) -> Res<()> {
        let mut c = self.cl();
        c.pump(Duration::from_millis(5))?;
        c.keepalive()?;
        if let Some(i) = c
            .events
            .iter()
            .position(|e| matches!(e, Event::DebugExit { .. }))
        {
            if let Some(Event::DebugExit { reason }) = c.events.remove(i) {
                return Err(CalError::DebugEnded(reason));
            }
        }
        Ok(())
    }

    fn pause(&self, d: Duration) -> Res<()> {
        let end = Instant::now() + d;
        while Instant::now() < end {
            if self.cancelled() {
                return Err(CalError::Cancelled);
            }
            self.hold()?;
            sleep(Duration::from_millis(40));
        }
        Ok(())
    }

    /// Best effort: motor off, Test mode off. True only when both were confirmed.
    pub fn cleanup(&self) -> bool {
        let mut c = self.cl();
        match c.command(&Cmd::Abort) {
            Ok(_) | Err(CalError::BadState) => {}
            Err(e) => {
                log::error!("cleanup: ABORT not confirmed ({e}); the winder will stop itself");
                return false;
            }
        }
        // The relay is off after ABORT; make it explicit (refused outside Test mode, which is fine).
        let _ = c.command(&Cmd::Relay(false));
        if let Err(e) = c.debug(false) {
            log::error!("cleanup: could not leave Test mode: {e}");
            return false;
        }
        true
    }

    /// Best effort: motor off (ABORT, then RELAY 0), staying in Test mode.
    pub fn stop_motor(&self) {
        let mut c = self.cl();
        if let Err(e) = c.command(&Cmd::Abort) {
            if e != CalError::BadState {
                log::error!("stop: ABORT not confirmed: {e}");
            }
        }
        let _ = c.command(&Cmd::Relay(false));
    }

    /// One recorded wind with live sampling.
    fn one_run(&self, pulses: i64, verify: bool, msg: &str) -> Res<RunRecord> {
        self.phase(Phase::Running, msg);
        let mut obs = JobObs {
            ctx: self,
            phased: true,
            lc: false,
        };
        let mut r = run_once_with(&self.client, pulses, self.cfg.run_timeout, &mut obs)?;
        r.verify = verify;
        {
            let mut runs = self.runs.lock().expect("runs lock poisoned");
            r.index = runs.len();
            runs.push(r.clone());
        }
        (self.notify)();
        Ok(r)
    }
}

/// Feeds the plot and, for jobs, moves the phase.
pub struct JobObs<'a> {
    ctx: &'a Ctx,
    /// Moves the job's phase on stop (false for manual runs, which have no phases).
    phased: bool,
    /// Plot into the length check's own series instead of the main one.
    lc: bool,
}

impl<'a> JobObs<'a> {
    /// An observer for manual runs: plots and honours Stop, but has no phases.
    pub fn plot_only(ctx: &'a Ctx) -> JobObs<'a> {
        JobObs {
            ctx,
            phased: false,
            lc: false,
        }
    }
}

impl Observer for JobObs<'_> {
    fn sample(&mut self, s: &Sample) {
        let lc = self.lc;
        self.ctx.upd(|st| {
            let epoch = if lc { st.lc_epoch } else { st.epoch };
            let t_ms = s.at.saturating_duration_since(epoch).as_millis() as u64;
            let pt = SeriesPt {
                t_ms,
                count: s.count,
                speed: s.speed,
                relay: s.relay as u8,
                run: s.run as u8,
            };
            let series = match (lc, st.view.length_check.as_mut()) {
                (true, Some(l)) => &mut l.series,
                _ => &mut st.series,
            };
            if series.len() >= SERIES_CAP {
                series.pop_front();
            }
            series.push_back(pt);
        });
    }

    fn mark(&mut self, kind: &str, count: i64) {
        let (phased, lc) = (self.phased, self.lc);
        self.ctx.upd(|st| {
            let epoch = if lc { st.lc_epoch } else { st.epoch };
            let m = Mark {
                t_ms: epoch.elapsed().as_millis() as u64,
                kind: kind.into(),
                count,
            };
            let marks = match (lc, st.view.length_check.as_mut()) {
                (true, Some(l)) => &mut l.marks,
                _ => &mut st.marks,
            };
            if marks.len() >= MARKS_CAP {
                marks.pop_front();
            }
            marks.push_back(m);
            if phased && kind == "stop" {
                st.view.phase = Phase::Settling;
                st.view.message = "Waiting for the paper to stop moving...".into();
            }
        });
    }

    fn cancelled(&self) -> bool {
        self.ctx.cancelled()
    }

    fn tail_ms(&self) -> u64 {
        self.ctx.cfg.tail_ms
    }
}

fn link_note(e: &CalError) -> &'static str {
    match e {
        CalError::Io(_) | CalError::Timeout => {
            " The winder stops its motor by itself within 5 seconds when it hears nothing from this app."
        }
        _ => "",
    }
}

/// Leave Test mode, then publish the outcome. `length_check` jobs file their result under
/// `auto.length_check.result` and leave the main test's `auto.result` alone.
fn finish(ctx: &Ctx, out: Res<AutoResult>, length_check: bool) {
    let cleaned = ctx.cleanup();
    match out {
        Ok(res) => {
            log::info!("auto finished: {res:?}");
            ctx.upd(|s| {
                if res.changed {
                    s.unsaved = Some((res.wheel_um_after, res.coast_after));
                }
                let (phase, msg) = if s.unsaved.is_some() {
                    (
                        Phase::ReadyToSave,
                        "Done. Review the numbers, then press Save to keep them on the winder.",
                    )
                } else {
                    (Phase::Done, "Done. Nothing needed changing.")
                };
                s.view.phase = phase;
                s.view.message = msg.into();
                match (length_check, s.view.length_check.as_mut()) {
                    (true, Some(l)) => l.result = Some(res),
                    _ => s.view.result = Some(res),
                }
            });
        }
        Err(CalError::Cancelled) => ctx.upd(|s| {
            s.view.phase = Phase::Cancelled;
            s.view.message = if cleaned {
                "Cancelled. The motor is off and Test mode is off.".into()
            } else {
                "Cancelled, but the winder did not confirm it stopped. It stops its own motor within 5 seconds when it hears nothing from this app; switch it off at the machine if the motor is still running.".into()
            };
            s.view.reason = None;
        }),
        Err(e) => {
            log::error!("auto failed: {e}");
            let reason = format!("{}{}", e.human(), link_note(&e));
            ctx.upd(|s| {
                s.view.phase = Phase::Failed;
                s.view.message = "Calibration stopped.".into();
                s.view.reason = Some(reason);
            });
        }
    }
}

/// Start the hands-off coast calibration on a background thread.
pub fn start_auto(ctx: &Ctx) -> Res<JoinHandle<()>> {
    ctx.auto
        .begin(ctx.cfg.max_attempts, "Turning on Test mode...", false)?;
    ctx.runs.lock().expect("runs lock poisoned").clear();
    (ctx.notify)();
    let c = ctx.clone();
    Ok(thread::spawn(move || {
        let out = auto_body(&c);
        finish(&c, out, false);
    }))
}

/// Start the optional length check on a background thread.
pub fn start_length_check(ctx: &Ctx) -> Res<JoinHandle<()>> {
    ctx.auto.begin(3, "Turning on Test mode...", true)?;
    (ctx.notify)();
    let c = ctx.clone();
    Ok(thread::spawn(move || {
        let out = length_body(&c);
        finish(&c, out, true);
    }))
}

fn auto_body(ctx: &Ctx) -> Res<AutoResult> {
    let cfg = &ctx.cfg;
    ctx.cl().debug(true)?;
    let before = ctx.cl().param()?;
    ctx.upd(|s| s.undo = Some(before.clone()));
    let cap = before.long.min(RUN_MAX);
    let min_len = min_test_len(before.coast);
    if cap < min_len {
        return Err(CalError::Refused(
            "The winder's settings are inconsistent (the coast is not shorter than the long length the test winds are limited to). Use Reset to factory settings first.".into(),
        ));
    }
    let (mut valid, mut attempt, mut scale) = (0usize, 0usize, 1i64);
    let mut lengths = HashSet::new();
    while attempt < cfg.max_attempts && !(valid >= NEEDED && lengths.len() >= 2) {
        if attempt > 0 {
            ctx.pause(cfg.pause)?;
        }
        if ctx.cancelled() {
            return Err(CalError::Cancelled);
        }
        attempt += 1;
        let want = cfg.base_pulses * scale * if attempt % 2 == 1 { 1 } else { 2 };
        let len = want.min(cap).max(min_len);
        ctx.upd(|s| {
            s.view.attempt = attempt;
            s.view.step = attempt;
        });
        let r = ctx.one_run(
            len,
            false,
            &format!("Test wind {attempt}: {len} pulses. Do not touch the machine."),
        )?;
        if r.speed_ok == Some(false) {
            scale = (scale * 2).min(64);
            log::warn!("wind {attempt} not up to speed; scale now {scale}");
            ctx.upd(|s| {
                s.view.message =
                    "That wind was too short to reach full speed; trying a longer one.".into()
            });
        } else {
            valid += 1;
            lengths.insert(r.target);
        }
        ctx.upd(|s| s.view.valid_runs = valid);
    }
    if valid < MIN_VALID_RUNS {
        return Err(CalError::Refused(format!(
            "Only {valid} of the test winds were usable (at least {MIN_VALID_RUNS} are needed). Check that the paper feeds freely and the wheel touches it, then try again."
        )));
    }
    ctx.phase(Phase::Computing, "Working out the coast...");
    let recs = ctx.runs.lock().expect("runs lock poisoned").clone();
    let stats = coast_stats(&recs);
    let coast = stats
        .coast
        .ok_or_else(|| CalError::Refused(stats.refused.clone().unwrap_or_default()))?;
    let residual_before = mean_residual(&recs);
    let checked = apply_and_verify(ctx, coast, scale, cap);
    match checked {
        Ok((after, miss)) => {
            let verified = miss.is_some_and(|m| m.abs() <= VERIFY_TOLERANCE_PULSES);
            let unverified_note = miss.is_none().then_some(
                "The final check wind never reached full speed, so the new coast was not verified.",
            );
            let note = match (stats.warning, unverified_note) {
                (Some(w), Some(u)) => Some(format!("{w} {u}")),
                (w, u) => w.or(u.map(String::from)),
            };
            Ok(AutoResult {
                coast_before: before.coast,
                coast_after: after.coast,
                wheel_um_before: before.wheel_um,
                wheel_um_after: after.wheel_um,
                spread_pct: Some(stats.spread_pct),
                residual_pulses: miss,
                verify_residual_pulses: miss,
                residual_before_pulses: residual_before,
                verified,
                saved: false,
                changed: true,
                note,
            })
        }
        Err(e) => {
            log::error!("apply failed ({e}); restoring {before:?}");
            if let Err(re) = restore_params(&mut ctx.cl(), &before) {
                log::error!("restore failed: {re}");
            }
            Err(e)
        }
    }
}

/// Mean signed miss (final - target) of the winds that count, i.e. before the fix.
fn mean_residual(recs: &[RunRecord]) -> Option<i64> {
    let misses: Vec<i64> = recs
        .iter()
        .filter(|r| r.counts())
        .map(|r| residual(r.final_count, r.target))
        .collect();
    (!misses.is_empty())
        .then(|| (misses.iter().sum::<i64>() as f64 / misses.len() as f64).round() as i64)
}

fn apply_and_verify(ctx: &Ctx, coast: i64, scale: i64, cap: i64) -> Res<(Params, Option<i64>)> {
    ctx.phase(Phase::Computing, "Setting the new coast...");
    let after = {
        let mut c = ctx.cl();
        c.set("coast", coast)?;
        c.param()?
    };
    if after.coast != coast {
        return Err(CalError::Malformed(format!(
            "coast reads back {} after setting {coast}",
            after.coast
        )));
    }
    let mut len = (ctx.cfg.base_pulses * scale)
        .min(cap)
        .max(min_test_len(coast));
    let mut miss = None;
    for tryno in 0..2 {
        ctx.pause(ctx.cfg.pause)?;
        ctx.phase(
            Phase::Verifying,
            "Checking the new coast with one more wind...",
        );
        let r = ctx.one_run(len, true, "Checking the new coast with one more wind...")?;
        if r.speed_ok != Some(false) {
            miss = Some(residual(r.final_count, r.target));
            break;
        }
        log::warn!("verification wind {tryno} not up to speed");
        len = (len * 2).min(cap);
    }
    Ok((after, miss))
}

fn length_body(ctx: &Ctx) -> Res<AutoResult> {
    ctx.cl().debug(true)?;
    let p = ctx.cl().param()?;
    // Keep the earlier undo record when a main result is still waiting for Save.
    ctx.upd(|s| {
        if s.unsaved.is_none() {
            s.undo = Some(p.clone());
        }
    });
    let pulses = pulses_for_inches(&p, CHECK_INCHES);
    if pulses <= p.coast || pulses > RUN_MAX {
        return Err(CalError::Refused(
            "A 36 inch test wind does not fit the winder's current settings. Use Reset to factory settings first.".into(),
        ));
    }
    ctx.upd(|s| {
        if let Some(l) = s.view.length_check.as_mut() {
            l.pulses = pulses;
        }
        s.lc_wheel = Some(p.wheel_um);
    });
    let rec = ctx.one_run_unrecorded(pulses)?;
    let expected = inches_for_pulses(&p, rec.final_count);
    ctx.upd(|s| {
        s.view.phase = Phase::NeedMeasure;
        s.view.step = 2;
        s.view.message = format!(
            "Measure the paper that came out, from where it started to where it is now. The winder expects about {expected:.1} inches."
        );
        if let Some(l) = s.view.length_check.as_mut() {
            l.expected_in = Some(expected);
        }
    });
    let deadline = Instant::now() + ctx.cfg.human_timeout;
    let unchanged = |note: String| AutoResult {
        coast_before: p.coast,
        coast_after: p.coast,
        wheel_um_before: p.wheel_um,
        wheel_um_after: p.wheel_um,
        note: Some(note),
        ..AutoResult::default()
    };
    loop {
        if ctx.cancelled() {
            return Err(CalError::Cancelled);
        }
        if Instant::now() >= deadline {
            return Err(CalError::Refused(
                "No measurement was entered within 30 minutes, so the check was stopped.".into(),
            ));
        }
        ctx.hold()?;
        let human = {
            let mut s = ctx.auto.lock();
            if s.human.is_none() {
                s = ctx
                    .auto
                    .cv
                    .wait_timeout(s, Duration::from_millis(150))
                    .expect("auto lock poisoned")
                    .0;
            }
            s.human.take()
        };
        match human {
            None => {}
            Some(Human::Skip) => return Ok(unchanged("Skipped; nothing was changed.".into())),
            Some(Human::Measured(m)) => {
                let new = wheel_after_check(p.wheel_um, expected, m)?;
                let pct = 100.0 * (new as f64 / p.wheel_um as f64 - 1.0);
                if pct.abs() < NOT_WORTH_PCT {
                    return Ok(unchanged(format!(
                        "Your measurement is within {NOT_WORTH_PCT}% of what the winder expected ({pct:+.2}%), so no change is needed."
                    )));
                }
                ctx.upd(|s| {
                    s.view.message = format!(
                        "The winder's length reading is off by {pct:+.2}%. Press Apply to correct it, or Skip to leave it."
                    );
                    if let Some(l) = s.view.length_check.as_mut() {
                        l.proposal = Some(Proposal { measured_in: m, new_wheel_um: new, percent: pct });
                    }
                });
            }
            Some(Human::Apply) => {
                let new = ctx
                    .auto
                    .lock()
                    .view
                    .length_check
                    .as_ref()
                    .and_then(|l| l.proposal.as_ref().map(|pr| pr.new_wheel_um))
                    .ok_or_else(|| CalError::Refused("Enter the measured length first.".into()))?;
                return apply_wheel(ctx, &p, new);
            }
        }
    }
}

fn apply_wheel(ctx: &Ctx, p: &Params, new: i64) -> Res<AutoResult> {
    ctx.phase(Phase::Computing, "Applying the corrected wheel size...");
    let r = (|| {
        let mut c = ctx.cl();
        c.set("wheel_um", new)?;
        // Coast goes last so it is checked against the re-derived lengths.
        c.set("coast", p.coast)?;
        c.param()
    })();
    match r {
        Ok(a) if a.wheel_um == new => Ok(AutoResult {
            coast_before: p.coast,
            coast_after: a.coast,
            wheel_um_before: p.wheel_um,
            wheel_um_after: a.wheel_um,
            changed: true,
            note: Some(
                "Wheel size corrected. Run the automatic coast calibration again, then Save."
                    .into(),
            ),
            ..AutoResult::default()
        }),
        other => {
            let e = other
                .err()
                .unwrap_or_else(|| CalError::Malformed("wheel size did not read back".into()));
            log::error!("apply wheel failed ({e}); restoring");
            if let Err(re) = restore_params(&mut ctx.cl(), p) {
                log::error!("restore failed: {re}");
            }
            Err(e)
        }
    }
}

impl Ctx {
    /// A wind that is plotted but not added to the coast statistics.
    fn one_run_unrecorded(&self, pulses: i64) -> Res<RunRecord> {
        self.phase(
            Phase::Running,
            "Winding about 36 inches. Do not touch the machine.",
        );
        let mut obs = JobObs {
            ctx: self,
            phased: true,
            lc: true,
        };
        run_once_with(&self.client, pulses, self.cfg.run_timeout, &mut obs)
    }
}

/// SAVE the reviewed settings; valid only while a result is waiting, and only if the
/// machine still holds exactly the numbers that were reviewed.
pub fn save(ctx: &Ctx) -> Res<()> {
    let expected = {
        let s = ctx.auto.lock();
        match (s.view.phase.busy() || s.manual, s.unsaved) {
            (false, Some(e)) => e,
            _ => return Err(CalError::Refused("There is nothing to save yet.".into())),
        }
    };
    let now = ctx.cl().param()?;
    if (now.wheel_um, now.coast) != expected {
        log::error!("refusing to save: machine holds {now:?}, reviewed {expected:?}");
        let msg = "The winder's settings changed after you reviewed the result (it was restarted, reset or edited), so nothing was saved. Run the calibration again.";
        ctx.upd(|s| {
            s.unsaved = None;
            s.view.phase = Phase::Failed;
            s.view.message = "Nothing was saved.".into();
            s.view.reason = Some(msg.into());
        });
        return Err(CalError::Refused(msg.into()));
    }
    ctx.cl().save()?;
    ctx.upd(|s| {
        s.unsaved = None;
        s.view.phase = Phase::Done;
        s.view.message = "Saved. The winder will remember these settings.".into();
        if let Some(r) = s.view.result.as_mut() {
            r.saved = true;
        }
        if let Some(r) = s.view.length_check.as_mut().and_then(|l| l.result.as_mut()) {
            r.saved = true;
        }
    });
    Ok(())
}

/// Throw away an unsaved result by restoring the settings from before the job.
pub fn discard(ctx: &Ctx) -> Res<()> {
    let undo = {
        let s = ctx.auto.lock();
        if s.view.phase != Phase::ReadyToSave {
            return Err(CalError::Refused("There is nothing to discard.".into()));
        }
        s.undo.clone()
    };
    if let Some(before) = undo {
        restore_params(&mut ctx.cl(), &before)?;
    }
    ctx.upd(|s| s.unsaved = None);
    ctx.phase(
        Phase::Cancelled,
        "Discarded. The earlier settings are back.",
    );
    Ok(())
}

#[cfg(test)]
#[path = "auto_tests.rs"]
mod tests;
