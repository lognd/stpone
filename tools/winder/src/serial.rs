//! serialport-backed Transport, port listing and HELLO probing.

use std::io::{ErrorKind, Read, Write};
use std::time::{Duration, Instant};

use serde::Serialize;

use crate::client::Transport;
use crate::error::{CalError, Res};

pub const DEFAULT_BAUD: u32 = 115_200;
/// Longest unterminated run of bytes kept; real protocol lines are far shorter.
const LINE_CAP: usize = 512;

/// Line transport over a real serial port (115200 8N1 by default).
pub struct SerialTransport {
    port: Box<dyn serialport::SerialPort>,
    buf: Vec<u8>,
}

fn io(e: impl std::fmt::Display) -> CalError {
    CalError::Io(e.to_string())
}

impl SerialTransport {
    /// Open `name` (e.g. COM5 or /dev/ttyUSB0).
    pub fn open(name: &str, baud: u32) -> Res<SerialTransport> {
        let port = serialport::new(name, baud)
            .timeout(Duration::from_millis(50))
            .open()
            .map_err(io)?;
        log::info!("opened {name} at {baud} baud");
        Ok(SerialTransport {
            port,
            buf: Vec::new(),
        })
    }

    fn take_line(&mut self) -> Option<String> {
        let i = self.buf.iter().position(|&b| b == b'\n')?;
        let line: Vec<u8> = self.buf.drain(..=i).collect();
        Some(String::from_utf8_lossy(&line).trim().to_string())
    }
}

impl Transport for SerialTransport {
    fn write_line(&mut self, line: &str) -> Res<()> {
        self.port
            .write_all(format!("{line}\n").as_bytes())
            .map_err(io)?;
        self.port.flush().map_err(io)
    }

    fn read_line(&mut self, timeout: Duration) -> Res<Option<String>> {
        let deadline = Instant::now() + timeout;
        loop {
            if let Some(l) = self.take_line() {
                return Ok(Some(l));
            }
            let left = deadline.saturating_duration_since(Instant::now());
            self.port
                .set_timeout(
                    left.max(Duration::from_millis(1))
                        .min(Duration::from_millis(50)),
                )
                .map_err(io)?;
            let mut tmp = [0u8; 128];
            match self.port.read(&mut tmp) {
                Ok(n) => {
                    self.buf.extend_from_slice(&tmp[..n]);
                    if self.buf.len() > LINE_CAP && !self.buf.contains(&b'\n') {
                        log::warn!(
                            "dropping {} bytes of line noise with no newline",
                            self.buf.len()
                        );
                        self.buf.clear();
                    }
                }
                Err(e) if e.kind() == ErrorKind::TimedOut => {}
                Err(e) => return Err(io(e)),
            }
            if left.is_zero() && !self.buf.contains(&b'\n') {
                return Ok(None);
            }
        }
    }
}

/// What the OS tells us about one serial port.
#[derive(Debug, Clone, Serialize)]
pub struct PortInfo {
    pub name: String,
    pub usb: bool,
    pub vid: Option<u16>,
    pub pid: Option<u16>,
    pub product: Option<String>,
    /// True for the Pro Micro / Leonardo bootloader (update mode) port.
    pub bootloader: bool,
}

/// USB ids of Arduino-compatible bootloader ports (SparkFun, Arduino Leonardo/Micro).
const BOOTLOADER_IDS: [(u16, u16); 5] = [
    (0x1B4F, 0x9205),
    (0x1B4F, 0x9206),
    (0x2341, 0x0036),
    (0x2341, 0x0037),
    (0x2A03, 0x0036),
];

/// Every serial port the OS reports, with USB details where known.
pub fn ports() -> Vec<PortInfo> {
    let found = match serialport::available_ports() {
        Ok(p) => p,
        Err(e) => {
            log::error!("cannot list ports: {e}");
            return Vec::new();
        }
    };
    found
        .into_iter()
        .map(|p| match p.port_type {
            serialport::SerialPortType::UsbPort(u) => PortInfo {
                bootloader: BOOTLOADER_IDS.contains(&(u.vid, u.pid)),
                name: p.port_name,
                usb: true,
                vid: Some(u.vid),
                pid: Some(u.pid),
                product: u.product,
            },
            _ => PortInfo {
                name: p.port_name,
                usb: false,
                vid: None,
                pid: None,
                product: None,
                bootloader: false,
            },
        })
        .collect()
}
