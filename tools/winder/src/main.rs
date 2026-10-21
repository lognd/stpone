//! winder: click-to-run calibration tool for the stpone winder (web UI + headless run).

mod auto;
mod calibrate;
mod client;
mod doctor;
mod error;
mod fake;
#[cfg(test)]
mod fake_tests;
mod plain;
mod protocol;
mod serial;
mod server;

use std::cell::Cell;
use std::process::ExitCode;
use std::sync::{Arc, Mutex};

use client::{Client, Link, Shared};
use doctor::Target;
use error::{CalError, Res};

const HELP: &str = "winder - stpone calibration tool

USAGE: winder [OPTIONS] [doctor | calibrate]

  --port NAME        serial port (default: probe every port with HELLO)
  --baud N           baud rate (default 115200)
  --fake             use an in-process simulated device
  --true-coast N     follow-through of the simulated device (default 40)
  --http-port N      UI port (default 8765; a free one if taken)
  --no-browser       do not open the browser
  --list-ports       print serial ports and exit
  --help             this text

doctor: check cable, permissions, wiring and firmware; print a checklist.

calibrate (headless):
  --lengths A,B,..   pulse counts to wind (default 50000,100000)
  --measured X,Y,..  measured inches, one per wind of the first pass (at least 3
                     winds are made, so give 3 or more numbers; optional, derives wheel_um)
  --save             SAVE the result to the controller EEPROM
";

struct Stderr;

impl log::Log for Stderr {
    fn enabled(&self, _: &log::Metadata) -> bool {
        true
    }
    fn log(&self, r: &log::Record) {
        eprintln!("[{}] {}", r.level(), r.args());
    }
    fn flush(&self) {}
}

#[derive(Default)]
struct Opts {
    port: Option<String>,
    baud: Option<u32>,
    fake: bool,
    true_coast: Option<i64>,
    http_port: Option<u16>,
    no_browser: bool,
    list_ports: bool,
    help: bool,
    calibrate: bool,
    doctor: bool,
    lengths: Option<Vec<i64>>,
    measured: Vec<f64>,
    save: bool,
}

fn num<T: std::str::FromStr>(flag: &str, v: Option<String>) -> Res<T> {
    v.as_deref()
        .and_then(|s| s.parse().ok())
        .ok_or_else(|| CalError::BadInput(format!("{flag} needs a number")))
}

fn list<T: std::str::FromStr>(flag: &str, v: Option<String>) -> Res<Vec<T>> {
    v.ok_or_else(|| CalError::BadInput(format!("{flag} needs a value")))?
        .split(',')
        .map(|s| {
            s.trim()
                .parse()
                .map_err(|_| CalError::BadInput(format!("{flag}: bad item {s}")))
        })
        .collect()
}

fn parse_args(args: impl Iterator<Item = String>) -> Res<Opts> {
    let mut o = Opts::default();
    let mut it = args;
    while let Some(a) = it.next() {
        match a.as_str() {
            "--port" => {
                o.port = Some(
                    it.next()
                        .ok_or_else(|| CalError::BadInput("--port needs a name".into()))?,
                )
            }
            "--baud" => o.baud = Some(num("--baud", it.next())?),
            "--fake" => o.fake = true,
            "--true-coast" => o.true_coast = Some(num("--true-coast", it.next())?),
            "--http-port" => o.http_port = Some(num("--http-port", it.next())?),
            "--no-browser" => o.no_browser = true,
            "--list-ports" => o.list_ports = true,
            "--help" | "-h" => o.help = true,
            "calibrate" => o.calibrate = true,
            "doctor" => o.doctor = true,
            "--lengths" => o.lengths = Some(list("--lengths", it.next())?),
            "--measured" => o.measured = list("--measured", it.next())?,
            "--save" => o.save = true,
            other => {
                return Err(CalError::BadInput(format!(
                    "unknown argument {other}; see --help"
                )))
            }
        }
    }
    Ok(o)
}

fn target(o: &Opts) -> Target {
    Target {
        port: o.port.clone(),
        baud: o.baud.unwrap_or(serial::DEFAULT_BAUD),
        fake_coast: o.fake.then(|| o.true_coast.unwrap_or(40)),
    }
}

fn headless(client: &Shared, o: &Opts) -> Res<()> {
    let lengths = o.lengths.clone().unwrap_or_else(|| vec![50_000, 100_000]);
    let i = Cell::new(0usize);
    let measure = |_: &calibrate::RunRecord| {
        let k = i.get();
        i.set(k + 1);
        o.measured.get(k).copied()
    };
    let count = calibrate::guided_count(&lengths);
    if !o.measured.is_empty() && o.measured.len() != count {
        return Err(CalError::BadInput(format!(
            "--measured needs {count} numbers (one per test wind of the first pass), but {} were given",
            o.measured.len()
        )));
    }
    let report = calibrate::guided_run(client, &lengths, &measure, o.save)?;
    println!(
        "{}",
        serde_json::to_string_pretty(&report).map_err(|e| CalError::Io(e.to_string()))?
    );
    Ok(())
}

fn run() -> Res<()> {
    let o = parse_args(std::env::args().skip(1))?;
    if o.help {
        print!("{HELP}\n{}\n", doctor::machine_line());
        return Ok(());
    }
    if o.list_ports {
        for p in serial::ports() {
            let note = if p.bootloader {
                " (update-mode port, not the adapter)"
            } else {
                ""
            };
            println!("{}{note}", p.name);
        }
        return Ok(());
    }
    let link = Link::default();
    let client: Shared = Arc::new(Mutex::new(Client::new(Box::new(link.clone()))));
    let tgt = target(&o);
    if o.doctor || o.calibrate {
        let report = doctor::diagnose(&tgt, &client, &link);
        if o.doctor || !report.ok {
            let mut report = report;
            // serve() picks another port when this one is taken; the check text says so.
            let url = format!("http://127.0.0.1:{}", o.http_port.unwrap_or(8765));
            report
                .checks
                .push(doctor::browser_check(server::browser_available(), &url));
            report.ok = report.checks.iter().all(|c| c.status == doctor::Status::Ok);
            print!("{}", doctor::render_text(&report));
            return if report.ok {
                Ok(())
            } else {
                Err(CalError::BadInput("the winder is not ready".into()))
            };
        }
        return headless(&client, &o);
    }
    server::serve(
        client,
        link,
        tgt,
        o.http_port.unwrap_or(8765),
        !o.no_browser,
    )
}

fn main() -> ExitCode {
    static LOGGER: Stderr = Stderr;
    let _ = log::set_logger(&LOGGER).map(|_| log::set_max_level(log::LevelFilter::Info));
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            log::error!("{e}");
            ExitCode::FAILURE
        }
    }
}
