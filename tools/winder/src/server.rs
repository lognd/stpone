//! Local web server: embedded UI, JSON commands, and an SSE state feed.

use std::io::Read;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::{Arc, Condvar, Mutex, OnceLock};
use std::thread::{self, sleep};
use std::time::{Duration, Instant};

use serde_json::{json, Value};
use tiny_http::{Header, Method, Request, Response, Server, StatusCode};

use crate::auto::{self, Auto, Cfg, Ctx, JobObs, Phase, Runs};
use crate::calibrate::{coast_stats, residual, run_once_with, suggest_wheel_from_records};
use crate::client::Notice;
use crate::client::{Link, Shared};
use crate::doctor::{diagnose, fix_dialout, fix_udev, is_crostini, Report, Target};
use crate::error::{CalError, Res};
use crate::plain::{banner, describe, set_refusal, BannerIn};
use crate::protocol::{Cmd, DevState, Dump, Params, Snapshot, FACTORY};

const INDEX: &str = include_str!("web/index.html");
const APP_JS: &str = include_str!("web/app.js");
const APP_CSS: &str = include_str!("web/components/app.css");
const TOKENS_CSS: &str = include_str!("web/tokens.css");

const POLL: Duration = Duration::from_millis(100);
/// A dead event stream is only noticed when a write to it fails, so keep writing: this bounds how
/// long a closed browser is counted as an open page (and so the abandoned-page switch-off delay).
const KEEPALIVE: Duration = Duration::from_secs(2);
const RUN_TIMEOUT: Duration = Duration::from_secs(120);
/// Test mode is switched off when no browser has talked to the app for this long.
const ABANDON_AFTER: Duration = Duration::from_secs(10);
/// Most event streams served at once (rapid reloads leave dying ones behind).
const MAX_STREAMS: usize = 16;
const DEFAULT_CPR: i64 = 2400;
const LOG_CAP: usize = 200;
static START: OnceLock<Instant> = OnceLock::new();

/// What the browser sees; rebuilt into JSON on every change.
#[derive(Default)]
struct Ui {
    snapshot: Option<Snapshot>,
    dump: Option<Dump>,
    params: Option<Params>,
    streaming: bool,
    runs: Runs,
    job: String,
    /// (current run, planned runs) while a calibration job is active.
    job_runs: Option<(usize, usize)>,
    error: String,
    doctor: Option<Report>,
    checking: bool,
    connected: bool,
    log: Vec<String>,
    seq: u64,
}

impl Ui {
    /// Append a plain-language line to the activity log.
    fn say(&mut self, msg: impl Into<String>) {
        let t = START.get_or_init(Instant::now).elapsed().as_secs();
        if self.log.len() >= LOG_CAP {
            self.log.remove(0);
        }
        self.log
            .push(format!("[{:02}:{:02}] {}", t / 60, t % 60, msg.into()));
    }
}

/// Shared server state: the device client plus the UI model and its wakeup.
struct App {
    client: Shared,
    link: Link,
    target: Target,
    ui: Mutex<Ui>,
    changed: Condvar,
    params_dirty: AtomicBool,
    /// Open event streams (browser pages watching).
    streams: AtomicUsize,
    /// Last time any browser request arrived.
    last_seen: Mutex<Instant>,
    /// Handle for the background calibration jobs (shares client, runs and the wakeup).
    ctx: Ctx,
}

impl App {
    /// Build the shared state around a client; `runs` is shared with the job context.
    fn new(client: Shared, link: Link, target: Target) -> Arc<App> {
        let runs: Runs = Runs::default();
        let ui = Ui {
            runs: Arc::clone(&runs),
            ..Ui::default()
        };
        Arc::new_cyclic(|weak: &std::sync::Weak<App>| {
            let w = weak.clone();
            let notify: auto::Notify = Arc::new(move || {
                if let Some(a) = w.upgrade() {
                    a.update(|_| {});
                }
            });
            App {
                ctx: Ctx {
                    client: Arc::clone(&client),
                    auto: Auto::new(),
                    runs,
                    notify,
                    cfg: Cfg::default(),
                },
                client,
                link,
                target,
                ui: Mutex::new(ui),
                changed: Condvar::new(),
                params_dirty: AtomicBool::new(true),
                streams: AtomicUsize::new(0),
                last_seen: Mutex::new(Instant::now()),
            }
        })
    }

    /// Note that a browser is still there.
    fn touch(&self) {
        *self.last_seen.lock().expect("last_seen lock poisoned") = Instant::now();
    }

    /// True when no page is watching and none has asked for anything lately.
    fn abandoned(&self) -> bool {
        self.streams.load(Ordering::SeqCst) == 0
            && self
                .last_seen
                .lock()
                .expect("last_seen lock poisoned")
                .elapsed()
                > ABANDON_AFTER
    }

    fn update(&self, f: impl FnOnce(&mut Ui)) {
        let mut ui = self.ui.lock().expect("ui lock poisoned");
        f(&mut ui);
        ui.seq += 1;
        self.changed.notify_all();
    }

    fn state_json(&self) -> (u64, String) {
        let ui = self.ui.lock().expect("ui lock poisoned");
        (ui.seq, self.render(&ui))
    }

    /// Reject anything but reads while a calibration job or test-wind run owns the wire.
    fn ensure_idle(&self) -> Res<()> {
        if self.ctx.auto.busy() {
            Err(CalError::Refused(
                "A calibration or test wind is running. Wait for it to finish or press Stop first."
                    .into(),
            ))
        } else {
            Ok(())
        }
    }

    fn render(&self, ui: &Ui) -> String {
        render(ui, &self.ctx.auto)
    }
}

fn render(ui: &Ui, auto: &Auto) -> String {
    let cpr = ui.params.as_ref().map_or(DEFAULT_CPR, |p| p.cpr);
    let recs = ui.runs.lock().expect("runs lock poisoned").clone();
    let stats = coast_stats(&recs);
    let runs: Vec<Value> = recs
        .iter()
        .map(|r| {
            json!({"index": r.index, "target": r.target, "at_stop": r.at_stop, "final": r.final_count,
                   "follow": r.follow, "residual": residual(r.final_count, r.target),
                   "measured_in": r.measured_in, "peak_speed": r.peak_speed,
                   "valid": r.counts(), "why_invalid": r.why_invalid, "verify": r.verify})
        })
        .collect();
    let (auto_v, series, marks) = auto.render();
    let bn = banner(&BannerIn {
        connected: ui.connected,
        checking: ui.checking,
        snapshot: ui.snapshot.as_ref(),
        dump: ui.dump.as_ref(),
        job: ui.job_runs.or_else(|| auto.steps()),
    });
    json!({
        "banner": bn, "doctor": ui.doctor, "connected": ui.connected, "log": ui.log,
        "snapshot": ui.snapshot, "dump": ui.dump, "params": ui.params, "factory": FACTORY,
        "streaming": ui.streaming, "runs": runs,
        "suggest": {"coast": stats.coast,
                    "wheel_um": suggest_wheel_from_records(&recs, cpr).ok(),
                    "coast_stats": stats},
        "auto": auto_v, "series": series, "marks": marks,
        "job": if ui.job.is_empty() { "idle" } else { ui.job.as_str() },
        "error": ui.error,
    })
    .to_string()
}

/// Poll the device at ~10 Hz and publish snapshot, dump and parameters.
fn poller(app: Arc<App>) {
    let mut tick: u64 = 0;
    loop {
        sleep(POLL);
        tick += 1;
        poll_step(&app, tick);
    }
}

/// Motor off and Test mode off because nobody is watching; the lease alone would never lapse
/// while this app keeps answering the machine.
fn switch_off_abandoned(app: &Arc<App>) {
    log::warn!("no browser page for {ABANDON_AFTER:?}; switching Test mode and the motor off");
    let cleaned = app.ctx.cleanup();
    app.update(|u| {
        u.streaming = false;
        u.say(if cleaned {
            "Nobody has this page open any more, so the motor and Test mode were switched off."
        } else {
            "Nobody has this page open any more; the winder could not be told to stop and will stop itself within 5 seconds."
        });
    });
}

/// One poll of the device: publish its state, and apply the abandoned-page safety.
fn poll_step(app: &Arc<App>, tick: u64) {
    let (streaming, connected) = {
        let ui = app.ui.lock().expect("ui lock poisoned");
        (ui.streaming, ui.connected)
    };
    let notices = app
        .client
        .lock()
        .expect("client lock poisoned")
        .take_notices();
    for n in notices {
        app.update(|u| match n {
            Notice::DebugExit(why) => {
                u.streaming = false;
                u.say(match why.as_str() {
                    "panel" => "Test mode ended by the machine: someone used the machine's own buttons.".to_string(),
                    _ => "Test mode ended by the machine: it heard nothing from this app for 5 seconds.".to_string(),
                });
            }
            Notice::LcdFault => u.say("The machine's display reported a problem."),
        });
    }
    if !connected {
        return;
    }
    if app.ctx.auto.busy() {
        // The job owns the wire; show what it last saw instead of polling.
        let snap = app
            .client
            .lock()
            .expect("client lock poisoned")
            .last_snapshot
            .clone();
        if snap.is_some() {
            app.update(|u| u.snapshot = snap);
        }
        return;
    }
    if app.abandoned() && app.client.lock().expect("client lock poisoned").debug {
        switch_off_abandoned(app);
        return;
    }
    let polled = {
        let mut c = app.client.lock().expect("client lock poisoned");
        c.pump(Duration::from_millis(1))
            .and_then(|_| c.get())
            .and_then(|snap| {
                let dump = if streaming && snap.state == DevState::Debug {
                    c.last_dump.clone()
                } else {
                    Some(c.dump()?)
                };
                let params =
                    if tick.is_multiple_of(5) || app.params_dirty.swap(false, Ordering::SeqCst) {
                        Some(c.param()?)
                    } else {
                        None
                    };
                Ok((snap, dump, params))
            })
    };
    match polled {
        Ok((snap, dump, params)) => app.update(|u| {
            if snap.state != DevState::Debug {
                u.streaming = false;
            }
            u.snapshot = Some(snap);
            u.dump = dump;
            if params.is_some() {
                u.params = params;
            }
            u.error.clear();
        }),
        Err(e) => {
            log::warn!("poll failed: {e}");
            app.update(|u| {
                u.snapshot = None;
                u.error = e.human();
                u.say("Lost contact with the winder.");
            });
        }
    }
}

/// Blocking Read that yields one SSE frame per state change (or a keepalive).
struct Sse {
    app: Arc<App>,
    seen: u64,
    buf: Vec<u8>,
    pos: usize,
}

impl Sse {
    /// A new stream counted as a watching page; its first frame is the full current state.
    fn new(app: &Arc<App>) -> Sse {
        app.streams.fetch_add(1, Ordering::SeqCst);
        Sse {
            app: Arc::clone(app),
            // No frame has been sent yet, so the first read sends the snapshot at once.
            seen: u64::MAX,
            buf: Vec::new(),
            pos: 0,
        }
    }
}

impl Drop for Sse {
    fn drop(&mut self) {
        self.app.streams.fetch_sub(1, Ordering::SeqCst);
        self.app.touch();
    }
}

impl Read for Sse {
    fn read(&mut self, out: &mut [u8]) -> std::io::Result<usize> {
        if self.pos >= self.buf.len() {
            let mut ui = self.app.ui.lock().expect("ui lock poisoned");
            if ui.seq == self.seen {
                let (g, _) = self
                    .app
                    .changed
                    .wait_timeout(ui, KEEPALIVE)
                    .expect("ui lock poisoned");
                ui = g;
            }
            self.buf = if ui.seq == self.seen {
                b": keepalive\n\n".to_vec()
            } else {
                self.seen = ui.seq;
                format!("data: {}\n\n", render(&ui, &self.app.ctx.auto)).into_bytes()
            };
            self.pos = 0;
        }
        let n = out.len().min(self.buf.len() - self.pos);
        out[..n].copy_from_slice(&self.buf[self.pos..self.pos + n]);
        self.pos += n;
        Ok(n)
    }
}

fn header(name: &str, value: &str) -> Header {
    Header::from_bytes(name.as_bytes(), value.as_bytes()).expect("static header is valid")
}

fn header_of(req: &Request, name: &'static str) -> Option<String> {
    req.headers()
        .iter()
        .find(|h| h.field.equiv(name))
        .map(|h| h.value.to_string())
}

/// Reject requests whose Host/Origin is not this loopback server (DNS rebinding guard).
fn allowed(req: &Request, port: u16) -> bool {
    let hosts = [format!("127.0.0.1:{port}"), format!("localhost:{port}")];
    let host_ok = header_of(req, "Host").is_some_and(|h| hosts.contains(&h));
    let origin_ok =
        header_of(req, "Origin").is_none_or(|o| hosts.iter().any(|h| o == format!("http://{h}")));
    host_ok && origin_ok
}

fn body_json(req: &mut Request) -> Res<Value> {
    let mut s = String::new();
    req.as_reader()
        .take(16 * 1024)
        .read_to_string(&mut s)
        .map_err(|e| CalError::Io(e.to_string()))?;
    serde_json::from_str(&s).map_err(|e| CalError::BadInput(format!("bad JSON: {e}")))
}

fn arg_string(v: &Value) -> String {
    match v {
        Value::String(s) => s.clone(),
        other => other.to_string(),
    }
}

fn api_command(app: &Arc<App>, body: &Value) -> Res<Value> {
    let verb = body["cmd"]
        .as_str()
        .ok_or_else(|| CalError::BadInput("missing cmd".into()))?;
    let args: Vec<String> = body["args"]
        .as_array()
        .map_or_else(Vec::new, |a| a.iter().map(arg_string).collect());
    let cmd = Cmd::from_parts(verb, &args)?;
    log::info!("ui command {cmd:?}");
    if !matches!(cmd, Cmd::Hello | Cmd::Get | Cmd::Param | Cmd::Dump) {
        if app.ctx.auto.manual_active()
            && matches!(cmd, Cmd::Abort | Cmd::Relay(false) | Cmd::Debug(false))
        {
            // A stop request must end the whole run of test winds, not just the current one.
            log::warn!("ui {cmd:?} during a test-wind run: stopping the run");
            let _ = app.ctx.auto.request_cancel();
        } else {
            app.ensure_idle()?;
        }
    }
    let reply = match app
        .client
        .lock()
        .expect("client lock poisoned")
        .command(&cmd)
    {
        Ok(r) => r,
        Err(e) => {
            let e = match &cmd {
                Cmd::Set(name, _) if matches!(e, CalError::BadArg | CalError::BadState) => {
                    CalError::Refused(set_refusal(name, &e))
                }
                _ => e,
            };
            app.update(|u| u.say(format!("Could not do that: {}", e.human())));
            return Err(e);
        }
    };
    app.update(|u| u.say(describe(&cmd)));
    match cmd {
        Cmd::Stream(on) => app.update(|u| u.streaming = on),
        Cmd::Debug(false) => app.update(|u| u.streaming = false),
        Cmd::Set(..) | Cmd::Save | Cmd::Load | Cmd::Defaults => {
            app.params_dirty.store(true, Ordering::SeqCst)
        }
        _ => {}
    }
    Ok(json!({"reply": reply}))
}

fn api_run(app: &Arc<App>, body: &Value) -> Res<Value> {
    let pulses = body["pulses"]
        .as_i64()
        .filter(|p| *p > 0)
        .ok_or_else(|| CalError::BadInput("pulses must be positive".into()))?;
    let count = body["count"].as_u64().unwrap_or(1).clamp(1, 20) as usize;
    // One slot for jobs and manual runs, claimed atomically (double clicks, two tabs).
    app.ctx.auto.claim_manual()?;
    app.update(|u| {
        u.job_runs = Some((1, count));
        u.job = format!("running {count} test wind(s) of {pulses} pulses");
        u.say(format!(
            "Starting {count} test wind(s) of {pulses} pulses. Do not touch the machine."
        ));
    });
    let app = Arc::clone(app);
    thread::spawn(move || manual_winds(&app, pulses, count, RUN_TIMEOUT));
    Ok(json!({}))
}

/// Gives the job slot back when the manual run ends, however it ends.
struct SlotGuard(Arc<Auto>);

impl Drop for SlotGuard {
    fn drop(&mut self) {
        self.0.release_manual();
    }
}

/// Wind `count` test winds. Any failure, timeout or Stop ends the whole sequence with the
/// motor off; a failure other than Stop also leaves Test mode.
fn manual_winds(app: &Arc<App>, pulses: i64, count: usize, timeout: Duration) {
    let _slot = SlotGuard(Arc::clone(&app.ctx.auto));
    for n in 1..=count {
        if app.ctx.auto.cancel_requested() {
            log::warn!("test winds stopped before wind {n}");
            app.ctx.stop_motor();
            app.update(|u| {
                u.job.clear();
                u.job_runs = None;
                u.say("Test winds stopped.");
            });
            return;
        }
        app.update(|u| u.job_runs = Some((n, count)));
        app.ctx.auto.reset_series();
        let mut obs = JobObs::plot_only(&app.ctx);
        let res = run_once_with(&app.client, pulses, timeout, &mut obs);
        log::info!("calibration run {n}/{count} of {pulses}: {res:?}");
        match res {
            Ok(mut r) => {
                {
                    let mut runs = app.ctx.runs.lock().expect("runs lock poisoned");
                    r.index = runs.len();
                    runs.push(r.clone());
                }
                app.update(|u| {
                    u.say(format!(
                        "Test wind {n} finished: stopped at {}, coasted {} more, settled at {}.",
                        r.at_stop, r.follow, r.final_count
                    ));
                });
            }
            Err(CalError::Cancelled) => {
                app.ctx.stop_motor();
                app.update(|u| {
                    u.job.clear();
                    u.job_runs = None;
                    u.say(format!(
                        "Test wind {n} was stopped; the remaining winds were cancelled."
                    ));
                });
                return;
            }
            Err(e) => {
                let cleaned = app.ctx.cleanup();
                app.update(|u| {
                    u.job = format!("error: {}", e.human());
                    u.say(format!("Test wind {n} failed: {}", e.human()));
                    u.say(if cleaned {
                        "The motor is off and Test mode is off."
                    } else {
                        "The winder did not confirm it stopped; it stops its own motor within 5 seconds when it hears nothing from this app."
                    });
                    u.job_runs = None;
                });
                return;
            }
        }
    }
    app.update(|u| {
        u.job.clear();
        u.job_runs = None;
        u.say("All test winds finished. Enter the measured lengths, then use the suggestions.");
    });
}

/// Re-run the doctor in the background and publish the result.
fn recheck(app: &Arc<App>) {
    if app.ui.lock().expect("ui lock poisoned").checking {
        return;
    }
    app.update(|u| {
        u.checking = true;
        u.connected = false;
        u.say("Checking the connection to the winder...");
    });
    let app = Arc::clone(app);
    thread::spawn(move || {
        let report = diagnose(&app.target, &app.client, &app.link);
        app.params_dirty.store(true, Ordering::SeqCst);
        app.update(|u| {
            u.connected = report.ok;
            u.checking = false;
            u.say(if report.ok {
                "The winder is connected and ready.".to_string()
            } else {
                let bad = report
                    .checks
                    .iter()
                    .find(|c| c.status == crate::doctor::Status::Fail);
                format!(
                    "Not ready: {}",
                    bad.map_or("see the checklist", |c| c.message.as_str())
                )
            });
            u.doctor = Some(report);
        });
    });
}

fn api_fix(app: &Arc<App>, body: &Value) -> Res<Value> {
    let id = body["id"].as_str().unwrap_or("");
    // Every fix and the recheck after it re-open the serial link, so none may run mid-job.
    app.ensure_idle()?;
    let msg = match id {
        "udev" => fix_udev().map_err(CalError::BadInput)?,
        "dialout" => fix_dialout().map_err(CalError::BadInput)?,
        "factory" => {
            let mut c = app.client.lock().expect("client lock poisoned");
            c.command(&Cmd::Defaults)?;
            c.save()?;
            "Factory settings restored and saved.".to_string()
        }
        other => return Err(CalError::BadInput(format!("unknown fix {other}"))),
    };
    app.update(|u| u.say(msg.clone()));
    recheck(app);
    Ok(json!({"message": msg}))
}

fn api_measure(app: &Arc<App>, body: &Value) -> Res<Value> {
    let inches = body["inches"].as_f64();
    let Some(i) = body["index"].as_u64() else {
        // No run index: this is the length-check measurement.
        let m = inches.ok_or_else(|| CalError::BadInput("inches must be a number".into()))?;
        app.ctx.auto.offer_measure(m)?;
        return Ok(json!({}));
    };
    let mut runs = app.ctx.runs.lock().expect("runs lock poisoned");
    match runs.get_mut(i as usize) {
        Some(r) => r.measured_in = inches,
        None => return Err(CalError::BadInput(format!("no run {i}"))),
    }
    drop(runs);
    app.update(|_| {});
    Ok(json!({}))
}

/// Cancel a running job, or discard a finished-but-unsaved result.
fn api_cancel(app: &Arc<App>) -> Res<Value> {
    if app.ctx.auto.manual_active() {
        app.ctx.auto.request_cancel()?;
    } else if app.ctx.auto.view().phase == Phase::ReadyToSave {
        auto::discard(&app.ctx)?;
        app.params_dirty.store(true, Ordering::SeqCst);
    } else {
        app.ctx.auto.request_cancel()?;
    }
    Ok(json!({}))
}

/// Start a job thread (detached) and return at once.
fn spawn_job(app: &Arc<App>, start: fn(&Ctx) -> Res<thread::JoinHandle<()>>) -> Res<Value> {
    let ctx = app.ctx.clone();
    start(&ctx)?;
    app.params_dirty.store(true, Ordering::SeqCst);
    Ok(json!({}))
}

fn route(app: &Arc<App>, mut req: Request, port: u16) {
    if !allowed(&req, port) {
        log::warn!("refused request with foreign Host/Origin: {}", req.url());
        let _ = req.respond(Response::from_string("forbidden").with_status_code(403));
        return;
    }
    app.touch();
    let url = req.url().to_string();
    let method = req.method().clone();
    let page = |body: &'static str, ctype: &str| {
        Response::from_string(body).with_header(header("Content-Type", ctype))
    };
    let result = match (&method, url.as_str()) {
        (Method::Get, "/") => req.respond(page(INDEX, "text/html; charset=utf-8")),
        (Method::Get, "/app.js") => req.respond(page(APP_JS, "text/javascript; charset=utf-8")),
        (Method::Get, "/app.css") => req.respond(page(APP_CSS, "text/css; charset=utf-8")),
        (Method::Get, "/tokens.css") => req.respond(page(TOKENS_CSS, "text/css; charset=utf-8")),
        (Method::Get, "/api/state") => {
            let (_, s) = app.state_json();
            req.respond(
                Response::from_string(s).with_header(header("Content-Type", "application/json")),
            )
        }
        (Method::Get, "/events") => {
            if app.streams.load(Ordering::SeqCst) >= MAX_STREAMS {
                log::warn!("too many open event streams; refusing another");
                let _ = req.respond(Response::from_string("too many pages").with_status_code(503));
                return;
            }
            let sse = Sse::new(app);
            let headers = vec![
                header("Content-Type", "text/event-stream"),
                header("Cache-Control", "no-store"),
            ];
            req.respond(Response::new(StatusCode(200), headers, sse, None, None))
        }
        (Method::Post, p) if p.starts_with("/api/") => {
            let out = body_json(&mut req).and_then(|b| match p {
                "/api/command" => api_command(app, &b),
                "/api/calibrate/run" => api_run(app, &b),
                "/api/calibrate/measure" => api_measure(app, &b),
                "/api/doctor/run" => app.ensure_idle().map(|_| {
                    recheck(app);
                    json!({})
                }),
                "/api/doctor/fix" => api_fix(app, &b),
                "/api/calibrate/clear" => app.ensure_idle().map(|_| {
                    app.ctx.runs.lock().expect("runs lock poisoned").clear();
                    app.update(|_| {});
                    json!({})
                }),
                "/api/calibrate/auto" => spawn_job(app, auto::start_auto),
                "/api/calibrate/length_check" => spawn_job(app, auto::start_length_check),
                "/api/calibrate/cancel" => api_cancel(app),
                "/api/calibrate/save" => auto::save(&app.ctx).map(|_| {
                    app.params_dirty.store(true, Ordering::SeqCst);
                    json!({})
                }),
                "/api/calibrate/apply_wheel" => app.ctx.auto.offer_apply().map(|_| json!({})),
                "/api/calibrate/skip_measure" => app.ctx.auto.offer_skip().map(|_| json!({})),
                _ => Err(CalError::BadInput(format!("no such endpoint {p}"))),
            });
            let doc = match out {
                Ok(mut v) => {
                    v["ok"] = json!(true);
                    v
                }
                Err(e) => {
                    log::warn!("{p} failed: {e}");
                    json!({"ok": false, "error": e.human()})
                }
            };
            req.respond(
                Response::from_string(doc.to_string())
                    .with_header(header("Content-Type", "application/json")),
            )
        }
        _ => req.respond(Response::from_string("not found").with_status_code(404)),
    };
    if let Err(e) = result {
        log::debug!("response to {url} aborted: {e}");
    }
}

/// Whether this system has a way to open a browser (always assumed on Windows/macOS).
pub fn browser_available() -> bool {
    if cfg!(target_os = "linux") {
        std::env::var_os("PATH")
            .is_some_and(|p| std::env::split_paths(&p).any(|d| d.join("xdg-open").is_file()))
    } else {
        true
    }
}

/// Open the default browser on `url`; on failure leave winder-open-me.txt beside the program.
pub fn open_browser(url: &str) {
    let spawned = if !browser_available() {
        Err(std::io::Error::new(
            std::io::ErrorKind::NotFound,
            "no xdg-open",
        ))
    } else if cfg!(target_os = "windows") {
        std::process::Command::new("cmd")
            .args(["/c", "start", "", url])
            .spawn()
    } else if cfg!(target_os = "macos") {
        std::process::Command::new("open").arg(url).spawn()
    } else {
        std::process::Command::new("xdg-open").arg(url).spawn()
    };
    let Err(e) = spawned else { return };
    log::warn!("could not open a browser ({e}); open {url} yourself");
    let port = url.rsplit(':').next().unwrap_or("").trim_end_matches('/');
    let chrome = if is_crostini() {
        format!("\r\nOn a Chromebook: open Chrome and type localhost:{port}\r\n")
    } else {
        String::new()
    };
    eprintln!("Could not open a browser. Open one yourself and type: {url}");
    let note = format!("No web browser could be opened automatically.\r\nOpen any browser and type this address:\r\n\r\n{url}\r\n{chrome}");
    let path = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.join("winder-open-me.txt")));
    match path {
        Some(p) => match std::fs::write(&p, note) {
            Ok(()) => {
                log::warn!("wrote the address to {}", p.display());
                if cfg!(target_os = "windows") {
                    let _ = std::process::Command::new("notepad").arg(&p).spawn();
                }
            }
            Err(e) => log::error!("could not write {}: {e}", p.display()),
        },
        None => log::error!("cannot locate the program folder for winder-open-me.txt"),
    }
}

/// Serve the UI on 127.0.0.1 (falling back to a free port); blocks forever.
pub fn serve(client: Shared, link: Link, target: Target, http_port: u16, open: bool) -> Res<()> {
    let server = Server::http(("127.0.0.1", http_port))
        .or_else(|e| {
            log::warn!("port {http_port} unavailable ({e}); picking a free one");
            Server::http(("127.0.0.1", 0))
        })
        .map_err(|e| CalError::Io(e.to_string()))?;
    let port = server
        .server_addr()
        .to_ip()
        .map(|a| a.port())
        .ok_or_else(|| CalError::Io("no ip address".into()))?;
    let url = format!("http://127.0.0.1:{port}/");
    log::info!("serving {url}");
    eprintln!("winder UI: {url}");
    let app = App::new(client, link, target);
    let p = Arc::clone(&app);
    thread::spawn(move || poller(p));
    recheck(&app);
    if open {
        open_browser(&url);
    }
    for req in server.incoming_requests() {
        let app = Arc::clone(&app);
        thread::spawn(move || route(&app, req, port));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::client::Client;
    use crate::fake::FakeDevice;

    fn app_with(d: FakeDevice) -> Arc<App> {
        let client: Shared = Arc::new(Mutex::new(Client::new(Box::new(d))));
        let target = Target {
            port: None,
            baud: 115_200,
            fake_coast: Some(0),
        };
        let app = App::new(client, Link::default(), target);
        app.ui.lock().unwrap().connected = true; // no seq bump: nothing has changed yet
        app
    }

    fn dev(speed: f64) -> FakeDevice {
        let mut d = FakeDevice::new(0);
        d.speed = speed;
        d.settle = Duration::from_millis(20);
        d
    }

    /// (state is Test mode, relay on, firmware run active) as the machine reports it.
    fn machine(app: &Arc<App>) -> (bool, bool, bool) {
        let mut c = app.client.lock().unwrap();
        let _ = c.pump(Duration::from_millis(5));
        let snap = c.get().unwrap();
        let d = c.dump().unwrap();
        (snap.state == DevState::Debug, snap.relay, d.run)
    }

    fn wait_until(what: &str, f: impl Fn() -> bool) {
        let end = Instant::now() + Duration::from_secs(20);
        while !f() {
            assert!(Instant::now() < end, "timed out waiting for {what}");
            sleep(Duration::from_millis(20));
        }
    }

    fn post(app: &Arc<App>, cmd: &str, args: Value) -> Res<Value> {
        api_command(app, &json!({"cmd": cmd, "args": args}))
    }

    #[test]
    fn first_event_frame_is_the_current_snapshot() {
        let app = app_with(dev(1e6));
        assert_eq!(app.ui.lock().unwrap().seq, 0, "nothing has changed yet");
        let mut sse = Sse::new(&app);
        assert_eq!(app.streams.load(Ordering::SeqCst), 1);
        let mut buf = [0u8; 65536];
        let n = sse.read(&mut buf).unwrap();
        let frame = String::from_utf8_lossy(&buf[..n]).to_string();
        assert!(frame.starts_with("data: {"), "{frame}");
        assert!(frame.contains("\"connected\":true") && frame.contains("\"auto\""));
        drop(sse);
        assert_eq!(app.streams.load(Ordering::SeqCst), 0);
    }

    #[test]
    fn failed_wind_stops_the_motor_leaves_test_mode_and_frees_the_slot() {
        let app = app_with(dev(10.0)); // never reaches its target
        post(&app, "DEBUG", json!([1])).unwrap();
        app.ctx.auto.claim_manual().unwrap();
        manual_winds(&app, 1_000_000, 3, Duration::from_millis(400));
        assert_eq!(
            machine(&app),
            (false, false, false),
            "motor off, Test mode off"
        );
        assert!(!app.client.lock().unwrap().debug, "keepalive must stop");
        assert!(!app.ctx.auto.busy(), "the slot is free again");
        let ui = app.ui.lock().unwrap();
        assert!(ui.job_runs.is_none() && ui.job.starts_with("error"));
        assert!(ui.runs.lock().unwrap().is_empty());
    }

    #[test]
    fn stop_during_a_multi_wind_run_stops_the_whole_sequence() {
        let app = app_with(dev(50_000.0)); // 400000 pulses take 8 s
        post(&app, "DEBUG", json!([1])).unwrap();
        app.ctx.auto.claim_manual().unwrap();
        let a = Arc::clone(&app);
        let h = thread::spawn(move || manual_winds(&a, 400_000, 5, Duration::from_secs(30)));
        wait_until("the first wind to start", || machine(&app).2);
        // The UI's stop: ABORT through the command endpoint while the run is active.
        post(&app, "ABORT", json!([])).unwrap();
        h.join().unwrap();
        sleep(Duration::from_millis(400)); // a restarted wind would show here
        assert_eq!(
            machine(&app),
            (true, false, false),
            "stopped, still in Test mode"
        );
        assert!(!app.ctx.auto.busy());
        assert!(
            app.ui.lock().unwrap().runs.lock().unwrap().is_empty(),
            "the aborted wind is not a sample"
        );
        assert!(app.ui.lock().unwrap().job_runs.is_none());
    }

    #[test]
    fn cancel_endpoint_stops_the_sequence_too() {
        let app = app_with(dev(50_000.0));
        post(&app, "DEBUG", json!([1])).unwrap();
        app.ctx.auto.claim_manual().unwrap();
        let a = Arc::clone(&app);
        let h = thread::spawn(move || manual_winds(&a, 400_000, 5, Duration::from_secs(30)));
        wait_until("the first wind to start", || machine(&app).2);
        api_cancel(&app).unwrap();
        h.join().unwrap();
        sleep(Duration::from_millis(300));
        assert_eq!(machine(&app), (true, false, false));
    }

    #[test]
    fn manual_runs_exclude_everything_else() {
        let app = app_with(dev(50_000.0));
        post(&app, "DEBUG", json!([1])).unwrap();
        api_run(&app, &json!({"pulses": 400_000, "count": 1})).unwrap();
        assert!(matches!(
            api_run(&app, &json!({"pulses": 400_000})),
            Err(CalError::Refused(_))
        ));
        assert!(matches!(
            spawn_job(&app, auto::start_auto),
            Err(CalError::Refused(_))
        ));
        assert!(matches!(
            post(&app, "SET", json!(["coast", 5])),
            Err(CalError::Refused(_))
        ));
        assert!(
            app.ensure_idle().is_err(),
            "doctor recheck and fixes are refused"
        );
        post(&app, "ABORT", json!([])).unwrap();
        wait_until("the run to end", || !app.ctx.auto.busy());
        assert_eq!(machine(&app), (true, false, false));
    }

    #[test]
    fn abandoned_page_switches_the_motor_and_test_mode_off() {
        let app = app_with(dev(1e6));
        post(&app, "DEBUG", json!([1])).unwrap();
        post(&app, "RELAY", json!([1])).unwrap();
        // A page is watching: nothing happens however long it has been quiet.
        let sse = Sse::new(&app);
        *app.last_seen.lock().unwrap() = Instant::now() - Duration::from_secs(60);
        poll_step(&app, 1);
        assert_eq!(machine(&app), (true, true, false));
        // The page goes away and stays away.
        drop(sse);
        *app.last_seen.lock().unwrap() = Instant::now() - Duration::from_secs(60);
        poll_step(&app, 2);
        assert_eq!(machine(&app), (false, false, false));
    }

    #[test]
    fn recent_activity_keeps_test_mode_on() {
        let app = app_with(dev(1e6));
        post(&app, "DEBUG", json!([1])).unwrap();
        app.touch();
        poll_step(&app, 1);
        assert!(machine(&app).0);
    }
}
