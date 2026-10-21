//! FakeDevice end-to-end tests through the Client.

use std::time::Duration;

use crate::client::{wait_event, Client};
use crate::error::CalError;
use crate::fake::FakeDevice;
use crate::protocol::*;
use std::sync::{Arc, Mutex};

fn fast(coast: i64) -> Client {
    let mut d = FakeDevice::new(coast);
    d.speed = 1e9;
    d.settle = Duration::ZERO;
    d.emit_speed = false;
    Client::new(Box::new(d))
}

#[test]
fn state_rules_are_enforced() {
    let mut c = fast(0);
    assert_eq!(c.hello().unwrap().proto, 1);
    assert_eq!(c.run(10), Err(CalError::BadState));
    assert_eq!(c.command(&Cmd::Relay(true)), Err(CalError::BadState));
    assert_eq!(c.command(&Cmd::Stream(true)), Err(CalError::BadState));
    c.debug(true).unwrap();
    assert_eq!(c.get().unwrap().state, DevState::Debug);
    let mut slow = Client::new(Box::new(FakeDevice::new(0)));
    slow.debug(true).unwrap();
    slow.run(100_000).unwrap();
    assert_eq!(slow.run(100), Err(CalError::BadState));
}

#[test]
fn params_save_load_defaults() {
    let mut c = fast(0);
    c.set("coast", 77).unwrap();
    assert_eq!(c.param().unwrap().coast, 77);
    c.save().unwrap();
    c.command(&Cmd::Defaults).unwrap();
    assert_eq!(c.param().unwrap().coast, FACTORY.coast);
    c.command(&Cmd::Load).unwrap();
    assert_eq!(c.param().unwrap().coast, 77);
}

#[test]
fn coast_stop_rule_and_settled_event() {
    let shared = Arc::new(Mutex::new(fast(300)));
    {
        let mut c = shared.lock().unwrap();
        c.debug(true).unwrap();
        c.set("coast", 200).unwrap();
        c.run(1000).unwrap();
    }
    let stop = wait_event(&shared, Duration::from_secs(2), |e| match e {
        Event::Stop {
            at_stop, reason, ..
        } => Some((*at_stop, reason.clone())),
        _ => None,
    })
    .unwrap();
    assert_eq!(stop, (800, "target".to_string()));
    let fin = wait_event(&shared, Duration::from_secs(2), |e| match e {
        Event::Settled {
            final_count,
            follow,
        } => Some((*final_count, *follow)),
        _ => None,
    })
    .unwrap();
    assert_eq!(fin, (1100, 300));
}

#[test]
fn dump_and_stream() {
    let mut c = fast(0);
    c.debug(true).unwrap();
    let d = c.dump().unwrap();
    assert_eq!(d.state, DevState::Debug);
    assert_eq!(d.prev, None);
    c.command(&Cmd::Stream(true)).unwrap();
    c.pump(Duration::from_millis(150)).unwrap();
    assert!(c.last_dump.is_some());
    c.debug(false).unwrap();
    assert_eq!(c.command(&Cmd::Stream(true)), Err(CalError::BadState));
}

#[test]
fn abort_emits_stop() {
    let mut d = FakeDevice::new(0);
    d.speed = 1.0;
    let shared = Arc::new(Mutex::new(Client::new(Box::new(d))));
    {
        let mut c = shared.lock().unwrap();
        c.debug(true).unwrap();
        c.run(1_000_000).unwrap();
        c.command(&Cmd::Abort).unwrap();
    }
    let r = wait_event(&shared, Duration::from_secs(2), |e| match e {
        Event::Stop { reason, .. } => Some(reason.clone()),
        _ => None,
    })
    .unwrap();
    assert_eq!(r, "abort");
}

#[test]
fn lease_is_five_seconds_by_default() {
    assert_eq!(FakeDevice::new(0).lease, Duration::from_secs(5));
}

#[test]
fn short_lease_ends_debug_with_timeout() {
    let mut d = FakeDevice::new(0);
    d.lease = Duration::from_millis(150);
    let shared = Arc::new(Mutex::new(Client::new(Box::new(d))));
    shared.lock().unwrap().debug(true).unwrap();
    shared.lock().unwrap().keepalive_every = Duration::from_secs(100);
    let r = wait_event(&shared, Duration::from_secs(2), |_| None::<()>);
    assert_eq!(r, Err(CalError::DebugEnded("timeout".into())));
    let mut c = shared.lock().unwrap();
    assert!(!c.debug);
    assert!(c
        .take_notices()
        .contains(&crate::client::Notice::DebugExit("timeout".into())));
    assert_eq!(c.get().unwrap().state, DevState::Idle);
}

#[test]
fn keepalive_holds_debug_open() {
    let mut d = FakeDevice::new(0);
    d.lease = Duration::from_millis(300);
    let shared = Arc::new(Mutex::new(Client::new(Box::new(d))));
    {
        let mut c = shared.lock().unwrap();
        c.debug(true).unwrap();
        c.keepalive_every = Duration::from_millis(50);
    }
    let r = wait_event(&shared, Duration::from_millis(900), |_| None::<()>);
    assert_eq!(r, Err(CalError::Timeout));
    assert_eq!(shared.lock().unwrap().get().unwrap().state, DevState::Debug);
}

#[test]
fn busy_is_retried_once() {
    let mut d = FakeDevice::new(0);
    d.drop_next = 1;
    let mut c = Client::new(Box::new(d));
    assert!(c.hello().is_ok());
    let mut d = FakeDevice::new(0);
    d.drop_next = 2;
    let mut c = Client::new(Box::new(d));
    assert_eq!(c.hello(), Err(CalError::Busy));
}

#[test]
fn io_and_no_data_errors() {
    let mut d = FakeDevice::new(0);
    d.fail_save = true;
    let mut c = Client::new(Box::new(d));
    assert_eq!(c.save(), Err(CalError::DeviceIo));
    assert_eq!(c.command(&Cmd::Load), Err(CalError::NothingStored));
}

#[test]
fn wheel_and_cpr_rederive_lengths() {
    let mut c = fast(0);
    let before = c.param().unwrap();
    c.set("wheel_um", before.wheel_um / 2).unwrap();
    let after = c.param().unwrap();
    assert!(after.short > before.short * 19 / 10, "{after:?}");
    c.set("cpr", 1200).unwrap();
    assert!(c.param().unwrap().short < after.short);
}

#[test]
fn run_cap_and_strict_parse() {
    let mut c = fast(0);
    c.debug(true).unwrap();
    assert!(c.run(RUN_MAX + 1).is_err());
    assert!(c.run(RUN_MAX).is_ok());
    use crate::client::Transport;
    let mut d = FakeDevice::new(0);
    for bad in [
        "SET coast +5",
        "SET coast 5x",
        "SET coast -5",
        "BOGUS",
        "SET coast 1 2",
    ] {
        d.write_line(bad).unwrap();
        let r = d.read_line(Duration::from_millis(100)).unwrap().unwrap();
        assert!(r.starts_with("ERR"), "{bad} -> {r}");
    }
}

#[test]
fn set_cpr_rederives_lengths_in_next_param() {
    let mut c = fast(0);
    assert_eq!(c.param().unwrap(), FACTORY);
    c.set("cpr", 1200).unwrap();
    let p = c.param().unwrap();
    assert_eq!(p.cpr, 1200);
    assert!(
        (p.short - FACTORY.short / 2).abs() <= 1 && (p.long - FACTORY.long / 2).abs() <= 1,
        "{p:?}"
    );
    // In-range values are accepted for every parameter.
    for (name, v) in [
        ("wheel_um", 100_000),
        ("cpr", 2000),
        ("short", 5000),
        ("long", 9000),
        ("coast", 100),
    ] {
        assert!(c.set(name, v).is_ok(), "{name}");
    }
}

#[test]
fn out_of_range_sets_are_rejected_like_the_firmware() {
    let mut c = fast(0);
    for (name, v) in [
        ("wheel_um", 49_999),
        ("wheel_um", 300_001),
        ("wheel_um", 100),
        ("cpr", 99),
        ("cpr", 100_001),
        ("short", 999),
        ("long", 4_000_001),
        ("coast", 206_187), // not below the short length
        ("margin", 2001),
        ("bump", 2),
    ] {
        assert_eq!(c.set(name, v), Err(CalError::BadArg), "{name}={v}");
    }
    assert_eq!(c.param().unwrap(), FACTORY, "rejected sets change nothing");
    // A wheel SET re-derives the lengths with the firmware's integer math.
    c.set("short", 210_000).unwrap();
    c.set("wheel_um", 101_638).unwrap();
    let p = c.param().unwrap();
    assert_eq!((p.short, p.long), (206_188, 618_563), "{p:?}");
}

#[test]
fn load_and_defaults_are_allowed_in_debug_when_not_running() {
    // Firmware quiet() is "IDLE and nothing running"; DEBUG keeps the IDLE state.
    let mut c = fast(0);
    c.set("coast", 77).unwrap();
    c.save().unwrap();
    c.debug(true).unwrap();
    c.command(&Cmd::Defaults).unwrap();
    assert_eq!(c.param().unwrap().coast, FACTORY.coast);
    c.command(&Cmd::Load).unwrap();
    assert_eq!(c.param().unwrap().coast, 77);
    let mut slow = Client::new(Box::new(FakeDevice::new(0)));
    slow.debug(true).unwrap();
    slow.run(100_000).unwrap();
    assert_eq!(slow.command(&Cmd::Load), Err(CalError::BadState));
    assert_eq!(slow.command(&Cmd::Defaults), Err(CalError::BadState));
}

#[test]
fn zero_is_refused_during_a_run() {
    let mut slow = Client::new(Box::new(FakeDevice::new(0)));
    slow.debug(true).unwrap();
    slow.command(&Cmd::Zero).unwrap();
    slow.run(100_000).unwrap();
    assert_eq!(slow.command(&Cmd::Zero), Err(CalError::BadState));
}

#[test]
fn debug_one_while_in_debug_silently_drops_the_run_and_relay() {
    let mut slow = Client::new(Box::new(FakeDevice::new(0)));
    slow.debug(true).unwrap();
    slow.run(100_000).unwrap();
    assert!(slow.get().unwrap().relay);
    slow.debug(true).unwrap(); // firmware enter_debug(): no EVT stop
    let g = slow.get().unwrap();
    assert!(!g.relay);
    assert_eq!(g.state, DevState::Debug);
    assert!(
        !slow.events.iter().any(|e| matches!(e, Event::Stop { .. })),
        "{:?}",
        slow.events
    );
    slow.run(5000).unwrap(); // a new RUN is accepted again (longer than the factory coast)
}

#[test]
fn settled_final_is_a_magnitude_even_when_the_encoder_counts_backwards() {
    let mut d = FakeDevice::new(300);
    d.speed = 1e9;
    d.settle = Duration::ZERO;
    d.reversed = true;
    let shared = Arc::new(Mutex::new(Client::new(Box::new(d))));
    {
        let mut c = shared.lock().unwrap();
        c.debug(true).unwrap();
        c.set("coast", 0).unwrap();
        c.run(1000).unwrap();
    }
    let fin = wait_event(&shared, Duration::from_secs(2), |e| match e {
        Event::Settled {
            final_count,
            follow,
        } => Some((*final_count, *follow)),
        _ => None,
    })
    .unwrap();
    assert_eq!(fin, (1300, 300));
    assert!(
        shared.lock().unwrap().get().unwrap().count < 0,
        "GET stays signed"
    );
}
