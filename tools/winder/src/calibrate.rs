//! Calibration analysis (pure) and the guided run against a shared Client.

use std::thread::sleep;
use std::time::{Duration, Instant};

use serde::Serialize;

use crate::client::Shared;
use crate::error::{CalError, Res};
use crate::protocol::{Cmd, Event, Params, UM_PER_INCH, WHEEL_UM_MAX, WHEEL_UM_MIN};

/// A wind is "up to speed" when its last second stays within this fraction of the plateau.
const PLATEAU_FRACTION: f64 = 0.98;
/// Default window before the stop that must sit on the plateau.
pub const TAIL_MS: u64 = 1000;
/// Fewest good winds a coast suggestion may rest on.
pub const MIN_VALID_RUNS: usize = 3;
/// Spread (stddev over mean) above which the winds disagree too much to trust.
const MAX_SPREAD: f64 = 0.25;

/// One test wind: what was asked, what happened, what the operator measured.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct RunRecord {
    pub target: i64,
    pub at_stop: i64,
    #[serde(rename = "final")]
    pub final_count: i64,
    pub follow: i64,
    pub measured_in: Option<f64>,
    /// Position in the job's run list (assigned by the owner of the list).
    pub index: usize,
    /// Highest smoothed speed seen (pulses per second), when the firmware reports speed.
    pub peak_speed: Option<f64>,
    /// Smoothed speed just before the stop.
    pub stop_speed: Option<f64>,
    /// Whether the wind reached its plateau; None when speed is unknown (firmware 3).
    pub speed_ok: Option<bool>,
    /// Plain-English reason the wind is excluded, when it is.
    pub why_invalid: Option<String>,
    /// A verification wind: reported but never fed into the coast statistics.
    pub verify: bool,
}

impl RunRecord {
    /// True when the wind may feed the coast statistics.
    pub fn counts(&self) -> bool {
        !self.verify && self.speed_ok != Some(false)
    }
}

/// One reading taken during a wind.
#[derive(Debug, Clone, Copy)]
pub struct Sample {
    pub at: Instant,
    pub count: i64,
    pub speed: Option<u32>,
    pub relay: bool,
    pub run: bool,
}

/// Watches a wind as it happens and may ask for it to be cancelled.
pub trait Observer {
    /// A reading (about every 100 ms).
    fn sample(&mut self, _s: &Sample) {}
    /// A named moment: "start", "stop" or "settled".
    fn mark(&mut self, _kind: &str, _count: i64) {}
    /// True when the user asked to cancel.
    fn cancelled(&self) -> bool {
        false
    }
    /// How long before the stop the wind must already be at full speed.
    fn tail_ms(&self) -> u64 {
        TAIL_MS
    }
}

/// An observer that ignores everything.
pub struct NoObserver;
impl Observer for NoObserver {}

/// Speed-based verdict on one wind.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct Assessment {
    pub peak: Option<f64>,
    pub stop_speed: Option<f64>,
    pub ok: Option<bool>,
    pub why: Option<String>,
}

/// Judge a wind from (ms since RUN, speed) samples taken before the stop; pure.
pub fn assess(samples: &[(u64, Option<u32>)], tail_ms: u64) -> Assessment {
    let pts: Vec<(u64, f64)> = samples
        .iter()
        .filter_map(|(t, s)| s.map(|s| (*t, s as f64)))
        .collect();
    if pts.is_empty() {
        return Assessment::default();
    }
    if pts.len() < 3 {
        return Assessment {
            ok: Some(false),
            why: Some(
                "Too short: the winder did not get up to speed before it had to stop.".into(),
            ),
            ..Assessment::default()
        };
    }
    let smooth: Vec<(u64, f64)> = (0..pts.len())
        .map(|i| {
            let lo = i.saturating_sub(2);
            let w = &pts[lo..=i];
            (
                pts[i].0,
                w.iter().map(|p| p.1).sum::<f64>() / w.len() as f64,
            )
        })
        .collect();
    let peak = smooth.iter().map(|p| p.1).fold(0.0, f64::max);
    let last_t = smooth.last().map(|p| p.0).unwrap_or(0);
    let tail: Vec<&(u64, f64)> = smooth.iter().filter(|p| p.0 + tail_ms > last_t).collect();
    let stop_speed = tail.last().map(|p| p.1);
    let ok = peak > 0.0 && tail.iter().all(|p| p.1 >= PLATEAU_FRACTION * peak);
    Assessment {
        peak: Some(peak),
        stop_speed,
        ok: Some(ok),
        why: (!ok).then(|| "Too short: the winder was not up to full speed when it stopped, so this wind is not used.".into()),
    }
}

/// Least-squares line follow = a + b * speed.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct Fit {
    pub a: f64,
    pub b: f64,
}

/// Summary of the usable winds and the coast it supports.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct CoastStats {
    pub coast: Option<i64>,
    pub mean: f64,
    pub stddev: f64,
    pub spread_pct: f64,
    pub n: usize,
    pub rejected: usize,
    pub refused: Option<String>,
    pub fit: Option<Fit>,
    pub warning: Option<String>,
}

/// Mean follow-through of the usable winds, refusing when they are too few or too scattered.
pub fn coast_stats(records: &[RunRecord]) -> CoastStats {
    let good: Vec<&RunRecord> = records.iter().filter(|r| r.counts()).collect();
    let rejected = records
        .iter()
        .filter(|r| !r.verify && r.speed_ok == Some(false))
        .count();
    let n = good.len();
    let mean = if n == 0 {
        0.0
    } else {
        good.iter().map(|r| r.follow as f64).sum::<f64>() / n as f64
    };
    let stddev = if n < 2 {
        0.0
    } else {
        (good
            .iter()
            .map(|r| (r.follow as f64 - mean).powi(2))
            .sum::<f64>()
            / (n as f64 - 1.0))
            .sqrt()
    };
    let spread_pct = if mean.abs() > 0.0 {
        100.0 * stddev / mean.abs()
    } else {
        0.0
    };
    let mut out = CoastStats {
        coast: None,
        mean,
        stddev,
        spread_pct,
        n,
        rejected,
        refused: None,
        fit: None,
        warning: None,
    };
    if n < MIN_VALID_RUNS {
        out.refused = Some(format!(
            "Only {n} good test winds so far; at least {MIN_VALID_RUNS} are needed."
        ));
        return out;
    }
    if stddev > MAX_SPREAD * mean.abs() || (mean == 0.0 && stddev > 0.0) {
        out.refused = Some(format!(
            "The test winds disagree too much (they vary by {spread_pct:.0}%). Check that the paper runs freely and the wheel is clean, then try again."
        ));
        return out;
    }
    out.coast = Some(mean.round() as i64);
    let pts: Vec<(f64, f64)> = good
        .iter()
        .filter_map(|r| r.stop_speed.map(|s| (s, r.follow as f64)))
        .collect();
    if pts.len() >= 4 {
        let (lo, hi) = pts
            .iter()
            .fold((f64::MAX, f64::MIN), |(l, h), p| (l.min(p.0), h.max(p.0)));
        if hi > 0.0 && (hi - lo) / hi > 0.05 {
            let m = pts.len() as f64;
            let (sx, sy) = pts.iter().fold((0.0, 0.0), |(x, y), p| (x + p.0, y + p.1));
            let sxx: f64 = pts.iter().map(|p| p.0 * p.0).sum();
            let sxy: f64 = pts.iter().map(|p| p.0 * p.1).sum();
            let den = m * sxx - sx * sx;
            if den.abs() > f64::EPSILON {
                let b = (m * sxy - sx * sy) / den;
                let a = (sy - b * sx) / m;
                out.fit = Some(Fit { a, b });
                let at_max = a + b * hi;
                if mean.abs() > 0.0 && (at_max - mean).abs() > 0.2 * mean.abs() {
                    out.warning = Some(
                        "Coasting distance depends on speed here; the suggestion is an average and may be off at full speed.".into(),
                    );
                }
            }
        }
    }
    log::info!(
        "coast stats: n={n} rejected={rejected} mean={mean:.1} sd={stddev:.1} coast={:?}",
        out.coast
    );
    out
}

/// Pulses that wind `inches` of paper on the wheel and encoder described by `p`.
pub fn pulses_for_inches(p: &Params, inches: f64) -> i64 {
    let wheel_in = p.wheel_um as f64 / UM_PER_INCH;
    (inches * p.cpr as f64 / (std::f64::consts::PI * wheel_in)).round() as i64
}

/// Inches of paper that `pulses` counts represent on the wheel described by `p`.
pub fn inches_for_pulses(p: &Params, pulses: i64) -> f64 {
    pulses as f64 * std::f64::consts::PI * (p.wheel_um as f64 / UM_PER_INCH) / p.cpr as f64
}

/// True for a finite number above zero (NaN and infinity are not lengths).
fn positive(x: f64) -> bool {
    x.is_finite() && x > 0.0
}

/// New wheel_um after a measured length check (expected vs measured inches).
pub fn wheel_after_check(current_um: i64, expected_in: f64, measured_in: f64) -> Res<i64> {
    if !positive(expected_in) || !positive(measured_in) {
        return Err(CalError::BadInput("lengths must be above zero".into()));
    }
    Ok((current_um as f64 * measured_in / expected_in).round() as i64)
}

/// Signed overshoot of a run in pulses; should approach 0.
pub fn residual(final_count: i64, target: i64) -> i64 {
    final_count - target
}

/// Wheel diameter (um) that makes `final_count` counts equal `measured_in` inches.
pub fn suggest_wheel_um(measured_in: f64, final_count: i64, cpr: i64) -> Res<i64> {
    if !positive(measured_in) || final_count == 0 || cpr <= 0 {
        return Err(CalError::BadInput(format!(
            "cannot derive wheel from {measured_in} in, {final_count} counts, cpr {cpr}"
        )));
    }
    let wheel_in = measured_in * cpr as f64 / (std::f64::consts::PI * final_count.abs() as f64);
    Ok((wheel_in * UM_PER_INCH).round() as i64)
}

/// Mean wheel_um suggestion over every record that has a measured length.
pub fn suggest_wheel_from_records(records: &[RunRecord], cpr: i64) -> Res<i64> {
    let s: Vec<i64> = records
        .iter()
        .filter_map(|r| r.measured_in.map(|m| (m, r.final_count)))
        .filter_map(|(m, f)| suggest_wheel_um(m, f, cpr).ok())
        .collect();
    if s.is_empty() {
        return Err(CalError::NoData);
    }
    Ok((s.iter().sum::<i64>() as f64 / s.len() as f64).round() as i64)
}

/// Best effort: stop the motor after a wind that did not end normally. Never fails the caller.
fn stop_wind(client: &Shared, why: &CalError) {
    log::warn!("stopping the motor after a failed wind ({why})");
    let mut c = client.lock().expect("client lock poisoned");
    match c.command(&Cmd::Abort) {
        Ok(_) | Err(CalError::BadState) => {}
        Err(e) => log::error!("could not abort the wind: {e}; the winder stops itself within 5 s"),
    }
}

/// ZERO and RUN `pulses`, sampling at about 10 Hz until the stop and settled events arrive.
///
/// Every failure after the RUN was sent (timeout, link error, cancel, the machine
/// leaving Test mode, an aborted wind) sends ABORT first, so a failed wind never
/// leaves the motor running.
pub fn run_once_with(
    client: &Shared,
    pulses: i64,
    timeout: Duration,
    obs: &mut dyn Observer,
) -> Res<RunRecord> {
    let t0 = Instant::now();
    {
        let mut c = client.lock().expect("client lock poisoned");
        c.events.clear();
        c.zero()?;
        if let Err(e) = c.run(pulses) {
            drop(c);
            // A lost reply may still mean the wind started.
            stop_wind(client, &e);
            return Err(e);
        }
    }
    obs.mark("start", 0);
    let out = follow_wind(client, pulses, timeout, obs, t0);
    if let Err(e) = &out {
        stop_wind(client, e);
    }
    out
}

fn follow_wind(
    client: &Shared,
    pulses: i64,
    timeout: Duration,
    obs: &mut dyn Observer,
    t0: Instant,
) -> Res<RunRecord> {
    let mut series: Vec<(u64, Option<u32>)> = Vec::new();
    let mut stop: Option<i64> = None;
    let mut settled: Option<(i64, i64)> = None;
    let mut counted_backwards = false;
    while settled.is_none() {
        if obs.cancelled() {
            log::warn!("run cancelled by the user");
            return Err(CalError::Cancelled);
        }
        if t0.elapsed() > timeout {
            log::error!("run of {pulses} pulses timed out");
            return Err(CalError::Timeout);
        }
        let sample;
        {
            let mut c = client.lock().expect("client lock poisoned");
            c.pump(Duration::from_millis(2))?;
            let snap = c.get()?;
            // `count` is signed while `at_stop`/`final` are magnitudes: a negative count during
            // a wind is the only sign of an encoder wired the wrong way round.
            counted_backwards |= snap.count < 0;
            sample = Sample {
                at: Instant::now(),
                count: snap.count,
                speed: snap.speed,
                relay: snap.relay,
                run: stop.is_none(),
            };
            while let Some(ev) = c.events.pop_front() {
                match ev {
                    Event::Stop {
                        at_stop, reason, ..
                    } => {
                        if reason != "target" {
                            // Someone stopped the wind (ABORT): never record it as a sample.
                            log::warn!("wind ended early (reason={reason})");
                            return Err(CalError::Cancelled);
                        }
                        stop = Some(at_stop);
                        obs.mark("stop", at_stop);
                    }
                    Event::Settled {
                        final_count,
                        follow,
                    } => {
                        settled = Some((final_count, follow));
                        obs.mark("settled", final_count);
                    }
                    Event::DebugExit { reason } => return Err(CalError::DebugEnded(reason)),
                    _ => {}
                }
            }
        }
        // A reading taken in the same pass as the stop is ambiguous, so drop it.
        if stop.is_none() {
            let s = sample;
            series.push((s.at.duration_since(t0).as_millis() as u64, s.speed));
            obs.sample(&s);
        }
        sleep(Duration::from_millis(if stop.is_some() { 20 } else { 100 }));
    }
    let (final_count, reported_follow) = settled.expect("loop exits only when settled");
    if counted_backwards {
        return Err(CalError::Refused(
            "The measuring wheel counted backwards. Swap the encoder's A and B wires (or the direction setting), then try again.".into(),
        ));
    }
    let at_stop = stop.unwrap_or(final_count - reported_follow);
    let follow = final_count - at_stop;
    if follow != reported_follow {
        log::warn!("settle report follow={reported_follow} differs from final-at_stop={follow}");
    }
    let a = assess(&series, obs.tail_ms());
    log::info!(
        "run {pulses}: at_stop={at_stop} final={final_count} follow={follow} speed_ok={:?}",
        a.ok
    );
    Ok(RunRecord {
        target: pulses,
        at_stop,
        final_count,
        follow,
        measured_in: None,
        index: 0,
        peak_speed: a.peak,
        stop_speed: a.stop_speed,
        speed_ok: a.ok,
        why_invalid: a.why,
        verify: false,
    })
}

/// [`run_once_with`] without an observer.
pub fn run_once(client: &Shared, pulses: i64, timeout: Duration) -> Res<RunRecord> {
    run_once_with(client, pulses, timeout, &mut NoObserver)
}

fn pass(
    client: &Shared,
    lengths: &[i64],
    count: usize,
    verify: bool,
    measure: &dyn Fn(&RunRecord) -> Option<f64>,
) -> Res<Vec<RunRecord>> {
    (0..count)
        .map(|i| {
            let n = lengths[i % lengths.len()];
            let mut r = run_once(client, n, Duration::from_secs(60))?;
            r.index = i;
            r.verify = verify;
            r.measured_in = measure(&r);
            Ok(r)
        })
        .collect()
}

/// Outcome of a guided run.
#[derive(Debug, Serialize)]
pub struct Report {
    pub before: Vec<RunRecord>,
    pub after: Vec<RunRecord>,
    pub coast: i64,
    pub wheel_um: Option<i64>,
    pub residuals: Vec<i64>,
    pub stats: CoastStats,
    pub params: Params,
}

/// Put RAM parameters back to `before`: coast down first (always valid), then wheel,
/// then the lengths the wheel SET re-derived, then the original coast last.
pub fn restore_params(c: &mut crate::client::Client, before: &Params) -> Res<()> {
    let now = c.param()?;
    if now == *before {
        return Ok(());
    }
    log::warn!("restoring parameters {now:?} -> {before:?}");
    c.set("coast", now.coast.min(before.coast))?;
    if now.wheel_um != before.wheel_um {
        c.set("wheel_um", before.wheel_um)?;
    }
    let cur = c.param()?;
    if cur.short != before.short {
        c.set("short", before.short)?;
    }
    if cur.long != before.long {
        c.set("long", before.long)?;
    }
    if cur.coast != before.coast {
        c.set("coast", before.coast)?;
    }
    Ok(())
}

/// The doc's procedure: probe, DEBUG, tune coast, verify, suggest wheel; always leaves DEBUG.
pub fn guided_run(
    client: &Shared,
    lengths: &[i64],
    measure: &dyn Fn(&RunRecord) -> Option<f64>,
    save: bool,
) -> Res<Report> {
    if lengths.is_empty() {
        return Err(CalError::BadInput("no test lengths given".into()));
    }
    {
        let mut c = client.lock().expect("client lock poisoned");
        c.hello()?;
        c.debug(true)?;
    }
    let out = guided_body(client, lengths, measure, save);
    if let Err(e) = client.lock().expect("client lock poisoned").debug(false) {
        log::error!("could not leave debug mode: {e}");
    }
    out
}

/// How many winds the "before" pass makes for these lengths (and so how many `--measured` values).
pub fn guided_count(lengths: &[i64]) -> usize {
    lengths.len().max(MIN_VALID_RUNS)
}

fn guided_body(
    client: &Shared,
    lengths: &[i64],
    measure: &dyn Fn(&RunRecord) -> Option<f64>,
    save: bool,
) -> Res<Report> {
    let orig = client.lock().expect("client lock poisoned").param()?;
    let before = pass(client, lengths, guided_count(lengths), false, measure)?;
    let stats = coast_stats(&before);
    let coast = stats
        .coast
        .ok_or_else(|| CalError::Refused(stats.refused.clone().unwrap_or_default()))?;
    let wheel_um = suggest_wheel_from_records(&before, orig.cpr).ok();
    if let Some(w) = wheel_um {
        if !(WHEEL_UM_MIN..=WHEEL_UM_MAX).contains(&w) {
            return Err(CalError::BadInput(format!(
                "the measured lengths imply a wheel size of {w} um, outside {WHEEL_UM_MIN}..{WHEEL_UM_MAX}; re-measure"
            )));
        }
    }
    let applied = apply_and_check(client, lengths, wheel_um, coast, save);
    match applied {
        Ok((params, after)) => {
            let residuals = after
                .iter()
                .map(|r| residual(r.final_count, r.target))
                .collect();
            Ok(Report {
                before,
                after,
                coast,
                wheel_um,
                residuals,
                stats,
                params,
            })
        }
        Err(e) => {
            // Unverified values must not stay live in RAM.
            let mut c = client.lock().expect("client lock poisoned");
            if let Err(re) = restore_params(&mut c, &orig) {
                log::error!("could not restore the earlier settings: {re}");
            }
            Err(e)
        }
    }
}

fn apply_and_check(
    client: &Shared,
    lengths: &[i64],
    wheel_um: Option<i64>,
    coast: i64,
    save: bool,
) -> Res<(Params, Vec<RunRecord>)> {
    let params = {
        let mut c = client.lock().expect("client lock poisoned");
        if let Some(w) = wheel_um {
            c.set("wheel_um", w)?;
        }
        // Coast goes last so it is validated against the re-derived lengths.
        c.set("coast", coast)?;
        c.param()?
    };
    let after = pass(client, lengths, lengths.len(), true, &|_| None)?;
    if save {
        client.lock().expect("client lock poisoned").save()?;
    }
    Ok((params, after))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::client::Client;
    use crate::fake::FakeDevice;
    use std::sync::{Arc, Mutex};

    fn shared(true_coast: i64) -> Shared {
        let mut d = FakeDevice::new(true_coast);
        d.speed = 1e9;
        d.settle = Duration::ZERO;
        d.emit_speed = false;
        Arc::new(Mutex::new(Client::new(Box::new(d))))
    }

    fn rec(follow: i64, stop_speed: Option<f64>, ok: Option<bool>) -> RunRecord {
        RunRecord {
            target: 1000,
            at_stop: 1000,
            final_count: 1000 + follow,
            follow,
            measured_in: None,
            index: 0,
            peak_speed: stop_speed,
            stop_speed,
            speed_ok: ok,
            why_invalid: None,
            verify: false,
        }
    }

    #[test]
    fn stats_mean_refusals_and_exclusions() {
        let good = [
            rec(100, None, None),
            rec(110, None, None),
            rec(90, None, None),
        ];
        let s = coast_stats(&good);
        assert_eq!((s.coast, s.n, s.rejected), (Some(100), 3, 0));
        assert!((s.stddev - 10.0).abs() < 1e-9 && (s.spread_pct - 10.0).abs() < 1e-9);
        assert!(coast_stats(&good[..2]).refused.is_some());
        let scattered = [
            rec(10, None, None),
            rec(100, None, None),
            rec(190, None, None),
        ];
        assert!(coast_stats(&scattered).coast.is_none());
        let mut mixed = good.to_vec();
        mixed.push(rec(900, None, Some(false)));
        let mut v = rec(5, None, None);
        v.verify = true;
        mixed.push(v);
        let s = coast_stats(&mixed);
        assert_eq!((s.coast, s.n, s.rejected), (Some(100), 3, 1));
    }

    #[test]
    fn stats_fit_and_warning() {
        let runs: Vec<RunRecord> = (0..5)
            .map(|i| rec(100 + 5 * i, Some(1000.0 + 500.0 * i as f64), Some(true)))
            .collect();
        let s = coast_stats(&runs);
        let fit = s.fit.expect("fit");
        assert!((fit.b - 0.01).abs() < 1e-9);
        assert!(s.warning.is_none() || s.coast.is_some());
        let flat: Vec<RunRecord> = (0..5)
            .map(|i| rec(100, Some(1000.0 + 5.0 * i as f64), Some(true)))
            .collect();
        assert!(coast_stats(&flat).fit.is_none());
    }

    #[test]
    fn assess_plateau_and_ramp() {
        let flat: Vec<(u64, Option<u32>)> = (0..20).map(|i| (i * 100, Some(5000))).collect();
        let a = assess(&flat, TAIL_MS);
        assert_eq!(a.ok, Some(true));
        let ramp: Vec<(u64, Option<u32>)> =
            (0..20).map(|i| (i * 100, Some(i as u32 * 250))).collect();
        assert_eq!(assess(&ramp, TAIL_MS).ok, Some(false));
        assert!(assess(&ramp, TAIL_MS).why.is_some());
        assert_eq!(assess(&flat[..2], TAIL_MS).ok, Some(false));
        let none: Vec<(u64, Option<u32>)> = (0..20).map(|i| (i * 100, None)).collect();
        assert_eq!(assess(&none, TAIL_MS).ok, None);
    }

    #[test]
    fn wheel_check_scales() {
        assert_eq!(wheel_after_check(100_000, 36.0, 36.72), Ok(102_000));
        assert!(wheel_after_check(100_000, 0.0, 1.0).is_err());
    }

    #[test]
    fn wheel_follows_the_doc_formula() {
        // One revolution of the default wheel is cpr counts and pi*4.0015 in of paper.
        let one_rev = std::f64::consts::PI * 4.0015;
        let w = suggest_wheel_um(one_rev, 2400, 2400).unwrap();
        assert!((w - 101_638).abs() <= 1, "{w}");
        assert!(suggest_wheel_um(0.0, 10, 2400).is_err());
        assert!(suggest_wheel_um(5.0, 0, 2400).is_err());
        assert_eq!(suggest_wheel_from_records(&[], 2400), Err(CalError::NoData));
    }

    #[test]
    fn run_once_measures_follow_through() {
        let c = shared(900);
        c.lock().unwrap().debug(true).unwrap();
        c.lock().unwrap().set("coast", 0).unwrap();
        let r = run_once(&c, 50_000, Duration::from_secs(5)).unwrap();
        assert_eq!((r.at_stop, r.final_count, r.follow), (50_000, 50_900, 900));
    }

    #[test]
    fn guided_run_converges_and_leaves_debug() {
        let c = shared(900);
        let rev = std::f64::consts::PI * 4.0015 / 2400.0; // inches per count
        let measure = move |r: &RunRecord| Some(r.final_count as f64 * rev);
        let rep = guided_run(&c, &[50_000, 100_000], &measure, true).unwrap();
        assert_eq!(rep.coast, 900);
        assert_eq!(rep.before.len(), 3);
        assert_eq!(rep.residuals, vec![0, 0]);
        assert!((rep.wheel_um.unwrap() - 101_638).abs() <= 1);
        let mut g = c.lock().unwrap();
        assert_eq!(g.get().unwrap().state, crate::protocol::DevState::Idle);
        assert_eq!(g.param().unwrap().coast, 900);
    }

    #[test]
    fn implausible_measurement_is_refused_before_any_set() {
        let c = shared(900);
        let before = c.lock().unwrap().param().unwrap();
        let absurd = |r: &RunRecord| Some(r.final_count as f64); // inches == counts
        let out = guided_run(&c, &[50_000, 100_000], &absurd, false);
        assert!(matches!(out, Err(CalError::BadInput(_))), "{out:?}");
        let mut g = c.lock().unwrap();
        assert_eq!(g.param().unwrap(), before);
        assert_eq!(g.get().unwrap().state, crate::protocol::DevState::Idle);
    }

    #[test]
    fn restore_puts_back_hand_set_lengths() {
        let c = shared(0);
        let mut g = c.lock().unwrap();
        g.set("short", 210_000).unwrap();
        g.set("coast", 400).unwrap();
        let before = g.param().unwrap();
        g.set("wheel_um", 105_000).unwrap(); // re-derives short and long
        g.set("coast", 900).unwrap();
        restore_params(&mut g, &before).unwrap();
        assert_eq!(g.param().unwrap(), before);
        // Nothing to do when nothing changed.
        restore_params(&mut g, &before).unwrap();
    }

    #[test]
    fn guided_count_is_at_least_the_minimum() {
        assert_eq!(guided_count(&[1, 2]), 3);
        assert_eq!(guided_count(&[1, 2, 3, 4]), 4);
    }
}
