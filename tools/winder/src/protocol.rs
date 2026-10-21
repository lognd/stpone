//! Pure models and parse/format functions for the host link protocol (no I/O).
//! Contract: docs/hardware/serial-protocol.md.

use std::collections::HashMap;

use serde::Serialize;

use crate::error::{CalError, Res};

pub const MAX_LINE_BYTES: usize = 87;
/// Largest pulse count the firmware accepts for RUN.
pub const RUN_MAX: i64 = 4_000_000;
pub const UM_PER_INCH: f64 = 25400.0;
/// Wheel diameter range (um) the firmware accepts (Params::valid).
pub const WHEEL_UM_MIN: i64 = 50_000;
pub const WHEEL_UM_MAX: i64 = 300_000;
pub const PARAM_NAMES: [&str; 7] = [
    "wheel_um", "cpr", "short", "long", "coast", "margin", "bump",
];
/// Largest margin (pulses) the firmware accepts (Params::valid, kMaxMarginPulses).
pub const MARGIN_MAX: i64 = 2000;

/// Controller states reported by GET and DUMP.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "UPPERCASE")]
pub enum DevState {
    Idle,
    Winding,
    Paused,
    /// Panel wind finishing with short bumps (firmware 6, DEC-010).
    Trim,
    Debug,
    /// A state name this tool does not know (a newer firmware): shown, never an error.
    Unknown,
}

impl DevState {
    /// Wire name of the state.
    pub fn as_str(self) -> &'static str {
        match self {
            DevState::Idle => "IDLE",
            DevState::Winding => "WINDING",
            DevState::Paused => "PAUSED",
            DevState::Trim => "TRIM",
            DevState::Debug => "DEBUG",
            DevState::Unknown => "UNKNOWN",
        }
    }

    /// Parse a wire name.
    pub fn parse(s: &str) -> Res<DevState> {
        match s {
            "IDLE" => Ok(DevState::Idle),
            "WINDING" => Ok(DevState::Winding),
            "PAUSED" => Ok(DevState::Paused),
            "TRIM" => Ok(DevState::Trim),
            "DEBUG" => Ok(DevState::Debug),
            other if !other.is_empty() && other.bytes().all(|c| c.is_ascii_uppercase()) => {
                log::warn!("unknown device state {other}; firmware newer than this tool?");
                Ok(DevState::Unknown)
            }
            _ => Err(CalError::Malformed(format!("state {s}"))),
        }
    }
}

/// Every host command, with its validated arguments.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Cmd {
    Hello,
    Get,
    Param,
    Set(String, i64),
    Save,
    Load,
    Defaults,
    Debug(bool),
    Relay(bool),
    Zero,
    Run(i64),
    Abort,
    Dump,
    Stream(bool),
}

/// Whole-token unsigned decimal, exactly as the firmware accepts it (no sign, no junk).
fn int(s: &str) -> Res<i64> {
    if s.is_empty() || !s.bytes().all(|c| c.is_ascii_digit()) {
        return Err(CalError::BadInput(format!("not a whole number: {s}")));
    }
    s.parse::<i64>()
        .map_err(|_| CalError::BadInput(format!("number too large: {s}")))
}

fn flag(s: &str) -> Res<bool> {
    match s {
        "0" => Ok(false),
        "1" => Ok(true),
        _ => Err(CalError::BadInput(format!("not 0 or 1: {s}"))),
    }
}

impl Cmd {
    /// Build a command from a verb and string arguments, validating arity and range.
    pub fn from_parts(verb: &str, args: &[String]) -> Res<Cmd> {
        let n = args.len();
        let arity = |want: usize| -> Res<()> {
            if n == want {
                Ok(())
            } else {
                Err(CalError::BadInput(format!(
                    "{verb} takes {want} argument(s)"
                )))
            }
        };
        match verb.to_ascii_uppercase().as_str() {
            "HELLO" => arity(0).map(|_| Cmd::Hello),
            "GET" => arity(0).map(|_| Cmd::Get),
            "PARAM" => arity(0).map(|_| Cmd::Param),
            "SAVE" => arity(0).map(|_| Cmd::Save),
            "LOAD" => arity(0).map(|_| Cmd::Load),
            "DEFAULTS" => arity(0).map(|_| Cmd::Defaults),
            "ZERO" => arity(0).map(|_| Cmd::Zero),
            "ABORT" => arity(0).map(|_| Cmd::Abort),
            "DUMP" => arity(0).map(|_| Cmd::Dump),
            "DEBUG" => arity(1).and_then(|_| flag(&args[0])).map(Cmd::Debug),
            "RELAY" => arity(1).and_then(|_| flag(&args[0])).map(Cmd::Relay),
            "STREAM" => arity(1).and_then(|_| flag(&args[0])).map(Cmd::Stream),
            "RUN" => {
                arity(1)?;
                let v = int(&args[0])?;
                if (1..=RUN_MAX).contains(&v) {
                    Ok(Cmd::Run(v))
                } else {
                    Err(CalError::BadInput(format!(
                        "RUN needs 1 to {RUN_MAX} pulses"
                    )))
                }
            }
            "SET" => {
                arity(2)?;
                if !PARAM_NAMES.contains(&args[0].as_str()) {
                    return Err(CalError::BadInput(format!("unknown parameter {}", args[0])));
                }
                let v = int(&args[1])?;
                Ok(Cmd::Set(args[0].clone(), v))
            }
            other => Err(CalError::BadInput(format!("unknown command {other}"))),
        }
    }

    /// Render the command line (no newline).
    pub fn format(&self) -> Res<String> {
        let b = |v: &bool| if *v { 1 } else { 0 };
        let line = match self {
            Cmd::Hello => "HELLO".to_string(),
            Cmd::Get => "GET".into(),
            Cmd::Param => "PARAM".into(),
            Cmd::Save => "SAVE".into(),
            Cmd::Load => "LOAD".into(),
            Cmd::Defaults => "DEFAULTS".into(),
            Cmd::Zero => "ZERO".into(),
            Cmd::Abort => "ABORT".into(),
            Cmd::Dump => "DUMP".into(),
            Cmd::Set(n, v) => format!("SET {n} {v}"),
            Cmd::Debug(v) => format!("DEBUG {}", b(v)),
            Cmd::Relay(v) => format!("RELAY {}", b(v)),
            Cmd::Stream(v) => format!("STREAM {}", b(v)),
            Cmd::Run(p) => format!("RUN {p}"),
        };
        if line.len() > MAX_LINE_BYTES {
            return Err(CalError::BadInput("line too long".into()));
        }
        Ok(line)
    }
}

/// Reply to HELLO.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Hello {
    pub proto: i64,
    pub fw: String,
}

/// Reply to GET.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Snapshot {
    pub state: DevState,
    pub count: i64,
    /// Pulses per second (firmware 4 and later).
    pub speed: Option<u32>,
    pub errors: i64,
    pub relay: bool,
    pub btn: String,
    /// Pass flag of the last panel wind (firmware 6): Some(true) PASS, Some(false) SHORT or
    /// STOPPED, None while running, never run, legacy mode or older firmware.
    pub pass: Option<bool>,
}

/// Reply to PARAM.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Params {
    pub wheel_um: i64,
    pub cpr: i64,
    pub short: i64,
    pub long: i64,
    pub coast: i64,
    /// Safety allowance added to the target (pulses; 0 on firmware older than 6).
    pub margin: i64,
    /// 1 = bump (trim) mode with the minimum-amount guarantee, 0 = legacy single cut.
    pub bump: i64,
}

/// Factory settings the firmware loads on DEFAULTS (shown by the UI next to the live values).
pub const FACTORY: Params = Params {
    wheel_um: 101638,
    cpr: 2400,
    short: 206187,
    long: 618562,
    // 72 in (2 yd): the main run stops that far short and the trim bumps the rest (DEC-012).
    coast: 13746,
    margin: 191,
    bump: 1,
};

/// Reply to DUMP and payload of EVT state.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Dump {
    pub state: DevState,
    /// Reserved by the firmware: always `-` (None).
    pub prev: Option<DevState>,
    pub count: i64,
    /// Pulses per second (firmware 4 and later).
    pub speed: Option<u32>,
    pub errors: i64,
    pub relay: bool,
    pub manual: bool,
    pub target: i64,
    pub eff_target: i64,
    pub coast: i64,
    pub btn: String,
    pub toggles: Vec<i64>,
    pub pending: Vec<i64>,
    pub enc_dir: i64,
    pub uptime_ms: i64,
    pub run: bool,
    /// Never-used stack bytes (firmware 5 and later).
    pub stack_free: Option<u32>,
    /// Longest main-loop pass in ms since boot or ZERO (firmware 5 and later).
    pub pass_max_ms: Option<u32>,
    /// Worst encoder-ISR entry latency in CPU cycles (poll period 400; firmware 5 and later).
    pub isr_lat_max: Option<u32>,
    /// Safety allowance in pulses (firmware 6 and later).
    pub margin: Option<u32>,
    /// Bump (trim) mode switch, 1 or 0 (firmware 6 and later).
    pub bump: Option<u32>,
    /// Bumps made in the current or last wind (firmware 6 and later).
    pub bumps: Option<u32>,
    /// The trim phase is active, running or paused (firmware 6 and later).
    pub trim: Option<bool>,
    /// Pass flag of the last panel wind, as in [`Snapshot::pass`] (firmware 6 and later).
    pub pass: Option<bool>,
}

/// Unsolicited device events.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Event {
    Stop {
        target: i64,
        at_stop: i64,
        reason: String,
    },
    Settled {
        final_count: i64,
        follow: i64,
    },
    State(Box<Dump>),
    /// Debug mode ended by itself; reason is `timeout` or `panel`.
    DebugExit {
        reason: String,
    },
    /// The display timed out (debug only); informational.
    LcdFault,
}

/// key=value fields of a reply body.
pub struct Fields<'a>(HashMap<&'a str, &'a str>);

impl<'a> Fields<'a> {
    /// Split `key=value` tokens.
    pub fn parse(tokens: &[&'a str]) -> Res<Fields<'a>> {
        let mut m = HashMap::new();
        for t in tokens {
            match t.split_once('=') {
                Some((k, v)) if !k.is_empty() => {
                    m.insert(k, v);
                }
                _ => return Err(CalError::Malformed(format!("field {t}"))),
            }
        }
        Ok(Fields(m))
    }

    fn opt_u32(&self, k: &str) -> Res<Option<u32>> {
        match self.0.get(k) {
            None => Ok(None),
            Some(v) => v
                .parse()
                .map(Some)
                .map_err(|_| CalError::Malformed(format!("bad integer for {k}"))),
        }
    }

    /// Optional 0/1 flag.
    fn opt_b(&self, k: &str) -> Res<Option<bool>> {
        match self.0.get(k) {
            None => Ok(None),
            Some(&"0") => Ok(Some(false)),
            Some(&"1") => Ok(Some(true)),
            Some(_) => Err(CalError::Malformed(format!("bad flag for {k}"))),
        }
    }

    /// The `pass=` flag: `-` (or absent) is None, `0`/`1` are the verdict.
    fn pass(&self) -> Res<Option<bool>> {
        match self.0.get("pass") {
            None | Some(&"-") => Ok(None),
            Some(&"0") => Ok(Some(false)),
            Some(&"1") => Ok(Some(true)),
            Some(_) => Err(CalError::Malformed("bad pass flag".into())),
        }
    }

    fn s(&self, k: &str) -> Res<&'a str> {
        self.0
            .get(k)
            .copied()
            .ok_or_else(|| CalError::Malformed(format!("missing {k}")))
    }

    fn i(&self, k: &str) -> Res<i64> {
        self.s(k)?
            .parse()
            .map_err(|_| CalError::Malformed(format!("bad integer for {k}")))
    }

    fn b(&self, k: &str) -> Res<bool> {
        match self.s(k)? {
            "0" => Ok(false),
            "1" => Ok(true),
            _ => Err(CalError::Malformed(format!("bad flag for {k}"))),
        }
    }

    fn bits(&self, k: &str) -> Res<String> {
        let v = self.s(k)?;
        if v.len() == 4 && v.bytes().all(|c| c == b'0' || c == b'1') {
            Ok(v.to_string())
        } else {
            Err(CalError::Malformed(format!("bad bits for {k}")))
        }
    }

    fn list(&self, k: &str) -> Res<Vec<i64>> {
        self.s(k)?
            .split(',')
            .map(|p| {
                p.parse()
                    .map_err(|_| CalError::Malformed(format!("bad list for {k}")))
            })
            .collect()
    }
}

/// Split an OK/ERR reply: tokens after OK, or the device error code as Err.
pub fn split_reply(line: &str) -> Res<Vec<String>> {
    let toks: Vec<&str> = line.split_whitespace().collect();
    match toks.as_slice() {
        ["OK", rest @ ..] => Ok(rest.iter().map(|s| s.to_string()).collect()),
        ["ERR", code] => Err(match *code {
            "bad_command" => CalError::BadCommand,
            "bad_arg" => CalError::BadArg,
            "bad_state" => CalError::BadState,
            "busy" => CalError::Busy,
            "io" => CalError::DeviceIo,
            "no_data" => CalError::NothingStored,
            other => CalError::Malformed(format!("ERR {other}")),
        }),
        _ => Err(CalError::Malformed(line.to_string())),
    }
}

fn refs(tokens: &[String]) -> Vec<&str> {
    tokens.iter().map(String::as_str).collect()
}

/// Parse `stpone proto=1 fw=x` (tokens after OK).
pub fn parse_hello(tokens: &[String]) -> Res<Hello> {
    let t = refs(tokens);
    if t.first() != Some(&"stpone") {
        return Err(CalError::Malformed("HELLO reply".into()));
    }
    let f = Fields::parse(&t[1..])?;
    Ok(Hello {
        proto: f.i("proto")?,
        fw: f.s("fw")?.to_string(),
    })
}

/// Parse a GET reply body.
pub fn parse_snapshot(tokens: &[String]) -> Res<Snapshot> {
    let f = Fields::parse(&refs(tokens))?;
    Ok(Snapshot {
        state: DevState::parse(f.s("state")?)?,
        count: f.i("count")?,
        speed: f.opt_u32("speed")?,
        errors: f.i("errors")?,
        relay: f.b("relay")?,
        btn: f.bits("btn")?,
        pass: f.pass()?,
    })
}

/// Parse a PARAM reply body.
pub fn parse_params(tokens: &[String]) -> Res<Params> {
    let f = Fields::parse(&refs(tokens))?;
    Ok(Params {
        wheel_um: f.i("wheel_um")?,
        cpr: f.i("cpr")?,
        short: f.i("short")?,
        long: f.i("long")?,
        coast: f.i("coast")?,
        margin: f.opt_u32("margin")?.map_or(0, i64::from),
        bump: f.opt_u32("bump")?.map_or(0, i64::from),
    })
}

/// Parse a DUMP reply body (or the tail of an EVT state line).
pub fn parse_dump(tokens: &[String]) -> Res<Dump> {
    let f = Fields::parse(&refs(tokens))?;
    let enc = f.i("enc_dir")?;
    if !(-1..=1).contains(&enc) {
        return Err(CalError::Malformed("enc_dir".into()));
    }
    Ok(Dump {
        state: DevState::parse(f.s("state")?)?,
        prev: match f.s("prev")? {
            "-" => None,
            other => Some(DevState::parse(other)?),
        },
        count: f.i("count")?,
        speed: f.opt_u32("speed")?,
        errors: f.i("errors")?,
        relay: f.b("relay")?,
        manual: f.b("manual")?,
        target: f.i("target")?,
        eff_target: f.i("eff_target")?,
        coast: f.i("coast")?,
        btn: f.bits("btn")?,
        toggles: f.list("toggles")?,
        pending: f.list("pending")?,
        enc_dir: enc,
        uptime_ms: f.i("uptime_ms")?,
        run: f.b("run")?,
        stack_free: f.opt_u32("stack_free")?,
        pass_max_ms: f.opt_u32("pass_max_ms")?,
        isr_lat_max: f.opt_u32("isr_lat_max")?,
        margin: f.opt_u32("margin")?,
        bump: f.opt_u32("bump")?,
        bumps: f.opt_u32("bumps")?,
        trim: f.opt_b("trim")?,
        pass: f.pass()?,
    })
}

/// Parse one unsolicited `EVT ...` line.
pub fn parse_event(line: &str) -> Res<Event> {
    let toks: Vec<&str> = line.split_whitespace().collect();
    match toks.as_slice() {
        ["EVT", "state", rest @ ..] => {
            let owned: Vec<String> = rest.iter().map(|s| s.to_string()).collect();
            parse_dump(&owned).map(|d| Event::State(Box::new(d)))
        }
        ["EVT", "stop", rest @ ..] => {
            let f = Fields::parse(rest)?;
            let reason = f.s("reason")?;
            if reason != "target" && reason != "abort" {
                return Err(CalError::Malformed("stop reason".into()));
            }
            Ok(Event::Stop {
                target: f.i("target")?,
                at_stop: f.i("at_stop")?,
                reason: reason.to_string(),
            })
        }
        ["EVT", "settled", rest @ ..] => {
            let f = Fields::parse(rest)?;
            Ok(Event::Settled {
                final_count: f.i("final")?,
                follow: f.i("follow")?,
            })
        }
        ["EVT", "debug_exit", rest @ ..] => {
            let f = Fields::parse(rest)?;
            let reason = f.s("reason")?;
            if reason != "timeout" && reason != "panel" {
                return Err(CalError::Malformed("debug_exit reason".into()));
            }
            Ok(Event::DebugExit {
                reason: reason.to_string(),
            })
        }
        ["EVT", "lcd_fault"] => Ok(Event::LcdFault),
        _ => Err(CalError::Malformed(line.to_string())),
    }
}

/// Render a Dump as the DUMP reply body (used by the fake device).
pub fn format_dump(d: &Dump) -> String {
    let csv = |v: &[i64]| {
        v.iter()
            .map(|x| x.to_string())
            .collect::<Vec<_>>()
            .join(",")
    };
    let b = |v: bool| if v { 1 } else { 0 };
    let speed = d.speed.map_or(String::new(), |v| format!(" speed={v}"));
    let mut tail: String = [
        ("stack_free", d.stack_free),
        ("pass_max_ms", d.pass_max_ms),
        ("isr_lat_max", d.isr_lat_max),
        ("margin", d.margin),
        ("bump", d.bump),
        ("bumps", d.bumps),
    ]
    .iter()
    .filter_map(|(k, v)| v.map(|v| format!(" {k}={v}")))
    .collect();
    if let Some(t) = d.trim {
        tail.push_str(&format!(" trim={}", b(t)));
    }
    if d.trim.is_some() || d.pass.is_some() {
        tail.push_str(&format!(
            " pass={}",
            d.pass.map_or("-", |p| if p { "1" } else { "0" })
        ));
    }
    format!(
        "state={} prev={} count={}{speed} errors={} relay={} manual={} target={} \
         eff_target={} coast={} btn={} toggles={} pending={} enc_dir={} uptime_ms={} run={}{tail}",
        d.state.as_str(),
        d.prev.map_or("-", DevState::as_str),
        d.count,
        d.errors,
        b(d.relay),
        b(d.manual),
        d.target,
        d.eff_target,
        d.coast,
        d.btn,
        csv(&d.toggles),
        csv(&d.pending),
        d.enc_dir,
        d.uptime_ms,
        b(d.run)
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sv(s: &str) -> Vec<String> {
        s.split_whitespace().map(String::from).collect()
    }

    const DUMP: &str = "state=DEBUG prev=- count=-5 speed=1200 errors=1 relay=1 manual=0 target=100 \
        eff_target=90 coast=10 btn=0101 toggles=0,1,0,0 pending=1,0,0,0 enc_dir=-1 uptime_ms=42 run=1";

    #[test]
    fn commands_format_and_validate() {
        assert_eq!(Cmd::Set("coast".into(), 5).format().unwrap(), "SET coast 5");
        assert_eq!(Cmd::Run(10).format().unwrap(), "RUN 10");
        assert_eq!(Cmd::Stream(true).format().unwrap(), "STREAM 1");
        assert!(Cmd::from_parts("set", &sv("bogus 1")).is_err());
        assert!(Cmd::from_parts("RUN", &sv("0")).is_err());
        assert!(Cmd::from_parts("DEBUG", &sv("2")).is_err());
        assert!(Cmd::from_parts("GET", &sv("x")).is_err());
        assert!(Cmd::from_parts("SET", &sv("coast -5")).is_err());
        assert!(Cmd::from_parts("SET", &sv("coast +5")).is_err());
        assert!(Cmd::from_parts("SET", &sv("coast 5x")).is_err());
        assert!(Cmd::from_parts("SET", &sv("cpr 2400")).is_ok());
        assert!(Cmd::from_parts("RUN", &sv("4000001")).is_err());
        assert!(Cmd::from_parts("RUN", &sv("4000000")).is_ok());
        assert_eq!(Cmd::from_parts("run", &sv("7")).unwrap(), Cmd::Run(7));
    }

    #[test]
    fn replies_split_and_parse() {
        let h = parse_hello(&split_reply("OK stpone proto=1 fw=1.2").unwrap()).unwrap();
        assert_eq!(
            h,
            Hello {
                proto: 1,
                fw: "1.2".into()
            }
        );
        assert_eq!(split_reply("ERR busy"), Err(CalError::Busy));
        assert_eq!(split_reply("ERR bad_state"), Err(CalError::BadState));
        assert_eq!(split_reply("ERR io"), Err(CalError::DeviceIo));
        assert_eq!(split_reply("ERR no_data"), Err(CalError::NothingStored));
        let s = parse_snapshot(&sv(
            "state=DEBUG count=5 speed=900 errors=0 relay=1 btn=0000",
        ))
        .unwrap();
        assert_eq!(s.speed, Some(900));
        let s = parse_snapshot(&sv("state=DEBUG count=5 errors=0 relay=1 btn=0000")).unwrap();
        assert_eq!(s.speed, None);
        assert!(split_reply("garbage").is_err());
        let s = parse_snapshot(&sv("state=IDLE count=-3 errors=0 relay=0 btn=1010")).unwrap();
        assert_eq!((s.count, s.state), (-3, DevState::Idle));
        assert!(parse_snapshot(&sv("state=IDLE count=x errors=0 relay=0 btn=1010")).is_err());
        let p = parse_params(&sv("wheel_um=1 cpr=2 short=3 long=4 coast=5")).unwrap();
        assert_eq!(p.coast, 5);
    }

    #[test]
    fn firmware_5_dump_fields_are_optional_and_round_trip() {
        let old = parse_dump(&sv(DUMP)).unwrap();
        assert_eq!(
            (old.stack_free, old.pass_max_ms, old.isr_lat_max),
            (None, None, None)
        );
        let text = format!("{DUMP} stack_free=321 pass_max_ms=47 isr_lat_max=110");
        let d = parse_dump(&sv(&text)).unwrap();
        assert_eq!(
            (d.stack_free, d.pass_max_ms, d.isr_lat_max),
            (Some(321), Some(47), Some(110))
        );
        assert_eq!(format_dump(&d), text);
    }

    #[test]
    fn firmware_6_fields_are_optional_and_round_trip() {
        let old = parse_dump(&sv(DUMP)).unwrap();
        assert_eq!(
            (old.margin, old.bump, old.bumps, old.trim, old.pass),
            (None, None, None, None, None)
        );
        let text = format!("{DUMP} margin=191 bump=1 bumps=3 trim=1 pass=-");
        let d = parse_dump(&sv(&text)).unwrap();
        assert_eq!(
            (d.margin, d.bump, d.bumps, d.trim, d.pass),
            (Some(191), Some(1), Some(3), Some(true), None)
        );
        assert_eq!(format_dump(&d), text);
        let done = parse_dump(&sv(&format!(
            "{DUMP} margin=0 bump=0 bumps=2 trim=0 pass=0"
        )))
        .unwrap();
        assert_eq!(done.pass, Some(false));
        let ok = parse_dump(&sv(&format!(
            "{DUMP} margin=0 bump=0 bumps=2 trim=0 pass=1"
        )))
        .unwrap();
        assert_eq!(ok.pass, Some(true));
        assert!(parse_dump(&sv(&format!("{DUMP} trim=2"))).is_err());
        assert!(parse_dump(&sv(&format!("{DUMP} pass=x"))).is_err());
        // GET and PARAM: fw<6 omit the new fields; fw6 reports them
        let g = parse_snapshot(&sv("state=TRIM count=5 errors=0 relay=1 btn=0000 pass=-")).unwrap();
        assert_eq!((g.state, g.pass), (DevState::Trim, None));
        let g = parse_snapshot(&sv("state=IDLE count=5 errors=0 relay=0 btn=0000 pass=0")).unwrap();
        assert_eq!(g.pass, Some(false));
        let g = parse_snapshot(&sv("state=IDLE count=5 errors=0 relay=0 btn=0000")).unwrap();
        assert_eq!(g.pass, None);
        let p = parse_params(&sv("wheel_um=1 cpr=2 short=3 long=4 coast=5")).unwrap();
        assert_eq!((p.margin, p.bump), (0, 0));
        let p = parse_params(&sv(
            "wheel_um=1 cpr=2 short=3 long=4 coast=5 margin=191 bump=1",
        ))
        .unwrap();
        assert_eq!((p.margin, p.bump), (191, 1));
    }

    #[test]
    fn an_unknown_state_name_does_not_break_the_parse() {
        assert_eq!(DevState::parse("TRIM").unwrap(), DevState::Trim);
        assert_eq!(DevState::parse("HOMING").unwrap(), DevState::Unknown);
        assert!(DevState::parse("").is_err());
        assert!(DevState::parse("idle").is_err());
        let s = parse_snapshot(&sv("state=HOMING count=1 errors=0 relay=0 btn=0000")).unwrap();
        assert_eq!(s.state, DevState::Unknown);
    }

    #[test]
    fn set_accepts_the_new_parameter_names() {
        assert_eq!(
            Cmd::Set("margin".into(), 191).format().unwrap(),
            "SET margin 191"
        );
        assert_eq!(Cmd::Set("bump".into(), 0).format().unwrap(), "SET bump 0");
        assert!(Cmd::from_parts("SET", &sv("margin 191")).is_ok());
        assert!(Cmd::from_parts("SET", &sv("bump 1")).is_ok());
    }

    #[test]
    fn dump_and_events_round_trip() {
        let d = parse_dump(&sv(DUMP)).unwrap();
        assert_eq!(d.toggles, vec![0, 1, 0, 0]);
        assert_eq!(d.enc_dir, -1);
        assert_eq!(
            format_dump(&d),
            DUMP.split_whitespace().collect::<Vec<_>>().join(" ")
        );
        match parse_event(&format!("EVT state {DUMP}")).unwrap() {
            Event::State(x) => assert_eq!(*x, d),
            e => panic!("{e:?}"),
        }
        assert_eq!(
            parse_event("EVT stop target=10 at_stop=9 reason=abort").unwrap(),
            Event::Stop {
                target: 10,
                at_stop: 9,
                reason: "abort".into()
            }
        );
        assert_eq!(
            parse_event("EVT settled final=11 follow=2").unwrap(),
            Event::Settled {
                final_count: 11,
                follow: 2
            }
        );
        assert_eq!(
            parse_event("EVT debug_exit reason=timeout").unwrap(),
            Event::DebugExit {
                reason: "timeout".into()
            }
        );
        assert_eq!(
            parse_event("EVT debug_exit reason=panel").unwrap(),
            Event::DebugExit {
                reason: "panel".into()
            }
        );
        assert_eq!(parse_event("EVT lcd_fault").unwrap(), Event::LcdFault);
        assert!(parse_event("EVT debug_exit reason=x").is_err());
        assert!(parse_event("EVT nope").is_err());
        assert!(parse_event("EVT stop target=1 at_stop=1 reason=x").is_err());
    }
}
