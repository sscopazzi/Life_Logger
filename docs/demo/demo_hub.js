// SPDX-FileCopyrightText: 2026 Aspect Wave LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/* Life Logger demo: a pretend box that runs in the page. It answers the dashboard's /api/... calls the same
   way the firmware does, keeps your changes in this browser (localStorage), and makes up climate history
   and GPS tracks around today's date. Nothing here talks to real hardware. */
(function () {
  'use strict';
  const KEY = 'logbook-demo-4';
  const SEED = /*__SEED__*/null;
  const DAY = 86400000;
  const pad = n => String(n).padStart(2, '0');
  function localISO(t) {
    const d = new Date(t), o = -d.getTimezoneOffset();
    return d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate()) + 'T' + pad(d.getHours()) + ':' + pad(d.getMinutes()) + ':' + pad(d.getSeconds()) +
      (o >= 0 ? '+' : '-') + pad(Math.floor(Math.abs(o) / 60)) + ':' + pad(Math.abs(o) % 60);
  }
  const localDate = t => localISO(t).slice(0, 10);
  const utcISO = t => new Date(t).toISOString().replace(/\.\d+Z$/, 'Z');
  const dayStart = d => new Date(d + 'T00:00:00').getTime();
  const addDay = (d, n) => localDate(dayStart(d) + n * DAY + 3 * 3600000);

  // ---------- state (kept in this browser) ----------
  // blank = what a brand-new box holds: no events, no documents, no files, no sensor history
  function freshState(blank) {
    // move the demo data so "today" in it is today: dates by whole days, timestamps so the newest is a few minutes ago
    const today = localDate(Date.now());
    const dOff = Math.round((Date.parse(today + 'T12:00:00Z') - Date.parse(SEED.anchor + 'T12:00:00Z')) / DAY);
    let maxTs = 0;
    for (const l of SEED.log) { const m = l.match(/"ts":"([^"]+)"/); if (m) maxTs = Math.max(maxTs, Date.parse(m[1])); }
    const msOff = Date.now() - 6 * 60000 - maxTs;
    const shift = s => s.replace(/(\d{4})-(\d\d)-(\d\d)(T\d\d:\d\d:\d\d(?:\.\d+)?(Z|[+-]\d\d:\d\d))?/g, (m, y, mo, d, tpart, z) => {
      if (tpart) { const t = Date.parse(m) + msOff; return z === 'Z' ? new Date(t).toISOString().replace(/\.\d+Z$/, m.includes('.') ? '.000Z' : 'Z') : localISO(t); }
      const x = new Date(Date.UTC(+y, +mo - 1, +d) + dOff * DAY);
      return x.getUTCFullYear() + '-' + pad(x.getUTCMonth() + 1) + '-' + pad(x.getUTCDate());
    });
    const docs = {};
    for (const [k, v] of Object.entries(SEED.docs)) docs[shift(k)] = shift(v);
    // month keys ("mo":"YYYY-MM") move by whole months, so rent records stay on their month
    const mOff = (+today.slice(0, 4) * 12 + +today.slice(5, 7)) - (+SEED.anchor.slice(0, 4) * 12 + +SEED.anchor.slice(5, 7));
    const shiftMo = s => s.replace(/"mo":"(\d{4})-(\d\d)"/g, (m, y, mo) => { const k = +y * 12 + (+mo - 1) + mOff; return '"mo":"' + Math.floor(k / 12) + '-' + pad(k % 12 + 1) + '"'; });
    return { v: 1, sv: SEED.ver, made: Date.now(), blank: !!blank, log: blank ? [] : SEED.log.map(l => shiftMo(shift(l))), docs: blank ? {} : docs, docVer: 1,
      att: blank ? {} : Object.assign({}, SEED.att || {}), toured: false,
      cfg: { apSsid: 'Life Logger', apPass: 'demo-hotspot', homeSsid: 'HomeWiFi', homePass: 'x', tz: 'demo', setupDone: true, contrast: 160, bnwasOn: false, bnwasMin: 30 } };
  }
  // ?factory in the address (for testing): a brand-new box out of its box: not set up, no password, the starting
  // hotspot password, the clock unset, nothing stored. Kept apart from the demo's own data.
  const FACTORY = /[?&]factory\b/.test(location.search), SKEY = FACTORY ? KEY + '.factory' : KEY;
  let ST;
  try { ST = JSON.parse(localStorage.getItem(SKEY) || 'null'); } catch (e) { ST = null; }
  if (!ST || ST.v !== 1 || ST.sv !== SEED.ver) {
    ST = freshState(FACTORY);   // a newer demo: start from its own examples
    if (FACTORY) Object.assign(ST.cfg, {apPass: 'lifelogger-setup', homeSsid: '', homePass: '', tz: 'UTC0', setupDone: false, pass: '', clock: null});
    if (FACTORY) ST.toured = true;   // no demo welcome on a factory box
  }
  const hubNow = () => FACTORY && ST.cfg.clock ? Date.now() + ST.cfg.clock.off : Date.now();   // a factory box keeps the time it was set to
  let saveErr = false;
  function save() {
    try { localStorage.setItem(SKEY, JSON.stringify(ST)); saveErr = false; return true; }
    catch (e) { saveErr = true; return false; }
  }
  save();

  // ---------- made-up climate history (same numbers every time for a given 5-minute slot) ----------
  const noise = t => { const x = Math.sin(t * 12.9898 / 300) * 43758.5453; return x - Math.floor(x); };
  function outside(t) {
    const d = new Date(t), hr = d.getHours() + d.getMinutes() / 60, day = t / DAY;
    const syn = 3 * Math.sin(day / 4.3) + 1.5 * Math.sin(day / 1.7 + 1);
    const tc = 11 + syn + 5.5 * Math.sin((hr - 9) / 24 * 2 * Math.PI) + (noise(t) - 0.5) * 0.4;
    const rh = Math.max(35, Math.min(99, 74 - 18 * Math.sin((hr - 9) / 24 * 2 * Math.PI) - syn * 2 + (noise(t + 7) - 0.5) * 3));
    return [tc, rh];
  }
  function inside(t) {
    const d = new Date(t), hr = d.getHours() + d.getMinutes() / 60;
    const heat = (hr >= 6.5 && hr < 9) || (hr >= 17 && hr < 22.5) ? 1.6 : 0;
    const tc = 18.6 + heat + 0.6 * Math.sin((hr - 15) / 24 * 2 * Math.PI) + 0.25 * Math.sin(t / 900000) + (noise(t + 3) - 0.5) * 0.15;
    const rh = 47 + 3 * Math.sin(hr / 4) + (noise(t + 11) - 0.5) * 1.5;
    return [tc, rh];
  }
  const boxTH = t => { const [c, h] = inside(t); return [c + 1.7 + (noise(t + 17) - 0.5) * 0.2, h - 6 + (noise(t + 19) - 0.5)]; };   // the box's own SHT4x
  const ENV_DAYS = 30;
  // a gap where the outside node's battery went flat, and one where the inside node was unplugged
  function envGap(node, t) {
    const now = Date.now();
    if (node === 'Outside') return t > now - 2 * DAY - 5 * 3600000 && t < now - 2 * DAY;
    return t > now - 9 * DAY - 14 * 3600000 && t < now - 9 * DAY - 6 * 3600000;
  }
  function envFile(date) {
    const t0 = dayStart(date), now = Date.now(), rows = ['utc,local,node,temp_c,rh_pct,vbat_v,soc_pct,rssi_dbm'];
    for (let t = Math.ceil(t0 / 300000) * 300000; t < t0 + DAY && t <= now; t += 300000) {
      if (!envGap('Outside', t)) { const [c, h] = outside(t); rows.push(utcISO(t) + ',' + localISO(t) + ',Outside,' + c.toFixed(2) + ',' + h.toFixed(1) + ',3.95,81.0,-71'); }
      if (!envGap('Inside', t)) { const [c, h] = inside(t); rows.push(utcISO(t) + ',' + localISO(t) + ',Inside,' + c.toFixed(2) + ',' + h.toFixed(1) + ',4.11,96.0,-48'); }
      if (!baroOff(t)) { const [c, h] = boxTH(t); rows.push(utcISO(t) + ',' + localISO(t) + ',Box,' + c.toFixed(2) + ',' + h.toFixed(1) + ',3.98,82.0,'); }
    }
    return rows.join('\n') + '\n';
  }
  const envDays = () => { if (ST.blank) return []; const out = [], t = Date.now(); for (let i = ENV_DAYS - 1; i >= 0; i--) out.push(localDate(t - i * DAY)); return out; };

  // ---------- made-up pressure: the box's BMP388, one saved value per 10 minutes (the average), with a stretch the box was off ----------
  const SLOT = 600000;
  function presAt(t) {
    const day = t / DAY, hr = new Date(t).getHours() + new Date(t).getMinutes() / 60;
    return 1013.5 + 8 * Math.sin(2 * Math.PI * day / 4.7) + 3 * Math.sin(2 * Math.PI * day / 1.9 + 1) + 0.8 * Math.cos(2 * Math.PI * (hr - 10) / 12) + (noise(t + 5) - 0.5) * 0.12;
  }
  const baroOff = t => { const now = Date.now(); return t > now - DAY - 9 * 3600000 && t < now - DAY - 6.5 * 3600000; };   // the box was switched off
  function presSlot(slot) {                                // a saved 10 minutes (NaN: none saved)
    const t = slot * SLOT; if (ST.blank || t + SLOT > Date.now() || t < Date.now() - 31 * DAY || baroOff(t)) return NaN;
    return Math.round(presAt(t + SLOT / 2) * 100) / 100;
  }
  const presNow = () => presAt(Math.floor(Date.now() / 60000) * 60000);
  function presWord(d) { const a = Math.abs(d); if (a < 0.1) return 'Steady'; const w = d > 0 ? 'Rising' : 'Falling'; return a <= 1.5 ? w + ' slowly' : a <= 3.5 ? w : a <= 6 ? w + ' quickly' : w + ' very rapidly'; }
  function pres3h() { const l = Math.floor(Date.now() / SLOT) - 1, a = presSlot(l), b = presSlot(l - 18); return isNaN(a) || isNaN(b) ? null : +(a - b).toFixed(2); }
  function baroFile(date) {
    const t0 = dayStart(date), rows = ['utc,hpa,box_c,n'];
    for (let t = t0; t < t0 + DAY; t += SLOT) { const h = presSlot(t / SLOT); if (!isNaN(h)) rows.push(utcISO(t) + ',' + h.toFixed(2) + ',26.4,10'); }
    return rows.join('\n') + '\n';
  }
  const baroDays = () => envDays();

  // ---------- made-up GPS tracks (a drive, a walk, a longer drive) ----------
  const HOME = [37.76925, -122.48620];
  const ROUTES = {
    drive: [[37.76925, -122.48620], [37.77140, -122.47720], [37.77280, -122.45470], [37.77520, -122.43260], [37.77950, -122.41880], [37.78640, -122.40480], [37.79540, -122.39420], [37.80250, -122.40210], [37.80640, -122.41640]],
    walk: [[37.76925, -122.48620], [37.76770, -122.49650], [37.76690, -122.50550], [37.76540, -122.50990], [37.76050, -122.51000], [37.75490, -122.50920]],
    long: [[37.76925, -122.48620], [37.78060, -122.47680], [37.79420, -122.47320], [37.80760, -122.47540], [37.82640, -122.47940], [37.83960, -122.48170], [37.85700, -122.48550], [37.86880, -122.50160]],
  };
  function track(date, route, startH, kn, gapFrom, gapTo) {
    const wp = ROUTES[route], segs = [];
    let tot = 0;
    for (let i = 1; i < wp.length; i++) { const d = dist(wp[i - 1], wp[i]); segs.push(d); tot += d; }
    const secs = tot / (kn * 0.5144), n = Math.max(2, Math.round(secs / 30)), t0 = dayStart(date) + startH * 3600000, pts = [];
    for (let k = 0; k <= n; k++) {
      if (k >= gapFrom && k < gapTo) continue;
      let s = tot * k / n, i = 0;
      while (i < segs.length - 1 && s > segs[i]) { s -= segs[i]; i++; }
      const f = Math.min(1, s / segs[i]), a = wp[i], b = wp[i + 1];
      pts.push({ t: t0 + k * 30000, lat: a[0] + (b[0] - a[0]) * f, lon: a[1] + (b[1] - a[1]) * f, seg: k === 0 || k === gapTo, kn });
    }
    return pts;
  }
  function dist(a, b) { const R = 6371000, r = Math.PI / 180, dl = (b[0] - a[0]) * r, dn = (b[1] - a[1]) * r;
    const x = Math.sin(dl / 2) ** 2 + Math.cos(a[0] * r) * Math.cos(b[0] * r) * Math.sin(dn / 2) ** 2; return 2 * R * Math.asin(Math.sqrt(x)); }
  function gpsTracks() {
    if (ST.blank) return {};
    const today = localDate(Date.now()), now = Date.now(), out = {};
    const put = (d, pts) => { pts = pts.filter(p => p.t <= now); if (pts.length) (out[d] = out[d] || []).push(...pts); };
    if (now - dayStart(today) > 9.5 * 3600000) put(today, track(today, 'drive', 8.25, 16, 14, 19));   // a stretch with no fix
    put(addDay(today, -1), track(addDay(today, -1), 'walk', 16.5, 2.2, -1, -1));
    put(addDay(today, -3), track(addDay(today, -3), 'long', 10, 24, -1, -1));
    put(addDay(today, -6), track(addDay(today, -6), 'drive', 13, 14, -1, -1));
    return out;
  }
  function gpsFile(pts) {
    return ['utc,lat,lon,alt_m,kn,sats,hdop,seg'].concat(pts.map(p => utcISO(p.t) + ',' + p.lat.toFixed(6) + ',' + p.lon.toFixed(6) + ',18.0,' + p.kn.toFixed(1) + ',9,0.9,' + (p.seg ? 1 : 0))).join('\n') + '\n';
  }

  // ---------- timers, find-my-box, BNWAS (same rules as the firmware) ----------
  const AL = { timers: [], next: 1, find: false, findAt: 0, bnStage: 0, bnT0: Date.now() };
  const timerRinging = () => AL.timers.some(k => k.ring);
  const alarmActive = () => AL.bnStage > 0 || AL.find || timerRinging();
  function alarmsTick() {
    const t = Date.now();
    for (const k of AL.timers.slice()) {
      if (!k.ring && t >= k.end) { k.ring = true; k.ringAt = t; }
      if (k.ring && t - k.ringAt > 600000) AL.timers.splice(AL.timers.indexOf(k), 1);
    }
    if (AL.find && t - AL.findAt > 120000) AL.find = false;
    const c = ST.cfg;
    if (!c.bnwasOn) AL.bnStage = 0;
    else if (AL.bnStage === 0 && t - AL.bnT0 >= c.bnwasMin * 60000) { AL.bnStage = 1; AL.bnT0 = t; }
    else if (AL.bnStage === 1 && t - AL.bnT0 >= 30000) { AL.bnStage = 2; AL.bnT0 = t; }
  }
  function silence(box) {
    AL.timers = AL.timers.filter(k => !k.ring); AL.find = false;
    if (box && ST.cfg.bnwasOn) { AL.bnStage = 0; AL.bnT0 = Date.now(); }
  }
  function bnLeft() {
    const t = Date.now(), c = ST.cfg; if (!c.bnwasOn) return 0;
    if (AL.bnStage === 0) return Math.max(0, Math.ceil((c.bnwasMin * 60000 - (t - AL.bnT0)) / 1000));
    if (AL.bnStage === 1) return Math.max(0, Math.ceil((30000 - (t - AL.bnT0)) / 1000));
    return 0;
  }
  function alarmsJson() {
    alarmsTick(); const t = Date.now();
    return { buzzer: true, alert: ['sound', 'vibrate', 'both'][ST.cfg.alert ?? 2], motor: true, timers: AL.timers.map(k => ({ id: k.id, name: k.name, secs: k.secs, left: Math.max(0, Math.ceil((k.end - t) / 1000)), ring: k.ring })),
      find: AL.find, findLeft: AL.find ? Math.max(0, Math.ceil((120000 - (t - AL.findAt)) / 1000)) : 0,
      bnwas: { on: ST.cfg.bnwasOn, min: ST.cfg.bnwasMin, stage: AL.bnStage, left: bnLeft() } };
  }

  // ---------- the box's own state (screen, knob, button) lives in the box panel; it reports here ----------
  const BOX = { screenOn: true, want: null, gpsMode: 'auto', gpsAsleep: false, fixEp: Date.now() - 47 * 60000, test: 0 };
  const boot = Date.now() - 2 * DAY - 3 * 3600000;

  function nodeNow(name) {
    const t = Math.floor(Date.now() / 300000) * 300000, [c, h] = name === 'Inside' ? inside(t) : outside(t);
    const age = Math.round((Date.now() % 60000) / 1000) + 3;
    return { name, mac: name === 'Inside' ? '7C:DF:A1:00:11:22' : '7C:DF:A1:00:33:44', tempC: +c.toFixed(2), rh: +h.toFixed(1), vbat: name === 'Inside' ? 4.11 : 3.95, soc: name === 'Inside' ? 96 : 81,
      rssi: name === 'Inside' ? -48 : -71, age, interval: 60, count: 1240, seq: 1240, sensorOK: true, live: true, backfilled: name === 'Outside' ? 288 : 0 };
  }
  function ptsToday() { const d = gpsTracks()[localDate(Date.now())]; return d ? d.length : 0; }
  const DEVS = {};   // the dashboards that have checked in lately, by the device name they send (for the box's Wi-Fi screen)
  const PINS0 = { back: 'f11', sel: 'f9', backLed: 'f18', selLed: 'f17', buzA: 'f5', buzB: 'f6', xb: [['', ''], ['', ''], ['', ''], ['', '']] };
  // indicator lights: the demo box has two, on A4 and A3 (a factory-new box has none chosen)
  const IND0 = [{ r: [['trip', 's'], ['job', 'b']], dim: true, slp: false }, { r: [['batt', 'f'], ['clk', 'b'], ['ap', 'p']], dim: true, slp: true }];
  const IND_K = ['job', 'jpause', 'trip', 'gpson', 'fix', 'nofix', 'batt', 'chg', 'sd', 'clk', 'ap', 'wifi', 'nowifi'];
  const hwPins = () => (ST.cfg.hw || {}).pins || Object.assign({}, PINS0, { ind: FACTORY ? ['', ''] : ['f14', 'f15'] });
  const indCfg = () => ST.cfg.ind || (FACTORY ? [] : IND0);
  function hwInfo() {   // what the stand-in box has fitted: the BMP388 inside it and an SHT4x (made-up)
    const H = ST.cfg.hw || {}, off = String(H.off || ''), pcf = H.pcf || 0;
    const parts = [{ k: 'bmp3', n: 'BMP388/390', w: 'press', a: 0x77 }, { k: 'sht4', n: 'SHT4x', w: 'th', a: 0x44 }].concat(pcf ? [{ k: 'pcf8574', n: 'PCF8574', w: 'io', a: pcf }] : [])
      .map(x => Object.assign(x, { on: !off.includes(x.k + '@' + x.a.toString(16).padStart(2, '0')) })).map(x => Object.assign(x, { used: x.on }));
    return { pins: hwPins(), feather: [[5, '5'], [6, '6'], [8, 'A5'], [9, '9'], [11, '11'], [12, '12'], [13, '13 (red LED)'], [14, 'A4'], [15, 'A3'], [17, 'A1'], [18, 'A0'], [38, 'RX'], [39, 'TX']],
      acts: ['home', 'clock', 'job', 'mark', 'trip', 'off', 'phone', 'beep', 'bnwas', 'alert', 'apon', 'setclk', 'gps'], pcf, pcfOK: !!pcf, aw: 0, awOK: false, bh: H.bh || 0,
      imuM: H.imuM || 0, imuF: !!H.imuF, imuW: H.imuW !== false, imuR: H.imuR !== false,
      scan: '0x10,0x36,0x37,0x3D,0x44,0x50,0x5A,0x68,0x77' + (pcf ? ',0x' + pcf.toString(16).toUpperCase() : ''), parts, down: [false, false, false, false, false, false] };
  }
  function status() {
    const t = hubNow(), logBytes = ST.log.reduce((a, l) => a + l.length + 1, 0);
    const pct = Math.max(12, 82 - Math.floor((t - ST.made) / 3600000) % 70);
    const hwCfg = ST.cfg.hw || {}, thOn = !String(hwCfg.off || '').includes('sht4@44');
    const bt = boxTH(Math.floor(Date.now() / 300000) * 300000);
    return { fw: '2.3', hw: { pr: 'BMP388/390', th: thOn ? 'SHT4x' : '', boxC: thOn && !ST.blank ? +bt[0].toFixed(2) : null, boxRH: thOn && !ST.blank ? +bt[1].toFixed(1) : null, lx: '', lux: null, imu: '', rot: 0, ind: (hwPins().ind || ['', '']).map((p, i) => p ? (BOX.ind ? BOX.ind[i] : -1) : -2), indCfg: indCfg() }, dev: { clkMan: !!ST.cfg.clkMan, apHomeOff: !!ST.cfg.apHomeOff, apForce: false, mdns: ST.cfg.mdns || 'lifelogger', gpsDet: ST.cfg.gpsDet ?? 1, tripAskMin: ST.cfg.tripAskMin || 60, envMin: ST.cfg.envMin || 5 }, findPhone: ST.findPhone || undefined, epoch: Math.floor(t / 1000), local: localISO(t), tz: ST.cfg.tz, setupDone: ST.cfg.setupDone !== false, passSet: !FACTORY || !!ST.cfg.pass, login: false, remembered: 1,
      timeValid: !FACTORY || !!ST.cfg.clock, timeSrc: FACTORY ? (ST.cfg.clock ? ST.cfg.clock.src : 'none') : ST.cfg.clkMan ? 'rtc' : 'ntp', rtcOK: true, store: 'flash', storeOK: true,
      flashOK: true, flashKB: Math.ceil(logBytes / 1024) + 8, flashTotalKB: 1408, sdOK: true, cardMB: 30436, eeOK: true, lastAlive: localISO(boot - 98 * 60000), resetReason: 'power-on',
      envFlashDays: 10, uiSrc: 'flash', docVer: ST.docVer, logSeq: ST.log.length, logBytes, logBad: 0, envLogS: (ST.cfg.envMin || 5) * 60,
      sta: { ssid: ST.cfg.homeSsid, up: true, ip: '192.168.1.42', rssi: -58 }, ap: { up: !ST.cfg.apHomeOff, ssid: ST.cfg.apSsid, ip: '192.168.4.1', clients: ST.cfg.apHomeOff ? 0 : 1 }, channel: 6, mdns: (ST.cfg.mdns || 'lifelogger') + '.local',
      devs: Object.entries(DEVS).filter(([, t0]) => t - t0 < 600000).sort((a, b) => b[1] - a[1]).map(([n, t0]) => ({ n, age: Math.floor((t - t0) / 1000) })),
      hubMac: '7C:DF:A1:9A:BC:DE', uptime: Math.floor((t - boot) / 1000), heap: 214000, nodes: ST.blank ? [] : [nodeNow('Inside'), nodeNow('Outside')],
      screen: { ok: true, on: BOX.screenOn, enc: true, encAddr: 0x37, offMin: ST.cfg.scrOffMin ?? 5, clHide: !!ST.cfg.clHide, btnLed: ST.cfg.btnLed !== false, bright: ST.cfg.contrast || 128 }, alarm: alarmsJson(), gpsBus: true,
      batt: { ok: true, pct, v: vFromPct(pct), rate: -6.2, left: +(pct / 6.2).toFixed(1), chg: false, usbSense: true, measuring: false, offV: 3.3 },
      baro: ST.blank ? { ok: true, hpa: +presNow().toFixed(2), d3: null, word: '', boxC: 26.4, saveS: 600 } : { ok: true, hpa: +presNow().toFixed(2), d3: pres3h(), word: pres3h() == null ? '' : presWord(pres3h()), boxC: 26.4, saveS: 600, lastEp: (Math.floor(t / SLOT) - 1) * 600 },
      lastFix: { lat: HOME[0], lon: HOME[1], ep: Math.floor(BOX.fixEp / 1000) },
      gps: BOX.gpsAsleep ? { mode: BOX.gpsMode, asleep: true, seen: false, fix: false, sats: 0, ptsToday: ptsToday(), store: 'sd' } : { mode: BOX.gpsMode, asleep: false, seen: true, fix: true, sats: 9, hdop: 0.9, kn: 0.1, lat: HOME[0], lon: HOME[1], fixAge: 1, ptsToday: ptsToday(), store: 'sd' } };
  }

  // the box's discharge curve ([volts, %], % = charge left before it switches itself off at 3.30 V): the demo's voltage from its %
  const BCURVE = [[4.15, 100.0], [4.13, 97.4], [4.11, 96.0], [4.09, 94.4], [4.07, 92.7], [4.05, 90.8], [4.03, 86.4], [4.01, 82.9], [3.99, 81.0], [3.97, 79.1], [3.95, 77.2], [3.93, 74.8], [3.91, 69.0], [3.89, 63.3], [3.87, 60.2], [3.85, 57.9], [3.83, 56.0], [3.81, 54.1], [3.79, 52.4], [3.77, 50.5], [3.75, 48.5], [3.73, 46.6], [3.71, 44.3], [3.69, 41.7], [3.67, 38.5], [3.65, 34.7], [3.63, 30.1], [3.61, 25.6], [3.59, 22.2], [3.57, 19.2], [3.55, 16.5], [3.53, 14.0], [3.51, 11.9], [3.49, 9.9], [3.47, 8.0], [3.45, 6.0], [3.43, 4.3], [3.41, 3.1], [3.39, 2.3], [3.37, 1.6], [3.35, 1.1], [3.33, 0.6], [3.3, 0.0]];
  function vFromPct(p) { for (let i = 1; i < BCURVE.length; i++) if (p >= BCURVE[i][1]) { const [v0, p0] = BCURVE[i], [v1, p1] = BCURVE[i - 1]; return +(v0 + (v1 - v0) * (p - p0) / ((p1 - p0) || 1)).toFixed(2); } return 3.3; }

  // ---------- requests ----------
  const J = (o, code) => new Response(JSON.stringify(o), { status: code || 200, headers: { 'Content-Type': 'application/json' } });
  const T = (s, code, type) => new Response(s, { status: code || 200, headers: { 'Content-Type': type || 'text/plain' } });
  const err = (code, m) => J({ ok: false, err: m }, code);
  const NOT_HERE = 'Not part of the demo: on a real box this installs new firmware.';
  async function formFile(body) {
    if (!(body instanceof FormData)) return null;
    for (const [, v] of body.entries()) if (v instanceof Blob) return v;
    return null;
  }
  const blobToDataURL = b => new Promise((ok, bad) => { const r = new FileReader(); r.onload = () => ok(r.result); r.onerror = bad; r.readAsDataURL(b); });
  function appendEvents(text) {
    const ts = localISO(Date.now()), first = ST.log.length + 1;
    for (let line of String(text).split('\n')) {
      line = line.trim(); if (line.length < 2 || line[0] !== '{' || line[line.length - 1] !== '}') continue;
      ST.log.push('{"seq":' + (ST.log.length + 1) + ',"ts":"' + ts + '","e":' + line + '}');
    }
    return { first, last: ST.log.length, ts };
  }

  async function hub(url, opt) {
    const u = new URL(url, 'http://box'), p = u.pathname, q = k => u.searchParams.get(k) || '', m = (opt.method || 'GET').toUpperCase(), body = opt.body;
    if (m === 'GET') {
      if (p === '/api/status') { if (q('dev')) DEVS[q('dev').slice(0, 24)] = Date.now(); return J(status()); }
      if (p === '/api/timers') return J(alarmsJson());
      if (p === '/api/log') return T(ST.log.join('\n') + (ST.log.length ? '\n' : ''), 200, 'application/x-ndjson');
      if (p === '/api/doc') { const n = q('n'); return n in ST.docs ? T(ST.docs[n], 200, 'application/json') : err(404, 'No such document'); }
      if (p === '/api/docs') { const pre = ({ ics: 'ics/', cx: 'cx/', nt: 'nt/', na: 'na/', nh: 'nh/', pl: 'pl/' })[q('d')] || 'cl/'; return J(Object.keys(ST.docs).filter(k => k.startsWith(pre)).sort()); }
      if (p === '/api/config') return J({ apSsid: ST.cfg.apSsid, homeSsid: ST.cfg.homeSsid, homeSet: !!ST.cfg.homePass, tz: ST.cfg.tz, setupDone: ST.cfg.setupDone !== false, passSet: !FACTORY || !!ST.cfg.pass, apDefault: ST.cfg.apPass === 'lifelogger-setup', apHomeOff: !!ST.cfg.apHomeOff, mdns: ST.cfg.mdns || 'lifelogger' });
      if (p === '/api/scan') return J([{ ssid: ST.cfg.homeSsid, rssi: -52 }, { ssid: 'Neighbors-5G', rssi: -77 }, { ssid: 'CoffeeShop', rssi: -84 }]);
      if (p === '/api/envdays') return J(envDays());
      if (p === '/api/barodays') return J(ST.blank ? [] : baroDays());
      if (p === '/api/baro') { const d = q('date'); return !ST.blank && baroDays().includes(d) ? T(baroFile(d), 200, 'text/csv') : err(404, 'No file for that date'); }
      if (p === '/api/hw') return J(hwInfo());
      if (p === '/api/env') { const d = q('date'); return envDays().includes(d) ? T(envFile(d), 200, 'text/csv') : err(404, 'No file for that date'); }
      // a day's file: the box's own track, then any points brought in from Google Timeline (kept apart, so they can be taken off)
      if (p === '/api/gpsdays') { const g = gpsTracks(), X = ST.gpsx || {}; return J([...new Set(Object.keys(g).concat(Object.keys(X)))].sort().map(d => [d, g[d] ? gpsFile(g[d]).length : 0, (X[d] || '').length])); }
      if (p === '/api/gps') { const g = gpsTracks()[q('date')], x = (ST.gpsx || {})[q('date')]; return g || x ? T((g ? gpsFile(g) : 'utc,lat,lon,alt_m,kn,sats,hdop,seg\n') + (x || ''), 200, 'text/csv') : err(404, 'No track for that date'); }
      // a real box online fetches the list from Google; the demo has no box, and a web page can't ask Google itself
      if (p === '/api/gmaps-list') return err(503, 'the demo has no box to fetch it with. On a real box joined to Wi-Fi with internet, this brings the list in');
      if (p.startsWith('/att/')) { const a = ST.att[p]; if (!a) return err(404, 'Attachment not found'); return fetch(a); }
      return err(404, 'Not found');
    }
    // POST
    if (p === '/api/log') {
      const r = appendEvents(body);
      if (r.last + 1 === r.first) return err(400, 'No valid events');
      if (!save()) { ST.log.length = r.first - 1; return err(507, 'This browser\u2019s storage for the demo is full. Reset the demo to start again.'); }
      return J({ ok: true, first: r.first, last: r.last, ts: r.ts });
    }
    if (p === '/api/doc') {
      const n = q('n'); if (!(n === 'checklist' || n === 'oled' || n === 'plan' || /^pl\/[A-Za-z0-9-]{1,24}$/.test(n) || n.startsWith('cl/') || /^cx\/[0-9-]{7,10}$/.test(n) || /^n[tah]\/\d{1,4}$/.test(n)) || !String(body).startsWith('{')) return err(400, 'Bad document name');
      if (new TextEncoder().encode(String(body)).length > 98304) return err(400, 'Body must be a JSON object under 96 kB');   // same limit as the box
      ST.docs[n] = String(body); ST.docVer++; save(); return J({ ok: true, ver: ST.docVer });
    }
    if (p === '/api/doc-up') { const f = await formFile(body); if (!f) return err(400, 'No file'); ST.docs[q('n')] = await f.text(); ST.docVer++; save(); return J({ ok: true, size: f.size, ver: ST.docVer }); }
    if (p === '/api/gps-add') {
      const d = q('date'), rows = String(body || '').split('\n').filter(l => /^\d{4}-\d\d-\d\dT[^,]+,-?[\d.]+,-?[\d.]+,/.test(l));
      if (!/^\d{4}-\d\d-\d\d$/.test(d) || !rows.length) return err(400, 'date=YYYY-MM-DD and CSV rows, please');
      ST.gpsx = ST.gpsx || {}; const old = ST.gpsx[d] || ''; ST.gpsx[d] = old + rows.join('\n') + '\n';
      if (!save()) { ST.gpsx[d] = old; if (!old) delete ST.gpsx[d]; return err(507, 'This browser\u2019s storage for the demo is full: import fewer days (a real box keeps years of them)'); }
      return J({ ok: true, rows: rows.length });
    }
    if (p === '/api/gps-del') { if (ST.gpsx) delete ST.gpsx[q('date')]; save(); return J({ ok: true }); }
    if (p === '/api/doc-del') { delete ST.docs[q('n')]; ST.docVer++; save(); return J({ ok: true, removed: true }); }
    if (p === '/api/restore') {
      const f = await formFile(body); if (!f) return err(400, 'No file');
      const lines = (await f.text()).split('\n').filter(l => l.trim());
      const old = ST.log; ST.log = lines;
      if (!save()) { ST.log = old; return err(507, 'Too big for the demo\u2019s storage'); }
      return J({ ok: true, events: lines.length });
    }
    // the demo stands in for a box reached over its own hotspot: no login, and any box password is accepted
    if (p === '/api/password' || p === '/api/forget-devices' || p === '/api/logout') return J({ ok: true });
    if (p === '/api/config') {
      let d = {}; try { d = JSON.parse(body); } catch (e) {}
      const old = Object.assign({}, ST.cfg);
      if (FACTORY) {   // as the box: the box password once there is one; first setup needs a password and your own hotspot password
        if (ST.cfg.pass && q('pw') !== ST.cfg.pass) return err(403, 'Wrong box password');
        if (d.newPass != null && (d.newPass.length < 8 || d.newPass.length > 64)) return err(400, 'Box password: 8 to 64 characters');
        if (d.setupDone && !ST.cfg.pass && !d.newPass) return err(400, 'Choose a box password');
        if (d.setupDone && ST.cfg.setupDone === false && (d.apPass || ST.cfg.apPass) === 'lifelogger-setup') return err(400, 'Choose your own hotspot password (the starting one is public)');
        if (d.newPass) ST.cfg.pass = d.newPass;
        if (typeof d.setupDone === 'boolean') ST.cfg.setupDone = d.setupDone;
        if ('homePass' in d) ST.cfg.homePass = d.homePass;
      }
      for (const k of ['apSsid', 'homeSsid', 'tz']) if (k in d) ST.cfg[k] = d[k];
      if (d.apPass) { if (d.apPass.length < 8) return err(400, 'Hotspot password: 8 to 63 characters'); ST.cfg.apPass = d.apPass; }
      if ('mdns' in d) { const m = String(d.mdns || '').toLowerCase().replace(/\.local$/, ''); if (!/^[a-z0-9]([a-z0-9-]{0,30}[a-z0-9])?$/.test(m)) return err(400, 'Network name: 1 to 32 of a-z, 0-9 and -, not starting or ending with -'); ST.cfg.mdns = m; }
      if (typeof d.apHomeOff === 'boolean') ST.cfg.apHomeOff = d.apHomeOff;
      save();
      return J({ ok: true, apChanged: old.apSsid !== ST.cfg.apSsid || old.apPass !== ST.cfg.apPass || !!old.apHomeOff !== !!ST.cfg.apHomeOff, homeChanged: old.homeSsid !== ST.cfg.homeSsid });
    }
    if (p === '/api/att') {
      const f = await formFile(body); if (!f) return err(400, 'No file');
      if (f.size > 600 * 1024) return err(413, 'The demo keeps files up to 600 kB (a real box takes much bigger ones)');
      const path = '/att/' + (q('id') || 'x').replace(/[^A-Za-z0-9._-]/g, '_').slice(0, 12) + '_' + (f.name || 'file').replace(/[^A-Za-z0-9._-]/g, '_').slice(0, 40);
      ST.att[path] = await blobToDataURL(f);
      if (!save()) { delete ST.att[path]; return err(507, 'This browser\u2019s storage for the demo is full'); }
      return J({ ok: true, path, size: f.size, where: 'sd' });
    }
    if (p === '/api/att-del') { delete ST.att[q('f')]; save(); return J({ ok: true, removed: true }); }
    if (p === '/api/hw-scan' || p === '/api/hw-test') { if (p === '/api/hw-test') { BOX.ltest = Date.now(); return J({ ok: true }); } return J(hwInfo()); }
    if (p === '/api/hw') {
      let d = {}; try { d = JSON.parse(body); } catch (e) {}
      const H = ST.cfg.hw = ST.cfg.hw || {};
      if (d.pins) {
        const all = [d.pins.back, d.pins.sel, d.pins.backLed, d.pins.selLed, d.pins.buzA, d.pins.buzB, ...(d.pins.xb || []).map(x => x[0]), ...(d.pins.ind || [])].filter(Boolean);
        if (!d.pins.back || !d.pins.sel) return err(400, 'BACK and SELECT need a pin each');
        if (all.some((x, i) => all.indexOf(x) !== i)) return err(400, 'A pin is used twice');
        if ([d.pins.buzA, d.pins.buzB].some(x => x && x[0] !== 'f')) return err(400, 'The buzzer needs Feather pins');
        H.pins = d.pins;
      }
      if (d.add) Object.assign(H, d.add);
      save(); return J({ ok: true, restart: true });
    }
    if (p === '/api/time' && FACTORY) {   // the factory box takes the time it's given, as the box does
      const L = q('local'), e = L ? Date.parse(L.length === 16 ? L + ':00' : L) : +q('epoch') * 1000;
      if (!(e > Date.parse('2025-01-01T00:00:00Z'))) return err(400, L ? 'That date or time doesn\u2019t exist' : 'epoch looks wrong');
      ST.cfg.clock = {off: e - Date.now(), src: L ? 'manual' : 'laptop'}; save();
      return J({ ok: true, local: localISO(e), auto: !ST.cfg.clkMan });
    }
    if (p === '/api/time') return q('local') ? err(409, 'the demo runs on this browser\u2019s own clock, so it can\u2019t be set here (on a box, this sets it)') : J({ ok: false, kept: 'ntp' });
    if (p === '/api/ui-reset') return J({ ok: true });
    if (p === '/update' || p === '/update-ui') return T(NOT_HERE, 403);
    if (p === '/api/timers') {
      let d = {}; try { d = JSON.parse(body || '{}'); } catch (e) { return err(400, 'Bad request'); }
      const t = Date.now();
      if (d.op === 'add') {
        const secs = d.secs | 0;
        if (secs < 1 || secs > 86400) return err(400, 'Timer must be 1 second to 24 hours');
        if (AL.timers.length >= 8) return err(400, 'Up to 8 timers at once');
        AL.timers.push({ id: AL.next++, name: String(d.name || '').trim().slice(0, 24) || 'Timer', secs, end: t + secs * 1000, ring: false });
      } else if (d.op === 'del') AL.timers = AL.timers.filter(k => k.id !== d.id);
      else if (d.op === 'stop') silence(false);
      else if (d.op === 'find') { AL.find = !!d.on; AL.findAt = t; }
      else if (d.op === 'bnwas') {
        const mn = d.min ?? ST.cfg.bnwasMin, on = d.on ?? ST.cfg.bnwasOn;
        if (!(mn >= 1 && mn <= 240)) return err(400, 'Interval must be 1 to 240 minutes');
        if (on !== ST.cfg.bnwasOn || mn !== ST.cfg.bnwasMin) { ST.cfg.bnwasOn = on; ST.cfg.bnwasMin = mn; AL.bnStage = 0; AL.bnT0 = t; save(); }
      } else if (d.op === 'alert') {
        const a = ['sound', 'vibrate', 'both'].indexOf(d.mode); if (a < 0) return err(400, 'mode: sound, vibrate or both');
        ST.cfg.alert = a; save();
      } else if (d.op === 'test') BOX.test = t;
      else if (d.op === 'screen') {
        if (d.offMin != null) { if (!(d.offMin >= 0 && d.offMin <= 240)) return err(400, 'Sleep time must be 0 (never) to 240 minutes'); ST.cfg.scrOffMin = d.offMin | 0; save(); }
        if (typeof d.on === 'boolean') BOX.want = d.on;
        if (typeof d.clHide === 'boolean') { ST.cfg.clHide = d.clHide; ST.docVer++; save(); }
        if (typeof d.btnLed === 'boolean') { ST.cfg.btnLed = d.btnLed; save(); }
      } else if (d.op === 'dev') {
        if (d.envMin != null && ![1, 2, 5, 10, 15].includes(d.envMin)) return err(400, 'Climate log: 1, 2, 5, 10 or 15 minutes');
        if (typeof d.clkMan === 'boolean') ST.cfg.clkMan = d.clkMan;
        if (d.gpsDet != null) ST.cfg.gpsDet = Math.max(0, Math.min(2, d.gpsDet | 0));
        if (d.tripAskMin != null) ST.cfg.tripAskMin = Math.max(10, Math.min(480, d.tripAskMin | 0));
        if (d.envMin != null) ST.cfg.envMin = d.envMin;
        if (Array.isArray(d.ind)) {
          if (d.ind.length > 2) return err(400, 'Two indicator lights at most');
          for (const L of d.ind) {
            const r = L.r || []; if (r.length > 3) return err(400, 'Three states per light at most');
            if (r.some(x => !IND_K.includes(x[0]))) return err(400, 'Unknown state');
            if (r.some(x => !'sbfp'.includes(x[1]) || String(x[1]).length !== 1)) return err(400, 'Unknown pattern');
            if (r.some((x, j) => r.findIndex(y => y[0] === x[0]) !== j)) return err(400, 'A state is chosen twice for one light');
          }
          ST.cfg.ind = d.ind;
        }
        save();
      } else return err(400, 'Unknown op');
      return J(alarmsJson());
    }
    return err(404, 'Not found');
  }
  const isBox = url => typeof url === 'string' && /^\/(api\/|att\/|update)/.test(url);

  // fetch: the dashboard's calls go to the pretend box; everything else (map tiles) to the internet
  const realFetch = window.fetch.bind(window);
  window.fetch = function (input, opt) {
    const url = typeof input === 'string' ? input : input && input.url;
    if (isBox(url)) return new Promise(r => setTimeout(r, 25)).then(() => hub(url, opt || {}));
    return realFetch(input, opt);
  };
  // uploads use XMLHttpRequest (for the progress bar)
  const RealXHR = window.XMLHttpRequest;
  function DemoXHR() {
    const x = { upload: {}, readyState: 0, status: 0, responseText: '', _u: '', _m: 'GET', _real: null };
    x.open = (m, u) => { x._m = m; x._u = u; if (!isBox(u)) { x._real = new RealXHR(); x._real.open(m, u); } };
    x.setRequestHeader = (k, v) => { if (x._real) x._real.setRequestHeader(k, v); };
    x.abort = () => { if (x._real) x._real.abort(); };
    x.send = body => {
      if (x._real) { const r = x._real; r.onload = () => { x.status = r.status; x.responseText = r.responseText; x.readyState = 4; x.onload && x.onload(); }; r.onerror = () => x.onerror && x.onerror(); r.send(body); return; }
      let k = 0; const total = 100;
      const step = () => { k += 25; if (x.upload.onprogress) x.upload.onprogress({ lengthComputable: true, loaded: Math.min(k, total), total }); if (k < total) setTimeout(step, 60); else go(); };
      const go = async () => { const r = await hub(x._u, { method: x._m, body }); x.status = r.status; x.responseText = await r.text(); x.readyState = 4; x.onload && x.onload(); x.onreadystatechange && x.onreadystatechange(); };
      setTimeout(step, 30);
    };
    return x;
  }
  window.XMLHttpRequest = DemoXHR;
  // attachments: open from this browser's copy
  document.addEventListener('click', e => {
    const a = e.target.closest && e.target.closest('a[href^="/att/"]'); if (!a) return;
    e.preventDefault();
    const d = ST.att[a.getAttribute('href')]; if (!d) { alert('That file is not in this browser any more.'); return; }
    fetch(d).then(r => r.blob()).then(b => window.open(URL.createObjectURL(b), '_blank'));
  }, true);
  new MutationObserver(() => {
    document.querySelectorAll('img[src^="/att/"]').forEach(im => { const d = ST.att[im.getAttribute('src')]; if (d) im.src = d; else im.removeAttribute('src'); });
  }).observe(document.documentElement, { childList: true, subtree: true });

  // the box panel uses these
  window.LBDEMO = {
    FACTORY,
    // a brand-new box (?factory): set it up from scratch; its data stays apart from the demo's
    newBox() { try { localStorage.removeItem(KEY + '.factory'); } catch (e) {} location.href = location.pathname + '?factory'; },
    toDemo() { location.href = location.pathname; },
    hwPins, indCfg, ST, AL, BOX, save, presSlot, presNow, presWord, pres3h, alarmsTick, alarmActive, timerRinging, silence, bnLeft, appendEvents, localDate, localISO, nodeNow, gpsTracks, dist, HOME, status,
    reset() {
      try { localStorage.removeItem(KEY); Object.keys(localStorage).filter(k => k.startsWith('hwo.')).forEach(k => localStorage.removeItem(k)); } catch (e) {}
      location.reload();
    },
    // empty box: keeps the box's settings, drops every made-up record (and anything the visitor changed)
    clearAll() {
      const b = freshState(true); b.toured = true; b.cfg = Object.assign({}, ST.cfg);
      try { Object.keys(localStorage).filter(k => k.startsWith('hwo.')).forEach(k => localStorage.removeItem(k)); localStorage.setItem(KEY, JSON.stringify(b)); } catch (e) {}
      location.reload();
    },
    saveErr: () => saveErr,
  };
})();
