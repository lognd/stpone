"use strict";
// Winder UI. Everything shown is derived from the latest state document the
// server pushes over /events; nothing is cached as "running" on this side.

const $ = (id) => document.getElementById(id);
const busy = new Set(); // buttons whose request is in flight
let current = null; // latest state document
let streamLost = false;
let bannerAction = null;
let fixMsg = "";
const keys = {}; // render caches so unchanged regions are not rebuilt

// ---------------------------------------------------------------- backend
// The ONLY place that talks to the program. A second implementation (for
// example WebSerial + WASM, in page) can replace HttpBackend as long as it
// returns the same snapshot document and the same {ok, error?, message?} replies.
//
// Snapshot document (one JSON object, pushed on every change):
//   banner{kind,text,next}  connected  checking  error  job  log[]  streaming
//   doctor{ok, checks[{id,title,status:ok|fail|skipped,message,steps[],fix|null}]}|null
//   snapshot{state:IDLE|WINDING|PAUSED|DEBUG, count, errors, relay, btn}|null
//   dump{...machine internals incl. manual, eff_target, run}|null
//   params{wheel_um,cpr,short,long,coast}|null     factory{...same keys}
//   runs[{index,target,at_stop,final,follow,residual,measured_in,peak_speed,valid,why_invalid,verify}]
//   suggest{coast,wheel_um,coast_stats}
//   auto{phase,step,of,attempt,valid_runs,needed,message,reason,
//        result{coast_before,coast_after,wheel_um_before,wheel_um_after,spread_pct,residual_pulses,
//               verify_residual_pulses,residual_before_pulses,verified,saved,changed,note}|null   (the main test; a
//               length check never replaces it; verified = a check wind ran and |miss| <= 20 pulses),
//        length_check{expected_in,pulses,proposal{measured_in,new_wheel_um,percent}|null,
//               result{...same fields...}|null, series[], marks[]}|null}   (the check's own result and plot)
//   series[{t_ms,count,speed,relay,run}]   marks[{t_ms,kind,count}]
// Every method returns Promise<{ok, error?, message?}>; none of them throws and
// none of them shows anything: the caller decides what the user sees.

const LOST_TEXT = "Lost contact with the program. Keep this page open; it will reconnect.";

const HttpBackend = {
  _es: null,
  async _post(path, body) {
    try {
      const r = await fetch(path, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body || {}) });
      const doc = await r.json();
      return doc.ok ? doc : Object.assign({}, doc, { ok: false, error: doc.error || "That did not work. Please try again." });
    } catch (e) {
      return { ok: false, error: LOST_TEXT, lost: true };
    }
  },
  /** Start receiving snapshots; onLost fires when the stream drops (the next snapshot means it is back). */
  subscribe(onSnapshot, onLost) {
    const es = new EventSource("/events");
    let got = false;
    es.onmessage = (m) => {
      got = true;
      onSnapshot(JSON.parse(m.data));
    };
    es.onerror = () => onLost();
    this._es = es;
    // The stream only speaks on a change; ask once so the page never waits for the first one.
    fetch("/api/state")
      .then((r) => r.json())
      .then((doc) => { if (!got) onSnapshot(doc); })
      .catch(() => {});
  },
  close() {
    if (this._es) this._es.close();
  },
  command(name, ...args) { return this._post("/api/command", { cmd: name, args }); },
  doctorRun() { return this._post("/api/doctor/run"); },
  doctorFix(id) { return this._post("/api/doctor/fix", { id }); },
  calibrateRun(pulses, count) { return this._post("/api/calibrate/run", { pulses, count }); },
  calibrateClear() { return this._post("/api/calibrate/clear"); },
  calibrateMeasureRun(index, inches) { return this._post("/api/calibrate/measure", { index, inches }); },
  calibrateMeasureLength(inches) { return this._post("/api/calibrate/measure", { inches }); },
  calibrateAuto(pulses) { return this._post("/api/calibrate/auto", { pulses }); },
  calibrateLengthCheck() { return this._post("/api/calibrate/length_check", {}); },
  calibrateCancel() { return this._post("/api/calibrate/cancel", {}); },
  calibrateSave() { return this._post("/api/calibrate/save", {}); },
  applyWheel() { return this._post("/api/calibrate/apply_wheel", {}); },
  skipMeasure() { return this._post("/api/calibrate/skip_measure", {}); },
  /** Best-effort stop while the page is closing (must not wait for a reply). */
  leaveStop() {
    const send = (path, c) => navigator.sendBeacon(path, new Blob([JSON.stringify(c)], { type: "application/json" }));
    send("/api/calibrate/cancel", {});
    send("/api/command", { cmd: "ABORT", args: [] });
    send("/api/command", { cmd: "RELAY", args: [0] });
    send("/api/command", { cmd: "DEBUG", args: [0] });
  },
  reload() { location.reload(); },
};
const backend = HttpBackend;


// ---------------------------------------------------------------- helpers

function h(tag, cls, content, attrs) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (content !== undefined && content !== null) e.textContent = content;
  if (attrs) for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  return e;
}

function text(id, v) {
  const el = $(id);
  const t = v === null || v === undefined ? "-" : String(v);
  if (el.textContent !== t) el.textContent = t;
}

function setDis(el, cond) {
  el.disabled = !!cond || busy.has(el);
}

function changed(name, value) {
  const k = JSON.stringify(value);
  if (keys[name] === k) return false;
  keys[name] = k;
  return true;
}

/** Show a failed reply to the user (and clear the message on success); returns the reply. */
function shown(reply) {
  return reply.then((r) => {
    showError(r.ok ? "" : r.error || "That did not work. Please try again.");
    return r;
  });
}
const cmd = (name, ...args) => shown(backend.command(name, ...args));
const qcmd = (name, ...args) => backend.command(name, ...args);

function showError(msg) {
  const el = $("error");
  el.textContent = msg;
  el.hidden = !msg;
}

/** Run fn while the button is disabled, so a double click cannot repeat it. */
async function act(btn, fn) {
  if (busy.has(btn)) return undefined;
  busy.add(btn);
  btn.disabled = true;
  btn.setAttribute("aria-busy", "true");
  try {
    return await fn();
  } finally {
    busy.delete(btn);
    btn.removeAttribute("aria-busy");
    btn.disabled = false; // render() below re-disables the buttons it manages
    if (current) render(current);
  }
}

function bind(id, fn) {
  const b = $(id);
  b.addEventListener("click", () => act(b, fn));
}

const isDebug = (s) => !!(s.snapshot && s.snapshot.state === "DEBUG");
const jobRunning = (s) => s.job.startsWith("running");
const failing = (s) => (s.doctor ? s.doctor.checks.filter((c) => c.status === "fail") : []);

/** Inches of material for a pulse count, from the wheel size setting. */
function inchesFor(count, p) {
  if (!p || !p.cpr) return null;
  return (Math.abs(count) / p.cpr) * Math.PI * (p.wheel_um / 25400);
}
const fmt = (v, d) => (v === null || v === undefined ? "-" : v.toFixed(d));

function lengthText(count, p) {
  const inch = inchesFor(count, p);
  if (inch === null) return { big: "-", small: "" };
  return { big: fmt(inch / 36, 1) + " yd", small: fmt(inch, 0) + " inches" };
}


// ------------------------------------------------------------ main screen

function bannerFor(s) {
  const snap = s.snapshot;
  if (streamLost) {
    return { kind: "bad", word: "PROBLEM", title: "Lost contact with the program",
      next: "Keep this page open. It reconnects by itself.", action: { id: "reload", label: "Reload page" } };
  }
  if (s.checking) return { kind: "wait", word: "WAIT", title: "Checking", next: "One moment, please." };
  if (!s.connected) {
    const bad = failing(s);
    if (bad.length) {
      const fx = bad.find((c) => c.fix);
      return { kind: "bad", word: "PROBLEM", title: "Not connected", next: "Follow the steps below.",
        action: fx ? { id: "fix", label: "Fix it", fix: fx.fix } : { id: "recheck", label: "Check again" } };
    }
    return { kind: "wait", word: "WAIT", title: "Not connected", next: "Press Check again.",
      action: { id: "recheck", label: "Check again" } };
  }
  if (!snap) {
    return { kind: "bad", word: "PROBLEM", title: "No answer from the machine",
      next: "Check the cable and the power.", action: { id: "recheck", label: "Check again" } };
  }
  if (autoBusy(s)) {
    return { kind: "wait", word: "BUSY", title: "Calibrating", next: s.auto.message || "Do not touch the machine." };
  }
  if (snap.state === "DEBUG") {
    return { kind: "test", word: "TEST MODE", title: "Test mode is on",
      next: "The machine only moves from this screen.", action: { id: "testoff", label: "Turn off Test mode" } };
  }
  if (jobRunning(s)) {
    return { kind: "wait", word: "BUSY", title: "Running a test", next: "Do not touch the machine." };
  }
  if (snap.state === "WINDING") {
    const d = s.dump;
    const pct = d && d.eff_target ? Math.min(100, Math.floor((Math.abs(snap.count) * 100) / Math.abs(d.eff_target))) : null;
    return { kind: "wait", word: "BUSY", title: pct === null ? "Winding" : "Winding " + pct + "%",
      next: "Please wait. Do not touch the machine." };
  }
  const manual = s.dump ? s.dump.manual : snap.relay;
  if (manual) {
    return { kind: "warn", word: "MOTOR ON", title: "Motor on by hand",
      next: "Press button 4 on the machine to turn it off." };
  }
  if (snap.state === "PAUSED") {
    return { kind: "warn", word: "PAUSED", title: "Paused", next: "Use the buttons on the machine to go on or stop." };
  }
  return { kind: "good", word: "OK", title: "Ready", next: "Start winding with the buttons on the machine." };
}

function renderBanner(s) {
  const b = bannerFor(s);
  $("banner").className = "banner banner-" + b.kind;
  text("banner-word", b.word);
  text("banner-title", b.title);
  text("banner-next", b.next);
  bannerAction = b.action || null;
  const btn = $("banner-btn");
  btn.hidden = !bannerAction;
  if (bannerAction) {
    if (btn.textContent !== bannerAction.label) btn.textContent = bannerAction.label;
    setDis(btn, s.checking && bannerAction.id !== "reload");
  }
}

async function copySteps(c) {
  const t = c.steps.map((x, i) => i + 1 + ". " + x).join("\n");
  try {
    await navigator.clipboard.writeText(t);
    $("fix-msg").textContent = "Steps copied.";
  } catch (e) {
    $("fix-msg").textContent = "Could not copy. Select the text and copy it by hand.";
  }
}

function renderProblem(s) {
  const bad = failing(s);
  const show = !s.checking && !s.connected && bad.length > 0;
  $("problem").hidden = !show;
  if (!show) return;
  if (changed("problem", [s.doctor, streamLost])) {
    const box = $("problem-cards");
    box.replaceChildren();
    for (const c of bad) {
      const card = h("article", "pcard");
      card.append(h("p", "pcard-word", "PROBLEM"), h("h2", "pcard-title", c.message));
      if (c.steps.length) {
        const ol = h("ol", "steps");
        for (const st of c.steps) ol.append(h("li", null, st));
        card.append(ol);
      }
      const row = h("div", "row");
      if (c.fix) {
        const fix = h("button", "btn btn-main", "Fix it", { type: "button" });
        fix.addEventListener("click", () =>
          act(fix, async () => {
            const r = await shown(backend.doctorFix(c.fix));
            fixMsg = r.ok ? r.message : "";
            $("fix-msg").textContent = fixMsg;
          }));
        row.append(fix);
      }
      if (c.steps.length) {
        const copy = h("button", "btn", "Copy steps", { type: "button" });
        copy.addEventListener("click", () => act(copy, () => copySteps(c)));
        row.append(copy);
      }
      card.append(row);
      box.append(card);
    }
    const oks = s.doctor.checks.filter((c) => c.status === "ok").length;
    $("problem-ok").textContent = oks ? "Everything else is OK." : "";
  }
}


// ----------------------------------------------------------------- charts
// Hand-written SVG, redrawn at most every 100 ms. Colours come from CSS classes
// (ch-*) so both themes work. A missing or short series never throws.

const NS = "http://www.w3.org/2000/svg";
function sv(tag, attrs, content) {
  const e = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs || {})) e.setAttribute(k, String(v));
  if (content !== undefined && content !== null) e.textContent = content;
  return e;
}

/** The latest run's points and marks, with times in seconds from its start. */
function runSeries(s) {
  const ser = Array.isArray(s.series) ? s.series.filter((q) => q && typeof q.t_ms === "number") : [];
  const marks = Array.isArray(s.marks) ? s.marks.filter((m) => m && typeof m.t_ms === "number") : [];
  return windowSeries(ser, marks);
}

function windowSeries(ser, marks) {
  let t0 = ser.length ? ser[0].t_ms : 0;
  const starts = marks.filter((m) => /start/i.test(m.kind || ""));
  if (starts.length) t0 = starts[starts.length - 1].t_ms - 200;
  const pts = ser.filter((q) => q.t_ms >= t0).slice(-600);
  const base = pts.length ? pts[0].t_ms : t0;
  return {
    pts: pts.map((q) => ({ t: (q.t_ms - base) / 1000, speed: Number(q.speed) || 0, count: Math.abs(Number(q.count) || 0) })),
    marks: marks.filter((m) => m.t_ms >= base - 50).map((m) => ({ t: (m.t_ms - base) / 1000, kind: String(m.kind || ""), count: Math.abs(Number(m.count) || 0) })),
  };
}

function markName(kind) {
  if (/start/i.test(kind)) return "start";
  if (/settle/i.test(kind)) return "settled";
  if (/stop/i.test(kind)) return "stop";
  return null;
}

/** "Up to speed" / "Speeding up" / "Slowing down" / "Stopped" from the speed series. */
function speedBadge(pts) {
  if (pts.length < 2) return "Waiting for data";
  const peak = Math.max(...pts.map((q) => q.speed));
  const last = pts[pts.length - 1].speed;
  const prev = pts[Math.max(0, pts.length - 4)].speed;
  if (peak <= 0 || last <= 0) return peak > 0 ? "Stopped" : "Not moving";
  if (last >= 0.9 * peak && Math.abs(last - prev) <= 0.05 * peak) return "Up to speed";
  return last >= prev ? "Speeding up" : "Slowing down";
}

function frame(el, w, hgt, label) {
  const svg = sv("svg", { viewBox: "0 0 " + w + " " + hgt, width: w, height: hgt, role: "img", "aria-label": label, class: "chart" });
  return svg;
}

const compact = (v) => (v >= 10000 ? (v / 1000).toFixed(0) + "k" : v >= 1000 ? (v / 1000).toFixed(1) + "k" : v.toFixed(0));

function axes(svg, g, xlabel, ylabel, xmax, ymax, ydec) {
  svg.append(sv("line", { x1: g.l, y1: g.t, x2: g.l, y2: g.b, class: "ch-axis" }));
  svg.append(sv("line", { x1: g.l, y1: g.b, x2: g.r, y2: g.b, class: "ch-axis" }));
  svg.append(sv("text", { x: g.l - 6, y: g.b, class: "ch-text", "text-anchor": "end" }, "0"));
  svg.append(sv("text", { x: g.l - 6, y: g.t + 10, class: "ch-text", "text-anchor": "end" }, ydec === 0 ? compact(ymax) : ymax.toFixed(ydec)));
  svg.append(sv("text", { x: g.l, y: g.b + 16, class: "ch-text", "text-anchor": "middle" }, "0"));
  if (xlabel !== "test wind") svg.append(sv("text", { x: g.r, y: g.b + 16, class: "ch-text", "text-anchor": "end" }, xmax.toFixed(1)));
  svg.append(sv("text", { x: (g.l + g.r) / 2, y: g.b + 32, class: "ch-text", "text-anchor": "middle" }, xlabel));
  svg.append(sv("text", { x: 12, y: (g.t + g.b) / 2, class: "ch-text", "text-anchor": "middle", transform: "rotate(-90 12 " + (g.t + g.b) / 2 + ")" }, ylabel));
}

function emptyChart(el, msg) {
  el.replaceChildren(h("p", "chart-empty", msg));
}

function put(el, svg, summary) {
  el.replaceChildren(svg, h("p", "chart-sum", summary));
}

/** Speed over time with the plateau shaded and start/stop/settled markers. */
function drawSpeed(el, d) {
  if (d.pts.length < 2) return emptyChart(el, "The speed graph appears when the wheel starts turning.");
  const w = Math.max(280, el.clientWidth || 320);
  const g = { l: 52, r: w - 10, t: 20, b: 150 };
  const tmax = Math.max(1, d.pts[d.pts.length - 1].t);
  const peak = Math.max(1, ...d.pts.map((q) => q.speed));
  const ymax = peak * 1.1;
  const sx = (t) => g.l + (t / tmax) * (g.r - g.l);
  const sy = (v) => g.b - (v / ymax) * (g.b - g.t);
  const badge = speedBadge(d.pts);
  const svg = frame(el, w, 186, "Speed over time. " + badge + ". Peak " + Math.round(peak) + " pulses per second.");
  const hi = d.pts.filter((q) => q.speed >= 0.9 * peak);
  if (hi.length > 1) {
    svg.append(sv("rect", { x: sx(hi[0].t), y: g.t, width: Math.max(1, sx(hi[hi.length - 1].t) - sx(hi[0].t)), height: g.b - g.t, class: "ch-plateau" }));
  }
  axes(svg, g, "seconds", "speed", tmax, ymax, 0);
  d.marks.forEach((m, i) => {
    const n = markName(m.kind);
    if (!n) return;
    svg.append(sv("line", { x1: sx(m.t), y1: g.t, x2: sx(m.t), y2: g.b, class: "ch-mark" }));
    const flip = sx(m.t) > g.r - 60;
    svg.append(sv("text", { x: sx(m.t) + (flip ? -3 : 3), y: g.t + 24 + (i % 2) * 14, class: "ch-text", "text-anchor": flip ? "end" : "start" }, n));
  });
  svg.append(sv("polyline", { points: d.pts.map((q) => sx(q.t).toFixed(1) + "," + sy(q.speed).toFixed(1)).join(" "), class: "ch-line" }));
  put(el, svg, badge + ". Top speed " + Math.round(peak) + " pulses per second. The shaded part is full speed.");
  return badge;
}

/** Length over time; the coast after the stop is labelled. */
function drawPosition(el, d, params) {
  if (d.pts.length < 2) return emptyChart(el, "The length graph appears when the wheel starts turning.");
  const w = Math.max(280, el.clientWidth || 320);
  const g = { l: 52, r: w - 10, t: 20, b: 150 };
  const inch = params && params.cpr ? (c) => inchesFor(c, params) : null;
  const val = (c) => (inch ? inch(c) : c);
  const tmax = Math.max(1, d.pts[d.pts.length - 1].t);
  const vmax = Math.max(1, ...d.pts.map((q) => val(q.count))) * 1.1;
  const sx = (t) => g.l + (t / tmax) * (g.r - g.l);
  const sy = (v) => g.b - (v / vmax) * (g.b - g.t);
  const stop = d.marks.find((m) => markName(m.kind) === "stop");
  const settle = d.marks.find((m) => markName(m.kind) === "settled");
  const coast = stop && settle ? settle.count - stop.count : null;
  const svg = frame(el, w, 186, "Length over time" + (coast === null ? "." : ". It coasted " + coast + " pulses after the stop."));
  axes(svg, g, "seconds", inch ? "length (inches)" : "length (pulses)", tmax, vmax, inch ? 0 : 0);
  svg.append(sv("polyline", { points: d.pts.map((q) => sx(q.t).toFixed(1) + "," + sy(val(q.count)).toFixed(1)).join(" "), class: "ch-line" }));
  if (stop && settle) {
    svg.append(sv("circle", { cx: sx(stop.t), cy: sy(val(stop.count)), r: 4, class: "ch-dot-ok" }));
    svg.append(sv("circle", { cx: sx(settle.t), cy: sy(val(settle.count)), r: 4, class: "ch-dot-bad" }));
    svg.append(sv("text", { x: Math.min(sx(settle.t), w - 12), y: sy(val(settle.count)) + 18, class: "ch-text", "text-anchor": "end" }, "coast " + coast + " pulses"));
  }
  put(el, svg, coast === null ? "Length so far." : "After the stop the paper coasted " + coast + " pulses.");
  return null;
}

function mean(a) {
  return a.reduce((x, y) => x + y, 0) / a.length;
}

/** Coast of every test wind: valid runs filled, invalid hollow, mean line and spread band. */
function drawFollow(el, runs, result) {
  const list = (runs || []).filter((r) => r && typeof r.follow === "number" && !r.verify);
  if (!list.length) return emptyChart(el, "No test winds yet.");
  const w = Math.max(280, el.clientWidth || 320);
  const g = { l: 52, r: w - 10, t: 14, b: 140 };
  const isOk = (r) => r.valid !== false;
  const good = list.filter(isOk).map((r) => r.follow);
  const mu = good.length ? mean(good) : mean(list.map((r) => r.follow));
  let spread = result && typeof result.spread_pct === "number" ? (Math.abs(mu) * result.spread_pct) / 100 : null;
  if (spread === null) spread = good.length > 1 ? Math.sqrt(mean(good.map((v) => (v - mu) ** 2))) : 0;
  const ymax = Math.max(1, ...list.map((r) => r.follow), mu + spread) * 1.15;
  const sx = (i) => g.l + ((i + 0.5) / list.length) * (g.r - g.l);
  const sy = (v) => g.b - (v / ymax) * (g.b - g.t);
  const svg = frame(el, w, 180, "Coast of each test wind. Average " + Math.round(mu) + " pulses.");
  axes(svg, g, "test wind", "coast (pulses)", list.length, ymax, 0);
  svg.append(sv("rect", { x: g.l, y: sy(mu + spread), width: g.r - g.l, height: Math.max(1, sy(mu - spread) - sy(mu + spread)), class: "ch-plateau" }));
  svg.append(sv("line", { x1: g.l, y1: sy(mu), x2: g.r, y2: sy(mu), class: "ch-mark" }));
  list.forEach((r, i) => {
    const c = sv("circle", { cx: sx(i), cy: sy(r.follow), r: 7, class: isOk(r) ? "ch-dot-ok" : "ch-dot-hollow", tabindex: 0 });
    const label = "Test wind " + (i + 1) + ": coast " + r.follow + (isOk(r) ? "" : ", left out: " + (r.why_invalid || "not usable"));
    c.setAttribute("aria-label", label);
    c.append(sv("title", {}, label));
    svg.append(c);
    svg.append(sv("text", { x: sx(i), y: g.b + 16, class: "ch-text", "text-anchor": "middle" }, String(i + 1)));
  });
  const out = list.filter((r) => !isOk(r));
  const box = h("div", "chart-sum");
  box.append(h("p", null, good.length + " of " + list.length + " test winds used. Average coast " + Math.round(mu) +
    " pulses, spread about " + Math.round(spread) + ". Filled dots count, hollow dots were left out."));
  list.forEach((r, i) => { if (!isOk(r)) box.append(h("p", null, "Test wind " + (i + 1) + " left out: " + String(r.why_invalid || "not usable").replace(/\.$/, "") + ".")); });
  el.replaceChildren(svg, box);
  return out.length;
}

/** Leftover error before and after, as two bars. */
function drawResidual(el, result) {
  if (!result || typeof result.residual_pulses !== "number") return emptyChart(el, "The check result appears after the test.");
  const rows = [];
  if (typeof result.residual_before_pulses === "number") rows.push(["Before", result.residual_before_pulses]);
  rows.push(["After", result.residual_pulses]);
  const w = Math.max(280, el.clientWidth || 320);
  const max = Math.max(1, ...rows.map((r) => Math.abs(r[1])));
  const l = 64;
  const svg = frame(el, w, 30 + rows.length * 36, "Leftover error: " + rows.map((r) => r[0] + " " + r[1] + " pulses").join(", "));
  rows.forEach(([name, v], i) => {
    const y = 10 + i * 36;
    svg.append(sv("text", { x: l - 8, y: y + 17, class: "ch-text", "text-anchor": "end" }, name));
    svg.append(sv("rect", { x: l, y, width: Math.max(2, (Math.abs(v) / max) * (w - l - 100)), height: 24, class: i === rows.length - 1 ? "ch-bar-after" : "ch-bar" }));
    svg.append(sv("text", { x: l + Math.max(2, (Math.abs(v) / max) * (w - l - 100)) + 6, y: y + 17, class: "ch-text" }, v + " pulses"));
  });
  el.replaceChildren(svg, h("p", "chart-sum", "Leftover error is how far the length is off after the fix. Smaller is better."));
  return null;
}

let chartTimer = null;
let chartLatest = null;
function scheduleCharts(s) {
  chartLatest = s;
  if (chartTimer) return;
  chartTimer = setTimeout(() => {
    chartTimer = null;
    try {
      drawAllCharts(chartLatest);
    } catch (e) {
      console.error("chart draw failed", e);
    }
  }, 100);
}

const visible = (el) => !!el && el.offsetParent !== null;

function drawAllCharts(s) {
  const d = runSeries(s);
  const res = s.auto && s.auto.result;
  const runs = Array.isArray(s.runs) ? s.runs : [];
  const practice = typeof flow !== "undefined" && flow.mode === "practice";
  for (const prefix of ["f", "g"]) {
    if (prefix === "f" && practice) continue; // the practice animation draws these itself
    const sp = $(prefix + "-speed");
    if (visible(sp)) {
      const b = drawSpeed(sp, d);
      $(prefix + "-badge").textContent = b || speedBadge(d.pts);
    }
    const ps = $(prefix + "-pos");
    if (visible(ps)) drawPosition(ps, d, s.params);
    const fo = $(prefix + "-follow");
    if (visible(fo)) drawFollow(fo, runs, res);
    const rs = $(prefix + "-resid");
    if (visible(rs)) drawResidual(rs, res);
  }
}

addEventListener("resize", () => current && scheduleCharts(current));


// ---------------------------------------------------- optional length check

const SHEETS = { letter: { name: "US letter (11 in)", inches: 11 }, a4: { name: "A4 (29.7 cm)", inches: 29.7 / 2.54 } };
const UNIT_KEY = "winder.units";
let memUnit = "in";

function unitPref() {
  try {
    return localStorage.getItem(UNIT_KEY) === "cm" ? "cm" : "in";
  } catch (e) {
    return memUnit;
  }
}
function setUnitPref(u) {
  memUnit = u;
  try {
    localStorage.setItem(UNIT_KEY, u);
  } catch (e) {
    /* storage blocked: keep it for this page load */
  }
}

/** Plain message for an unusable measured length (inches), or null when fine. */
function badLength(inches, expected) {
  if (!Number.isFinite(inches)) return "Type a number.";
  if (inches <= 0) return "The length must be more than zero.";
  if (expected > 0 && inches > 10 * expected) {
    return "That is far more than the machine moved (about " + fmt(expected, 0) + " inches). Check the number and the units.";
  }
  return null;
}

/** Plain-words verdict of a length-check proposal ({percent, new_wheel_um}); the sign is the wheel correction. */
function lengthVerdict(lc) {
  const pr = lc && lc.proposal;
  if (!pr || typeof pr.percent !== "number") return null;
  const dir = pr.percent > 0 ? "short" : "long";
  const p = current && current.params;
  const detail = p && pr.new_wheel_um
    ? "Wheel size " + fmt(p.wheel_um / 25400, 4) + " in -> " + fmt(pr.new_wheel_um / 25400, 4) + " in. Every length and the 30 yd and 90 yd targets move by " + fmt(Math.abs(pr.percent), 1) + "%."
    : "";
  return { small: false, text: fmt(Math.abs(pr.percent), 1) + "% off (the wheel reads " + dir + "). Apply?", detail };
}

function lengthCheckCard(opts) {
  const el = h("section", "lcheck");
  const st = { chosen: false, ran: false, decided: false, skipped: false, method: "ruler" };
  el.append(h("p", "lead", "This catches slipping that the machine cannot see."),
    h("p", "tour-p muted", "It checks whether the wheel size is right. Changing the wheel size changes every length the machine reports and the 30 yd and 90 yd targets (the machine re-derives them)."));
  const choice = h("div", "row");
  const doIt = h("button", "btn btn-big", "Do the check", { type: "button" });
  const skip = h("button", "btn btn-big", "Skip", { type: "button" });
  choice.append(doIt, skip);
  const done = h("p", "tour-p muted", "");
  done.hidden = true;

  const steps = h("ol", "steps lsteps");
  steps.hidden = true;
  const s1 = h("li", null, "Draw a line across the paper at the table edge.");
  const s2 = h("li", null);
  const run = h("button", "btn btn-main btn-big", "Run", { type: "button" });
  s2.append("Press Run. ", run);
  const s3 = h("li", null);
  s3.append("Measure from the table edge to the line.");
  const form = h("div", "lform");
  form.hidden = true;
  // units + method
  const unitRow = h("div", "row");
  const mkRadio = (name, value, label, checked, onChange) => {
    const lab = h("label", "check");
    const r = h("input", null, null, { type: "radio", name, value });
    r.checked = checked;
    r.addEventListener("change", onChange);
    lab.append(r, " " + label);
    return lab;
  };
  const unitLabel = () => (unitPref() === "cm" ? "centimetres" : "inches");
  const inputs = h("div", "linputs");
  const note = h("p", "tour-p muted");
  const err = h("p", "alert", "", { role: "alert" });
  err.hidden = true;
  const expectP = h("p", "tour-p muted");
  const cont = h("button", "btn btn-main btn-big", "Continue", { type: "button" });

  let valueInput = null;
  let sheetsInput = null;
  let sheetSel = null;

  const buildInputs = () => {
    inputs.replaceChildren();
    err.hidden = true;
    if (st.method === "ruler") {
      const lab = h("label", "lbl lbl-big", "Length in " + unitLabel() + " ");
      valueInput = h("input", "field field-big", null, { type: "number", step: "any", inputmode: "decimal" });
      lab.append(valueInput);
      inputs.append(lab);
      note.textContent = "Use a ruler or tape measure.";
    } else {
      sheetSel = h("select", "field field-big", null, { "aria-label": "Sheet size" });
      for (const [k, v] of Object.entries(SHEETS)) sheetSel.append(h("option", null, v.name, { value: k }));
      const l1 = h("label", "lbl lbl-big", "Sheet size ");
      l1.append(sheetSel);
      const l2 = h("label", "lbl lbl-big", "Whole sheets from the table edge ");
      sheetsInput = h("input", "field field-big", null, { type: "number", min: "0", step: "1", value: "0", inputmode: "numeric" });
      l2.append(sheetsInput);
      const l3 = h("label", "lbl lbl-big", "Line is past the last sheet end by (" + unitLabel() + ", minus if short) ");
      valueInput = h("input", "field field-big", null, { type: "number", step: "any", value: "0", inputmode: "decimal" });
      l3.append(valueInput);
      inputs.append(l1, l2, l3);
      note.textContent = "Lay sheets end to end from the table edge. This is only good for catching large errors.";
    }
  };
  const unitBox = h("div", "row");
  const redrawUnits = () => {
    unitBox.replaceChildren(
      h("span", "muted", "Units:"),
      mkRadio("lc-unit", "in", "inches", unitPref() === "in", () => { setUnitPref("in"); redrawUnits(); buildInputs(); }),
      mkRadio("lc-unit", "cm", "centimetres", unitPref() === "cm", () => { setUnitPref("cm"); redrawUnits(); buildInputs(); }),
    );
  };
  unitRow.append(
    h("span", "muted", "Measure with:"),
    mkRadio("lc-method", "ruler", "Ruler or tape", true, () => { st.method = "ruler"; buildInputs(); }),
    mkRadio("lc-method", "sheet", "Sheet of paper", false, () => { st.method = "sheet"; buildInputs(); }),
  );
  redrawUnits();
  buildInputs();
  form.append(expectP, unitRow, unitBox, inputs, note, err, cont);
  s3.append(form);
  steps.append(s1, s2, s3);

  const result = h("div", "lresult");
  result.hidden = true;
  const verdict = h("p", "tour-big", "");
  const vdetail = h("p", "tour-p muted", "");
  const apply = h("button", "btn btn-main btn-big", "Apply", { type: "button" });
  const keep = h("button", "btn btn-big", "Skip", { type: "button" });
  const rrow = h("div", "row");
  rrow.append(apply, keep);
  result.append(verdict, vdetail, rrow);

  el.append(choice, steps, result, done);

  const finish = (msg) => {
    st.decided = true;
    choice.hidden = true;
    steps.hidden = true;
    result.hidden = true;
    done.textContent = msg;
    done.hidden = false;
  };
  doIt.addEventListener("click", () => {
    st.chosen = true;
    choice.hidden = true;
    steps.hidden = false;
  });
  skip.addEventListener("click", () => {
    st.skipped = true;
    finish("Length check skipped.");
    if (opts && opts.onSkip) opts.onSkip();
  });
  run.addEventListener("click", () =>
    act(run, async () => {
      const r = await backend.calibrateLengthCheck();
      if (!r.ok) return flowFail("The length check did not start (" + (r.error || "no answer") + ").");
      st.ran = true;
      return undefined;
    }));
  cont.addEventListener("click", () =>
    act(cont, async () => {
      const unit = unitPref() === "cm" ? 2.54 : 1;
      let inches;
      if (st.method === "ruler") inches = Number(valueInput.value) / unit;
      else {
        const sheets = Number(sheetsInput.value);
        if (!Number.isInteger(sheets) || sheets < 0) {
          err.textContent = "Type a whole number of sheets, 0 or more.";
          err.hidden = false;
          return undefined;
        }
        inches = sheets * SHEETS[sheetSel.value].inches + Number(valueInput.value) / unit;
      }
      const a = (current && current.auto) || {};
      const expected = Number((a.length_check || {}).expected_in) || 0;
      const msg = valueInput.value === "" ? "Type a number." : badLength(inches, expected);
      err.hidden = !msg;
      err.textContent = msg || "";
      if (msg) return undefined;
      const r = await backend.calibrateMeasureLength(inches);
      if (!r.ok) {
        err.textContent = "The number was not accepted (" + (r.error || "no answer") + ").";
        err.hidden = false;
      }
      return undefined;
    }));
  apply.addEventListener("click", () =>
    act(apply, async () => {
      const r = await backend.applyWheel();
      if (!r.ok) return flowFail("The new wheel size was not accepted (" + (r.error || "no answer") + ").");
      finish("The wheel size was corrected from the length check.");
      return undefined;
    }));
  keep.addEventListener("click", () =>
    act(keep, async () => {
      await backend.skipMeasure();
      finish("Length check done. No change made.");
    }));

  return {
    el,
    started: () => st.chosen && !st.decided,
    decided: () => st.decided,
    running: (a) => st.chosen && !st.decided && !/need_measure|ready_to_save/i.test(String(a.phase || "")),
    everRan: () => st.ran,
    update(s, a) {
      const lc = a.length_check || {};
      const phase = String(a.phase || "");
      if (/preparing|running|settling|need_measure|computing/i.test(phase)) st.sawBusy = true;
      const expected = Number(lc.expected_in);
      expectP.textContent = expected > 0 ? "The paper should have moved about " + (unitPref() === "cm" ? fmt(expected * 2.54, 0) + " cm" : fmt(expected, 0) + " inches") + "." : "";
      setDis(run, st.ran || streamLost);
      setDis(doIt, false);
      const v = phase === "need_measure" ? lengthVerdict(lc) : null;
      form.hidden = phase !== "need_measure" || !!v;
      const showResult = !st.decided && !!v;
      result.hidden = !showResult;
      if (showResult) {
        verdict.textContent = v.text;
        vdetail.textContent = v.detail;
      }
      if (!st.decided && st.ran && st.sawBusy && phase === "ready_to_save") {
        finish((lc.result && lc.result.note) || (a.result && a.result.note) || "Length check done.");
      }
    },
  };
}


/** Fake run series for the browser-only practice (a ramp, a plateau, a stop, a coast). */
function fakeSeries(t, totalPulses) {
  const ser = [];
  const marks = [{ t_ms: 0, kind: "start", count: 0 }];
  const top = totalPulses / 4.3;
  let count = 0;
  for (let ms = 0; ms <= t * 1000; ms += 80) {
    const x = ms / 1000;
    let v;
    if (x < 1) v = top * x;
    else if (x < 4) v = top;
    else if (x < 4.6) v = top * (1 - (x - 4) / 0.6);
    else v = 0;
    count += (v * 0.08);
    ser.push({ t_ms: ms, count, speed: v, relay: x < 4, run: x < 4.6 });
  }
  if (t >= 4) marks.push({ t_ms: 4000, kind: "stop", count: totalPulses - 40 });
  if (t >= 5) marks.push({ t_ms: 5000, kind: "settled", count: totalPulses });
  return { ser, marks };
}


const AUTO_FAIL = /fail|cancel|abort|error/i;
const AUTO_DONE = /^(done|complete|completed|finished|verified|review|saved|ready_to_save|need_measure)$/i;

function wordFor(res) {
  // The program decides (auto.result.verified); the old guess only serves an older program.
  if (typeof res.verified === "boolean") return res.verified;
  return res.residual_pulses === null || res.residual_pulses === undefined ? true : Math.abs(Number(res.residual_pulses)) <= 20;
}

function summaryLines(res, p) {
  const out = [];
  if (res.wheel_um_before !== undefined && res.wheel_um_after !== undefined) {
    out.push(res.wheel_um_before === res.wheel_um_after
      ? "Wheel size " + fmt(res.wheel_um_after / 25400, 4) + " in (unchanged)"
      : "Wheel size " + fmt(res.wheel_um_before / 25400, 4) + " in -> " + fmt(res.wheel_um_after / 25400, 4) + " in");
  }
  if (res.coast_before !== undefined && res.coast_after !== undefined) {
    const pp = p || FACTORY_FALLBACK;
    const inch = (v) => fmt(inchesFor(v, { cpr: pp.cpr, wheel_um: res.wheel_um_after || pp.wheel_um }), 2) + " in";
    out.push(res.coast_before === res.coast_after
      ? "How far the paper coasts after the motor stops: " + inch(res.coast_after) + " (unchanged)"
      : "How far the paper coasts after the motor stops: " + inch(res.coast_before) + " -> " + inch(res.coast_after) + " (" + res.coast_before + " -> " + res.coast_after + " pulses)");
  }
  return out;
}



// ------------------------------------------------------- shared small state

const AUTO_BUSY = /^(preparing|running|settling|need_measure|computing|verifying)$/i;
const autoBusy = (s) => !!(s.auto && AUTO_BUSY.test(String(s.auto.phase || "")));

const FACTORY_FALLBACK = { wheel_um: 101638, cpr: 2400, short: 206187, long: 618562, coast: 0 };
const CAL_KEY = "winder.calibrated";
let memCal = "";
let settingsDirty = false;

function calibratedAt() {
  try {
    return localStorage.getItem(CAL_KEY) || memCal;
  } catch (e) {
    return memCal;
  }
}
function markCalibrated() {
  memCal = new Date().toISOString();
  try {
    localStorage.setItem(CAL_KEY, memCal);
  } catch (e) {
    /* storage blocked: remembered for this page load only */
  }
}

// ------------------------------------------------- wheel size dependency

/** The one wording used wherever a length appears (live wheel size interpolated). */
function wheelNoteText(p) {
  return p && p.wheel_um
    ? "Lengths are worked out from the measuring wheel size (currently " + fmt(p.wheel_um / 25400, 4) + " in). If that number is wrong, every length here is wrong by the same percentage."
    : "";
}

function goToWheelSetting() {
  showTab("settings");
  const el = $("p-wheel_um");
  if (el) {
    el.scrollIntoView({ block: "center" });
    el.focus();
  }
}

/** Fill every .wheel-note and .wheel-badge from the live wheel size. */
function renderWheelNotes(p) {
  for (const n of document.querySelectorAll(".wheel-note")) {
    if (!n.dataset.built) {
      n.dataset.built = "1";
      n.append(h("span", "wn-text"));
      if (!n.dataset.nolink) {
        const go = h("button", "linkbtn", "Change it in Settings", { type: "button" });
        go.addEventListener("click", goToWheelSetting);
        n.append(" ", go);
      }
    }
    const t = n.querySelector(".wn-text");
    const v = wheelNoteText(p);
    if (t.textContent !== v) t.textContent = v;
  }
  for (const b of document.querySelectorAll(".wheel-badge")) {
    if (!b.dataset.built) {
      b.dataset.built = "1";
      b.addEventListener("click", goToWheelSetting);
    }
    const v = p && p.wheel_um ? "based on wheel size " + fmt(p.wheel_um / 25400, 4) + " in" : "";
    if (b.textContent !== v) b.textContent = v;
    b.hidden = !v;
  }
}

// ------------------------------------------------------------ stop bar

function renderStop(s) {
  const snap = s.snapshot;
  const dbg = isDebug(s);
  const manual = !!snap && snap.relay && (snap.state === "IDLE" || snap.state === "PAUSED");
  const busyNow = autoBusy(s) || jobRunning(s);
  const active = dbg || busyNow || manual;
  $("stopbar").hidden = !active;
  $("stopbar").classList.toggle("is-test", dbg);
  if (!active) return;
  let msg = "The motor is on by hand.";
  if (dbg) msg = "TEST MODE - the machine only moves from this screen";
  else if (busyNow) msg = "A test is running. Do not touch the machine.";
  text("stopbar-text", msg);
  setDis($("stop-motor"), false);
}

// ------------------------------------------------------------------- tabs

const TABS = ["home", "calibrate", "settings", "advanced"];
let currentTab = "home";

function showTab(name, opts) {
  const o = opts || {};
  if (!TABS.includes(name)) name = "home";
  if (currentTab === "calibrate" && name !== "calibrate" && flow.firstRun) {
    flow.firstRun = false;
    markTourDone(); // the first-run visit is over once the user moves on
  }
  currentTab = name;
  for (const t of TABS) {
    const on = t === name;
    const tab = $("tab-" + t);
    tab.setAttribute("aria-selected", on ? "true" : "false");
    tab.tabIndex = on ? 0 : -1;
    $("panel-" + t).hidden = !on;
  }
  if (o.replace !== false) {
    try {
      history.replaceState(null, "", "#" + name);
    } catch (e) {
      /* no history API: the tab still works */
    }
  }
  if (o.focus) $("tab-" + name).focus();
  if (current) scheduleCharts(current);
}

$("tabs").addEventListener("keydown", (e) => {
  const i = TABS.indexOf(currentTab);
  let n = -1;
  if (e.key === "ArrowRight") n = (i + 1) % TABS.length;
  else if (e.key === "ArrowLeft") n = (i + TABS.length - 1) % TABS.length;
  else if (e.key === "Home") n = 0;
  else if (e.key === "End") n = TABS.length - 1;
  if (n < 0) return;
  e.preventDefault();
  showTab(TABS[n], { focus: true });
});
for (const t of TABS) $("tab-" + t).addEventListener("click", () => showTab(t));
addEventListener("hashchange", () => showTab(location.hash.replace("#", ""), { replace: false }));

// ------------------------------------------------------------------- home

function renderHome(s) {
  const snap = s.snapshot;
  let machine = "Ready";
  if (snap.state === "DEBUG") machine = "Test mode";
  else if (snap.state === "WINDING") machine = "Winding";
  else if (snap.state === "PAUSED") machine = "Paused";
  text("live-machine", machine);
  text("live-motor", snap.relay ? "On" : "Off");
  const l = lengthText(snap.count, s.params);
  text("live-length", l.big);
  text("live-length-in", l.small);
  const when = calibratedAt();
  const label = flow.started && !flow.saved ? "Go back to the calibration" : "Calibrate the machine";
  if ($("home-cal").textContent !== label) $("home-cal").textContent = label;
  text("home-cal-state", when
    ? "Last calibrated from this app on " + new Date(when).toLocaleDateString() + "."
    : "This machine has not been calibrated from this app yet.");
}
$("home-cal").addEventListener("click", () => {
  showTab("calibrate");
  stepHeading().focus();
});

// ----------------------------------------------------------------- settings

const SETTINGS = [
  { key: "wheel_um", label: "Measuring wheel size", help: "The measuring wheel across. Sets how long every length is.", unit: "inches", short: "in", dec: 4,
    rawUnit: "micrometres", min: 50000, max: 300000,
    toRaw: (x) => Math.round(x * 25400), fromRaw: (v) => v / 25400 },
  { key: "cpr", label: "Encoder counts per wheel turn", help: "Change only if the encoder was replaced.", unit: "counts", short: "", dec: 0,
    rawUnit: "counts", min: 100, max: 100000,
    toRaw: (x) => Math.round(x), fromRaw: (v) => v },
  { key: "short", label: "Short wind (30 yd button)", help: "How far the short wind goes.", unit: "yards", short: "yd", dec: 2,
    rawUnit: "pulses", min: 1000, max: 4000000,
    toRaw: (x, p) => Math.round((x * 36 * p.cpr) / (Math.PI * (p.wheel_um / 25400))),
    fromRaw: (v, p) => ((v / p.cpr) * Math.PI * (p.wheel_um / 25400)) / 36 },
  { key: "long", label: "Long wind (90 yd button)", help: "How far the long wind goes.", unit: "yards", short: "yd", dec: 2,
    rawUnit: "pulses", min: 1000, max: 4000000,
    toRaw: (x, p) => Math.round((x * 36 * p.cpr) / (Math.PI * (p.wheel_um / 25400))),
    fromRaw: (v, p) => ((v / p.cpr) * Math.PI * (p.wheel_um / 25400)) / 36 },
  { key: "coast", label: "Coast after the motor stops", help: "How far the paper coasts once the motor is cut.", unit: "inches", short: "in", dec: 2,
    rawUnit: "pulses", min: 0, max: null,
    toRaw: (x, p) => Math.round((x * p.cpr) / (Math.PI * (p.wheel_um / 25400))),
    fromRaw: (v, p) => (v / p.cpr) * Math.PI * (p.wheel_um / 25400) },
];

const nf = (v) => Number(v).toLocaleString("en-US");

/** "4.0015 in" style text for a raw setting value. */
function niceValue(def, v, p) {
  const f = def.fromRaw(v, p);
  return fmt(f, def.dec) + (def.short ? " " + def.short : "");
}
/** Friendly value with the raw one in brackets (raw is skipped when they are the same). */
function bothValue(def, v, p) {
  return def.key === "cpr" ? nf(v) + " counts" : niceValue(def, v, p) + " (" + nf(v) + " " + def.rawUnit + ")";
}

/** Plain message for an out-of-range setting (raw value n), or null. Same ranges as the firmware. */
function badSetting(def, n, p) {
  const lo = def.fromRaw(def.min, p);
  if (n < def.min || (def.max !== null && n > def.max)) {
    if (def.max === null) return def.label + " must be 0 or more.";
    return def.label + " must be between " + fmt(lo, def.dec) + " and " + fmt(def.fromRaw(def.max, p), def.dec) + " " + def.unit + ".";
  }
  if (def.key === "coast") {
    const lim = Math.min(p.short, p.long);
    if (n >= lim) return "The coast must be shorter than the short wind (" + fmt(def.fromRaw(lim, p), 2) + " inches).";
  }
  return null;
}

/** One or two plain sentences saying what a change will do. */
function consequence(def, oldV, newV, p) {
  const pct = fmt(Math.abs(newV / oldV - 1) * 100, 2) + "%";
  switch (def.key) {
    case "wheel_um":
      return "Every length and the 30 yd and 90 yd targets move by " + pct + " (the machine re-derives the targets). Real winds come out about " + pct + (newV > oldV ? " shorter" : " longer") + ".";
    case "cpr":
      return "Unless the encoder really changed, every wind will be about " + pct + (newV > oldV ? " longer" : " shorter") +
        ". The machine recalculates the 30 yd and 90 yd lengths by itself.";
    case "short":
      return "This changes the 30 yd button: it will wind " + niceValue(def, newV, p) + " instead of " + niceValue(def, oldV, p) + ".";
    case "long":
      return "This changes the 90 yd button: it will wind " + niceValue(def, newV, p) + " instead of " + niceValue(def, oldV, p) + ".";
    default:
      return newV > oldV
        ? "The motor will be cut off earlier, by " + fmt(def.fromRaw(newV - oldV, p), 2) + " inches."
        : "The motor will be cut off later, by " + fmt(def.fromRaw(oldV - newV, p), 2) + " inches.";
  }
}

/** Accessible "Are you sure?" dialog; resolves true only on the explicit yes. */
function confirmDialog({ title: t, lines, yes }) {
  const dlg = $("confirm");
  if (typeof dlg.showModal !== "function") return Promise.resolve(confirm(t + "\n" + lines.join("\n")));
  if (dlg.open) return Promise.resolve(false);
  $("confirm-title").textContent = t;
  $("confirm-body").replaceChildren(...lines.map((l) => h("p", "confirm-line", l)));
  $("confirm-yes").textContent = yes;
  dlg.returnValue = "";
  return new Promise((resolve) => {
    dlg.addEventListener("close", () => resolve(dlg.returnValue === "yes"), { once: true });
    dlg.showModal();
    $("confirm-no").focus();
  });
}

function buildSettingRow(def) {
  const k = def.key;
  const row = h("div", "srow", null, { id: "set-" + k });
  const name = h("div", "srow-name");
  name.append(h("p", "srow-label", def.label), h("p", "muted srow-help", def.help));
  const now = h("div", "srow-now");
  now.append(h("p", "srow-lab", "Now"), h("p", "srow-big", "-", { id: "now-" + k }), h("p", "srow-small", "", { id: "raw-" + k }));
  const fac = h("div", "srow-fac");
  fac.append(h("p", "srow-lab", "Factory"), h("p", "srow-fval", "-", { id: "fac-" + k }));
  const edit = h("div", "srow-edit");
  const inp = h("input", "field", null, { type: "number", id: "p-" + k, "aria-label": def.label + ", new value in " + def.unit, step: "any", inputmode: "decimal" });
  const btn = h("button", "btn", "Change", { type: "button", "aria-label": "Change " + def.label.toLowerCase() });
  edit.append(inp, btn);
  const ok = h("p", "setting-ok", "", { role: "status" });
  const bad = h("p", "setting-bad", "", { role: "alert" });
  bad.hidden = true;
  inp.addEventListener("input", () => { inp.dataset.touched = "1"; });
  btn.addEventListener("click", () =>
    act(btn, async () => {
      const p = current.params;
      ok.textContent = "";
      const x = inp.value.trim() === "" ? NaN : Number(inp.value);
      if (!Number.isFinite(x)) { bad.textContent = "Type a number."; bad.hidden = false; return; }
      const n = def.toRaw(x, p);
      const msg = badSetting(def, n, p);
      if (msg) { bad.textContent = msg; bad.hidden = false; return; }
      if (n === p[k]) { bad.textContent = "That is already the value."; bad.hidden = false; return; }
      bad.hidden = true;
      const yes = await confirmDialog({
        title: "Change " + def.label.toLowerCase() + "?",
        lines: [def.label + ": " + niceValue(def, p[k], p) + " -> " + niceValue(def, n, p), "(" + nf(p[k]) + " -> " + nf(n) + " " + def.rawUnit + ")", consequence(def, p[k], n, p)],
        yes: "Yes, change it",
      });
      if (!yes) return;
      const r = await qcmd("SET", k, n);
      if (r.ok) {
        settingsDirty = true;
        inp.dataset.touched = "";
        ok.textContent = "Changed.";
        setTimeout(() => { ok.textContent = ""; }, 6000);
      } else {
        bad.textContent = r.error || "The machine did not accept that.";
        bad.hidden = false;
      }
    }));
  row.append(name, now, fac, edit, ok, bad);
  return row;
}

function renderSettings(s) {
  const box = $("params");
  const p = s.params;
  if (!p) return;
  if (box.dataset.built !== "1") {
    box.dataset.built = "1";
    for (const def of SETTINGS) box.append(buildSettingRow(def));
  }
  const fp = s.factory || FACTORY_FALLBACK;
  for (const def of SETTINGS) {
    text("now-" + def.key, def.key === "cpr" ? nf(p.cpr) : niceValue(def, p[def.key], p));
    text("raw-" + def.key, def.key === "cpr" ? "counts" : nf(p[def.key]) + " " + def.rawUnit);
    text("fac-" + def.key, def.key === "cpr" ? nf(fp.cpr) : niceValue(def, fp[def.key], fp));
    const inp = $("p-" + def.key);
    if (document.activeElement !== inp && inp.dataset.touched !== "1") inp.value = def.fromRaw(p[def.key], p).toFixed(def.dec);
  }
  $("unsaved").hidden = !settingsDirty;
  const lab = "Settings" + (settingsDirty ? " *" : "");
  if ($("tab-settings").textContent !== lab) {
    $("tab-settings").textContent = lab;
    $("tab-settings").setAttribute("aria-label", settingsDirty ? "Settings, unsaved changes" : "Settings");
  }
}

// ----------------------------------------------------------------- advanced

/** Pulses for a distance in inches or yards, from the live wheel and counts per turn. */
function pulsesFor(dist, unit, p) {
  if (!p || !p.cpr || !(dist > 0)) return null;
  const inches = unit === "yd" ? dist * 36 : dist;
  return Math.round((inches * p.cpr) / (Math.PI * (p.wheel_um / 25400)));
}

function renderDump(d, streaming) {
  text("mstate-src", streaming ? "(live)" : "(refreshed 10 times a second)");
  if (!changed("dump", d)) return;
  const dl = $("mstate");
  dl.replaceChildren();
  if (!d) return;
  for (const [k, v] of Object.entries(d)) {
    dl.append(h("dt", null, k), h("dd", null, Array.isArray(v) ? v.join(",") : String(v)));
  }
}

function renderRuns(runs) {
  $("runs-empty").hidden = runs.length > 0;
  const body = $("runs");
  if (body.contains(document.activeElement) && body.children.length === runs.length) return;
  if (!changed("runs", runs)) return;
  body.replaceChildren();
  runs.forEach((r, i) => {
    const tr = h("tr");
    for (const v of [r.target, r.at_stop, r.final, r.follow, r.residual]) tr.append(h("td", null, v));
    const td = h("td");
    const inp = h("input", "field", null, { type: "number", step: "any", "aria-label": "Measured inches, test wind " + (i + 1) });
    inp.value = r.measured_in === null ? "" : r.measured_in;
    inp.addEventListener("change", () =>
      shown(backend.calibrateMeasureRun(i, inp.value === "" ? null : Number(inp.value))));
    td.append(inp);
    tr.append(td);
    body.append(tr);
  });
}

function renderLog(lines) {
  if (!changed("log", lines)) return;
  const ol = $("log");
  ol.replaceChildren();
  for (const l of lines.slice(-20).reverse()) ol.append(h("li", null, l));
}

function renderChecks(s) {
  if (!changed("checks", s.doctor)) return;
  const ul = $("checks");
  ul.replaceChildren();
  if (!s.doctor) return;
  const word = { ok: "OK", fail: "PROBLEM", skipped: "WAITING" };
  for (const c of s.doctor.checks) ul.append(h("li", null, word[c.status] + " - " + c.title + ": " + c.message));
}

function renderAdvanced(s) {
  const dbg = isDebug(s);
  $("debug").checked = dbg;
  setDis($("debug"), false);
  setDis($("relay"), !dbg);
  $("relay").textContent = "Motor: " + (s.snapshot.relay ? "on" : "off");
  const p = s.params;
  const rp = pulsesFor(Number($("run-dist").value), $("run-unit").value, p);
  const rd = Number($("run-dist").value);
  const rin = $("run-unit").value === "yd" ? rd * 36 : rd;
  text("run-note", rp
    ? fmt(rin, 1) + " in = " + fmt(rin / 36, 2) + " yd = " + nf(rp) + " pulses, sent to the machine. The pulse count depends on the wheel size." + (rp > 4000000 ? " That is too long (most is 4,000,000)." : "")
    : "Type a distance above zero.");
  setDis($("run"), !dbg || !rp || rp > 4000000);
  const cp = pulsesFor(Number($("cal-dist").value), $("cal-unit").value, p);
  setDis($("cal-add"), !dbg || jobRunning(s) || autoBusy(s) || !cp || cp > 4000000);
  text("job", s.job === "idle" ? "" : s.job);
  text("sug-coast", s.suggest.coast);
  text("sug-wheel", s.suggest.wheel_um);
  setDis($("apply"), s.suggest.coast === null && s.suggest.wheel_um === null);
  renderDump(s.dump, s.streaming);
  renderRuns(s.runs);
  renderChecks(s);
  setDis($("recheck"), s.checking);
}

// ------------------------------------------------------------ calibrate flow
// ONE guided flow: 1 Get ready, 2 Test winds, 3 Check the length (optional), 4 Save.
// The machine does the work (POST /api/calibrate/auto); this side only watches
// snapshot.auto and asks for the one thing it cannot know: a measured length.

const FLAG = "winder.tour.done";
let memDone = false;
let autoTried = false;

function tourDone() {
  try {
    return localStorage.getItem(FLAG) === "1" || memDone;
  } catch (e) {
    return memDone;
  }
}
function markTourDone() {
  memDone = true;
  try {
    localStorage.setItem(FLAG, "1");
  } catch (e) {
    /* storage blocked: fall back to once per page load */
  }
}

const AUTO_PULSES = 50000;
const CHECKS = [
  ["The roll of paper is on the spindle and taped to the core.", "Taped, so it cannot slip and spoil the measurement."],
  ["The paper path is clear and my hands are away from the wheel.", "The wheel turns by itself."],
  ["The machine is plugged in and switched on.", "It cannot move without power."],
  ["The stop button is within reach.", "You can stop everything at any moment."],
];

const flow = {
  step: 1, mode: "real", ticks: CHECKS.map(() => false), started: false, saw: false, startAt: 0, err: null,
  autoActive: false, res1: null, saved: false, more: false, timer: null, firstRun: false, adopted: false, card: null,
};

const STEP_NAMES = ["Get ready", "Test winds", "Check the length (optional)", "Save"];

function stepHeading() {
  let id = "st" + flow.step;
  if (flow.err) id = "st-err";
  else if (flow.mode === "practice" && flow.step >= 3) id = "st-p";
  return $(id).querySelector(".step-title");
}

function syncFlow() {
  const practice = flow.mode === "practice";
  for (let i = 1; i <= 4; i++) $("st" + i).hidden = !!flow.err || (practice && i >= 3) || flow.step !== i;
  $("st-err").hidden = !flow.err;
  $("st-p").hidden = !(practice && flow.step >= 3 && !flow.err);
  $("practice-strip").hidden = !practice;
  $("flow-restart").hidden = !(flow.step > 1 || flow.started || practice);
  const items = $("stepper").children;
  for (let i = 0; i < items.length; i++) {
    const n = i + 1;
    const li = items[i];
    li.classList.toggle("is-current", n === flow.step);
    li.classList.toggle("is-done", n < flow.step);
    if (n === flow.step) li.setAttribute("aria-current", "step");
    else li.removeAttribute("aria-current");
    let sr = li.querySelector(".sr");
    if (!sr) {
      sr = h("span", "sr");
      li.append(sr);
    }
    const t = n < flow.step ? " (done)" : n === flow.step ? " (current step)" : "";
    if (sr.textContent !== t) sr.textContent = t;
  }
}

function setStep(n, focus) {
  flow.step = n;
  flow.err = null;
  syncFlow();
  if (focus) stepHeading().focus();
  if (current) renderFlow(current);
}

function setFlowStatus(t) {
  const el = $("flow-status");
  if (el.textContent !== t) el.textContent = t;
}

function stopSim() {
  if (flow.timer) clearInterval(flow.timer);
  flow.timer = null;
}

function mountCheckCard() {
  flow.card = lengthCheckCard({ onSkip: () => setStep(4, true) });
  $("st3-card").replaceChildren(flow.card.el);
}

/** Back to a clean start (used by Start again, Leave and finishing). */
function resetFlow() {
  stopSim();
  flow.mode = "real";
  flow.step = 1;
  flow.ticks = CHECKS.map(() => false);
  flow.started = false;
  flow.saw = false;
  flow.err = null;
  flow.autoActive = false;
  flow.res1 = null;
  flow.saved = false;
  flow.more = false;
  buildPrep();
  mountCheckCard();
  $("st2-practice").hidden = false;
  syncFlow();
}

function flowFail(msg) {
  if (flow.err) return;
  stopSim();
  if (flow.mode === "real" && flow.autoActive) haltMachine(current);
  flow.autoActive = false;
  flow.err = { msg };
  text("st-err-msg", msg);
  syncFlow();
  if (currentTab === "calibrate") stepHeading().focus();
}

async function haltMachine(s) {
  await backend.calibrateCancel();
  if (s && s.snapshot && s.snapshot.state === "DEBUG") {
    await qcmd("ABORT");
    await qcmd("RELAY", 0);
  }
}

// ---- step 1: checklist

function buildPrep() {
  const ul = $("prep");
  ul.replaceChildren();
  CHECKS.forEach(([what, why], i) => {
    const li = h("li", "prep-item");
    const lab = h("label", "check check-big");
    const cb = h("input", null, null, { type: "checkbox" });
    cb.checked = flow.ticks[i];
    cb.addEventListener("change", () => {
      flow.ticks[i] = cb.checked;
      updatePrep();
    });
    lab.append(cb, " " + what);
    li.append(lab, h("p", "prep-why", why));
    ul.append(li);
  });
  updatePrep();
}

function updatePrep() {
  const left = flow.ticks.filter((t) => !t).length;
  $("st1-next").disabled = left > 0 || busy.has($("st1-next"));
  if (flow.step === 1 && flow.mode === "real") {
    setFlowStatus(left ? "Tick " + left + " more " + (left === 1 ? "box" : "boxes") + " to go on." : "All set. You can go on.");
  }
}

// ---- graphs in the flow

function applyGraphVisibility(primary) {
  for (const w of $("f-graphs").children) w.hidden = !(w.dataset.g === primary || flow.more);
  const more = $("f-more");
  const t = flow.more ? "Show fewer graphs" : "Show more graphs";
  if (more.textContent !== t) more.textContent = t;
  more.setAttribute("aria-expanded", flow.more ? "true" : "false");
}
$("f-more").addEventListener("click", () => {
  flow.more = !flow.more;
  if (current) renderFlow(current);
  scheduleCharts(current || {});
});

// ---- step 2: test winds

function runRows(rows, params) {
  const tb = $("st2-rows");
  tb.replaceChildren();
  rows.forEach((r, i) => {
    const tr = h("tr");
    const inch = (v) => (params ? fmt(inchesFor(v, params), 1) : "-");
    tr.append(h("td", null, r.verify ? "Check" : String(i + 1)));
    const w = h("td", null, inch(r.final) + " ");
    w.append(h("span", "srow-small", "(" + nf(Math.abs(r.final)) + " pulses)"));
    const c = h("td", null, inch(r.follow) + " ");
    c.append(h("span", "srow-small", "(" + nf(r.follow) + " pulses)"));
    tr.append(w, c);
    tr.append(h("td", null, r.verify ? "Check wind" : r.valid === false ? "No: " + String(r.why_invalid || "not usable").replace(/\.$/, "") : "Yes"));
    tb.append(tr);
  });
}

function setVerdict(el, ok, extra) {
  el.textContent = (ok ? "OK - " : "PROBLEM - ") + (ok ? "the check wind came out right." : "the check wind was still off. Save only if you trust it, or leave without saving.") + (extra || "");
  el.className = "verdict " + (ok ? "verdict-ok" : "verdict-bad");
}

function renderStep2(s, a) {
  const pre = !flow.started;
  $("st2-pre").hidden = !pre;
  $("st2-run").hidden = pre;
  const turns = Math.round(AUTO_PULSES / ((s.params && s.params.cpr) || 2400));
  text("st2-plan", "The motor will turn the wheel about " + turns + " times and stop on its own. It will keep coasting a little after it stops. " +
    "Nothing else on the machine will work meanwhile. You can press Stop at any moment.");
  setDis($("st2-start"), flow.started || streamLost || !s.connected);
  setDis($("st2-practice"), flow.started);
  if (pre) {
    setFlowStatus("Nothing has moved yet.");
    return;
  }
  const phase = a ? String(a.phase || "") : "";
  const results = /^(ready_to_save|done)$/.test(phase);
  $("st2-results").hidden = !results;
  $("st2-bar").hidden = results;
  $("st2-good").hidden = results;
  if (a && a.of > 0) {
    const frac = Math.min(1, Math.max(0, (a.step || 0) / a.of));
    $("st2-fill").style.width = Math.round(frac * 100) + "%";
    $("st2-bar").setAttribute("aria-valuenow", String(Math.round(frac * 100)));
  }
  text("st2-good", a && a.needed ? "Good winds: " + (a.valid_runs || 0) + " of " + a.needed : "");
  applyGraphVisibility(results ? "follow" : "speed");
  $("st2-next").hidden = !results;
  setDis($("st2-next"), phase !== "ready_to_save");
  if (results) {
    const res = flow.res1 || (a && a.result) || {};
    if (changed("st2rows", [s.runs, s.params])) runRows(s.runs, s.params);
    setVerdict($("st2-verdict"), wordFor(res));
    setFlowStatus((a && a.message) || "Finished. Look at the result.");
  } else {
    setFlowStatus((a && a.message) || "Working...");
  }
}

// ---- step 3: optional length check

function renderStep3(s, a) {
  if (!a) return;
  flow.card.update(s, a);
  const decided = flow.card.decided();
  $("st3-next").hidden = !decided;
  if (decided) setFlowStatus("Length check finished. Go on to save.");
  else if (!flow.card.started()) setFlowStatus("This check is optional. Skip it or do it.");
  else setFlowStatus(a.message || "Working...");
}

// ---- step 4: save

function mergedResult(a) {
  const res = Object.assign({}, flow.res1 || (a && a.result) || {});
  const lcRes = a && a.length_check && a.length_check.result;
  const src = lcRes || (flow.card && flow.card.everRan() ? a.result : null);
  if (src && typeof src.wheel_um_after === "number") res.wheel_um_after = src.wheel_um_after;
  return res;
}

function renderStep4(s, a) {
  const saved = flow.saved || !!(a && a.result && a.result.saved);
  $("st4-sum").hidden = saved;
  $("st4-done").hidden = !saved;
  if (saved) {
    setFlowStatus("");
    return;
  }
  const res = mergedResult(a);
  const lines = summaryLines(res, s.params);
  if (changed("st4", [res])) {
    $("st4-lines").replaceChildren(...lines.map((t) => h("li", null, t)));
    setVerdict($("st4-verdict"), wordFor(res));
  }
  setDis($("st4-save"), !a || a.phase !== "ready_to_save");
  setFlowStatus("Look at what changed, then save it to the machine.");
}

// ---- driver

function adoptRunning(s) {
  const a = s.auto;
  if (flow.adopted || !a) return;
  flow.adopted = true;
  const phase = String(a.phase || "");
  if (!AUTO_BUSY.test(phase) && phase !== "ready_to_save") return;
  flow.started = true;
  flow.saw = true;
  flow.startAt = Date.now();
  flow.autoActive = AUTO_BUSY.test(phase);
  if (phase === "ready_to_save" && a.result && !a.length_check) flow.res1 = a.result;
  flow.step = a.length_check ? 3 : 2;
  syncFlow();
}

function renderFlow(s) {
  syncFlow();
  if (flow.mode === "practice") return;
  adoptRunning(s);
  const a = s.auto || null;
  if (flow.started && !flow.saved && !flow.err) {
    if (streamLost || !s.connected || !s.snapshot) {
      flowFail("Lost contact with the machine. If the motor is still running, switch it off at the machine.");
      return;
    }
    const elapsed = Date.now() - flow.startAt;
    if (!a) {
      if (elapsed > 4000) flowFail("The program did not report any progress.");
    } else {
      const phase = String(a.phase || "");
      const fail = AUTO_FAIL.test(phase);
      const fin = AUTO_DONE.test(phase);
      if (phase && !fail && !fin && phase !== "idle") flow.saw = true;
      const stale = (fail || fin) && !flow.saw && elapsed < 2500; // left over from an earlier test
      if (!stale) {
        if (fail) {
          flowFail(a.reason || a.message || "The test stopped.");
          return;
        }
        if (phase === "ready_to_save" && a.result && !flow.res1 && !(flow.card && flow.card.everRan())) flow.res1 = a.result;
        if (a.result && a.result.saved) flow.saved = true;
      }
    }
  }
  if (flow.err) return;
  if (flow.step === 1) updatePrep();
  else if (flow.step === 2) renderStep2(s, a);
  else if (flow.step === 3) renderStep3(s, a);
  else renderStep4(s, a);
}

// ---- practice (browser only; sends nothing to the machine)

function startPractice() {
  flow.mode = "practice";
  flow.step = 2;
  flow.more = false;
  syncFlow();
  $("st2-pre").hidden = true;
  $("st2-run").hidden = false;
  $("st2-results").hidden = true;
  $("st2-bar").hidden = false;
  $("st2-good").hidden = false;
  $("st2-next").hidden = true;
  $("st2-fill").style.width = "0%";
  applyGraphVisibility("speed");
  setFlowStatus("Practice wheel is turning. Nothing is really moving.");
  $("st2-good").textContent = "Good winds: 0 of 4 (example)";
  stepHeading().focus();
  const t0 = Date.now();
  const params = current && current.params;
  const fake = [0, 1, 2, 3].map((i) => ({ final: AUTO_PULSES, follow: [40, 41, 39, 90][i], valid: i !== 3, why_invalid: i === 3 ? "It slipped on the wheel." : null }));
  stopSim();
  flow.timer = setInterval(() => {
    const t = (Date.now() - t0) / 1000;
    const frac = Math.min(1, t / 4);
    $("st2-fill").style.width = Math.round(frac * 100) + "%";
    $("st2-bar").setAttribute("aria-valuenow", String(Math.round(frac * 100)));
    const fs = fakeSeries(Math.min(t, 5), AUTO_PULSES);
    const d = windowSeries(fs.ser, fs.marks);
    drawSpeed($("f-speed"), d);
    drawPosition($("f-pos"), d, params);
    $("f-badge").textContent = speedBadge(d.pts) + " (example)";
    if (t >= 5) {
      stopSim();
      $("st2-bar").hidden = true;
      $("st2-good").hidden = true;
      $("st2-results").hidden = false;
      runRows(fake.map((r) => Object.assign({}, r)), params || { cpr: 2400, wheel_um: 101638 });
      const note = h("tr");
      note.append(h("td", "muted", "Example rows"), h("td"), h("td"), h("td"));
      $("st2-rows").append(note);
      setVerdict($("st2-verdict"), true, " (example)");
      drawFollow($("f-follow"), fake, { spread_pct: 3 });
      applyGraphVisibility("follow");
      $("st2-next").hidden = false;
      $("st2-next").disabled = false;
      setFlowStatus("Practice finished. Look at the example result.");
    }
  }, 100);
}

function buildPracticeStep(n) {
  const box = $("st-p");
  box.replaceChildren();
  const hd = h("h2", "step-title", n === 3 ? "Check the length (optional)" : "Save", { tabindex: "-1" });
  box.append(hd);
  if (n === 3) {
    box.append(h("p", "lead", "After the main test you may check the length too. It catches slipping that the machine cannot see. You can also skip it."));
    const ol = h("ol", "steps");
    ol.append(h("li", null, "Draw a line across the paper at the table edge."), h("li", null, "Press Run. The machine winds about 36 inches."),
      h("li", null, "Measure from the table edge to the line, with a ruler or with sheets of paper."));
    box.append(ol, h("p", "verdict", "Example: the wheel reads 0.3% short. Not worth changing."));
    const row = h("div", "step-actions");
    const skip = h("button", "btn btn-big", "Skip", { type: "button" });
    const next = h("button", "btn btn-big", "Next", { type: "button" });
    skip.addEventListener("click", () => practiceTo(4));
    next.addEventListener("click", () => practiceTo(4));
    row.append(skip, next);
    box.append(row);
    setFlowStatus("Example only. Nothing is sent to the machine.");
  } else {
    box.append(h("p", "lead", "What changed (example):"));
    const ul = h("ul", "steps");
    ul.append(h("li", null, "Wheel size 4.000 in -> 4.002 in"), h("li", null, "Coast after stopping 0.2 in -> 0.1 in"));
    box.append(ul, h("p", "verdict verdict-ok", "OK - the check wind came out right. (example)"),
      h("p", "muted", "In the real test you press Save to the machine here."));
    const row = h("div", "step-actions");
    const real = h("button", "btn btn-main btn-big", "Do the real test", { type: "button" });
    const again = h("button", "btn btn-big", "Practice again", { type: "button" });
    const later = h("button", "linkbtn", "Not now", { type: "button" });
    real.addEventListener("click", () => {
      stopSim();
      flow.mode = "real";
      flow.started = false;
      setStep(2, true);
    });
    again.addEventListener("click", () => startPractice());
    later.addEventListener("click", () => {
      resetFlow();
      showTab("home");
    });
    row.append(real, again, later);
    box.append(row);
    setFlowStatus("Practice finished. Nothing was sent to the machine.");
  }
}

function practiceTo(n) {
  flow.step = n;
  buildPracticeStep(n);
  syncFlow();
  stepHeading().focus();
}

// ---- wiring

$("st1-next").addEventListener("click", () => setStep(2, true));
$("st2-practice").addEventListener("click", () => startPractice());
$("st2-start").addEventListener("click", () =>
  act($("st2-start"), async () => {
    const r = await backend.calibrateAuto(AUTO_PULSES);
    if (!r.ok) return flowFail("The machine did not start the test (" + (r.error || "no answer") + ").");
    flow.autoActive = true;
    flow.started = true;
    flow.startAt = Date.now();
    flow.saw = false;
    flow.res1 = null;
    flow.saved = false;
    setFlowStatus("Starting...");
    return undefined;
  }));
$("st2-next").addEventListener("click", () => {
  if (flow.mode === "practice") practiceTo(3);
  else setStep(3, true);
});
$("st3-next").addEventListener("click", () => setStep(4, true));
$("st4-save").addEventListener("click", () =>
  act($("st4-save"), async () => {
    const r = await backend.calibrateSave();
    if (!r.ok) return flowFail("The settings could not be saved (" + (r.error || "no answer") + ").");
    flow.saved = true;
    flow.autoActive = false;
    settingsDirty = false;
    markCalibrated();
    markTourDone();
    flow.firstRun = false;
    return undefined;
  }));
$("st4-home").addEventListener("click", () => {
  resetFlow();
  showTab("home");
});
$("st-err-retry").addEventListener("click", () =>
  act($("st-err-retry"), async () => {
    flow.err = null;
    flow.started = false;
    flow.res1 = null;
    flow.saved = false;
    mountCheckCard();
    setStep(2, true);
  }));
$("st-err-leave").addEventListener("click", () => {
  resetFlow();
  showTab("home");
});
$("flow-restart").addEventListener("click", () =>
  act($("flow-restart"), async () => {
    if (flow.mode === "real" && flow.started && !flow.saved) {
      const yes = await confirmDialog({
        title: "Start again?",
        lines: ["This stops the test and throws away results that are not saved."],
        yes: "Yes, start again",
      });
      if (!yes) return;
      await haltMachine(current);
      if (current && isDebug(current)) await qcmd("DEBUG", 0);
    }
    resetFlow();
    setStep(1, true);
  }));

function maybeFirstRun(s) {
  if (autoTried || tourDone()) return;
  const healthy = s.connected && !s.checking && !streamLost && failing(s).length === 0 && s.snapshot;
  if (!healthy || s.snapshot.state !== "IDLE" || autoBusy(s)) return;
  autoTried = true;
  if (location.hash && TABS.includes(location.hash.replace("#", ""))) return; // the user came to a specific tab
  flow.firstRun = true;
  showTab("calibrate");
}

// ----------------------------------------------------------------- render

function render(s) {
  current = s;
  if (streamLost) streamLost = false; // a message arrived: the stream is back
  renderBanner(s);
  renderProblem(s);
  const problem = !s.checking && !s.connected && failing(s).length > 0;
  const showApp = !problem && s.connected && !s.checking && !!s.snapshot;
  $("app").hidden = !showApp;
  renderStop(s);
  if (showApp) {
    renderWheelNotes(s.params);
    renderHome(s);
    renderSettings(s);
    renderAdvanced(s);
    renderLog(s.log);
    renderFlow(s);
  }
  if (s.error) showError(s.error);
  scheduleCharts(s);
  if (showApp) maybeFirstRun(s);
}

// ---------------------------------------------------------------- actions

async function stopMotor() {
  await backend.calibrateCancel();
  await qcmd("ABORT");
  const r = await qcmd("RELAY", 0);
  showError(r.ok ? "" : "Could not stop the motor: " + (r.error || "no answer") + ". Switch it off at the machine.");
}

async function enableTestMode() {
  const r = await cmd("DEBUG", 1);
  if (r.ok) await cmd("STREAM", 1);
  return r;
}

bind("stop-motor", stopMotor);
bind("banner-btn", async () => {
  const a = bannerAction;
  if (!a) return;
  if (a.id === "recheck") await shown(backend.doctorRun());
  else if (a.id === "fix") {
    const r = await shown(backend.doctorFix(a.fix));
    $("fix-msg").textContent = r.ok ? r.message : "";
  } else if (a.id === "reload") backend.reload();
  else if (a.id === "testoff") await cmd("DEBUG", 0);
});
bind("recheck", () => shown(backend.doctorRun()));
$("debug").addEventListener("change", (e) =>
  act(e.target, async () => {
    const on = e.target.checked;
    const r = on ? await enableTestMode() : await cmd("DEBUG", 0);
    if (!r.ok) e.target.checked = !on;
  }));
bind("relay", () => cmd("RELAY", current.snapshot.relay ? 0 : 1));
bind("zero", () => cmd("ZERO"));
bind("run", () => cmd("RUN", pulsesFor(Number($("run-dist").value), $("run-unit").value, current.params)));
async function saveSettings() {
  const r = await cmd("SAVE");
  if (r.ok) settingsDirty = false;
  return r;
}
bind("u-save", saveSettings);
bind("u-undo", async () => {
  const r = await cmd("LOAD");
  if (r.ok) settingsDirty = false;
});
bind("factory", async () => {
  const yes = await confirmDialog({
    title: "Reset to factory settings?",
    lines: ["This puts the wheel size, counts per turn, both lengths and the coast back to the factory values, and saves them to the machine."],
    yes: "Yes, reset everything",
  });
  if (!yes) return;
  const r = await cmd("DEFAULTS");
  if (r.ok) await saveSettings();
});
bind("cal-add", () =>
  shown(backend.calibrateRun(pulsesFor(Number($("cal-dist").value), $("cal-unit").value, current.params), Number($("cal-count").value))));
bind("cal-clear", () => shown(backend.calibrateClear()));
bind("apply", async () => {
  const sg = current.suggest;
  if (sg.coast !== null) await cmd("SET", "coast", sg.coast);
  if (sg.wheel_um !== null) await cmd("SET", "wheel_um", sg.wheel_um);
  settingsDirty = true;
});
for (const id of ["run-dist", "run-unit", "cal-dist", "cal-unit", "cal-count"]) {
  $(id).addEventListener("input", () => current && render(current));
}

addEventListener("pagehide", () => {
  if (flow.mode === "real" && flow.autoActive) backend.leaveStop();
});

// ----------------------------------------------------------------- start

buildPrep();
mountCheckCard();
syncFlow();
showTab(TABS.includes(location.hash.replace("#", "")) ? location.hash.replace("#", "") : "home", { replace: false });

backend.subscribe(
  (doc) => render(doc),
  () => {
    streamLost = true;
    if (current) render(current);
    else {
      $("banner").className = "banner banner-bad";
      text("banner-word", "PROBLEM");
      text("banner-title", "Lost contact with the program");
      text("banner-next", "Keep this page open. It reconnects by itself.");
    }
  },
);
