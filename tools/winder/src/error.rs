//! Error vocabulary: device ERR codes plus host-side faults.

use std::fmt;

/// Everything that can go wrong between this tool and the winder.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CalError {
    BadCommand,
    BadArg,
    BadState,
    Busy,
    Malformed(String),
    Timeout,
    Io(String),
    BadInput(String),
    NoData,
    /// ERR io: SAVE read-back failed.
    DeviceIo,
    /// ERR no_data: LOAD with nothing stored.
    NothingStored,
    /// Debug mode ended by the machine (`timeout` or `panel`).
    DebugEnded(String),
    /// A calibration suggestion was refused, with the reason.
    Refused(String),
    /// The user cancelled a calibration job.
    Cancelled,
}

impl fmt::Display for CalError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CalError::BadCommand => write!(f, "device: unknown command"),
            CalError::BadArg => write!(f, "device: argument rejected"),
            CalError::BadState => write!(f, "device: not valid in the current state"),
            CalError::Busy => write!(f, "device: busy, a line was dropped"),
            CalError::Malformed(s) => write!(f, "malformed protocol line: {s}"),
            CalError::Timeout => write!(f, "the device did not answer in time"),
            CalError::Io(s) => write!(f, "serial I/O failed: {s}"),
            CalError::BadInput(s) => write!(f, "bad input: {s}"),
            CalError::NoData => write!(f, "not enough samples for this calculation"),
            CalError::DeviceIo => write!(f, "device: the settings memory did not take the save"),
            CalError::NothingStored => write!(f, "device: no saved settings"),
            CalError::DebugEnded(r) => write!(f, "debug mode ended by the machine ({r})"),
            CalError::Refused(m) => write!(f, "refused: {m}"),
            CalError::Cancelled => write!(f, "cancelled"),
        }
    }
}

impl CalError {
    /// Plain-English sentence plus the next step, for people who never saw a terminal.
    pub fn human(&self) -> String {
        match self {
            CalError::BadCommand => "The winder did not understand that request. Unplug and replug its USB cable, then press Check again.".into(),
            CalError::BadArg => "The winder refused that number because it is outside what the machine allows. Type a smaller or larger value, or press Reset to factory settings.".into(),
            CalError::BadState => "The winder cannot do that right now. Make sure it is stopped (not winding) and that Test mode is on for test winds, then try again.".into(),
            CalError::Busy => "The winder was busy for a moment and missed that. Please try again.".into(),
            CalError::DeviceIo => "The winder could not store the settings (its memory did not accept them). Press Save again; if it keeps failing, the machine needs service.".into(),
            CalError::NothingStored => "No saved settings were found on the winder, so it keeps using the factory settings. Nothing is wrong.".into(),
            CalError::DebugEnded(r) => if r == "panel" {
                "Test mode was ended by the machine because a button was pressed on it. The motor is off. Turn Test mode on again to continue.".into()
            } else {
                "Test mode was ended by the machine because it heard nothing from this app for 5 seconds. The motor is off. Turn Test mode on again to continue.".into()
            },
            CalError::Refused(m) => m.clone(),
            CalError::Cancelled => "Calibration was cancelled. The motor is off and Test mode is off.".into(),
            CalError::Malformed(_) => "The winder sent something unexpected. Press Check again; if it keeps happening, unplug and replug the USB cable.".into(),
            CalError::Timeout => "The winder did not answer. Check that it is powered on and the USB cable is plugged in, then press Check again.".into(),
            CalError::Io(_) => "The connection to the winder was lost. Plug the cable back in and press Check again.".into(),
            CalError::BadInput(m) => format!("That input is not valid ({m}). Fix it and try again."),
            CalError::NoData => "There is not enough data yet. Add at least one test wind first.".into(),
        }
    }
}

impl std::error::Error for CalError {}

/// Crate-wide result type.
pub type Res<T> = Result<T, CalError>;
