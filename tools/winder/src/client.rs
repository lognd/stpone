//! Synchronous Client over an abstract line Transport.

use std::collections::VecDeque;
use std::sync::{Arc, Mutex};
use std::thread::sleep;
use std::time::{Duration, Instant};

use crate::error::{CalError, Res};
use crate::protocol::*;

/// A line-oriented link: ASCII lines in, ASCII lines out.
pub trait Transport: Send {
    /// Send one line (the transport appends the newline).
    fn write_line(&mut self, line: &str) -> Res<()>;
    /// Return one line, or Ok(None) when nothing arrived in time.
    fn read_line(&mut self, timeout: Duration) -> Res<Option<String>>;
}

/// A swappable transport: lets the Client exist before (and after) a device is connected.
#[derive(Clone, Default)]
pub struct Link(Arc<Mutex<Option<Box<dyn Transport>>>>);

impl Link {
    /// Install (or remove, with None) the underlying transport.
    pub fn set(&self, t: Option<Box<dyn Transport>>) {
        *self.0.lock().expect("link lock poisoned") = t;
    }
}

impl Transport for Link {
    fn write_line(&mut self, line: &str) -> Res<()> {
        match self.0.lock().expect("link lock poisoned").as_mut() {
            Some(t) => t.write_line(line),
            None => Err(CalError::Io("no device connected".into())),
        }
    }

    fn read_line(&mut self, timeout: Duration) -> Res<Option<String>> {
        match self.0.lock().expect("link lock poisoned").as_mut() {
            Some(t) => t.read_line(timeout),
            None => Err(CalError::Io("no device connected".into())),
        }
    }
}

/// Issues commands, matches replies, and queues unsolicited events.
pub struct Client {
    t: Box<dyn Transport>,
    reply_timeout: Duration,
    pub events: VecDeque<Event>,
    pub last_dump: Option<Dump>,
    /// True while the device is believed to be in Test (DEBUG) mode.
    pub debug: bool,
    /// The most recent GET reply, so other threads can read it without touching the wire.
    pub last_snapshot: Option<Snapshot>,
    /// Longest silence tolerated in Test mode before a GET is sent (the 5 s lease).
    pub keepalive_every: Duration,
    last_ok: Instant,
    notices: VecDeque<Notice>,
    /// A reply timed out, so its late line may still be on the wire.
    stale_reply: bool,
}

/// Something the machine did on its own that the UI should tell the user about.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Notice {
    /// Test mode ended by itself (`timeout` or `panel`).
    DebugExit(String),
    /// The machine's display timed out.
    LcdFault,
}

/// A client shared between the poller, the HTTP handlers and calibration jobs.
pub type Shared = Arc<Mutex<Client>>;

const EVENT_CAP: usize = 1000;
/// How long to listen for a late reply after a timed-out command.
const STALE_DRAIN: Duration = Duration::from_millis(60);

impl Client {
    /// Wrap `t`; replies must arrive within two seconds.
    pub fn new(t: Box<dyn Transport>) -> Client {
        Client {
            t,
            reply_timeout: Duration::from_secs(2),
            events: VecDeque::new(),
            last_dump: None,
            debug: false,
            last_snapshot: None,
            keepalive_every: Duration::from_secs(1),
            last_ok: Instant::now(),
            notices: VecDeque::new(),
            stale_reply: false,
        }
    }

    /// Shorten the reply timeout (tests).
    #[cfg(test)]
    pub fn set_reply_timeout(&mut self, d: Duration) {
        self.reply_timeout = d;
    }

    /// Drain the machine-initiated notices seen so far.
    pub fn take_notices(&mut self) -> Vec<Notice> {
        self.notices.drain(..).collect()
    }

    /// In Test mode, send GET if the lease would otherwise lapse (EVT state does not renew it).
    pub fn keepalive(&mut self) -> Res<()> {
        if self.debug && self.last_ok.elapsed() >= self.keepalive_every {
            log::debug!("keepalive GET");
            self.get()?;
        }
        Ok(())
    }

    fn take_event(&mut self, line: &str) {
        match parse_event(line) {
            Ok(ev) => {
                if let Event::State(d) = &ev {
                    self.last_dump = Some((**d).clone());
                    return; // streamed state is not queued for waiters
                }
                match &ev {
                    Event::DebugExit { reason } => {
                        log::warn!("machine ended Test mode: {reason}");
                        self.debug = false;
                        self.notices.push_back(Notice::DebugExit(reason.clone()));
                    }
                    Event::LcdFault => {
                        log::warn!("machine reports a display fault");
                        self.notices.push_back(Notice::LcdFault);
                        return;
                    }
                    _ => {}
                }
                if self.events.len() >= EVENT_CAP {
                    self.events.pop_front();
                }
                self.events.push_back(ev);
            }
            Err(e) => log::warn!("dropping bad event line {line:?}: {e}"),
        }
    }

    /// Read pending lines into the event queue (replies here are strays).
    pub fn pump(&mut self, timeout: Duration) -> Res<()> {
        let deadline = Instant::now() + timeout;
        loop {
            let left = deadline.saturating_duration_since(Instant::now());
            let Some(line) = self.t.read_line(left)? else {
                break;
            };
            if line.starts_with("EVT") {
                self.take_event(&line);
            } else {
                log::warn!("stray line {line:?}");
            }
        }
        Ok(())
    }

    /// Send a command and return the tokens after OK, or the device ERR.
    pub fn command(&mut self, cmd: &Cmd) -> Res<Vec<String>> {
        match self.command_once(cmd) {
            Err(CalError::Busy) => {
                log::warn!("device was busy for {cmd:?}; retrying once");
                sleep(Duration::from_millis(60));
                self.command_once(cmd)
            }
            other => other,
        }
    }

    /// After a timeout, discard any late reply so it is not taken for the next command's.
    fn drain_stale(&mut self) -> Res<()> {
        self.stale_reply = false;
        let mut dropped = 0;
        while let Some(l) = self.t.read_line(STALE_DRAIN)? {
            if l.starts_with("EVT") {
                self.take_event(&l);
            } else {
                dropped += 1;
                log::warn!("dropping late reply {l:?} from a timed-out command");
            }
        }
        log::debug!("drained {dropped} late reply line(s)");
        Ok(())
    }

    fn command_once(&mut self, cmd: &Cmd) -> Res<Vec<String>> {
        let line = cmd.format()?;
        if self.stale_reply {
            self.drain_stale()?;
        }
        log::debug!("-> {line}");
        self.t.write_line(&line)?;
        let deadline = Instant::now() + self.reply_timeout;
        loop {
            let left = deadline.saturating_duration_since(Instant::now());
            match self.t.read_line(left)? {
                None if left.is_zero() => {
                    log::error!("timeout waiting for reply to {line}");
                    self.stale_reply = true;
                    return Err(CalError::Timeout);
                }
                None => continue,
                Some(l) if l.starts_with("EVT") => self.take_event(&l),
                Some(l) => {
                    log::debug!("<- {l}");
                    let r = split_reply(&l);
                    if r.is_ok() {
                        self.last_ok = Instant::now();
                    }
                    return r;
                }
            }
        }
    }

    fn ack(&mut self, cmd: Cmd) -> Res<()> {
        self.command(&cmd).map(|_| ())
    }

    /// HELLO: identify the device.
    pub fn hello(&mut self) -> Res<Hello> {
        parse_hello(&self.command(&Cmd::Hello)?)
    }
    /// GET: live snapshot.
    pub fn get(&mut self) -> Res<Snapshot> {
        let snap = parse_snapshot(&self.command(&Cmd::Get)?)?;
        self.debug = snap.state == DevState::Debug;
        self.last_snapshot = Some(snap.clone());
        Ok(snap)
    }
    /// PARAM: current parameters.
    pub fn param(&mut self) -> Res<Params> {
        parse_params(&self.command(&Cmd::Param)?)
    }
    /// DUMP: full controller state.
    pub fn dump(&mut self) -> Res<Dump> {
        parse_dump(&self.command(&Cmd::Dump)?)
    }
    /// SET a parameter in RAM.
    pub fn set(&mut self, name: &str, v: i64) -> Res<()> {
        self.ack(Cmd::Set(name.into(), v))
    }
    /// SAVE parameters to EEPROM.
    pub fn save(&mut self) -> Res<()> {
        self.ack(Cmd::Save)
    }
    /// DEBUG 0|1: leave or enter debug mode.
    pub fn debug(&mut self, on: bool) -> Res<()> {
        self.ack(Cmd::Debug(on))?;
        self.debug = on;
        if on {
            self.events.clear();
        }
        Ok(())
    }
    /// ZERO the count and error counter.
    pub fn zero(&mut self) -> Res<()> {
        self.ack(Cmd::Zero)
    }
    /// RUN a test wind of `pulses` (debug only).
    pub fn run(&mut self, pulses: i64) -> Res<()> {
        self.ack(Cmd::Run(pulses))
    }
}

/// Block until `pick` accepts a queued event (other events stay queued).
#[cfg(test)]
pub fn wait_event<T>(
    shared: &Shared,
    timeout: Duration,
    pick: impl Fn(&Event) -> Option<T>,
) -> Res<T> {
    let deadline = Instant::now() + timeout;
    loop {
        {
            let mut c = shared.lock().expect("client lock poisoned");
            if let Some(i) = c.events.iter().position(|e| pick(e).is_some()) {
                let ev = c.events.remove(i).expect("index just found");
                return Ok(pick(&ev).expect("picked above"));
            }
            if let Some(i) = c
                .events
                .iter()
                .position(|e| matches!(e, Event::DebugExit { .. }))
            {
                if let Some(Event::DebugExit { reason }) = c.events.remove(i) {
                    return Err(CalError::DebugEnded(reason));
                }
            }
            c.pump(Duration::from_millis(5))?;
            c.keepalive()?;
        }
        if Instant::now() >= deadline {
            log::error!("timeout waiting for an event");
            return Err(CalError::Timeout);
        }
        sleep(Duration::from_millis(2));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Scripted line device: replies are queued by the test, `hold` delays delivery.
    struct Scripted {
        lines: Arc<Mutex<VecDeque<String>>>,
        hold: Arc<Mutex<bool>>,
        on_write: VecDeque<Option<&'static str>>,
    }

    impl Transport for Scripted {
        fn write_line(&mut self, _line: &str) -> Res<()> {
            if let Some(Some(r)) = self.on_write.pop_front() {
                self.lines.lock().unwrap().push_back(r.into());
            }
            Ok(())
        }
        fn read_line(&mut self, timeout: Duration) -> Res<Option<String>> {
            if !*self.hold.lock().unwrap() {
                if let Some(l) = self.lines.lock().unwrap().pop_front() {
                    return Ok(Some(l));
                }
            }
            sleep(timeout.min(Duration::from_millis(5)));
            Ok(None)
        }
    }

    #[test]
    fn late_reply_after_a_timeout_is_not_taken_for_the_next_reply() {
        let hold = Arc::new(Mutex::new(true));
        let t = Scripted {
            lines: Arc::default(),
            hold: hold.clone(),
            // GET answers late (held); PARAM answers at once.
            on_write: VecDeque::from([
                Some("OK state=IDLE count=0 errors=0 relay=0 btn=0000"),
                Some("OK wheel_um=101638 cpr=2400 short=206187 long=618562 coast=0"),
            ]),
        };
        let mut c = Client::new(Box::new(t));
        c.set_reply_timeout(Duration::from_millis(30));
        assert_eq!(c.get().map(|_| ()), Err(CalError::Timeout));
        *hold.lock().unwrap() = false; // the late GET reply is now on the wire
        let p = c.param().expect("PARAM must get its own reply");
        assert_eq!(p.wheel_um, 101_638);
    }
}
