//! In-process simulated winder: states, coast stop rule, follow-through, settled event.

use std::collections::VecDeque;
use std::thread::sleep;
use std::time::{Duration, Instant};

use crate::client::Transport;
use crate::error::Res;
use crate::protocol::*;

/// Debug-mode lease of the real firmware.
pub const LEASE: Duration = Duration::from_secs(5);
/// pi in millionths, as the firmware's kPiMicro.
const PI_MICRO: u64 = 3_141_593;
const STREAM_MIN: Duration = Duration::from_millis(20);
const STREAM_MAX: Duration = Duration::from_millis(100);

/// The seven tunable parameters.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
struct ParamSet {
    wheel_um: i64,
    cpr: i64,
    short: i64,
    long: i64,
    coast: i64,
    margin: i64,
    bump: i64,
}

const DEFAULTS: ParamSet = ParamSet {
    wheel_um: FACTORY.wheel_um,
    cpr: FACTORY.cpr,
    short: FACTORY.short,
    long: FACTORY.long,
    coast: FACTORY.coast,
    margin: FACTORY.margin,
    bump: FACTORY.bump,
};

impl ParamSet {
    /// Re-derive short and long from wheel and cpr, as the firmware's integer math does.
    fn rederive(&mut self) {
        let circ = PI_MICRO * self.wheel_um as u64; // circumference in um * 1e6
        let pulses =
            |len_um: u64| ((len_um * self.cpr as u64 * 1_000_000 + circ / 2) / circ) as i64;
        self.short = pulses(1080 * 25_400);
        self.long = pulses(3240 * 25_400);
    }

    /// The firmware's `Params::valid` (hostlink.h), range for range.
    fn valid(&self) -> bool {
        let lo = self.short.min(self.long);
        let hi = self.short.max(self.long);
        (WHEEL_UM_MIN..=WHEEL_UM_MAX).contains(&self.wheel_um)
            && (100..=100_000).contains(&self.cpr)
            && (1000..=RUN_MAX).contains(&self.short)
            && (1000..=RUN_MAX).contains(&self.long)
            && (0..lo).contains(&self.coast)
            && (0..=MARGIN_MAX).contains(&self.margin)
            && (0..=1).contains(&self.bump)
            && hi + self.margin <= RUN_MAX
    }
}

/// Simulated firmware. Speed is pulses per second; time is real.
pub struct FakeDevice {
    pub true_coast: i64,
    pub speed: f64,
    pub settle: Duration,
    /// Time to reach full speed from a standing start (zero = instantly).
    pub ramp: Duration,
    /// Report `speed=` (firmware 4); false behaves like firmware 3.
    pub emit_speed: bool,
    /// Report the firmware 6 fields (margin, bump, pass, ...); false behaves like firmware 5.
    pub emit_v6: bool,
    /// Debug lease; the real firmware uses [`LEASE`].
    pub lease: Duration,
    /// Make the next SAVE fail with ERR io.
    pub fail_save: bool,
    /// Drop this many upcoming lines with ERR busy.
    pub drop_next: u32,
    /// Encoder wired backwards: GET/DUMP `count` is negative; `at_stop`/`final`/`follow` stay
    /// magnitudes, as in the firmware (`read_enc()` is |count|).
    pub reversed: bool,
    state: DevState,
    vel: f64,
    run_started: Instant,
    last_host: Instant,
    count: i64,
    pos: f64,
    errors: i64,
    relay: bool,
    params: ParamSet,
    eeprom: Option<ParamSet>,
    run_target: i64,
    running: bool,
    settling: Option<(Instant, i64, i64)>,
    streaming: bool,
    out: VecDeque<String>,
    born: Instant,
    last_tick: Instant,
    last_emit: Instant,
    last_key: String,
}

impl FakeDevice {
    /// New device in IDLE with the given real follow-through (pulses).
    pub fn new(true_coast: i64) -> FakeDevice {
        let now = Instant::now();
        FakeDevice {
            true_coast,
            speed: 30_000.0,
            settle: Duration::from_millis(500),
            ramp: Duration::ZERO,
            emit_speed: true,
            emit_v6: true,
            lease: LEASE,
            fail_save: false,
            drop_next: 0,
            reversed: false,
            state: DevState::Idle,
            vel: 0.0,
            run_started: now,
            last_host: now,
            count: 0,
            pos: 0.0,
            errors: 0,
            relay: false,
            params: DEFAULTS,
            eeprom: None,
            run_target: 0,
            running: false,
            settling: None,
            streaming: false,
            out: VecDeque::new(),
            born: now,
            last_tick: now,
            last_emit: now,
            last_key: String::new(),
        }
    }

    /// A device that spins up over half a second, like the real motor, so speed checks mean something.
    pub fn realistic(true_coast: i64) -> FakeDevice {
        let mut d = FakeDevice::new(true_coast);
        d.ramp = Duration::from_millis(500);
        d
    }

    #[cfg(test)]
    /// True while the device is in Test (DEBUG) mode.
    pub fn in_debug(&self) -> bool {
        self.state == DevState::Debug
    }

    #[cfg(test)]
    /// True while the motor relay is energised.
    pub fn relay_on(&self) -> bool {
        self.relay
    }

    fn p(&self, name: &str) -> i64 {
        match name {
            "wheel_um" => self.params.wheel_um,
            "cpr" => self.params.cpr,
            "short" => self.params.short,
            "long" => self.params.long,
            "margin" => self.params.margin,
            "bump" => self.params.bump,
            _ => self.params.coast,
        }
    }

    fn speed_now(&self) -> Option<u32> {
        self.emit_speed
            .then_some(if self.running { self.vel as u32 } else { 0 })
    }

    /// Return to a pristine IDLE (debug exit): relay, manual, count and errors cleared.
    fn pristine(&mut self) {
        self.state = DevState::Idle;
        self.count = 0;
        self.pos = 0.0;
        self.errors = 0;
        self.relay = false;
        self.running = false;
        self.vel = 0.0;
        self.streaming = false;
    }

    /// Abandon a RUN in progress, reporting it like the firmware does.
    fn abort_run(&mut self) {
        if self.running {
            self.running = false;
            self.relay = false;
            self.vel = 0.0;
            self.settling = None;
            self.out.push_back(format!(
                "EVT stop target={} at_stop={} reason=abort",
                self.run_target, self.count
            ));
        }
    }

    /// The count as GET and DUMP report it (signed); events use the magnitude.
    fn reported_count(&self) -> i64 {
        if self.reversed {
            -self.count
        } else {
            self.count
        }
    }

    /// Current full state.
    pub fn dump(&self) -> Dump {
        Dump {
            state: self.state,
            prev: None,
            count: self.reported_count(),
            speed: self.speed_now(),
            errors: self.errors,
            relay: self.relay,
            manual: false,
            target: self.run_target,
            eff_target: self.run_target - self.p("coast"),
            coast: self.p("coast"),
            btn: "0000".into(),
            toggles: vec![0; 4],
            pending: vec![0; 4],
            enc_dir: if self.running { 1 } else { 0 },
            uptime_ms: self.born.elapsed().as_millis() as i64,
            run: self.running,
            stack_free: None,
            pass_max_ms: None,
            isr_lat_max: None,
            margin: self.emit_v6.then_some(self.params.margin as u32),
            bump: self.emit_v6.then_some(self.params.bump as u32),
            bumps: self.emit_v6.then_some(0),
            trim: self.emit_v6.then_some(false),
            pass: None,
        }
    }

    fn advance(&mut self) {
        let now = Instant::now();
        let dt = now.duration_since(self.last_tick).as_secs_f64();
        self.last_tick = now;
        if self.state == DevState::Debug && now.duration_since(self.last_host) > self.lease {
            log::warn!("fake: debug lease expired");
            self.abort_run();
            self.pristine();
            self.out.push_back("EVT debug_exit reason=timeout".into());
        }
        if self.running {
            let eff = (self.run_target - self.p("coast")).max(0);
            self.vel = if self.ramp.is_zero() {
                self.speed
            } else {
                self.speed
                    * (now.duration_since(self.run_started).as_secs_f64() / self.ramp.as_secs_f64())
                        .min(1.0)
            };
            self.pos = (self.pos + dt * self.vel).min(eff as f64);
            self.count = self.pos as i64;
            if self.count >= eff {
                self.running = false;
                self.relay = false;
                let at_stop = self.count;
                self.vel = 0.0;
                self.count += self.true_coast;
                self.pos = self.count as f64;
                self.settling = Some((now + self.settle, at_stop, self.count));
                self.out.push_back(format!(
                    "EVT stop target={} at_stop={at_stop} reason=target",
                    self.run_target
                ));
            }
        }
        if let Some((at, at_stop, fin)) = self.settling {
            if !self.running && now >= at {
                self.settling = None;
                self.out.push_back(format!(
                    "EVT settled final={} follow={}",
                    fin.abs(),
                    fin.abs() - at_stop.abs()
                ));
            }
        }
        if self.streaming && self.out.is_empty() {
            let mut d = self.dump();
            d.uptime_ms = 0;
            let key = format_dump(&d);
            let since = now.duration_since(self.last_emit);
            if (key != self.last_key && since >= STREAM_MIN) || since >= STREAM_MAX {
                self.last_key = key;
                self.last_emit = now;
                self.out
                    .push_back(format!("EVT state {}", format_dump(&self.dump())));
            }
        }
    }

    fn handle(&mut self, line: &str) -> String {
        let mut it = line.split_whitespace();
        let Some(verb) = it.next() else {
            return "ERR bad_command".into();
        };
        let args: Vec<String> = it.map(String::from).collect();
        // Unknown verbs and 4+ tokens are bad_command; bad arguments are bad_arg.
        let known = [
            "HELLO", "GET", "PARAM", "SET", "SAVE", "LOAD", "DEFAULTS", "DEBUG", "RELAY", "ZERO",
            "RUN", "ABORT", "DUMP", "STREAM",
        ];
        if !known.contains(&verb) || args.len() > 2 {
            return "ERR bad_command".into();
        }
        let Ok(cmd) = Cmd::from_parts(verb, &args) else {
            return "ERR bad_arg".into();
        };
        let in_debug = self.state == DevState::Debug;
        let idle = self.state == DevState::Idle;
        let quiet = idle || in_debug;
        match cmd {
            Cmd::Hello => format!(
                "OK stpone proto=1 fw={}",
                if self.emit_v6 {
                    6
                } else if self.emit_speed {
                    4
                } else {
                    3
                }
            ),
            Cmd::Get => {
                let speed = self
                    .speed_now()
                    .map_or(String::new(), |v| format!(" speed={v}"));
                format!(
                    "OK state={} count={}{speed} errors={} relay={} btn=0000{}",
                    self.state.as_str(),
                    self.reported_count(),
                    self.errors,
                    self.relay as i32,
                    if self.emit_v6 { " pass=-" } else { "" }
                )
            }
            Cmd::Param => format!(
                "OK wheel_um={} cpr={} short={} long={} coast={}{}",
                self.p("wheel_um"),
                self.p("cpr"),
                self.p("short"),
                self.p("long"),
                self.p("coast"),
                if self.emit_v6 {
                    format!(" margin={} bump={}", self.p("margin"), self.p("bump"))
                } else {
                    String::new()
                }
            ),
            Cmd::Dump => format!("OK {}", format_dump(&self.dump())),
            Cmd::Set(n, v) => {
                if !quiet || self.running {
                    return "ERR bad_state".into();
                }
                let mut next = self.params;
                match n.as_str() {
                    "wheel_um" => next.wheel_um = v,
                    "cpr" => next.cpr = v,
                    "short" => next.short = v,
                    "long" => next.long = v,
                    "margin" => next.margin = v,
                    "bump" => next.bump = v,
                    _ => next.coast = v,
                }
                // Like the firmware: derive only from a sane wheel/cpr (no divide by zero).
                if (n == "wheel_um" || n == "cpr") && next.valid() {
                    next.rederive();
                }
                if !next.valid() {
                    return "ERR bad_arg".into();
                }
                self.params = next;
                "OK".into()
            }
            Cmd::Save => {
                if self.fail_save {
                    self.fail_save = false;
                    return "ERR io".into();
                }
                self.eeprom = Some(self.params);
                "OK".into()
            }
            Cmd::Load => {
                // Firmware quiet(): IDLE and nothing running (DEBUG counts as IDLE).
                if !quiet || self.running {
                    return "ERR bad_state".into();
                }
                match self.eeprom {
                    Some(p) => {
                        self.params = p;
                        "OK".into()
                    }
                    None => "ERR no_data".into(),
                }
            }
            Cmd::Defaults => {
                if !quiet || self.running {
                    return "ERR bad_state".into();
                }
                self.params = DEFAULTS;
                "OK".into()
            }
            Cmd::Debug(on) => {
                if !quiet {
                    return "ERR bad_state".into();
                }
                if on {
                    // Firmware enter_debug(): already in debug, it silently drops a RUN and the
                    // relay (no EVT stop); the count is untouched.
                    self.running = false;
                    self.relay = false;
                    self.vel = 0.0;
                    self.state = DevState::Debug;
                } else {
                    self.abort_run();
                    self.pristine();
                }
                "OK".into()
            }
            Cmd::Relay(on) => {
                if !in_debug {
                    return "ERR bad_state".into();
                }
                self.relay = on;
                "OK".into()
            }
            Cmd::Zero => {
                // Firmware quiet(): refused while a RUN is in progress.
                if !quiet || self.running {
                    return "ERR bad_state".into();
                }
                self.count = 0;
                self.pos = 0.0;
                self.errors = 0;
                self.settling = None;
                "OK".into()
            }
            Cmd::Run(n) => {
                if !in_debug || self.running {
                    return "ERR bad_state".into();
                }
                if n <= self.p("coast") {
                    return "ERR bad_arg".into();
                }
                self.run_target = n;
                self.running = true;
                self.relay = true;
                self.settling = None;
                self.run_started = Instant::now();
                "OK".into()
            }
            Cmd::Abort => {
                if self.running {
                    self.running = false;
                    self.relay = false;
                    self.vel = 0.0;
                    self.settling = Some((Instant::now() + self.settle, self.count, self.count));
                    self.out.push_back(format!(
                        "EVT stop target={} at_stop={} reason=abort",
                        self.run_target, self.count
                    ));
                }
                "OK".into()
            }
            Cmd::Stream(on) => {
                if !in_debug {
                    return "ERR bad_state".into();
                }
                self.streaming = on;
                self.last_key.clear();
                "OK".into()
            }
        }
    }
}

impl Transport for FakeDevice {
    fn write_line(&mut self, line: &str) -> Res<()> {
        self.advance();
        let reply = if self.drop_next > 0 {
            self.drop_next -= 1;
            "ERR busy".to_string()
        } else {
            self.handle(line.trim())
        };
        if reply.starts_with("OK") {
            self.last_host = Instant::now();
        }
        self.out.push_back(reply);
        Ok(())
    }

    fn read_line(&mut self, timeout: Duration) -> Res<Option<String>> {
        let deadline = Instant::now() + timeout;
        loop {
            if let Some(l) = self.out.pop_front() {
                return Ok(Some(l));
            }
            self.advance();
            if let Some(l) = self.out.pop_front() {
                return Ok(Some(l));
            }
            let left = deadline.saturating_duration_since(Instant::now());
            if left.is_zero() {
                return Ok(None);
            }
            sleep(left.min(Duration::from_millis(2)));
        }
    }
}
