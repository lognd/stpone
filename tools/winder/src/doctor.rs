//! Doctor: ordered self-checks with plain-English results, steps and one-click fixes.

use std::process::Command;

use serde::Serialize;

use crate::client::{Link, Shared};
use crate::fake::FakeDevice;
use crate::serial::{ports, PortInfo, SerialTransport};

/// Result of one check.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum Status {
    Ok,
    Fail,
    Skipped,
}

/// One line of the checklist: what was checked, the verdict, and how to fix it.
#[derive(Debug, Clone, Serialize)]
pub struct Check {
    pub id: &'static str,
    pub title: &'static str,
    pub status: Status,
    pub message: String,
    pub steps: Vec<String>,
    /// Id of an automatic fix the UI may offer ("udev", "factory").
    pub fix: Option<&'static str>,
}

/// The whole checklist plus the port in use when everything passed far enough to connect.
#[derive(Debug, Clone, Default, Serialize)]
pub struct Report {
    pub checks: Vec<Check>,
    pub ok: bool,
    pub port: Option<String>,
    pub fw: Option<String>,
}

/// How to reach the winder.
#[derive(Debug, Clone)]
pub struct Target {
    pub port: Option<String>,
    pub baud: u32,
    pub fake_coast: Option<i64>,
}

const TITLES: [(&str, &str); 5] = [
    ("adapter", "USB serial adapter found"),
    ("permission", "Allowed to use the adapter"),
    ("hello", "Winder answers"),
    ("firmware", "Firmware is compatible"),
    ("params", "Saved settings are valid"),
];

fn title(id: &str) -> &'static str {
    TITLES.iter().find(|(i, _)| *i == id).map_or("", |(_, t)| t)
}

fn check(id: &'static str, status: Status, message: impl Into<String>) -> Check {
    Check {
        id,
        title: title(id),
        status,
        message: message.into(),
        steps: Vec::new(),
        fix: None,
    }
}

fn pass(id: &'static str, message: impl Into<String>) -> Check {
    check(id, Status::Ok, message)
}

fn fail(id: &'static str, message: impl Into<String>, steps: &[&str]) -> Check {
    let mut c = check(id, Status::Fail, message);
    c.steps = steps.iter().map(|s| s.to_string()).collect();
    c
}

fn skipped(id: &'static str) -> Check {
    check(
        id,
        Status::Skipped,
        "Not checked yet because an earlier step failed.",
    )
}

fn finish(mut checks: Vec<Check>, port: Option<String>, fw: Option<String>) -> Report {
    for &(id, _) in TITLES.iter() {
        if !checks.iter().any(|c| c.id == id) {
            checks.push(skipped(id));
        }
    }
    let order = |id: &str| TITLES.iter().position(|(i, _)| *i == id).unwrap_or(99);
    checks.sort_by_key(|c| order(c.id));
    let ok = checks.iter().all(|c| c.status != Status::Fail)
        && !checks.iter().any(|c| c.status == Status::Skipped);
    log::info!("doctor finished ok={ok}");
    Report {
        checks,
        ok,
        port,
        fw,
    }
}

/// Pure Crostini (ChromeOS Linux) test from the facts read at runtime.
pub fn crostini_from(
    cros_milestone: bool,
    hostname: &str,
    os_release: &str,
    env_marker: bool,
) -> bool {
    cros_milestone
        || env_marker
        || hostname.trim() == "penguin"
        || os_release.to_lowercase().contains("crostini")
}

/// True when running inside the ChromeOS Linux container (Crostini); unverified on real hardware.
pub fn is_crostini() -> bool {
    if !cfg!(target_os = "linux") {
        return false;
    }
    let host = std::fs::read_to_string("/etc/hostname").unwrap_or_default();
    let osr = std::fs::read_to_string("/etc/os-release").unwrap_or_default();
    let env = std::env::var_os("CROS_USER_ID_HASH").is_some()
        || std::env::var_os("SOMMELIER_VERSION").is_some();
    let yes = crostini_from(
        std::path::Path::new("/dev/.cros_milestone").exists(),
        &host,
        &osr,
        env,
    );
    log::debug!("crostini detection: {yes}");
    yes
}

/// Release file to download for an OS/arch pair, or None when no build ships for it.
pub fn asset_for(os: &str, arch: &str) -> Option<&'static str> {
    match (os, arch) {
        ("linux", "x86_64") => Some("winder-linux-x86_64.tar.gz"),
        ("linux", "aarch64") => Some("winder-linux-aarch64.tar.gz"),
        ("macos", "x86_64") => Some("winder-macos-x86_64.tar.gz"),
        ("macos", "aarch64") => Some("winder-macos-aarch64.tar.gz"),
        ("windows", "x86_64") => Some("winder-windows-x86_64.exe"),
        _ => None,
    }
}

/// One line naming this computer and the matching download.
pub fn machine_line() -> String {
    let (os, arch) = (std::env::consts::OS, std::env::consts::ARCH);
    let kind = if is_crostini() {
        "Chromebook Linux"
    } else {
        os
    };
    let asset = asset_for(os, arch);
    let unpack = match asset {
        Some(a) if a.ends_with(".tar.gz") => format!(" Unpack it with: tar -xzf {a}"),
        _ => String::new(),
    };
    format!(
        "This computer: {kind} {arch}. Matching download: {}.{unpack}",
        asset.unwrap_or("none (no build for this computer)")
    )
}

fn driver_steps() -> Vec<&'static str> {
    if is_crostini() {
        vec![
            "Plug in the USB cable or adapter.",
            "Open the Chromebook Settings -> Advanced -> Developers -> Linux development environment -> Manage USB devices.",
            "Switch ON the adapter (it may be called USB Serial, CP210x, CH340 or FTDI).",
            "Press Check again.",
            "If there is no Linux development environment: Settings -> Advanced -> Developers -> Linux development environment -> Turn on. Some school or work Chromebooks block this; ask the administrator.",
        ]
    } else if cfg!(target_os = "windows") {
        vec![
            "Plug the adapter into a USB port on this computer.",
            "Open Device Manager and look under 'Ports (COM & LPT)'. If you see a yellow warning sign instead, the driver is missing.",
            "Install the driver for your adapter's chip (CH340, CP210x or FTDI; the chip name is printed on the adapter or its listing) and plug it in again.",
        ]
    } else if cfg!(target_os = "macos") {
        vec![
            "Plug the adapter into a USB port on this computer.",
            "Adapters with a CH340, CP210x or FTDI chip need a driver on older macOS; install the one for your chip's name and plug the adapter in again.",
            "Avoid USB hubs; try a port directly on the computer.",
        ]
    } else {
        vec![
            "Plug the adapter into a USB port on this computer.",
            "Try a different USB cable; some cables only charge and carry no data.",
        ]
    }
}

fn candidates(target: &Target, all: &[PortInfo]) -> Vec<String> {
    if let Some(p) = &target.port {
        return vec![p.clone()];
    }
    let usb: Vec<&PortInfo> = all.iter().filter(|p| p.usb && !p.bootloader).collect();
    usb.iter().map(|p| p.name.clone()).collect()
}

fn adapter_check(target: &Target, all: &[PortInfo], cands: &[String]) -> Check {
    if !cands.is_empty() {
        return pass("adapter", format!("Found: {}.", cands.join(", ")));
    }
    if all.iter().any(|p| p.bootloader) {
        return fail(
            "adapter",
            "Only the winder's update-mode port is visible, not a serial adapter.",
            &[
                "Unplug the winder's own USB cable and wait ten seconds.",
                "Plug in the USB serial adapter that is wired to the winder (not the winder's own USB socket).",
                "Press Check again.",
            ],
        );
    }
    let _ = target;
    fail(
        "adapter",
        "No USB serial adapter was found.",
        &driver_steps(),
    )
}

fn permission_fail(open_err: &str) -> Check {
    let e = open_err.to_lowercase();
    if e.contains("permission") && is_crostini() {
        let mut c = fail(
            "permission",
            "This account is not allowed to use the adapter (not in the dialout group).",
            &[
                "Press 'Fix it', or open the Linux Terminal and run: sudo usermod -aG dialout $USER",
                "Restart Linux: in the Terminal type 'exit', right-click the Terminal icon on the shelf and choose 'Shut down Linux', then open the Terminal again.",
                "Press Check again.",
            ],
        );
        c.fix = Some("dialout");
        c
    } else if e.contains("permission") {
        let mut c = fail(
            "permission",
            "This account is not allowed to use the adapter.",
            &[
                "Press 'Fix it' and enter your password when asked.",
                "Unplug the adapter, plug it in again, and press Check.",
                "If it still fails, log out and back in once, then reopen this app.",
            ],
        );
        if cfg!(target_os = "linux") {
            c.fix = Some("udev");
        }
        c
    } else if e.contains("busy")
        || e.contains("access is denied")
        || e.contains("in use")
        || e.contains("denied")
    {
        fail(
            "permission",
            "Another program is already using the adapter.",
            &[
                "Close the Arduino IDE, serial monitors, PuTTY, and any other copy of this app.",
                "Unplug the adapter, plug it in again, and press Check.",
            ],
        )
    } else {
        fail(
            "permission",
            "The adapter could not be opened.",
            &driver_steps(),
        )
    }
}

fn wiring_fail(reason: &str) -> Check {
    fail(
        "hello",
        format!("The adapter opened but the winder did not answer ({reason})."),
        &[
            "Check the winder is powered on.",
            "Swap the two signal wires on the adapter: the adapter's TX goes to the winder's RX (D0) and its RX to the winder's TX (D1).",
            "Make sure the adapter's GND wire is connected to the winder's GND.",
            "Use an adapter set to 5 V signals; a 3.3 V-only adapter is unreliable.",
            "Press Check again.",
        ],
    )
}

/// Run every check in order, connect `link` to the winder, and report.
pub fn diagnose(target: &Target, client: &Shared, link: &Link) -> Report {
    link.set(None);
    let mut checks = Vec::new();
    if let Some(coast) = target.fake_coast {
        link.set(Some(Box::new(FakeDevice::realistic(coast))));
        checks.push(pass("adapter", "Simulated winder (no hardware needed)."));
        checks.push(pass("permission", "Not needed for the simulated winder."));
        return after_connect(checks, client, link, Some("simulated".into()));
    }
    let all = ports();
    let cands = candidates(target, &all);
    let adapter = adapter_check(target, &all, &cands);
    let found = adapter.status == Status::Ok;
    checks.push(adapter);
    if !found {
        return finish(checks, None, None);
    }
    let mut last_open_err = String::new();
    let mut last_hello_err = String::new();
    let mut opened_any = false;
    for name in &cands {
        let t = match SerialTransport::open(name, target.baud) {
            Ok(t) => t,
            Err(e) => {
                log::warn!("{name}: {e:?}");
                last_open_err = format!("{e:?}");
                continue;
            }
        };
        opened_any = true;
        link.set(Some(Box::new(t)));
        let hello = (0..2).find_map(|_| {
            client
                .lock()
                .expect("client lock poisoned")
                .hello()
                .map_err(|e| last_hello_err = e.to_string())
                .ok()
        });
        if hello.is_some() {
            checks.push(pass("permission", format!("Opened {name}.")));
            return after_connect(checks, client, link, Some(name.clone()));
        }
        link.set(None);
    }
    if !opened_any {
        checks.push(permission_fail(&last_open_err));
    } else {
        checks.push(pass("permission", "The adapter can be opened."));
        checks.push(wiring_fail(&last_hello_err));
    }
    finish(checks, None, None)
}

fn after_connect(
    mut checks: Vec<Check>,
    client: &Shared,
    link: &Link,
    port: Option<String>,
) -> Report {
    let mut c = client.lock().expect("client lock poisoned");
    let h = match c.hello() {
        Ok(h) => h,
        Err(e) => {
            drop(c);
            link.set(None);
            checks.push(wiring_fail(&e.to_string()));
            return finish(checks, None, None);
        }
    };
    checks.push(pass("hello", "The winder answered."));
    if h.proto != 1 {
        checks.push(fail(
            "firmware",
            format!(
                "The winder speaks protocol {} but this app needs protocol 1.",
                h.proto
            ),
            &[
                "Download the newer version of this app that matches your winder's firmware.",
                "Or re-flash the winder with the firmware that came with this app.",
            ],
        ));
        return finish(checks, port, Some(h.fw));
    }
    checks.push(pass(
        "firmware",
        format!("Firmware {} is compatible.", h.fw),
    ));
    match c.param() {
        Ok(p) if p.wheel_um > 0 && p.cpr > 0 && p.short > 0 && p.long > 0 => {
            checks.push(pass("params", "Settings look sensible."))
        }
        Ok(_) => {
            let mut f = fail(
                "params",
                "The saved settings contain impossible values (zero).",
                &["Press 'Fix it' to restore the factory settings."],
            );
            f.fix = Some("factory");
            checks.push(f);
        }
        Err(e) => checks.push(fail("params", e.human(), &["Press Check again."])),
    }
    drop(c);
    finish(checks, port, Some(h.fw))
}

/// Doctor line for "can a browser be opened"; `url` is the address to type otherwise.
pub fn browser_check(available: bool, url: &str) -> Check {
    if available {
        return Check {
            id: "browser",
            title: "A web browser can be opened",
            status: Status::Ok,
            message: "A browser will open by itself.".into(),
            steps: Vec::new(),
            fix: None,
        };
    }
    Check {
        id: "browser",
        title: "A web browser can be opened",
        status: Status::Fail,
        message: "No browser could be opened automatically.".into(),
        steps: vec![format!(
            "Open any web browser yourself and type the address winder prints when it starts (normally {url}; if that port is busy winder picks another and prints it)"
        )],
        fix: None,
    }
}

/// Linux: install a udev rule and add the user to the serial group via pkexec.
pub fn fix_udev() -> Result<String, String> {
    if !cfg!(target_os = "linux") {
        return Err("There is no automatic fix on this system; follow the numbered steps.".into());
    }
    let user: String = std::env::var("USER")
        .unwrap_or_default()
        .chars()
        .filter(|c| c.is_ascii_alphanumeric() || *c == '_' || *c == '-')
        .collect();
    if user.is_empty() {
        return Err(
            "Could not tell which account you are using; follow the numbered steps.".into(),
        );
    }
    let script = format!(
        "g=dialout; getent group dialout >/dev/null || g=uucp; \
         printf 'SUBSYSTEM==\"tty\", ENV{{ID_BUS}}==\"usb\", TAG+=\"uaccess\", MODE=\"0660\", GROUP=\"%s\"\\n' \"$g\" > /etc/udev/rules.d/99-stpone.rules; \
         usermod -aG \"$g\" {user}; udevadm control --reload-rules; udevadm trigger"
    );
    log::info!("running fix: pkexec sh -c <udev script> for {user}");
    match Command::new("pkexec").args(["sh", "-c", &script]).status() {
        Ok(s) if s.success() => Ok("Done. Unplug the adapter, plug it in again and press Check. If it still fails, log out and in once.".into()),
        Ok(s) => Err(format!("The fix was cancelled or refused (exit {s}). Follow the numbered steps instead.")),
        Err(e) => Err(format!("Could not ask for permission ({e}). Follow the numbered steps instead.")),
    }
}

/// Crostini: add the user to dialout with passwordless sudo (no pkexec exists there).
pub fn fix_dialout() -> Result<String, String> {
    let user: String = std::env::var("USER")
        .unwrap_or_default()
        .chars()
        .filter(|c| c.is_ascii_alphanumeric() || *c == '_' || *c == '-')
        .collect();
    if user.is_empty() {
        return Err(
            "Could not tell which account you are using; follow the numbered steps.".into(),
        );
    }
    log::info!("running fix: sudo -n usermod -aG dialout {user}");
    match Command::new("sudo").args(["-n", "usermod", "-aG", "dialout", &user]).status() {
        Ok(s) if s.success() => Ok("Done. Now restart Linux ('Shut down Linux' on the Terminal icon, then open the Terminal again) and press Check.".into()),
        Ok(_) => Err("That needs a password, so it cannot be done from here. Run this in the Linux Terminal instead: sudo usermod -aG dialout $USER".into()),
        Err(e) => Err(format!("Could not run sudo ({e}). Run this in the Linux Terminal instead: sudo usermod -aG dialout $USER")),
    }
}

/// Plain-text rendering for `winder doctor`.
pub fn render_text(r: &Report) -> String {
    let mut out = format!("{}\n", machine_line());
    for c in &r.checks {
        let mark = match c.status {
            Status::Ok => "[ OK ]",
            Status::Fail => "[FAIL]",
            Status::Skipped => "[ -- ]",
        };
        out.push_str(&format!("{mark} {}: {}\n", c.title, c.message));
        for (i, s) in c.steps.iter().enumerate() {
            out.push_str(&format!("         {}. {s}\n", i + 1));
        }
    }
    out.push_str(if r.ok {
        "\nEverything is ready.\n"
    } else {
        "\nFix the red items, then run again.\n"
    });
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::client::Client;
    use std::sync::{Arc, Mutex};

    fn rig() -> (Shared, Link) {
        let link = Link::default();
        (
            Arc::new(Mutex::new(Client::new(Box::new(link.clone())))),
            link,
        )
    }

    #[test]
    fn fake_device_passes_every_check() {
        let (c, l) = rig();
        let r = diagnose(
            &Target {
                port: None,
                baud: 115200,
                fake_coast: Some(40),
            },
            &c,
            &l,
        );
        assert!(r.ok, "{}", render_text(&r));
        assert_eq!(r.checks.len(), 5);
    }

    #[test]
    fn unopenable_port_gets_a_plain_message() {
        let (c, l) = rig();
        let r = diagnose(
            &Target {
                port: Some("/dev/does-not-exist".into()),
                baud: 115200,
                fake_coast: None,
            },
            &c,
            &l,
        );
        assert!(!r.ok);
        let p = r
            .checks
            .iter()
            .find(|x| x.id == "permission")
            .expect("permission check");
        assert_eq!(p.status, Status::Fail);
        assert!(!p.steps.is_empty());
    }

    #[test]
    fn crostini_detection_and_assets() {
        assert!(crostini_from(true, "x", "", false));
        assert!(crostini_from(false, "penguin\n", "", false));
        assert!(crostini_from(false, "x", "", true));
        assert!(!crostini_from(false, "laptop", "ID=debian", false));
        assert_eq!(
            asset_for("linux", "aarch64"),
            Some("winder-linux-aarch64.tar.gz")
        );
        assert_eq!(
            asset_for("linux", "x86_64"),
            Some("winder-linux-x86_64.tar.gz")
        );
        assert_eq!(
            asset_for("windows", "x86_64"),
            Some("winder-windows-x86_64.exe")
        );
        assert_eq!(asset_for("linux", "riscv64"), None);
        assert!(machine_line().contains("Matching download"));
    }

    #[test]
    fn permission_text_maps_to_udev_fix_on_linux() {
        let c = permission_fail("Io(\"Permission denied\")");
        assert_eq!(c.fix.is_some(), cfg!(target_os = "linux"));
        assert!(permission_fail("Io(\"Device or resource busy\")")
            .message
            .contains("Another program"));
    }
}
