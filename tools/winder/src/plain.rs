//! Plain-language wording: what a command did, and the big status banner.

use serde::Serialize;

use crate::error::CalError;

use crate::protocol::{Cmd, DevState, Dump, Snapshot};

/// Sentence for the activity log describing what a command does.
pub fn describe(cmd: &Cmd) -> String {
    match cmd {
        Cmd::Hello => "Said hello to the winder.".into(),
        Cmd::Get => "Read the winder's status.".into(),
        Cmd::Param => "Read the winder's settings.".into(),
        Cmd::Set(n, v) => format!("Changed the setting '{}' to {v}.", setting_name(n)),
        Cmd::Save => "Saved the settings so they survive power-off.".into(),
        Cmd::Load => "Re-loaded the saved settings.".into(),
        Cmd::Defaults => "Restored the factory settings (not yet saved).".into(),
        Cmd::Debug(true) => {
            "Turned Test mode ON. The machine only moves when you press a test button here.".into()
        }
        Cmd::Debug(false) => "Turned Test mode OFF. The machine is back to normal.".into(),
        Cmd::Relay(true) => "Switched the motor relay ON.".into(),
        Cmd::Relay(false) => "Switched the motor relay OFF.".into(),
        Cmd::Zero => "Reset the position counter to 0.".into(),
        Cmd::Run(n) => format!("Started a test wind of {n} pulses."),
        Cmd::Abort => "Stopped the test wind.".into(),
        Cmd::Dump => "Read the winder's inner state.".into(),
        Cmd::Stream(true) => "Started live updates from the winder.".into(),
        Cmd::Stream(false) => "Stopped live updates from the winder.".into(),
    }
}

/// Friendly name of a setting.
pub fn setting_name(raw: &str) -> &str {
    match raw {
        "wheel_um" => "wheel size (micrometres)",
        "cpr" => "encoder counts per turn",
        "short" => "short length (pulses)",
        "long" => "long length (pulses)",
        "coast" => "coast (pulses the motor runs on after stopping)",
        "margin" => "margin (extra pulses added to the target)",
        "bump" => "bump mode (1 = finish with short bumps, 0 = single stop)",
        other => other,
    }
}

/// Plain-English reason the winder refused `SET name`, naming the setting and what to do.
pub fn set_refusal(name: &str, e: &CalError) -> String {
    let what = setting_name(name);
    match e {
        CalError::BadArg => format!(
            "The winder refused that value for the {what}. It must be a whole number above zero, and the coast must stay smaller than the short length. Type a different number, or use Reset to factory settings."
        ),
        CalError::BadState => format!(
            "The {what} cannot be changed right now. Stop any winding first (the machine must be idle or in Test mode, not running), then try again."
        ),
        other => other.human(),
    }
}

/// The big status banner and the "what to do next" line.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Banner {
    /// One of: problem, idle, busy, manual, test.
    pub kind: &'static str,
    pub text: String,
    pub next: String,
}

/// Inputs that decide the banner.
pub struct BannerIn<'a> {
    pub connected: bool,
    pub checking: bool,
    pub snapshot: Option<&'a Snapshot>,
    pub dump: Option<&'a Dump>,
    /// (current run number, planned runs) while a calibration job runs.
    pub job: Option<(usize, usize)>,
}

fn b(kind: &'static str, text: impl Into<String>, next: impl Into<String>) -> Banner {
    Banner {
        kind,
        text: text.into(),
        next: next.into(),
    }
}

/// Decide the banner from the live state.
pub fn banner(i: &BannerIn) -> Banner {
    if i.checking {
        return b(
            "busy",
            "Checking the connection...",
            "Please wait a few seconds.",
        );
    }
    if !i.connected {
        return b(
            "problem",
            "Not connected to the winder",
            "Follow the red items in the checklist, then press Check again.",
        );
    }
    if let Some((n, m)) = i.job {
        return b(
            "busy",
            format!("Calibration run {n} of {m}"),
            "Do not touch the machine until this finishes.",
        );
    }
    let Some(s) = i.snapshot else {
        return b(
            "problem",
            "The winder stopped answering",
            "Check the cable and power, then press Check again.",
        );
    };
    match s.state {
        DevState::Debug => b(
            "test",
            "Test mode",
            "Use the test buttons below. Turn Test mode off when you are done.",
        ),
        DevState::Winding => {
            let pct = i
                .dump
                .filter(|d| d.eff_target != 0)
                .map(|d| (s.count.abs() * 100 / d.eff_target.abs()).clamp(0, 100));
            let text = pct.map_or("Winding".to_string(), |p| format!("Winding {p}%"));
            b("busy", text, "Do not touch the machine until it finishes.")
        }
        DevState::Trim => b(
            "busy",
            "Finishing the wind (trimming)",
            "Do not touch the machine until it finishes.",
        ),
        DevState::Unknown => b(
            "problem",
            "The winder reports a state this program does not know",
            "Update this program, or check the machine's own display.",
        ),
        DevState::Idle if s.pass == Some(false) => b(
            "problem",
            "UNDER TARGET - the last wind came out short",
            "Measure the paper. Press any button on the machine to clear its alarm.",
        ),
        DevState::Paused | DevState::Idle if i.dump.map_or(s.relay, |d| d.manual) => b(
            "manual",
            "Manual motor ON",
            "Click button 4 on the machine to switch manual off.",
        ),
        DevState::Paused => b(
            "idle",
            "Paused",
            "Use the machine's own buttons to continue or stop.",
        ),
        DevState::Idle => b(
            "idle",
            "Idle - ready",
            "To calibrate, turn on Test mode below and add test winds.",
        ),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn snap(state: DevState, relay: bool, count: i64) -> Snapshot {
        Snapshot {
            state,
            count,
            errors: 0,
            speed: None,
            relay,
            btn: "0000".into(),
            pass: None,
        }
    }

    #[test]
    fn trim_unknown_and_failed_wind_banners() {
        assert_eq!(
            banner(&input(Some(&snap(DevState::Trim, true, 5)))).kind,
            "busy"
        );
        assert_eq!(
            banner(&input(Some(&snap(DevState::Unknown, false, 5)))).kind,
            "problem"
        );
        let mut short = snap(DevState::Idle, false, 5);
        short.pass = Some(false);
        assert!(banner(&input(Some(&short))).text.contains("UNDER TARGET"));
        short.pass = Some(true);
        assert_eq!(banner(&input(Some(&short))).text, "Idle - ready");
    }

    fn input(s: Option<&Snapshot>) -> BannerIn<'_> {
        BannerIn {
            connected: true,
            checking: false,
            snapshot: s,
            dump: None,
            job: None,
        }
    }

    #[test]
    fn banner_covers_the_states() {
        assert_eq!(
            banner(&input(Some(&snap(DevState::Idle, false, 0)))).text,
            "Idle - ready"
        );
        assert_eq!(
            banner(&input(Some(&snap(DevState::Idle, true, 0)))).text,
            "Manual motor ON"
        );
        assert_eq!(
            banner(&input(Some(&snap(DevState::Debug, false, 0)))).kind,
            "test"
        );
        let idle = snap(DevState::Idle, false, 0);
        let mut i = input(Some(&idle));
        i.job = Some((2, 3));
        assert_eq!(banner(&i).text, "Calibration run 2 of 3");
        i.connected = false;
        assert_eq!(banner(&i).kind, "problem");
    }

    #[test]
    fn describes_commands_in_words() {
        assert!(describe(&Cmd::Zero).contains("counter"));
        assert!(describe(&Cmd::Set("coast".into(), 5)).contains("coast"));
    }
}
