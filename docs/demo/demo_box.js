// SPDX-FileCopyrightText: 2026 Aspect Wave LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/* Life Logger demo: the physical box, simulated. Same screens, font and controls as firmware 2.3:
   knob = rotary encoder (one row per click); its push is ACTION (a press runs a shortcut, a hold opens the quick
   menu); SELECT (a press opens, ticks or starts what is highlighted; a hold turns the screen off); BACK (back;
   hold it and turn the knob for brightness). Any press stops an alarm and restarts BNWAS, the screen sleeps when
   left alone and the buttons glow dim while it does. Alerts play through this browser's speaker, and a
   vibration shakes the lid. SUN & MOON is worked out here the same way as on the box (astro.h). */
(function () {
  'use strict';
  const D = window.LBDEMO; if (!D) return;
  const FONT = Uint8Array.from(atob('/*__FONT__*/'), c => c.charCodeAt(0));
  /*__CLK__*/
  const W = 128, ROWS = 13, ON = 15, DIM = 5, OFF = 0, OFF_MS = 1500, CLICK_DEG = 15;   // 24 clicks a turn
  const px = new Uint8Array(W * W);
  // ---------- drawing (mirrors drawScreen() in the firmware) ----------
  const clear = () => px.fill(0);
  const rect = (x, y, w, h, c) => { for (let j = Math.max(0, y); j < Math.min(W, y + h); j++) for (let i = Math.max(0, x); i < Math.min(W, x + w); i++) px[j * W + i] = c; };
  const frame = (x, y, w, h, c) => { rect(x, y, w, 1, c); rect(x, y + h - 1, w, 1, c); rect(x, y, 1, h, c); rect(x + w - 1, y, 1, h, c); };
  function text(x, y, s, c, size) {
    size = size || 1;
    for (const ch of s) {
      const o = (ch.charCodeAt(0) & 255) * 5;
      for (let col = 0; col < 5; col++) { const bits = FONT[o + col]; for (let r = 0; r < 8; r++) if (bits >> r & 1) rect(x + col * size, y + r * size, size, size, c); }
      x += 6 * size;
    }
  }
  const ascii = s => String(s).replace(/[^\x20-\x7e]/g, '?');
  const fit = (s, n) => (n = n || 21, s.length <= n ? s : s.slice(0, n - 1) + '~');
  const MON = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
  const DOW = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
  const shortDate = d => MON[+d.slice(5, 7) - 1] + ' ' + (+d.slice(8, 10));
  const dayNum = d => Math.round(Date.UTC(+d.slice(0, 4), +d.slice(5, 7) - 1, +d.slice(8, 10)) / 86400000);
  function head(name) {
    const d = new Date(), p2 = v => String(v).padStart(2, '0'), h = d.getHours();
    const tm = oledDoc().h12 ? ((h % 12) || 12) + ':' + p2(d.getMinutes()) + ':' + p2(d.getSeconds()) + (h < 12 ? 'a' : 'p') : p2(h) + ':' + p2(d.getMinutes()) + ':' + p2(d.getSeconds());
    name = fit(name, 21 - tm.length - 1);
    const st = D.status(), wb = tm + ' ' + st.batt.pct + '%', r = wb.length <= 21 - name.length - 1 ? wb : tm;
    rect(0, 0, 128, 9, ON); text(1, 1, name, OFF);
    text(127 - 6 * r.length, 1, r, OFF);
  }
  let yOff = 0;   // LIFE LOGGER pushes the rows after a dividing line down a little
  function row(i, s, inv, c) {
    const y = 11 + i * 9 + yOff;
    if (inv) { rect(0, y - 1, 128, 9, ON); text(1, y, fit(s), OFF); } else text(1, y, fit(s), c == null ? ON : c);
  }
  function rowLR(i, l, r, c, inv) { if (c == null) c = ON; const room = 21 - r.length - 1; row(i, fit(l, Math.max(room, 1)), inv, c); text(127 - 6 * r.length, 11 + i * 9 + yOff, r, inv ? OFF : c); }
  // CLOCK: segment digits like a Casio watch (gray-edged glyphs from tools/clock_glyphs.py, the same bytes the firmware draws)
  function glyph(set, ch, x, y) {
    const G = CLK[set], i = G.c.indexOf(ch); if (i < 0) return;
    const n = G.w * G.h, o = i * n;
    for (let j = 0; j < G.h; j++) for (let k = 0; k < G.w; k++) { const v = G.px[o + j * G.w + k], X = x + k, Y = y + j; if (v && X >= 0 && X < W && Y >= 0 && Y < W && v > px[Y * W + X]) px[Y * W + X] = v; }
  }
  function drawClock() {
    const d = new Date(), p2 = v => String(v).padStart(2, '0'), H = d.getHours(), h12 = oledDoc().h12, st = D.status();
    frame(2, 4, 51, 27, DIM);                                                                     // the day, in a box
    DOW[d.getDay()].toUpperCase().split('').forEach((c, i) => glyph('day', c, 8 + i * 15, 10));
    if (st.batt && st.batt.pct != null) { const t = st.batt.pct + '%'; text(126 - 6 * t.length, 5, t, ON); }   // the battery, top right
    if (st.batt && st.batt.v != null) { const t = st.batt.v.toFixed(2) + 'V'; text(126 - 6 * t.length, 17, t, DIM); }
    rect(60, 34, 67, 1, DIM);
    const y0 = 42, hh = h12 ? String((H % 12) || 12).padStart(2, ' ') : p2(H), hm = hh + p2(d.getMinutes());
    let x = 4;
    for (let i = 0; i < 4; i++) {
      glyph('big', hm[i], x, y0); x += 20;
      if (i === 1) { rect(x, y0 + 9, 3, 3, ON); rect(x - 1, y0 + 22, 3, 3, ON); x += 6; }
    }
    x += 2;
    if (h12) text(x + 3, y0 + 2, H < 12 ? 'A' : 'P', ON);                                         // A or P above the seconds
    for (const c of p2(d.getSeconds())) { glyph('sec', c, x, y0 + 13); x += 12; }
    rect(2, 84, 124, 1, DIM);
    const ds = String(d.getFullYear()) + ' ' + p2(d.getMonth() + 1) + '-' + p2(d.getDate());   // 2026 11-21
    x = 6;
    for (const c of ds) { if (c === ' ') { x += 7; continue; } glyph('date', c, x, 92); x += 12; }
  }
  // lines and a filled triangle, for the compass
  function line(x0, y0, x1, y1, c) {
    x0 = Math.round(x0); y0 = Math.round(y0); x1 = Math.round(x1); y1 = Math.round(y1);
    const dx = Math.abs(x1 - x0), dy = -Math.abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1; let e = dx + dy;
    for (;;) { if (x0 >= 0 && x0 < W && y0 >= 0 && y0 < W) px[y0 * W + x0] = c; if (x0 === x1 && y0 === y1) break; const e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; } }
  }
  function tri(a, b, c, col) {
    const x0 = Math.floor(Math.min(a[0], b[0], c[0])), x1 = Math.ceil(Math.max(a[0], b[0], c[0])), y0 = Math.floor(Math.min(a[1], b[1], c[1])), y1 = Math.ceil(Math.max(a[1], b[1], c[1]));
    const ar = (p, q, r) => (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]), A = ar(a, b, c); if (!A) return;
    for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) { const p = [x + 0.5, y + 0.5], w0 = ar(b, c, p) / A, w1 = ar(c, a, p) / A, w2 = ar(a, b, p) / A; if (w0 >= 0 && w1 >= 0 && w2 >= 0 && x >= 0 && x < W && y >= 0 && y < W) px[y * W + x] = col; }
  }
  // a compass rose, true north up, with an arrow along the course over ground
  function rose(cx, cy, r, cog) {
    for (let a = 0; a < 360; a += 2) { const t = a * Math.PI / 180; const x = Math.round(cx + r * Math.sin(t)), y = Math.round(cy - r * Math.cos(t)); if (x >= 0 && x < W && y >= 0 && y < W) px[y * W + x] = DIM; }
    for (let a = 0; a < 360; a += 30) { const t = a * Math.PI / 180, k = a % 90 ? 3 : 6; line(cx + (r - k) * Math.sin(t), cy - (r - k) * Math.cos(t), cx + r * Math.sin(t), cy - r * Math.cos(t), a % 90 ? DIM : ON); }
    text(cx - 2, cy - r + 8, 'N', ON); text(cx + r - 14, cy - 3, 'E', DIM); text(cx - 2, cy + r - 15, 'S', DIM); text(cx - r + 10, cy - 3, 'W', DIM);
    const t = cog * Math.PI / 180, sn = Math.sin(t), cs = Math.cos(t), L = r - 9, tip = [cx + L * sn, cy - L * cs], base = [cx + (L - 9) * sn, cy - (L - 9) * cs];
    line(cx - L * 0.45 * sn, cy + L * 0.45 * cs, base[0], base[1], ON); line(cx - L * 0.45 * sn + cs, cy + L * 0.45 * cs + sn, base[0] + cs, base[1] + sn, ON);
    tri(tip, [base[0] + 4.5 * cs, base[1] + 4.5 * sn], [base[0] - 4.5 * cs, base[1] - 4.5 * sn], ON);
    rect(cx - 1, cy - 1, 3, 3, ON);
  }
  // word wrap to the screen's 21 characters
  function hyph(wd, room) {   // how many characters of wd go before the '-' (0 = don't split here)
    const max = Math.min(room - 1, wd.length - 2);
    if (max < 2) return 0;
    const h = wd.lastIndexOf('-', max);                    // already hyphenated: break after its own hyphen
    if (h >= 1) return -(h + 1);
    if (!/^[A-Za-z]+[.,;:!?'")]*$/.test(wd)) return 0;      // numbers, codes and links aren't hyphenated
    const v = c => /[aeiouyAEIOUY]/.test(c), core = wd.replace(/[^A-Za-z]+$/, '');
    for (let k = Math.min(max, core.length - 2); k >= 2; k--) {
      const a = core[k - 1], b = core[k];
      if (/^(th|ch|sh|ph|wh|gh|ck|ng|qu)$/i.test(a + b)) continue;
      if (!v(core.slice(0, k)) || !/[aeiouy]/i.test(core.slice(k))) continue;   // each part keeps a vowel
      if ((v(a) && !v(b) && k + 1 < core.length && v(core[k + 1])) || (!v(a) && !v(b))) return k;   // vo-cal, con-sonant
    }
    return 0;
  }
  function wrapW(s, w, cont) {
    const m = /^( *)(.*)$/.exec(s), out = []; let cur = m[1];
    const sp = () => cur.trim() ? 1 : 0;
    for (let wd of m[2].split(/ +/).filter(Boolean)) {
      for (;;) {
        if (cur.length + sp() + wd.length <= w) { cur += (sp() ? ' ' : '') + wd; break; }
        const room = w - cur.length - sp();
        let k = room >= 3 ? hyph(wd, room) : 0;
        if (k < 0) { cur += (sp() ? ' ' : '') + wd.slice(0, -k); wd = wd.slice(-k); }
        else if (k) { cur += (sp() ? ' ' : '') + wd.slice(0, k) + '-'; wd = wd.slice(k); }
        else if (wd.length > w - cont.length && room >= 2) {   // too long for any line: cut it at the edge
          const hy = /^[A-Za-z]+[.,;:!?'")]*$/.test(wd) && /[A-Za-z]/.test(wd[room - 2]) && /[A-Za-z]/.test(wd[room - 1]), r = hy ? room - 1 : room;
          cur += (sp() ? ' ' : '') + wd.slice(0, r) + (hy ? '-' : ''); wd = wd.slice(r);
        }
        out.push(cur); cur = cont;
      }
    }
    out.push(cur);
    return out.map(x => x.replace(/ +$/, ''));
  }
  function wrap(t, w) {   // long words split with a hyphen at the line end, as the dashboard does for notes
    w = w || 21; const out = [];
    for (const para of String(t || '').split('\n')) out.push(...wrapW(para.replace(/\s+/g, ' ').trim(), w, ''));
    while (out.length && !out[out.length - 1]) out.pop();
    return out;
  }
  // the highlight moves down the screen; the list only scrolls once it reaches the bottom (or top) row
  const listTop = {};
  // a highlighted line too long for the screen slides sideways; the box, time or step number stays put
  let mqT0 = Date.now(), mqOn = false;
  const fixLen = (s, mode) => { if (mode === 1) { const k = s.indexOf(' '); return k > 0 && k < 9 ? k + 1 : 0; } if (mode === 2) { let k = 0; while (s[k] === ' ') k++; return Math.min(k + 4, 12); } return 0; };
  function rowSlide(i, s, fix) {
    const y = 11 + i * 9, over = (s.length - 21) * 6; let off = 0;
    if (over > 0) { mqOn = true; const t = Date.now() - mqT0, run = over * 30; if (t >= 1500 + run + 1200) mqT0 = Date.now(); else if (t > 1500) off = Math.min(Math.floor((t - 1500) / 30), over); }
    rect(0, y - 1, 128, 9, ON);
    text(1 + 6 * fix - off, y, s.slice(fix), OFF);
    if (fix) { rect(0, y - 1, 1 + 6 * fix, 9, ON); text(1, y, s.slice(0, fix), OFF); }
  }
  function drawList(L, sel, showSel, mode) {
    let top = listTop[B.scr] || 0;
    if (showSel) { if (sel < top) top = sel; if (sel > top + ROWS - 1) top = sel - ROWS + 1; }
    top = Math.max(0, Math.min(top, Math.max(0, L.length - ROWS))); listTop[B.scr] = top;
    for (let i = 0; i < ROWS && top + i < L.length; i++) { if (showSel && top + i === sel) rowSlide(i, L[top + i], fixLen(L[top + i], mode || 0)); else row(i, L[top + i]); }
  }
  function alarmBox(title, l1, l2, inv) {
    const bg = inv ? ON : OFF, fg = inv ? OFF : ON;
    rect(0, 28, 128, 74, bg); frame(0, 28, 128, 74, ON); frame(2, 30, 124, 70, ON);
    text(64 - 6 * title.length, 40, title, fg, 2);
    const a = fit(l1, 18), b = fit(l2, 18);
    text(64 - 3 * a.length, 64, a, fg); text(64 - 3 * b.length, 80, b, fg);
  }
  const clockLeft = s => s >= 3600 ? Math.floor(s / 3600) + ':' + String(Math.floor(s / 60) % 60).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0') : Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');

  // ---------- data the screens show ----------
  function clRows() {
    let doc; try { doc = JSON.parse(D.ST.docs.checklist || '{}'); } catch (e) { doc = {}; }
    const items = Array.isArray(doc.items) ? doc.items : [], out = [];
    const ids = new Set(items.map(x => x.id));
    const hide = !!D.ST.cfg.clHide;   // hide ticked items (and what's under them), off unless chosen
    const walk = (pid, depth) => { for (const it of items) { const par = it.parent && ids.has(it.parent) ? it.parent : ''; if (par !== pid) continue; if (hide && it.done) continue; out.push({ id: it.id, text: ascii(it.text || ''), depth, done: !!it.done }); if (depth < 6) walk(it.id, depth + 1); } };
    walk('', 0);
    out.total = items.length; out.doneN = items.filter(x => x.done).length;
    return out;
  }
  function oledDoc() { try { return JSON.parse(D.ST.docs.oled || '{}'); } catch (e) { return {}; } }
  const todayItems = () => (oledDoc().days || {})[D.localDate(Date.now())] || [];
  function todayRows() { return todayItems().map(ev => (ev[0] || 'all  ') + ' ' + ascii(ev[2] || '')); }
  // an item opened from TODAY: when, what, where, and its notes
  function todayDetail(ev) {
    const L = [ev[0] ? ev[0].trim() + (ev[1] ? ' - ' + ev[1].trim() : '') : (ev[5] ? ascii(ev[5]) : 'All day')];
    if (Array.isArray(ev[6])) L.push(...ev[6].map(l => fit(ascii(l))));   // wrapped by the dashboard, as the box gets it
    else L.push(fit(ascii(ev[2] || '')));
    return L;
  }
  function trip() {
    const pts = D.gpsTracks()[D.localDate(Date.now())] || [];
    let m = 0, s = 0;
    for (let i = 1; i < pts.length; i++) { const a = pts[i - 1], b = pts[i]; if (!b.seg && b.t - a.t <= 400000) { m += D.dist([a.lat, a.lon], [b.lat, b.lon]); s += (b.t - a.t) / 1000; } }
    return { m, s, n: pts.length };
  }
  const fmt = () => Object.assign({spd: 'mph', dist: 'mi', pos: 'dd', temp: 'F', pres: 'mmHg'}, oledDoc().fmt || {});
  function tempLine(name, lbl) {
    const n = D.nodeNow(name); if (!n || n.tempC == null || isNaN(n.tempC)) return lbl + '--';
    return lbl + (fmt().temp === 'C' ? n.tempC.toFixed(1) + 'C ' : (n.tempC * 9 / 5 + 32).toFixed(1) + 'F ') + Math.round(n.rh) + '%';
  }
  const t0 = Date.now() - 3 * 86400000 - 4.2 * 3600000;   // "running for"
  function lbLine(k) {   // one line of LIFE LOGGER: [text, color]
    const d = new Date(), st = D.status(), f = fmt();
    if (k === 'date') return [DOW[d.getDay()] + ' ' + MON[d.getMonth()] + ' ' + String(d.getDate()).padStart(2, '0') + ' ' + d.getFullYear()];
    if (k === 'net') return ['WiFi home -58dBm'];
    if (k === 'ip') return ['IP 192.168.1.42'];
    if (k === 'gps') return [gpsAsleep() ? (G.mode === 2 ? 'GPS off (by hand)' : 'GPS asleep (home)') : 'GPS fix 9 sat'];
    if (k === 'sd') return ['SD ok  ' + trip().n + ' pts'];
    if (k === 'batt') return ['Battery ' + st.batt.pct + '% ' + st.batt.v.toFixed(2) + 'V'];
    if (k === 'volt') return ['Battery ' + st.batt.v.toFixed(2) + ' V'];
    if (k === 'left') { const h = st.batt.pct / -st.batt.rate; return ['Time left ~' + (h < 10 ? h.toFixed(1) : Math.round(h)) + ' h']; }
    if (k === 'chip') return ['Box ' + (f.temp === 'C' ? '26.4C' : '79.5F')];
    if (k === 'pres') { const h = D.presNow(), d = D.pres3h(), u = f.pres || 'mmHg', k2 = u === 'hPa' ? 1 : u === 'inHg' ? 0.0295300 : 0.750062; return ['Baro ' + (h * k2).toFixed(u === 'inHg' ? 2 : 1) + ' ' + u + (d == null ? '' : ' ' + (d >= 0 ? '+' : '') + (d * k2).toFixed(1))]; }
    if (k === 'sun') return [sunLine()];
    if (k === 'up') { const u = Math.floor((Date.now() - t0) / 1000); return ['Up ' + Math.floor(u / 86400) + 'd ' + String(Math.floor(u / 3600) % 24).padStart(2, '0') + ':' + String(Math.floor(u / 60) % 60).padStart(2, '0')]; }
    if (k.startsWith('n:')) { if (D.ST.blank) return ['No sensors paired', DIM]; const [n, l] = k.slice(2).split('|'); return [tempLine(n, l || '')]; }
    return [''];
  }
  let spotsToday = 0;
  function coord(v, lat) {
    const h = lat ? (v >= 0 ? 'N' : 'S') : (v >= 0 ? 'E' : 'W'), f = fmt().pos; v = Math.abs(v);
    const dg = Math.floor(v), m = (v - dg) * 60, deg = String.fromCharCode(248);
    if (f === 'dm') return dg + deg + ' ' + m.toFixed(3).padStart(6, '0') + "'" + h;
    if (f === 'dms') return dg + deg + ' ' + String(Math.floor(m)).padStart(2, '0') + "'" + ((m % 1) * 60).toFixed(1).padStart(4, '0') + '"' + h;
    return v.toFixed(5) + ' ' + h;
  }
  let driving = false;
  // GPS: Auto sleeps on the home Wi-Fi (here: while the box isn't moving), on elsewhere; the quick menu sets it by hand until a restart
  const G = {mode: 0, was: null};
  const gpsAsleep = () => !WT.on && (G.mode === 2 || (G.mode === 0 && !driving));
  function gpsSync() {
    const a = gpsAsleep(); D.BOX.gpsAsleep = a; D.BOX.gpsMode = ['auto', 'on', 'off'][G.mode];
    if (a && G.was === false) D.BOX.fixEp = Date.now();   // the last fix before it slept
    if (G.was !== null && a !== G.was) { D.appendEvents(JSON.stringify(Object.assign({op: a ? 'gps.sleep' : 'gps.wake', t: D.localISO(Date.now())}, a ? {why: G.mode === 2 ? 'hand' : 'home'} : {}))); D.save(); }
    G.was = a;
  }
  const motion = () => { if (!driving) return {kn: 0.1, cog: null}; const t = Date.now() / 1000; return {kn: 12 + 2 * Math.sin(t / 20), cog: (95 + 50 * Math.sin(t / 45) + 360) % 360}; };
  function mapLine(k) {
    const f = fmt(), t = trip();
    if (k === 'cog') { const m = motion(); return [m.cog == null ? 'COG --- (still)' : 'COG ' + String(Math.round(m.cog) % 360).padStart(3, '0') + String.fromCharCode(248) + 'T', m.cog == null ? DIM : ON]; }
    if (k === 'spd') { const kn = motion().kn, v = f.spd === 'kn' ? kn : f.spd === 'kmh' ? kn * 1.852 : kn * 1.15078; return ['Speed ' + v.toFixed(1) + ' ' + (f.spd === 'kmh' ? 'km/h' : f.spd)]; }
    if (k === 'dist') { const div = f.dist === 'km' ? 1000 : f.dist === 'nm' ? 1852 : 1609.344, v = t.m / div; return ['Dist  ' + v.toFixed(v < 10 ? 2 : 1) + ' ' + f.dist]; }
    if (k === 'fix') return ['Fix   ' + Math.floor(t.s / 3600) + 'h ' + Math.floor(t.s / 60) % 60 + 'm today'];
    if (k === 'pts') return ['Points ' + t.n + ' today'];
    if (k === 'spots') return ['Spots saved ' + spotsToday, DIM];
    if (gpsAsleep() && ['lat', 'lon', 'alt', 'sats'].includes(k)) return [k === 'lat' ? (G.mode === 2 ? 'GPS off (by hand)' : 'GPS asleep (home)') : ''];
    if (k === 'lat') return [coord(D.HOME[0], true)];
    if (k === 'lon') return [coord(D.HOME[1], false)];
    if (k === 'alt') return [f.dist === 'mi' ? 'Alt 49 ft' : 'Alt 15 m'];
    if (k === 'sats') return ['Sats 9'];
    return [''];
  }

  // ---------- SUN & MOON: the same sums as the box's astro.h ----------
  const AS = (() => {
    const D2R = Math.PI / 180, R2D = 180 / Math.PI, rev = x => ((x % 360) + 360) % 360, sind = x => Math.sin(x * D2R), cosd = x => Math.cos(x * D2R), jd = t => t / 86400 + 2440587.5;
    const eq = (lon, lat, eps, p) => { const x = cosd(lat) * cosd(lon), y = cosd(lat) * sind(lon), z = sind(lat), ye = y * cosd(eps) - z * sind(eps), ze = y * sind(eps) + z * cosd(eps); p.ra = rev(Math.atan2(ye, x) * R2D); p.dec = Math.atan2(ze, Math.sqrt(x * x + ye * ye)) * R2D; return p; };
    function sunPos(t) { const n = jd(t) - 2451545, L = rev(280.460 + 0.9856474 * n), g = rev(357.528 + 0.9856003 * n); const p = {lon: rev(L + 1.915 * sind(g) + 0.020 * sind(2 * g)), lat: 0, hp: 0}; return eq(p.lon, 0, 23.439 - 0.0000004 * n, p); }
    function moonPos(t) {
      const d = jd(t) - 2451543.5, N = rev(125.1228 - 0.0529538083 * d), i = 5.1454, w = rev(318.0634 + 0.1643573223 * d), a = 60.2666, e = 0.0549, M = rev(115.3654 + 13.0649929509 * d);
      let E = M + R2D * e * sind(M) * (1 + e * cosd(M)); for (let k = 0; k < 5; k++) E = E - (E - R2D * e * sind(E) - M) / (1 - e * cosd(E));
      const xv = a * (cosd(E) - e), yv = a * Math.sqrt(1 - e * e) * sind(E), v = Math.atan2(yv, xv) * R2D; let r = Math.sqrt(xv * xv + yv * yv);
      const xh = r * (cosd(N) * cosd(v + w) - sind(N) * sind(v + w) * cosd(i)), yh = r * (sind(N) * cosd(v + w) + cosd(N) * sind(v + w) * cosd(i)), zh = r * sind(v + w) * sind(i);
      let lon = Math.atan2(yh, xh) * R2D, lat = Math.atan2(zh, Math.sqrt(xh * xh + yh * yh)) * R2D;
      const Ms = rev(356.0470 + 0.9856002585 * d), ws = rev(282.9404 + 4.70935e-5 * d), Ls = Ms + ws, Lm = M + w + N, D = Lm - Ls, F = Lm - N;
      lon += -1.274 * sind(M - 2 * D) + 0.658 * sind(2 * D) - 0.186 * sind(Ms) - 0.059 * sind(2 * M - 2 * D) - 0.057 * sind(M - 2 * D + Ms) + 0.053 * sind(M + 2 * D) + 0.046 * sind(2 * D - Ms) + 0.041 * sind(M - Ms)
        - 0.035 * sind(D) - 0.031 * sind(M + Ms) - 0.015 * sind(2 * F - 2 * D) + 0.011 * sind(M - 4 * D);
      lat += -0.173 * sind(F - 2 * D) - 0.055 * sind(M - F - 2 * D) - 0.046 * sind(M + F - 2 * D) + 0.033 * sind(F + 2 * D) + 0.017 * sind(2 * M + F);
      r += -0.58 * cosd(M - 2 * D) - 0.46 * cosd(2 * D);
      const p = {lon: rev(lon), lat, hp: Math.asin(1 / r) * R2D}; return eq(p.lon, p.lat, 23.4393 - 3.563e-7 * d, p);
    }
    const alt = (p, la, lo, t) => { const g = rev(280.46061837 + 360.98564736629 * (jd(t) - 2451545)); return Math.asin(sind(la) * sind(p.dec) + cosd(la) * cosd(p.dec) * cosd(g + lo - p.ra)) * R2D; };
    const sunAlt = (la, lo, t) => alt(sunPos(t), la, lo, t);
    const moonRel = (la, lo, t) => { const p = moonPos(t); return alt(p, la, lo, t) - (0.7275 * p.hp - 0.5667); };
    function cross(f, t0, t1, dir, STEP) {
      STEP = STEP || 600; const s = t1 >= t0 ? STEP : -STEP; let a = f(t0);
      for (let t = t0; s > 0 ? t < t1 : t > t1; t += s) {
        const u = s > 0 ? Math.min(t + s, t1) : Math.max(t + s, t1), b = f(u);
        let lo = s > 0 ? t : u, hi = s > 0 ? u : t; const flo = s > 0 ? a : b; let fhi = s > 0 ? b : a;
        if ((dir > 0 && flo < 0 && fhi >= 0) || (dir < 0 && flo >= 0 && fhi < 0)) {
          while (hi - lo > 4) { const m = lo + Math.floor((hi - lo) / 2), fm = f(m); if ((fm >= 0) === (fhi >= 0)) { hi = m; fhi = fm; } else lo = m; }
          return lo + Math.floor((hi - lo) / 2);
        }
        a = b;
      }
      return 0;
    }
    function sunDay(la, lo, t0, t1) {
      const A = []; for (let t = t0; t <= t1 + 599 && A.length < 152; t += 600) A.push(sunAlt(la, lo, Math.min(t, t1)));
      const find = (h, dir) => { for (let k = 0; k + 1 < A.length; k++) { const up = A[k] - h < 0 && A[k + 1] - h >= 0, dn = A[k] - h >= 0 && A[k + 1] - h < 0; if (!(dir > 0 ? up : dn)) continue; const l = t0 + k * 600; return cross(t => sunAlt(la, lo, t) - h, l, Math.min(l + 600, t1), dir, 600); } return 0; };
      const o = {rise: find(-0.833, 1), set: find(-0.833, -1), cDawn: find(-6, 1), cDusk: find(-6, -1), nDawn: find(-12, 1), nDusk: find(-12, -1), noon: 0, noonAlt: NaN, all: 0};
      let n = t0 + Math.floor((t1 - t0) / 2);
      for (let k = 0; k < 4; k++) { const p = sunPos(n), g = rev(280.46061837 + 360.98564736629 * (jd(n) - 2451545)), ha = rev(g + lo - p.ra + 180) - 180; n -= Math.round(ha / 360.98564736629 * 86400); }
      if (n < t0) n += 86164; else if (n >= t1) n -= 86164;
      if (n >= t0 && n < t1) { o.noon = n; o.noonAlt = sunAlt(la, lo, n); }
      if (!o.rise && !o.set) o.all = sunAlt(la, lo, t0 + (t1 - t0) / 2) > -0.833 ? 1 : -1;
      return o;
    }
    const elong = t => rev(moonPos(t).lon - sunPos(t).lon);
    const nextElong = (t, tg) => { let x = t + Math.round(rev(tg - elong(t)) / 12.19 * 86400); for (let k = 0; k < 4; k++) x += Math.round((rev(tg - elong(x) + 180) - 180) / 12.19 * 86400); return x; };
    function moonNow(la, lo, now) {
      const f = t => moonRel(la, lo, t), up = f(now) >= 0, W = 30 * 3600, o = {up};
      if (up) { o.prev = cross(f, now, now - W, 1, 1200); o.next = cross(f, now, now + W, -1, 1200); } else { o.prev = cross(f, now, now - W, -1, 1200); o.next = cross(f, now, now + W, 1, 1200); }
      const m = moonPos(now), s = sunPos(now); o.frac = (1 - cosd(m.lat) * cosd(m.lon - s.lon)) / 2; o.elong = rev(m.lon - s.lon);
      o.full = nextElong(now, 180); o.nu = nextElong(now, 0); return o;
    }
    const phaseName = e => e < 12 || e >= 348 ? 'New moon' : e < 78 ? 'Waxing crescent' : e < 102 ? 'First quarter' : e < 168 ? 'Waxing gibbous' : e < 192 ? 'Full moon' : e < 258 ? 'Waning gibbous' : e < 282 ? 'Last quarter' : 'Waning crescent';
    return {sunDay, moonNow, phaseName};
  })();
  const sunC = {day: '', d: null, tm: null}, moonC = {at: 0, m: null};
  const where = () => { const asleep = gpsAsleep(); return {la: D.HOME[0], lo: D.HOME[1], src: asleep ? 'Fix ' + hm(Math.floor(D.BOX.fixEp / 1000)) : 'GPS fix'}; };
  function hm(t) { if (!t) return '--:--'; const d = new Date(t * 1000), h = d.getHours(), p2 = v => String(v).padStart(2, '0'); return oledDoc().h12 ? ((h % 12) || 12) + ':' + p2(d.getMinutes()) + (h < 12 ? 'a' : 'p') : p2(h) + ':' + p2(d.getMinutes()); }
  const dayAt = (t, plus) => { const d = new Date(t * 1000); d.setHours(0, 0, 0, 0); d.setDate(d.getDate() + plus); return Math.floor(d.getTime() / 1000); };
  const whenDay = t => { const k = Math.round((dayAt(t, 0) - dayAt(Math.floor(Date.now() / 1000), 0)) / 86400); return k === 1 ? ' tmrw' : k === -1 ? ' yday' : ''; };
  function sunCalc() {
    const w = where(), now = Math.floor(Date.now() / 1000), dd = D.localDate(Date.now());
    if (sunC.day !== dd) { const a = dayAt(now, 0), b = dayAt(now, 1), c = dayAt(now, 2); sunC.d = AS.sunDay(w.la, w.lo, a, b); sunC.tm = AS.sunDay(w.la, w.lo, b, c); sunC.day = dd; }
    if (!moonC.at || now - moonC.at >= 60 || (moonC.m.next && now >= moonC.m.next)) { moonC.m = AS.moonNow(w.la, w.lo, now); moonC.at = now; }
    return w;
  }
  function sunLine() {
    sunCalc(); const now = Date.now() / 1000, d = sunC.d;
    if (d.rise && now < d.rise) return 'Sunrise ' + hm(d.rise); if (d.set && now < d.set) return 'Sunset ' + hm(d.set);
    if (sunC.tm.rise) return 'Sunrise ' + hm(sunC.tm.rise) + ' tmrw'; return d.all > 0 ? 'Sun up all day' : 'Sun down all day';
  }
  function drawSun() {
    head('SUN & MOON');
    const w = sunCalc(), now = Math.floor(Date.now() / 1000), dt = new Date(), d = sunC.d;
    rowLR(0, DOW[dt.getDay()] + ' ' + MON[dt.getMonth()] + ' ' + String(dt.getDate()).padStart(2, '0'), w.src, DIM);
    if (d.all) { row(1, d.all > 0 ? 'Sun up all day' : 'Sun down all day'); rowLR(3, 'Daylight', d.all > 0 ? '24h 00m' : '0h 00m'); }
    else { rowLR(1, 'Sunrise', hm(d.rise)); rowLR(2, 'Sunset', hm(d.set));
      if (d.rise && d.set && d.set > d.rise) { const m = Math.floor((d.set - d.rise + 30) / 60); rowLR(3, 'Daylight', Math.floor(m / 60) + 'h ' + String(m % 60).padStart(2, '0') + 'm'); } else rowLR(3, 'Daylight', '--'); }
    rowLR(4, 'Civil', hm(d.cDawn) + ' ' + hm(d.cDusk), DIM); rowLR(5, 'Nautical', hm(d.nDawn) + ' ' + hm(d.nDusk), DIM);
    if (d.noon) rowLR(6, 'Noon', hm(d.noon) + '  ' + Math.round(d.noonAlt) + String.fromCharCode(248) + ' up', DIM);
    rect(0, 72, 128, 1, DIM);
    const m = moonC.m, wax = m.elong < 180, f = m.frac, flip = w.la < 0, cx = 14, cy = 95, R = 12;
    for (let y = -R; y <= R; y++) for (let x = -R; x <= R; x++) {
      if (x * x + y * y > R * R + R * 0.6) continue;
      const ww = Math.sqrt(Math.max(0, 1 - (y * y) / (R * R))), xt = (1 - 2 * f) * ww, xx = (flip ? -x : x) / R;
      px[(cy + y) * W + cx + x] = (wax ? xx >= xt : xx <= -xt) ? 14 : 2;
    }
    const L = [AS.phaseName(m.elong), Math.round(f * 100) + '% lit'];
    if (m.up) L.push(m.prev ? 'Rose ' + hm(m.prev) + whenDay(m.prev) : 'Up', m.next ? 'Sets ' + hm(m.next) + whenDay(m.next) : 'Sets --');
    else L.push(m.prev ? 'Set ' + hm(m.prev) + whenDay(m.prev) : 'Down', m.next ? 'Rises ' + hm(m.next) + whenDay(m.next) : 'Rises --');
    const s = Math.max(0, (wax ? m.full : m.nu) - now);
    L.push((wax ? 'Full in ' : 'New in ') + (s < 86400 ? Math.floor((s + 1800) / 3600) + 'h' : Math.floor((s + 43200) / 86400) + 'd'));
    L.forEach((t, i) => text(32, 75 + i * 9, fit(t, 16), i < 2 ? ON : DIM));
  }
  // ---------- BAROGRAPH: the saved 10-minute values, 24 h / 48 h / 7 days by the knob; gaps stay empty ----------
  const presU = () => fmt().pres || 'mmHg', presIn = h => presU() === 'hPa' ? h : presU() === 'inHg' ? h * 0.0295300 : h * 0.750062;
  function drawBaro() {
    const span = Math.max(0, Math.min(2, B.sel)), u = presU(), dec = u === 'inHg' ? 2 : 1;
    head(['BARO 24h', 'BARO 48h', 'BARO 7d'][span]);
    const hpa = D.presNow(), v = presIn(hpa).toFixed(dec);
    text(1, 13, v, ON, 2); text(1 + 12 * v.length + 4, 20, u, DIM);
    const d3 = D.pres3h();
    if (d3 == null) rowLR(3, '3 h change', '--', DIM);
    else { const dv = presIn(hpa) - presIn(hpa - d3), w = D.presWord(d3), c = (dv >= 0 ? '+' : '') + dv.toFixed(u === 'inHg' ? 2 : 1) + '/3h';
      if (w.length + c.length < 21) rowLR(3, w, c, ON); else { rowLR(2, '', c, ON); row(3, w); } }
    const gx = 26, gy = 48, gw = 101, gh = 62, last = Math.floor(Date.now() / 600000), n = [24, 48, 168][span] * 6, first = last - n;
    const cmin = new Array(gw).fill(NaN), cmax = new Array(gw).fill(NaN), cend = new Array(gw).fill(NaN); let lo = 1e9, hi = -1e9;
    for (let k = 0; k < n; k++) { const h = D.presSlot(first + 1 + k); if (isNaN(h)) continue; const y = presIn(h), c = Math.floor(k * gw / n);
      if (isNaN(cmin[c]) || y < cmin[c]) cmin[c] = y; if (isNaN(cmax[c]) || y > cmax[c]) cmax[c] = y; cend[c] = y; lo = Math.min(lo, y); hi = Math.max(hi, y); }
    const div = span === 2 ? 7 : 4;
    for (let k = 0; k <= div; k++) { const x = gx + Math.floor((gw - 1) * k / div); for (let y = gy; y < gy + gh; y += 2) px[y * W + x] = 3; }
    for (let x = gx; x < gx + gw; x += 2) { px[gy * W + x] = 3; px[(gy + gh - 1) * W + x] = 3; }
    const L0 = ['-24h', '-48h', '-7d'][span], L1 = ['-12h', '-24h', ''][span];
    text(gx - 3 * L0.length, gy + gh + 3, L0, DIM); text(gx + Math.floor(gw / 2) - 3 * L1.length, gy + gh + 3, L1, DIM); text(127 - 18, gy + gh + 3, 'now', DIM);
    if (hi < lo) { text(gx + 14, gy + 26, 'No values yet', DIM); return; }
    const minSpan = u === 'hPa' ? 10 : u === 'inHg' ? 0.3 : 8, step = u === 'inHg' ? 0.1 : 1;
    let Lo = Math.floor(lo / step) * step, Hi = Math.ceil(hi / step) * step;
    if (Hi - Lo < minSpan) { const mid = (lo + hi) / 2; Lo = Math.floor((mid - minSpan / 2) / step) * step; Hi = Lo + minSpan; }
    text(0, gy, Hi.toFixed(u === 'inHg' ? 1 : 0), DIM); text(0, gy + gh - 8, Lo.toFixed(u === 'inHg' ? 1 : 0), DIM);
    const Y = v => gy + gh - 1 - Math.round((v - Lo) / (Hi - Lo) * (gh - 1));
    for (let c = 0; c < gw; c++) {
      if (isNaN(cmin[c])) continue; const x = gx + c;
      if (c && !isNaN(cend[c - 1])) line(x - 1, Y(cend[c - 1]), x, Y(cmin[c] < cend[c - 1] ? cmin[c] : cmax[c]), ON);
      for (let y = Y(cmax[c]); y <= Y(cmin[c]); y++) px[y * W + x] = ON;
    }
  }

  // ---------- box state ----------
  const B = { scr: 'status', sel: 0, angle: 0, msg: '', msgUntil: 0, blinkUntil: 0, down: false, t0: 0, held: false, swallow: false, bright: false,
    on: true, lastInput: Date.now(), kDown: false, kT0: 0, kLong: false, kSwallow: false, kTurned: false };
  // the screens chosen in Settings, in their order (the box's own default until then)
  const scrOrder = () => ['status'].concat((oledDoc().scr || ['status', 'cl', 'today', 'notes', 'wo', 'job', 'exp', 'map', 'sun', 'baro', 'join', 'clock']).filter(k => ['cl', 'today', 'notes', 'wo', 'job', 'exp', 'map', 'card', 'pcard', 'join', 'clock', 'sun', 'baro'].includes(k)));
  const menuKeys = () => scrOrder().slice(1);   // LIFE LOGGER's menu: every other screen, in the order chosen in Settings
  const MENU = {cl: 'Checklist', today: 'Today', notes: 'Notes', wo: 'Work orders', job: 'Job timer', exp: 'Expiring', map: 'Map today', card: 'Business card', pcard: 'Personal card', join: 'Join (Wi-Fi)', clock: 'Clock', sun: 'Sun & Moon', baro: 'Barograph'};
  // EXPIRING: the name, and the days left on the right
  const expRows = () => { const dn = dayNum(D.localDate(Date.now())); return (oledDoc().exp || []).map(x => { const dd = dayNum(x[0]) - dn, r = dd < 0 ? 'EXPIRED' : dd === 0 ? 'today' : dd + 'd'; return fit(ascii(x[1]), 21 - r.length - 1).padEnd(21 - r.length) + r; }); };
  let noteOpen = -1, noteListSel = 0, woOpen = -1, woListSel = 0, todayOpen = -1, todayListSel = 0;   // what's being read (-1: the list)
  const boxNotes = () => oledDoc().notes || [];
  const boxWos = () => (oledDoc().wos || []).map(w => [ascii(w[0]), ascii(w[1]), (w[2] || []).map(l => fit(ascii(l))).concat((w[3] || []).length ? ((w[2] || []).length ? [''] : []).concat(['STEPS'], w[3].map((t, i) => (i + 1) + '. ' + ascii(t))) : [])]);
  const awake = () => B.on;
  const listLen = () => B.scr === 'status' ? menuKeys().length : B.scr === 'cl' ? clRows().length : B.scr === 'today' ? (todayOpen >= 0 && todayItems()[todayOpen] ? todayDetail(todayItems()[todayOpen]).length : todayRows().length) : B.scr === 'notes' ? (noteOpen >= 0 && boxNotes()[noteOpen] ? Math.max(1, boxNotes()[noteOpen][1].length - ROWS + 1) : boxNotes().length)
    : B.scr === 'wo' ? (woOpen >= 0 && boxWos()[woOpen] ? boxWos()[woOpen][2].length : boxWos().length) : B.scr === 'exp' ? expRows().length : B.scr === 'job' ? jobOpts().length : B.scr === 'map' ? 2 : B.scr === 'baro' ? 3 : 0;
  // each screen comes back where you left it; the checklist remembers the item, not just the row
  const selMem = {}; let clSelId = '';
  function moveSel(d) {
    if (WT.ask) { WT.askSel = Math.max(0, Math.min(1, WT.askSel + d)); return; }
    const n = listLen(), s0 = B.sel; B.sel = n ? Math.max(0, Math.min(n - 1, B.sel + d)) : 0; selMem[B.scr] = B.sel;
    if (B.sel !== s0) mqT0 = Date.now();
    if (B.scr === 'cl') { const r = clRows()[B.sel]; if (r) clSelId = r.id; }
  }
  window.__lbScr = s => setScreen(s);   // for the demo's own tests
  window.__lbGps = m => { G.mode = m; gpsSync(); };
  window.__lbState = () => ({scr: B.scr, on: B.on, msg: B.msg, opts: jobOpts().map(o => o.a + ':' + o.t), qm: QM.on ? qmRows().map(r => r[0]) : null, qmSel: QM.sel, gps: [G.mode, gpsAsleep()], bnwas: !!D.ST.cfg.bnwasOn});
  function setScreen(s) {
    if (B.scr === 'notes' && noteOpen >= 0) { noteOpen = -1; B.sel = noteListSel; }   // leaving a note goes back to the list
    if (B.scr === 'wo' && woOpen >= 0) { woOpen = -1; B.sel = woListSel; }
    if (B.scr === 'today' && todayOpen >= 0) { todayOpen = -1; B.sel = todayListSel; }
    selMem[B.scr] = B.sel; B.scr = s; mqT0 = Date.now();
    const n = listLen();
    if (B.scr === 'cl' && clSelId) { const k = clRows().findIndex(r => r.id === clSelId); if (k >= 0) selMem.cl = k; }
    B.sel = Math.max(0, Math.min(selMem[B.scr] || 0, Math.max(0, n - 1)));
  }
  // the button: back one step (an open note, work order or TODAY item to its list), then to LIFE LOGGER
  function back() {
    if (B.scr === 'notes' && noteOpen >= 0) { notesPress(); return; }
    if (B.scr === 'wo' && woOpen >= 0) { woPress(); return; }
    if (B.scr === 'today' && todayOpen >= 0) { todayPress(); return; }
    if (B.scr === 'status') return;
    const from = B.scr; setScreen('status');
    const k = menuKeys().indexOf(from); if (k >= 0) { B.sel = k; selMem.status = k; }
  }
  function todayPress() {
    const L = todayItems();
    if (todayOpen < 0) { if (!L.length) return; todayListSel = B.sel; todayOpen = Math.max(0, Math.min(B.sel, L.length - 1)); B.sel = 0; listTop.today = 0; }
    else { todayOpen = -1; B.sel = todayListSel; }
    selMem.today = B.sel; mqT0 = Date.now();
  }
  function woPress() {
    const W2 = boxWos();
    if (woOpen < 0) { if (!W2.length) return; woListSel = B.sel; woOpen = Math.max(0, Math.min(B.sel, W2.length - 1)); B.sel = 0; listTop.wo = 0; }
    else { woOpen = -1; B.sel = woListSel; }
    selMem.wo = B.sel; mqT0 = Date.now();
  }
  // ---------- JOB TIMER ----------
  function jobState() {
    const T = {};
    for (const l of D.ST.log) { if (l.indexOf('"time.') < 0) continue; let o; try { o = JSON.parse(l).e; } catch (e) { continue; }
      if (o.op === 'time.del') { if (T[o.id]) T[o.id].deleted = true; continue; } if (o.op !== 'time.set') continue;
      const x = T[o.id] || (T[o.id] = {id: o.id}); for (const k of ['desc', 'prj', 'bill', 'start', 'end', 'wo', 'paused']) if (k in o) x[k] = o[k];
      for (const k of ['start', 'end']) if (typeof x[k] === 'string') x[k] = Date.parse(x[k]) || null; x.deleted = false; }
    const L = Object.values(T).filter(x => !x.deleted && x.start).sort((a, b) => (b.end || 9e15) - (a.end || 9e15));
    const run = L.find(x => !x.end) || null, ended = L.filter(x => x.end), pz = !run && ended[0] && ended[0].paused ? ended[0] : null;
    const last = run || pz || ended[0] || null, d0 = new Date(); d0.setHours(0, 0, 0, 0);
    const todaySec = L.filter(x => x.start >= d0.getTime()).reduce((t, x) => t + ((x.end || Date.now()) - x.start) / 1000, 0);
    return {run, pz, last, todaySec, L};
  }
  const jobName = id => ((oledDoc().jobN || {})[id]) || (id ? 'Job' : 'No project');
  const hms = s => { s = Math.max(0, Math.floor(s)); return Math.floor(s / 3600) + ':' + String(Math.floor(s / 60) % 60).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0'); };
  // what the knob can pick: Pause / Stop (or Resume / Stop), then every active job and project to start (or switch to)
  function jobOpts() {
    const j = jobState(), ids = Object.keys(oledDoc().jobN || {}), cur = j.run && j.run.prj;
    const o = (j.run ? ['Pause', 'Stop'] : j.pz ? ['Resume', 'Stop'] : []).map(a => ({a, t: a}));
    for (const id of ids) if (!(j.run && id === cur)) o.push({a: 'go', id, t: (j.run ? 'Switch: ' : 'Start: ') + jobName(id)});
    if (!ids.length && !j.run && !j.pz && j.last) o.push({a: 'again', t: 'Start: ' + (j.last.desc || jobName(j.last.prj))});
    return o;
  }
  function jobPress(k) {
    const j = jobState(), o = jobOpts()[k == null ? B.sel : k], t = Date.now(), ev = [];
    if (!o) return;
    const start = (prj, desc, bill, wo) => { if (j.run) ev.push({op: 'time.set', id: j.run.id, end: t}); if (j.pz) ev.push({op: 'time.set', id: j.pz.id, paused: false});
      ev.push({op: 'time.set', id: 'tm-b' + t.toString(36), desc: desc || '', prj: prj || '', bill: !!bill, wo: wo || '', start: t}); };
    if (o.a === 'Pause' && j.run) ev.push({op: 'time.set', id: j.run.id, end: t, paused: true});
    else if (o.a === 'Stop') { if (j.run) ev.push({op: 'time.set', id: j.run.id, end: t}); else if (j.pz) ev.push({op: 'time.set', id: j.pz.id, paused: false}); }
    else if (o.a === 'Resume' && j.pz) start(j.pz.prj, j.pz.desc, j.pz.bill, j.pz.wo);
    else if (o.a === 'again' && j.last) start(j.last.prj, j.last.desc, j.last.bill, j.last.wo);
    else if (o.a === 'go') {   // the job's or project's last description carries on; billable as the dashboard has it
      const prev = j.L.filter(x => x.prj === o.id).sort((a, b) => b.start - a.start)[0];
      start(o.id, prev ? prev.desc : '', prev ? prev.bill : !!((oledDoc().jobB || {})[o.id]), '');
    }
    if (!ev.length) return;
    D.appendEvents(ev.map(e => JSON.stringify(e)).join('\n')); D.save();
    B.msg = o.a === 'Pause' ? 'Paused' : o.a === 'Stop' ? 'Stopped' : 'Started'; B.msgUntil = t + 1000; B.sel = 0;
    if (typeof window.pollStatus === 'function') window.pollStatus();
  }
  // ---------- MAP: Work trip ----------
  // tick it (in the driveway): from then the box adds up the distance it moves until End is pressed on MAP; a long red light doesn't end it.
  // After an hour without moving it beeps and asks whether you're still on it (turn to Keep going or End, press); unanswered it stops beeping after 2 min
  const BEEP_MS = 120e3, askMs = () => (D.ST.cfg.tripAskMin || 60) * 60e3;   // the wait is a setting (Settings › The box)
  const WT = {on: false, t1: 0, mi: 0, last: 0, lastMove: 0, still: 0, ask: false, askSel: 0, askAt: 0};
  window.__lbTripAsk = () => { if (WT.on) WT.still = Date.now() - askMs() - 1; };   // for the demo's own tests: as if parked for an hour
  const tripBeeping = () => WT.ask && Date.now() - WT.askAt < BEEP_MS;
  function tripStart() { const now = Date.now(); Object.assign(WT, {on: true, t1: now, mi: 0, last: now, lastMove: now, still: now, ask: false}); B.msg = 'Work trip on'; B.msgUntil = now + 1000; }
  function tripEnd(atLastMove) {
    WT.ask = false; if (!WT.on) return;
    const now = Date.now(), t2 = atLastMove ? WT.lastMove : now, id = 'b' + WT.t1.toString(36);
    D.appendEvents(JSON.stringify({op: 'trip.set', id, date: D.localDate(WT.t1), t1: D.localISO(WT.t1), t2: D.localISO(t2), mi: Math.round(WT.mi * 10) / 10, gps: true, src: 'box', biz: true,
      lat1: D.HOME[0], lon1: D.HOME[1], lat2: D.HOME[0] + 0.03, lon2: D.HOME[1] - 0.02}));
    D.save(); WT.on = false; B.msg = 'Work trip saved'; B.msgUntil = now + 1500; blink(now, 400);
    if (typeof window.pollStatus === 'function') window.pollStatus();
  }
  function tripWatch() {   // the demo's 🧭 drive moves the box
    if (!WT.on) return;
    const now = Date.now(), mv = motion();
    if (mv.cog != null && mv.kn >= 1) WT.mi += mv.kn * 1.15078 * (now - WT.last) / 3600e3;
    WT.last = now;
    if (mv.cog != null && mv.kn >= 3) { WT.still = now; WT.lastMove = now; }
    if (!WT.ask && now - WT.still > askMs()) { WT.ask = true; WT.askSel = 0; WT.askAt = now; }
  }
  function tripAnswer() { if (WT.askSel === 1) tripEnd(true); else { WT.ask = false; WT.still = Date.now(); B.msg = 'Still on it'; B.msgUntil = Date.now() + 1000; } }
  const fmtDist = mi => { const f = fmt().dist, v = f === 'km' ? mi * 1.609344 : f === 'nm' ? mi / 1.15078 : mi; return v.toFixed(v < 10 ? 2 : 1) + ' ' + f; };
  function mapPress() {
    if (B.sel === 0) { if (WT.on) tripEnd(false); else tripStart(); }
    else spotSave();
  }
  function spotSave() {   // MAP: a press saves where the box is as a spot on the Map tab
    const t = Date.now();
    if (gpsAsleep()) { B.msg = G.mode === 2 ? 'GPS is off' : 'GPS asleep at home'; B.msgUntil = t + 1500; return; }
    D.appendEvents(JSON.stringify({op: 'spot.add', id: 'sp-' + t.toString(16), lat: D.HOME[0] + (Math.random() - 0.5) * 0.002, lon: D.HOME[1] + (Math.random() - 0.5) * 0.002, t: D.localISO ? D.localISO(t) : new Date(t).toISOString(), src: 'box'}));
    D.save(); spotsToday++; B.msg = 'Spot saved'; B.msgUntil = t + 1500; blink(t, 400);
    if (typeof window.pollStatus === 'function') window.pollStatus();
  }
  // ACTION (the knob's push): a press runs a shortcut, a hold opens the quick menu; BACK + SELECT held 3 s, a third shortcut (Settings › The box's screen)
  const QM0 = ['screen', 'gps', 'job', 'trip', 'mark', 'bnwas', 'phone', 'clock'];
  const btnCfg = () => Object.assign({p: 'job', b: 'phone', q: QM0}, oledDoc().btn || {});
  const QM = {on: false, sel: 0, top: 0};
  function jobQuickLabel() {
    const j = jobState(), O = jobOpts(), nm = x => x.desc || jobName(x.prj);
    if (O.some(o => o.a === 'Pause')) return 'Pause ' + nm(j.run);
    if (O.some(o => o.a === 'Resume')) return 'Resume ' + nm(j.pz);
    const o = O.find(o => o.a === 'again' || o.a === 'go'); return o ? o.t : '';
  }
  function qmRows() {
    const r = [['Close', '']];
    if (D.ST.cfg.apHomeOff && !B.apForce) r.push(['Hotspot on', 'apon']);   // off at home: a way back without the dashboard
    for (const k of btnCfg().q || QM0) {
      if (k === 'screen') r.push(['Screen off', 'off']);
      else if (k === 'gps') { r.push(gpsAsleep() ? ['GPS on', 'gps1'] : ['GPS off', 'gps2']); if (G.mode) r.push(['GPS auto (home off)', 'gps0']); }
      else if (k === 'job') { const t = jobQuickLabel(); if (t) r.push([t, 'job']); }
      else if (k === 'trip') r.push([WT.on ? 'End work trip' : 'Start work trip', 'trip']);
      else if (k === 'mark') r.push(['Quick mark', 'mark']);
      else if (k === 'bnwas') r.push([D.ST.cfg.bnwasOn ? 'BNWAS off' : 'BNWAS on', 'bnwas']);
      else if (k === 'phone') r.push(['Find my phone', 'phone']);
      else if (k === 'clock') r.push(['Clock', 'clock']);
      else if (k === 'setclk') r.push(['Set clock', 'setclk']);
      else if (k === 'home') r.push(['Home', 'home']);
      else if (k === 'beep') r.push(['Test alert', 'beep']);
      else if (k === 'alert') { const N = ['Sound', 'Vibrate', 'Both'], a = alertMode(); r.push(['Alerts: ' + N[a] + ' > ' + N[(a + 1) % 3], 'alert']); }
      else if (k === 'restart') r.push(['Restart', 'restart']);
    }
    return r;
  }
  function qmDraw() {
    const R = qmRows(); QM.sel = Math.max(0, Math.min(QM.sel, R.length - 1));
    head('QUICK MENU');
    const vis = 12;
    if (QM.sel < QM.top) QM.top = QM.sel; if (QM.sel >= QM.top + vis) QM.top = QM.sel - vis + 1;
    for (let i = 0; i < vis && QM.top + i < R.length; i++) { const k = QM.top + i; row(i, (k === QM.sel ? '> ' : '  ') + R[k][0], k === QM.sel, k ? ON : DIM); }
  }
  function qmRun() {
    const R = qmRows(); QM.on = false;
    if (QM.sel <= 0 || QM.sel >= R.length) return;
    const a = R[QM.sel][1], now = Date.now();
    if (a.startsWith('gps')) { G.mode = +a[3]; gpsSync(); B.msg = G.mode === 1 ? 'GPS on' : G.mode === 2 ? 'GPS off' : (gpsAsleep() ? 'GPS auto: asleep' : 'GPS auto: on'); B.msgUntil = now + 1500; }
    else if (a === 'alert') { D.ST.cfg.alert = (alertMode() + 1) % 3; D.save(); B.msg = 'Alerts: ' + ['Sound', 'Vibrate', 'Both'][D.ST.cfg.alert]; B.msgUntil = now + 1500; }
    else if (a === 'setclk') { B.msg = 'Demo: browser clock'; B.msgUntil = now + 2000; }   // a box opens SET CLOCK here
    else if (a === 'apon') { B.apForce = true; B.msg = 'Hotspot on'; B.msgUntil = now + 1500; }
    else if (a === 'bnwas') { D.ST.cfg.bnwasOn = !D.ST.cfg.bnwasOn; D.AL.bnStage = 0; D.AL.bnT0 = now; D.save(); B.msg = D.ST.cfg.bnwasOn ? 'BNWAS on' : 'BNWAS off'; B.msgUntil = now + 1500; }
    else shortcut(a);
  }
  function jobQuick() {   // pause what's running, resume what's paused, else start the last one
    const O = jobOpts(); let k = -1;
    for (const a of ['Pause', 'Resume', 'again', 'go']) { k = O.findIndex(o => o.a === a); if (k >= 0) break; }
    if (k < 0) { B.msg = 'No job to start'; B.msgUntil = Date.now() + 1500; return; }
    jobPress(k);
  }
  function shortcut(k) {
    const now = Date.now();
    if (!B.on && !['phone', 'beep', 'restart'].includes(k)) B.on = true;
    if (k === 'home') setScreen('status');
    else if (k === 'clock') setScreen('clock');
    else if (k === 'job') jobQuick();
    else if (k === 'mark') spotSave();
    else if (k === 'trip') { if (WT.on) tripEnd(false); else tripStart(); }
    else if (k === 'off') B.on = false;
    else if (k === 'phone') { D.ST.findPhone = Math.floor(now / 1000); D.save(); B.msg = 'Calling phones'; B.msgUntil = now + 2000; blink(now, 1200); if (typeof window.pollStatus === 'function') window.pollStatus(); }
    else if (k === 'beep') { B.beepUntil = now + 700; B.msg = ['Sound', 'Vibrate', 'Sound + vibrate'][alertMode()]; B.msgUntil = now + 1200; }
    else if (k === 'restart') { B.on = false; G.mode = 0; gpsSync(); setTimeout(() => { B.on = true; setScreen('status'); B.msg = 'Restarted'; B.msgUntil = Date.now() + 1500; tick(true); }, 1500); }
  }
  // the knob's press: on LIFE LOGGER it opens the screen picked in the menu; it never moves between screens otherwise
  function blink(t, ms) { B.blinkUntil = t + ms; B.blinkMask = B.src || 0; }   // only the button that did it blinks (1 BACK, 2 SELECT, 3 both)
  function press() {
    if (WT.ask) { tripAnswer(); return; }
    if (B.scr === 'status') { const k = menuKeys()[B.sel]; if (k) setScreen(k); }
    else if (B.scr === 'cl') clToggle(B.sel); else if (B.scr === 'notes') notesPress(); else if (B.scr === 'wo') woPress(); else if (B.scr === 'today') todayPress(); else if (B.scr === 'map') mapPress(); else if (B.scr === 'job') jobPress();
  }
  const offMin = () => D.ST.cfg.scrOffMin ?? 5;
  const alertMode = () => D.ST.cfg.alert ?? 2;   // 0 sound, 1 vibrate, 2 both
  // any turn or press: restarts the sleep timer and BNWAS; true if it should do nothing else (stopped an alarm, or woke the screen)
  function touched() {
    B.lastInput = Date.now();
    if (D.ST.cfg.bnwasOn) { D.AL.bnStage = 0; D.AL.bnT0 = Date.now(); }
    if (D.alarmActive()) { D.silence(true); B.msg = 'Stopped'; B.msgUntil = Date.now() + 1000; return true; }
    if (tripBeeping()) { WT.askAt = Date.now() - BEEP_MS; B.on = true; return true; }   // the first touch only stops the beeping
    if (!B.on) { B.on = true; return true; }
    return false;
  }

  // NOTES: press opens the highlighted note, press again goes back to the list
  function notesPress() {
    const N = boxNotes();
    if (noteOpen < 0) { if (!N.length) return; noteListSel = B.sel; noteOpen = Math.max(0, Math.min(B.sel, N.length - 1)); B.sel = 0; }
    else { noteOpen = -1; B.sel = noteListSel; }
    selMem.notes = B.sel;
  }
  // like ticking on the phone: the item and everything under it together; unticking also unticks what it sits under
  function clToggle(idx) {
    const rows = clRows(); if (idx < 0 || idx >= rows.length) return;
    const doc = JSON.parse(D.ST.docs.checklist), items = doc.items, byId = id => items.find(x => x.id === id);
    const it = byId(rows[idx].id); if (!it) return;
    const done = !it.done, today = D.localDate(Date.now()), day = doc.day || today, stamp = new Date().toISOString();
    const ids = [it.id];
    for (let k = 0; k < ids.length && ids.length < 300; k++) for (const x of items) if (x.parent === ids[k]) ids.push(x.id);
    if (!done) for (let p = it.parent && byId(it.parent), g = 0; p && g < 10; p = p.parent && byId(p.parent), g++) if (p.done) ids.push(p.id);
    const evs = [];
    for (const id of ids) {
      const x = byId(id); if (!x) continue;
      if (!!x.done === done && x !== it) continue;
      x.done = done; x.doneAt = done ? today : null; x.updated = stamp;
      if (x.wo && x.step) evs.push(JSON.stringify({ op: 'wo.step', id: x.wo, step: x.step, done, date: day, text: x.text, via: 'cl' }));
    }
    if (done && !(it.wo && it.step)) {
      let lw = it.wo || '', par = it.parent || '';
      for (let g = 0; !lw && par && g < 10; g++) { const p = byId(par); if (!p) break; lw = p.wo || ''; par = p.parent || ''; }
      if (lw) evs.push(JSON.stringify({ op: 'wo.note', id: lw, text: 'Checklist ✓ ' + it.text + ' (' + shortDate(day) + ')' }));
    }
    D.ST.docs.checklist = JSON.stringify(doc); D.ST.docVer++;
    if (evs.length) D.appendEvents(evs.join('\n'));
    D.save();
    clSelId = it.id; blink(Date.now(), 400);                // the light blinks; the row shows the tick
    if (typeof window.pollStatus === 'function') window.pollStatus();
  }

  function draw() {
    const now = Date.now(); clear(); mqOn = false; yOff = 0;
    if (!scrOrder().includes(B.scr) && B.scr !== 'clock') setScreen('status');   // Clock is also reachable from a shortcut
    if (QM.on) qmDraw();
    else if (B.scr === 'status') {
      head('LIFE LOGGER');
      const keys = (oledDoc().lb || ['date', 'batt']).filter(k => k !== 'time').slice(0, 8);
      keys.forEach((k, i) => { const [t, c] = lbLine(k); row(i, t, false, c); });
      let r = keys.length;
      const al = [];
      for (const k of D.AL.timers) al.push([ascii(k.name), k.ring ? 'DONE' : clockLeft(Math.max(0, Math.ceil((k.end - now) / 1000))), ON]);
      if (D.ST.cfg.bnwasOn) al.push(['BNWAS', D.AL.bnStage ? 'PRESS' : clockLeft(D.bnLeft()), ON]);
      const maxRow = () => Math.floor((109 - yOff) / 9);   // the last row that fits, highlight included
      const divide = () => { rect(0, 12 + 9 * r + yOff, 128, 1, DIM); yOff += 5; };   // 2-3 px of space each side of the line
      const room = Math.max(0, Math.min(3, maxRow() + 1 - r - 3 - 1));   // alarms get at most 3 rows; the menu at least 3
      if (al.length && room) {
        if (r) divide();
        for (let i = 0; i < al.length && i < room; i++) {
          if (i === room - 1 && al.length > room) { row(r + i, '+' + (al.length - i) + ' more', false, DIM); break; }
          rowLR(r + i, al[i][0], al[i][1], al[i][2]);
        }
        r += Math.min(al.length, room);
      }
      // the menu: knob picks, press opens
      if (r) divide();
      const M = menuKeys(), n = Math.max(1, maxRow() + 1 - r);
      let top = listTop.status || 0;
      if (B.sel < top) top = B.sel; if (B.sel > top + n - 1) top = B.sel - n + 1;
      top = Math.max(0, Math.min(top, Math.max(0, M.length - n))); listTop.status = top;
      const cnt = k => { if (k === 'cl') { const c = clRows(); return c.total ? c.doneN + '/' + c.total : ''; } if (k === 'today') { const t = todayItems().length; return t ? String(t) : ''; }
        if (k === 'notes') { const t = boxNotes().length; return t ? String(t) : ''; } if (k === 'wo') { const t = boxWos().length; return t ? String(t) : ''; }
        if (k === 'exp') { const t = (oledDoc().exp || []).length; return t ? String(t) : ''; } return ''; };
      for (let i = 0; i < n && top + i < M.length; i++) { const k = M[top + i], sel = top + i === B.sel; rowLR(r + i, (sel ? '>' : ' ') + MENU[k], cnt(k), ON, sel); }
      if (M.length > n) { const H = n * 9, bh = Math.max(4, Math.floor(H * n / M.length)), by = 10 + 9 * r + yOff + Math.floor((H - bh) * top / (M.length - n)); rect(127, by, 1, bh, DIM); }
      yOff = 0;
    } else if (B.scr === 'cl') {
      const r = clRows();
      const ct = r.doneN + '/' + r.total; head(('CHECKLIST ' + ct).length <= 12 ? 'CHECKLIST ' + ct : 'LIST ' + ct);
      if (!r.length) { if (r.total) { row(1, 'All ticked'); row(2, '(ticked items hidden)', false, DIM); } else { row(1, 'No items'); row(2, 'Add them on the phone'); } }
      else drawList(r.map(x => ' '.repeat(x.depth) + (x.done ? '[x] ' : '[ ] ') + x.text), B.sel, true, 2);
    } else if (B.scr === 'today') {
      const L = todayRows(), T = todayItems(); head('TODAY ' + shortDate(D.localDate(now)));
      if (todayOpen >= T.length) todayOpen = -1;
      if (todayOpen >= 0) drawList(todayDetail(T[todayOpen]), B.sel, true, 0);
      else if (!L.length) row(1, oledDoc().days ? 'Nothing on today' : 'Open the dashboard once'); else drawList(L, B.sel, true, 1);
    } else if (B.scr === 'notes') {
      const N = boxNotes(); if (noteOpen >= N.length) noteOpen = -1;
      if (noteOpen < 0) { head('NOTES'); if (!N.length) { row(1, 'No notes here'); row(2, "Tick 'Show on the box'", false, DIM); row(3, 'on a note', false, DIM); } else drawList(N.map(x => x[0]), B.sel, true); }
      else {
        const nt = N[noteOpen], L = nt[1], n = L.length, top = Math.max(0, Math.min(B.sel, Math.max(0, n - ROWS)));
        head(nt[0]);
        for (let i = 0; i < ROWS && top + i < n; i++) row(i, L[top + i]);
        if (!n) row(1, '(no text)', false, DIM);
        if (n > ROWS) { const H = ROWS * 9, bh = Math.max(6, Math.floor(H * ROWS / n)), by = 10 + Math.floor((H - bh) * top / (n - ROWS)); rect(127, by, 1, bh, DIM); }
      }
    } else if (B.scr === 'wo') {
      const W2 = boxWos(); if (woOpen >= W2.length) woOpen = -1;
      if (woOpen < 0) { head('WORK ORDERS'); if (!W2.length) { row(1, 'None here'); row(2, "Tick 'Show on the box'", false, DIM); row(3, 'on a work order', false, DIM); } else drawList(W2.map(w => w[0] + ' ' + w[1]), B.sel, true, 1); }
      else { const w = W2[woOpen]; head(w[0] + ' ' + w[1]); if (!w[2].length) row(1, '(no description)', false, DIM); else drawList(w[2], B.sel, true, 1); }
    } else if (B.scr === 'exp') {
      const L = expRows(); head('EXPIRING');
      if (!L.length) { row(1, 'Nothing coming up'); row(2, 'Expiring tab on the', false, DIM); row(3, 'dashboard', false, DIM); } else drawList(L, B.sel, true, 0);
    } else if (B.scr === 'clock') {
      drawClock();
    } else if (B.scr === 'sun') {
      drawSun();
    } else if (B.scr === 'baro') {
      drawBaro();
    } else if (B.scr === 'card' || B.scr === 'pcard') {   // a card to hold up for someone: the business one, or your own
      const c = B.scr === 'card' ? oledDoc().card : oledDoc().pcard;
      if (!c || !(c.lines || []).length) { head(B.scr === 'card' ? 'BUSINESS CARD' : 'PERSONAL CARD'); row(1, 'Nothing to show yet'); row(2, 'Settings > Business', false, DIM); row(3, 'and Personal Info', false, DIM); }
      else {
        let y = 2;
        if (c.logo && c.logo[0]) {
          const [w, h, hx] = c.logo, x0 = Math.floor((128 - w) / 2);
          for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) { const b = parseInt(hx.substr(((j * w + i) >> 1) * 2, 2), 16), v = (i & 1) ? (b & 15) : (b >> 4); if (v) px[(y + j) * W + x0 + i] = v; }
          y += h + 4;
        }
        const room = Math.floor((128 - y) / 9);
        c.lines.slice(0, room).forEach((l, i) => { l = fit(ascii(l)); text(Math.floor((128 - l.length * 6) / 2), y + i * 9, l, ON); });
      }
    } else if (B.scr === 'job') {
      head('JOB TIMER');
      const j = jobState(), x = j.run || j.pz || j.last, n = new Date();
      if (!x) { row(0, 'Nothing timed yet', false, DIM); }
      else {
        row(0, ascii(jobName(x.prj))); row(1, ascii(x.desc || ''), false, DIM);
        const tt = j.run ? hms((Date.now() - j.run.start) / 1000) : j.pz ? hms((j.pz.end - j.pz.start) / 1000) : '0:00:00';
        text(Math.floor((128 - tt.length * 12) / 2), 32, tt, j.pz && Math.floor(Date.now() / 600) % 2 ? DIM : ON, 2);
        const st = j.run ? 'RUNNING' : j.pz ? 'PAUSED' : 'STOPPED'; text(Math.floor((128 - st.length * 6) / 2), 53, st, j.run ? ON : DIM);
        rowLR(6, 'Today', hms(j.todaySec), DIM);
      }
      {   // the choices: five rows, scrolling with the knob
        const O = jobOpts(), top = Math.max(0, Math.min(B.sel - 2, O.length - 5));
        if (x) rect(0, 12 + 9 * 7 + 3, 128, 1, DIM);
        if (!O.length) { row(8, 'Nothing to start yet:', false, DIM); row(9, 'pin a project in', false, DIM); row(10, 'Time > Projects, or', false, DIM); row(11, 'open a job', false, DIM); }
        O.slice(top, top + 5).forEach((o, i) => row(8 + i, ((top + i) === B.sel ? '> ' : '  ') + ascii(o.t), (top + i) === B.sel));
        if (top > 0) text(121, 11 + 9 * 8, '^', DIM); if (top + 5 < O.length) text(121, 11 + 9 * 12, 'v', DIM);
      }
    } else if (B.scr === 'map') {
      head('MAP TODAY');
      const L = oledDoc().map || ['spd', 'dist', 'fix', 'spots', 'lat', 'lon', 'sats'], mv = motion();
      if (mv.cog != null && mv.kn >= 1) {   // moving: the first lines, the course, and the compass under them
        const show = L.filter(k => k !== '-' && k !== 'cog').slice(0, 4).concat(['cog']);
        show.forEach((k, i) => { const [t, c] = mapLine(k); row(i, t, false, c); });
        const top = 11 + 9 * show.length + 1, h = 127 - 20 - top, r = Math.min(40, Math.floor(h / 2) - 1);
        rose(64, top + Math.floor(h / 2), r, mv.cog);
      } else L.slice(0, 10).forEach((k, i) => { const [t, c] = mapLine(k); row(i, t, false, c); });
      row(11, (B.sel === 0 ? '> ' : '  ') + (WT.on ? 'End trip ' + fmtDist(WT.mi) : '[ ] Work trip'), B.sel === 0);   // turn to pick, press to start / end or save
      row(12, (B.sel === 1 ? '> ' : '  ') + 'Save spot here', B.sel === 1);
    } else {
      head('JOIN'); row(0, 'WiFi ' + ascii(D.ST.cfg.apSsid)); row(1, 'Pass ' + ascii(D.ST.cfg.apPass));
      row(3, 'Open lifelogger.local'); row(4, '  or 192.168.4.1'); row(6, 'Home: 192.168.1.42');
      const dv = D.status().devs || [];   // dashboards open on phones and computers, newest first
      rect(0, 12 + 9 * 7 + 3, 128, 1, DIM);
      row(8, 'CONNECTED ' + dv.length, false, DIM);
      if (!dv.length) row(9, 'No dashboard open', false, DIM);
      dv.slice(0, dv.length > 4 ? 3 : 4).forEach((d, i) => rowLR(9 + i, ascii(d.n), d.age < 60 ? 'now' : Math.floor(d.age / 60) + 'm'));
      if (dv.length > 4) row(12, '+' + (dv.length - 3) + ' more', false, DIM);
    }
    const blk = Math.floor(now / 500) % 2 === 1, A = D.AL;
    if (A.bnStage === 1) alarmBox('BNWAS', 'Press a button', 'alarm in ' + D.bnLeft() + ' s', blk);
    else if (A.bnStage === 2) alarmBox('BNWAS', 'Press a button', '', blk);
    else if (D.timerRinging()) alarmBox('TIMER', ascii(A.timers.find(k => k.ring).name), 'Press to stop', blk);
    else if (A.find) alarmBox('HERE', 'Find my box', 'Press to stop', blk);
    else if (WT.ask) {   // still on the work trip? turn to pick, press
      rect(0, 26, 128, 78, OFF); frame(0, 26, 128, 78, ON); frame(2, 28, 124, 74, ON);
      text(64 - 6 * 9, 33, 'WORK TRIP', ON, 2);
      let q = 'Still on it? ' + fmtDist(WT.mi); if (q.length > 20) q = q.replace(/ (\S+)$/, '$1'); text(64 - 3 * q.length, 53, q, ON);
      ['Keep going', 'End trip'].forEach((o, i) => { const y = 68 + i * 13, on = i === WT.askSel; if (on) rect(8, y - 2, 112, 11, ON); text(64 - 3 * o.length, y, o, on ? OFF : ON); });
    }
    else if (now < B.msgUntil) { rect(14, 50, 100, 26, OFF); frame(14, 50, 100, 26, ON); text(64 - 3 * B.msg.length, 59, B.msg, ON); }
  }

  // ---------- panel ----------
  const css = `
#lbBox{position:fixed;right:16px;bottom:16px;z-index:45;width:min(830px,calc(100vw - 32px));background:#151a21;border:1px solid #2c3540;border-radius:14px;box-shadow:0 10px 32px rgba(0,0,0,.45);color:#d6dde6;font:13px/1.35 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;user-select:none;-webkit-user-select:none}
#lbBox.min{width:auto}
#lbBox .bh{display:flex;align-items:center;gap:8px;padding:8px 10px 8px 14px;border-bottom:1px solid #2c3540;cursor:pointer}
#lbBox.min .bh{border-bottom:0}
#lbBox .bh b{font-size:13px;letter-spacing:.3px}
#lbBox .bh .sp{flex:1}
#lbBox .bh button{background:none;border:0;color:#9aa6b2;font-size:15px;cursor:pointer;padding:2px 6px}
#lbBox.min .bb{display:none}
#lbBox .bb{padding:12px 14px 12px}
#lbBox .lidfit{position:relative;width:100%;overflow:hidden}
#lbBox .lidsc{position:absolute;left:0;top:0;width:800px;height:462px;transform-origin:0 0}
@keyframes lbVib{0%{transform:translate(0,0)}25%{transform:translate(-2px,1px)}50%{transform:translate(2px,-1px)}75%{transform:translate(-1px,-1px)}100%{transform:translate(1px,1px)}}
#lbBox .lid.vib{animation:lbVib .07s linear infinite}
#lbBox .lid{position:absolute;left:0;top:0;width:700px;height:462px;background:#9aa3b5;border-radius:4px;box-shadow:inset 0 0 0 2px #6f7a8c,0 2px 10px rgba(0,0,0,.5)}
#lbBox .scw{position:absolute;width:12px;height:12px;margin:-6px 0 0 -6px;border-radius:50%;background:#555e6c;box-shadow:inset 0 0 0 2px #7d8696}
#lbBox .scw.sc2{width:16px;height:16px;margin:-8px 0 0 -8px}
#lbBox .lid .cc{position:absolute;color:#2b323b;font-weight:600}
#lbBox .lid .ind{position:absolute;width:44px;margin-left:-22px;text-align:center;font:600 12px system-ui,sans-serif;color:#2b323b}
#lbBox .lid .ind i{display:block;width:30px;height:30px;margin:0 auto 4px;border-radius:50%;background:radial-gradient(circle at 38% 32%,#6a717b,#2f353d 70%);box-shadow:0 0 0 5px #7b8597,0 1px 3px rgba(0,0,0,.55);transition:background .05s,box-shadow .05s}
#lbBox .lid .ind i.lit{background:radial-gradient(circle at 38% 32%,#fff,var(--ic) 45%);box-shadow:0 0 0 5px #7b8597,0 0 16px 6px var(--ic)}
#lbBox .lid .ind i.faint{background:radial-gradient(circle at 38% 32%,#8a8f96,var(--ic) 70%);box-shadow:0 0 0 5px #7b8597,0 0 5px 1px var(--ic);opacity:.75}
#lbBox .lid .ind.nopin{opacity:.45}
#lbBox .lid .cc span{position:absolute;top:38px;left:104px;white-space:nowrap;font-size:13px}
#lbBox .lb2{left:70px;top:23px}
#lbBox .lid .cc .hold{position:absolute;top:60px;left:100px;font:600 calc(12px * var(--hs, 1)) system-ui,sans-serif;padding:calc(3px * var(--hs, 1)) calc(10px * var(--hs, 1));border-radius:calc(11px * var(--hs, 1));border:1px solid #5d6776;background:#c3cad6;color:#2b323b;cursor:pointer;touch-action:manipulation}
#lbBox .lid .cc .hold.on{background:#2b323b;color:#fff;border-color:#2b323b;box-shadow:0 0 0 2px #4ea1ff}
#lbBox .lb1{left:35px;top:130px}
#lbBox .lid #lbBtn,#lbBox .lid #lbBtn2{width:90px;height:90px;margin:3px}   /* 96 with the lit ring, as the 16 mm hole in the model */
#lbBox .lid #lbBtn,#lbBox .lid #lbBtn2{box-shadow:0 0 0 3px #3a424b,0 2px 6px rgba(0,0,0,.5)}
#lbBox .lid #lbBtn.led,#lbBox .lid #lbBtn2.led{box-shadow:0 0 0 3px #f4f8ff,0 0 14px 5px rgba(235,245,255,.55)}
#lbBox .lid #lbBtn.dimled,#lbBox .lid #lbBtn2.dimled{box-shadow:0 0 0 3px #6b7480,0 0 6px 1px rgba(235,245,255,.14)}
#lbBox .bez{position:absolute;left:453px;top:81px;width:166px;height:166px;padding:0;background:#07090b;border-radius:2px;display:flex;align-items:center;justify-content:center;box-shadow:inset 0 0 0 3px #232a32}
#lbBox .lid canvas{width:160px;height:160px}
#lbBox .side{position:absolute;left:704px;top:322px;width:96px;text-align:center;color:#9aa6b2}
#lbBox .side span{font-size:11px}
#lbBox .side .kl{font-size:11px;line-height:13px;color:#c3ccd6;white-space:nowrap}
#lbBox .side .kl b{color:#e8eef4}
#lbBox .bez{background:#07090b;border-radius:8px;padding:10px;display:flex;justify-content:center;box-shadow:inset 0 0 0 2px #232a32}
#lbBox canvas{image-rendering:pixelated;image-rendering:crisp-edges;background:#000;transition:filter .2s}

#lbBox .cc{display:flex;flex-direction:column;align-items:center;gap:6px;font-size:11.5px;color:#9aa6b2}
#lbKnob{width:84px;height:84px;cursor:grab;touch-action:none;outline:none}
#lbKnob:focus-visible{filter:drop-shadow(0 0 4px #4ea1ff)}
#lbBtn{width:62px;height:62px;border-radius:50%;border:0;cursor:pointer;touch-action:none;background:radial-gradient(circle at 40% 35%,#e9edf1,#9aa3ad 60%,#6d757e);box-shadow:0 0 0 5px #3a424b,0 3px 8px rgba(0,0,0,.6);transition:box-shadow .08s}
#lbBtn.led{box-shadow:0 0 0 5px #f4f8ff,0 0 16px 6px rgba(235,245,255,.55),0 3px 8px rgba(0,0,0,.6)}
#lbBtn.dimled{box-shadow:0 0 0 5px #6b7480,0 0 7px 1px rgba(235,245,255,.14),0 3px 8px rgba(0,0,0,.6)}
#lbBtn.pr{transform:translateY(1px) scale(.97)}
#lbBtn2{width:62px;height:62px;border-radius:50%;border:0;cursor:pointer;touch-action:none;background:radial-gradient(circle at 40% 35%,#e9edf1,#9aa3ad 60%,#6d757e);box-shadow:0 0 0 5px #3a424b,0 3px 8px rgba(0,0,0,.6);transition:box-shadow .08s}
#lbBtn2.led{box-shadow:0 0 0 5px #f4f8ff,0 0 16px 6px rgba(235,245,255,.55),0 3px 8px rgba(0,0,0,.6)}
#lbBtn2.dimled{box-shadow:0 0 0 5px #6b7480,0 0 7px 1px rgba(235,245,255,.14),0 3px 8px rgba(0,0,0,.6)}
#lbBtn2.pr{transform:translateY(1px) scale(.97)}
#lbBox .bh #lbHelp{font-weight:700;font-size:13px}
#lbBox .hintbtn{display:block;margin:10px auto 0;background:#26303a;color:#d6dde6;border:1px solid #3a4652;border-radius:7px;padding:5px 14px;font:inherit;font-size:12.5px;cursor:pointer}
#lbBox .hintbtn:hover{background:#2f3a46}
#lbBox .bh #lbDrive.on{background:#2563eb;border-radius:6px}
#lbBox .hint{margin-top:10px;font-size:11.5px;color:#8a96a3;line-height:1.45}
#lbBox .hint b{color:#c6d0da;font-weight:600}
#demoBar{display:flex;align-items:center;gap:10px;flex-wrap:wrap;padding:7px 22px;background:#123a44;color:#d7f0f4;font:13px/1.3 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
#demoBar b{color:#fff}
#demoBar .sp{flex:1}
#demoBar button{background:#1d5562;border:1px solid #2f7586;color:#e6f6f9;border-radius:7px;padding:4px 10px;font:inherit;cursor:pointer}
#demoBar button:hover{background:#246676}
#demoBar .dshort,#demoBar #dbMore{display:none}
@media (max-width:700px){
  #demoBar{padding:5px 10px;font-size:12px;gap:6px}
  #demoBar .dlong{display:none}#demoBar .dshort{display:inline}#demoBar #dbMore{display:inline-block}
  #demoBar:not(.open)>button:not(#dbMore){display:none}
  #demoBar.open>button{flex:1 1 40%}
  #lbBox{bottom:calc(64px + env(safe-area-inset-bottom))!important;max-height:calc(100vh - 150px);overflow:auto}
  #lbBox.min{right:auto!important;left:8px;width:auto!important}
  #lbBox.min .bh>span:not(.sp),#lbBox.min #lbDrive,#lbBox.min #lbMute{display:none}
  #lbBox.min .bh{padding:6px 6px 6px 12px}
}
@media (max-width:720px){
  #demoBar{padding:6px 10px;font-size:12px}
  #lbBox{right:8px;bottom:8px;width:calc(100vw - 16px)}
  #lbBox .hint{max-height:40vh;overflow:auto}
}
#lbWelcome{position:fixed;inset:0;z-index:120;background:rgba(5,8,12,.72);display:flex;align-items:center;justify-content:center;padding:16px;font:14px/1.5 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
#lbWelcome .wbox{background:#151a21;color:#d6dde6;border:1px solid #2c3540;border-radius:14px;max-width:760px;width:100%;max-height:calc(100vh - 32px);overflow:auto;padding:24px 26px;box-shadow:0 18px 50px rgba(0,0,0,.5)}
#lbWelcome h2{margin:0 0 8px;font-size:22px;color:#f1f5f9}
#lbWelcome .lead{margin:0 0 16px;font-size:15px;color:#c6d0da}
#lbWelcome .lead b{color:#fff}
#lbWelcome .wgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:14px 22px}
#lbWelcome h3{margin:0 0 4px;font-size:14px;color:#7cc4ff}
#lbWelcome p{margin:0;font-size:13.5px}
#lbWelcome .wbtn{display:flex;gap:10px;justify-content:flex-end;flex-wrap:wrap;margin-top:20px}
#lbWelcome button{font:inherit;padding:9px 16px;border-radius:8px;border:1px solid #2c3540;background:#1d232b;color:#e6edf3;cursor:pointer;min-height:42px}
#lbWelcome button.pri{background:#2f7cf6;border-color:#2f7cf6;color:#fff;font-weight:600}
@media (max-width:600px){#lbWelcome .wbox{padding:18px}#lbWelcome h2{font-size:19px}}`;
  const st = document.createElement('style'); st.textContent = css; document.head.appendChild(st);
  const K = 'logbook-demo-1.box';
  const ui = (() => { try { return JSON.parse(localStorage.getItem(K) || '{}'); } catch (e) { return {}; } })();
  const saveUi = () => { try { localStorage.setItem(K, JSON.stringify(ui)); } catch (e) {} };

  function build() {
    const bar = document.createElement('div'); bar.id = 'demoBar';
    const blank = !!D.ST.blank, fac = !!D.FACTORY;
    bar.innerHTML = fac ? '<span class="dlong"><b>Life Logger demo</b> · <b>a new box</b>, straight out of its box: set it up as you would a real one</span><span class="dshort"><b>Life Logger demo</b> · new box</span><button id="dbMore" aria-expanded="false">Demo ▾</button>' +
      '<button id="dbAbout">About this demo</button><button id="dbBox">Show the physical box</button><button id="dbNew">Start over</button><button id="dbDemo">Back to the demo</button>' : ((blank
      ? '<span><b>Life Logger demo</b> · <b>empty box</b>: this is how a new Life Logger starts · <b>Reset demo</b> brings the made-up data back</span>'
      : '<span class="dlong"><b>Life Logger demo</b> · this website stands in for the physical box · made-up data · your changes stay in this browser</span>') + '<span class="dshort"><b>Life Logger demo</b> · made-up data</span><span class="sp"></span><button id="dbMore" aria-expanded="false">Demo ▾</button>' +
      '<button id="dbAbout">About this demo</button><button id="dbBox">Show the physical box</button><button id="dbTour">Take the tour</button>' +
      (blank ? '' : '<button id="dbClear">Clear all fake data</button>') + '<button id="dbNew" title="A box straight out of its box: first setup, no data, no clock">Start a new box</button><button id="dbReset">Reset demo</button>');
    document.body.insertBefore(bar, document.body.firstChild);
    bar.querySelector('#dbMore').onclick = () => { const o = bar.classList.toggle('open'); bar.querySelector('#dbMore').setAttribute('aria-expanded', o); };
    const box = document.createElement('div'); box.id = 'lbBox';
    box.innerHTML = '<div class="bh" id="lbHead"><b>THE PHYSICAL BOX</b><span style="color:#8a96a3;font-size:11.5px">simulated</span><span class="sp"></span><button id="lbDrive" title="Simulate the box moving: MAP shows a compass with the course" aria-pressed="false">🧭</button><button id="lbHelp" title="How to use the box">?</button><button id="lbMute" title="Sound on/off">🔊</button><button id="lbMin" title="Hide">–</button></div>' +
      // drawn to the scale of the real lid (700 x 462 as modelled): BACK low-left, SELECT up-right, the screen top right; the knob comes out of the right-hand side, 79 % of the way down
      '<div class="bb"><div class="lidfit" id="lbFit"><div class="lidsc" id="lbSc"><div class="lid"><i class="scw" style="left:8px;top:8px"></i><i class="scw" style="left:348px;top:8px"></i><i class="scw" style="left:690px;top:8px"></i><i class="scw" style="left:8px;top:453px"></i><i class="scw" style="left:348px;top:453px"></i><i class="scw" style="left:690px;top:453px"></i><i class="scw sc2" style="left:431px;top:49px"></i><i class="scw sc2" style="left:642px;top:49px"></i><i class="scw sc2" style="left:431px;top:327px"></i><i class="scw sc2" style="left:642px;top:327px"></i>' +
      '<div class="cc lb2"><button id="lbBtn2" aria-label="SELECT button"></button><span>select</span><button class="hold" id="lbHold2" aria-pressed="false" title="Keep SELECT pressed (tap again to let go)">hold</button></div>' +
      '<div class="cc lb1"><button id="lbBtn" aria-label="BACK button"></button><span>back</span><button class="hold" id="lbHold1" aria-pressed="false" title="Keep BACK pressed (tap again to let go)">hold</button></div>' +
      '<div class="ind" id="lbInd0w" style="left:300px;top:300px"><i id="lbInd0"></i>1</div><div class="ind" id="lbInd1w" style="left:356px;top:300px"><i id="lbInd1"></i>2</div>' +
      '<div class="bez"><canvas id="lbScr" width="128" height="128"></canvas></div></div>' +
      '<div class="cc side"><svg id="lbKnob" viewBox="0 0 100 100" tabindex="0" role="slider" aria-label="Knob: turn it, or press it in (ACTION)"><title>Drag around or scroll to turn. Click to press it in (ACTION); hold it in for the quick menu.</title></svg><span class="kl"><b>knob</b><br>drag: turn<br>click: ACTION<br>hold: menu</span></div></div></div>' +

      '<button id="lbHintBtn" class="hintbtn" aria-expanded="false">Show instructions</button><div class="hint" id="lbHint"><b>Knob:</b> drag around it or scroll to turn it, one row per click. <b>Click</b> it to press it in: that is ACTION, which runs your shortcut. <b>Hold it in</b> (press and keep the mouse or finger still) for the quick menu; turn to a row and click the knob or SELECT to run it, BACK to close it.<br>' +
      '<b>SELECT</b> (upper right) opens, ticks or starts what is highlighted; hold it 1.5 s to turn the screen off.<br>' +
      '<b>BACK</b> (lower left) goes back; hold it and turn the knob for brightness. Holding BACK and SELECT 3 s runs a third shortcut.<br>' +
      '<b>hold</b> beside each button keeps it pressed (tap again to let go), so you can turn the knob with it held.<br>' +
      '<b>🧭</b> makes the box move: the GPS wakes and MAP shows the course. <b>🔊</b> turns the sound on or off; a vibration shakes the lid. <b>?</b> opens these instructions.<br>' +
      '<b>Any button or the knob</b> stops a timer or alarm. Left alone for ' + (D.ST.cfg.scrOffMin ?? 5) + ' min the screen sleeps and the buttons glow dim; any turn or press wakes it.</div></div>';
    document.body.appendChild(box);
    const setMin = m => { box.classList.toggle('min', m); ui.min = m; saveUi(); document.getElementById('dbBox').textContent = m ? 'Show the physical box' : 'Hide the physical box'; };
    document.getElementById('lbHead').addEventListener('click', e => { if (e.target.id === 'lbMute' || e.target.id === 'lbHelp' || e.target.id === 'lbDrive') return; setMin(!box.classList.contains('min')); });
    const drv = document.getElementById('lbDrive');
    const setDrive = on => { driving = on; drv.setAttribute('aria-pressed', on); drv.classList.toggle('on', on); drv.title = on ? 'Moving (simulated): click to stop' : 'Simulate the box moving: MAP shows a compass with the course'; };
    drv.onclick = e => { e.stopPropagation(); if (box.classList.contains('min')) setMin(false); setDrive(!driving); if (driving) { if (scrOrder().includes('map')) setScreen('map'); B.msg = 'Moving'; B.msgUntil = Date.now() + 1200; } };
    document.getElementById('dbBox').onclick = () => setMin(!box.classList.contains('min'));
    document.getElementById('dbNew').onclick = () => { if (!fac || confirm('Start the new box over? What you set up on it is deleted.')) D.newBox(); };
    if (fac) document.getElementById('dbDemo').onclick = () => D.toDemo();
    if (!fac) document.getElementById('dbReset').onclick = () => { if (confirm(D.ST.blank ? 'Bring back the made-up data? Anything you entered in the empty box is deleted.' : 'Start the demo again from the beginning? Your changes in this demo are deleted.')) D.reset(); };
    if (!blank && !fac) document.getElementById('dbClear').onclick = () => { setMin(true); clearAsk(); };
    if (!fac) document.getElementById('dbTour').onclick = () => { if (typeof window.tourStart === 'function') { setMin(true); window.tourStart(); } };
    const mute = document.getElementById('lbMute');
    const setMute = m => { ui.mute = m; saveUi(); mute.textContent = m ? '🔇' : '🔊'; };
    mute.onclick = e => { e.stopPropagation(); setMute(!ui.mute); };
    setMute(!!ui.mute);
    const hint = document.getElementById('lbHint');
    const hb = document.getElementById('lbHintBtn');
    const setHelp = h => { ui.help = h; saveUi(); hint.style.display = h ? '' : 'none'; hb.textContent = h ? 'Hide instructions' : 'Show instructions'; hb.setAttribute('aria-expanded', h); };
    document.getElementById('lbHelp').onclick = e => { e.stopPropagation(); if (box.classList.contains('min')) setMin(false); setHelp(!ui.help); };
    hb.onclick = () => setHelp(!ui.help);
    setHelp(!!ui.help);   // tucked away until asked for
    // first visit: the welcome screen, then (if wanted) the tour, then the box
    const first = !D.ST.toured;
    const narrow = window.matchMedia('(max-width:720px)').matches;
    setMin(first || narrow ? true : !!ui.min);
    const startTour = () => {
      const end = window.tourEnd;
      if (typeof end === 'function') window.tourEnd = function () { end.apply(this, arguments); window.tourEnd = end; if (!narrow) setMin(false); };
      if (typeof window.tourStart === 'function') window.tourStart(); else setMin(false);
    };
    document.getElementById('dbAbout').onclick = () => welcome(false, startTour, () => {});
    if (first) {
      D.ST.toured = true; D.save();
      setTimeout(() => welcome(true, startTour, () => { if (!narrow) setMin(false); }), 900);
    }
    knobSetup(); btnSetup(); btn2Setup();
    // the lid keeps its real proportions: everything scales together to fit the panel
    const fitLid = () => { const f = document.getElementById('lbFit'), sc = document.getElementById('lbSc'); if (!f || !sc || !f.clientWidth) return; const k = Math.min(1, f.clientWidth / 800); sc.style.setProperty('--hs', Math.max(1, Math.min(2.6, 1.1 / k)).toFixed(2)); sc.style.transform = 'scale(' + k + ')'; f.style.height = Math.ceil(462 * k) + 'px'; };
    window.addEventListener('resize', fitLid); new ResizeObserver(fitLid).observe(document.getElementById('lbFit')); fitLid();
  }

  // ---------- clear all fake data: explain first, then empty the demo ----------
  function clearAsk() {
    const old = document.getElementById('lbWelcome'); if (old) old.remove();
    const w = document.createElement('div'); w.id = 'lbWelcome';
    w.innerHTML = '<div class="wbox" role="dialog" aria-modal="true" aria-labelledby="lbCT" style="max-width:640px">' +
      '<h2 id="lbCT">Clear all fake data?</h2>' +
      '<p class="lead">This empties the demo so you see Life Logger the way a <b>brand-new box</b> looks on day one: set up and ready, with nothing in it yet.</p>' +
      '<div class="wgrid">' +
      '<div><h3>What gets cleared</h3><p>Every made-up record: work orders, routines, the checklist and its archive, templates, notes and their archive, calendar events, assets, purchases, money and accounts, documents and expiry dates, attachments, climate history and GPS tracks. Anything you added or changed in the demo goes too.</p></div>' +
      '<div><h3>What stays</h3><p>The box itself and its settings: hotspot name, screen, sound and so on, as if you had just finished setting it up. No climate sensors are paired yet, so there are no temperatures.</p></div>' +
      '<div><h3>Getting it back</h3><p><b>Reset demo</b> in the bar at the top brings all the made-up data back, any time.</p></div>' +
      '<div><h3>Private</h3><p>Nothing leaves this browser, either way.</p></div>' +
      '</div><div class="wbtn"><button id="lbCNo">Keep the fake data</button><button id="lbCYes" class="pri" style="background:#d64545;border-color:#d64545">Clear all fake data</button></div></div>';
    document.body.appendChild(w);
    const close = () => { w.remove(); document.removeEventListener('keydown', k); };
    function k(e) { if (e.key === 'Escape') close(); }
    document.addEventListener('keydown', k);
    document.getElementById('lbCNo').onclick = close;
    document.getElementById('lbCYes').onclick = () => { close(); D.clearAll(); };
    w.addEventListener('click', e => { if (e.target === w) close(); });
    document.getElementById('lbCNo').focus();
  }

  // ---------- welcome: what this demo is, and what the real thing is ----------
  function welcome(first, onTour, onLook) {
    const old = document.getElementById('lbWelcome'); if (old) old.remove();
    const w = document.createElement('div'); w.id = 'lbWelcome';
    w.innerHTML = '<div class="wbox" role="dialog" aria-modal="true" aria-labelledby="lbWT">' +
      '<h2 id="lbWT">Welcome to the Life Logger demo</h2>' +
      '<p class="lead">This is a copy of Life Logger running on a website, so you can try it without the hardware. <b>The real Life Logger is not a website.</b></p>' +
      '<div class="wgrid">' +
      '<div><h3>A physical box</h3><p>Life Logger is a small battery-powered box with its own screen, two buttons and a knob. It makes its own Wi-Fi hotspot and serves this dashboard to your phone or laptop over that hotspot. Join the hotspot, open a browser, and this is what you see.</p></div>' +
      '<div><h3>Standalone</h3><p>No account, no cloud, no app, no subscription, no outside servers. Work orders, checklists, money and everything else are stored on the box itself and its SD card. It works with no internet at all. If you give it your home Wi-Fi, it uses that only to set its clock and to be reachable at http://lifelogger.local when it\'s in range of your Wi-Fi, so you don\'t have to connect directly to the box when you\'re at home.</p></div>' +
      '<div><h3>Private</h3><p>On its own hotspot, nobody can see your data without the hotspot password. On your home Wi-Fi, anyone on that network can open it, like any other device there. The only outside request is the street map on the Map tab, made by your own phone or laptop when it has internet; without internet, the track is drawn on a plain grid.</p></div>' +
      '<div><h3>In this demo</h3><p>Everything is made up. Your changes stay in this browser only and nobody else sees them. The panel in the corner simulates the physical box\'s screen, buttons and knob, laid out as on the real box. <b>Clear all fake data</b> shows an empty box, the way a new one starts; <b>Reset demo</b> brings the made-up data back.</p></div>' +
      '</div><div class="wbtn"><button id="lbWLook">' + (first ? 'Look around on my own' : 'Close') + '</button><button id="lbWTour" class="pri">Take the tour</button></div></div>';
    document.body.appendChild(w);
    const close = () => w.remove();
    document.getElementById('lbWLook').onclick = () => { close(); onLook(); };
    document.getElementById('lbWTour').onclick = () => { close(); onTour(); };
    w.addEventListener('click', e => { if (e.target === w) { close(); onLook(); } });
    document.addEventListener('keydown', function k(e) { if (e.key === 'Escape' && document.getElementById('lbWelcome')) { close(); onLook(); document.removeEventListener('keydown', k); } });
    document.getElementById('lbWTour').focus();
  }

  // ---------- knob: a rotary encoder, 24 clicks a turn, with a push switch ----------
  function knobDraw() {
    const r = Math.PI / 180, svg = document.getElementById('lbKnob');
    let ticks = '';
    for (let i = 0; i < 24; i++) { const t = i * 15 * r; ticks += '<line x1="' + (50 + 44 * Math.cos(t)).toFixed(1) + '" y1="' + (50 + 44 * Math.sin(t)).toFixed(1) + '" x2="' + (50 + 48 * Math.cos(t)).toFixed(1) + '" y2="' + (50 + 48 * Math.sin(t)).toFixed(1) + '" stroke="#4a545e" stroke-width="2"/>'; }
    const p = (B.angle - 90) * r;
    svg.innerHTML = ticks + '<circle cx="50" cy="50" r="34" fill="' + (B.kDown ? '#252b32' : '#2a3139') + '" stroke="#48525d" stroke-width="2"/><circle cx="50" cy="50" r="' + (B.kDown ? 24 : 26) + '" fill="#323a43"/>' +
      '<text x="50" y="' + (B.kDown ? 74 : 73) + '" text-anchor="middle" font-size="8" font-weight="700" letter-spacing="1" fill="#8b96a3" font-family="system-ui,sans-serif">PUSH</text>' +
      '<line x1="' + (50 + 12 * Math.cos(p)).toFixed(1) + '" y1="' + (50 + 12 * Math.sin(p)).toFixed(1) + '" x2="' + (50 + 30 * Math.cos(p)).toFixed(1) + '" y2="' + (50 + 30 * Math.sin(p)).toFixed(1) + '" stroke="#e8eef4" stroke-width="4" stroke-linecap="round"/>';
  }
  function turn(clicks) {
    if (!clicks) return;
    B.angle = (B.angle + clicks * CLICK_DEG) % 360; knobDraw();
    if (QM.on) { QM.sel = Math.max(0, Math.min(qmRows().length - 1, QM.sel + clicks)); B.lastInput = Date.now(); tick(true); return; }
    if (touched()) { tick(true); return; }                        // that click woke the screen (or stopped an alarm)
    if (B.down && !B.swallow) {                                           // holding BACK: brightness
      B.bright = true; B.held = true;
      D.ST.cfg.contrast = Math.max(1, Math.min(255, (D.ST.cfg.contrast || 128) + clicks * 8));
      B.msg = 'Bright ' + Math.round(D.ST.cfg.contrast * 100 / 255) + '%'; B.msgUntil = Date.now() + 1500;
    } else moveSel(clicks);
    tick(true);
  }
  // ACTION is the knob's push: a press runs the shortcut (or the quick menu's row); a hold opens the quick menu, which stays open
  function kPress() {
    if (B.kDown) return; B.kDown = true; B.kT0 = Date.now(); B.kLong = false; B.kTurned = false;
    const wasOff = !B.on && !D.alarmActive() && !tripBeeping();
    B.kSwallow = touched(); B.kWoke = B.kSwallow && wasOff; knobDraw(); tick(true);
  }
  function kRelease() {
    if (!B.kDown) return; B.kDown = false; knobDraw();
    if (!B.kSwallow && !B.kLong && !B.kTurned) { if (QM.on) qmRun(); else shortcut(btnCfg().p); }
    B.kSwallow = false; tick(true);
  }
  function knobSetup() {
    const svg = document.getElementById('lbKnob');
    let drag = false, lastA = 0, acc = 0;
    const ang = e => { const b = svg.getBoundingClientRect(); return Math.atan2(e.clientY - (b.top + b.height / 2), e.clientX - (b.left + b.width / 2)) * 180 / Math.PI; };
    svg.addEventListener('pointerdown', e => { drag = true; acc = 0; lastA = ang(e); svg.setPointerCapture(e.pointerId); audioInit(); kPress(); e.preventDefault(); });
    svg.addEventListener('pointermove', e => {
      if (!drag) return; let a = ang(e), d = a - lastA; if (d > 180) d -= 360; if (d < -180) d += 360; lastA = a; acc += d;
      const c = Math.trunc(acc / CLICK_DEG); if (c) { acc -= c * CLICK_DEG; B.kTurned = true; turn(c); }   // turning while pressed: a turn, not a press
    });
    const up = () => { if (drag) { drag = false; kRelease(); } };
    svg.addEventListener('pointerup', up); svg.addEventListener('pointercancel', up);
    svg.addEventListener('wheel', e => { e.preventDefault(); audioInit(); turn(e.deltaY < 0 ? -1 : 1); }, { passive: false });
    svg.addEventListener('keydown', e => {
      if (e.key === 'ArrowRight' || e.key === 'ArrowDown') { turn(1); e.preventDefault(); }
      if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') { turn(-1); e.preventDefault(); }
      if ((e.key === ' ' || e.key === 'Enter') && !e.repeat) { kPress(); e.preventDefault(); }
    });
    svg.addEventListener('keyup', e => { if (e.key === ' ' || e.key === 'Enter') { kRelease(); e.preventDefault(); } });
    knobDraw();
  }
  // ---------- button ----------
  function btnSetup() {
    const b = document.getElementById('lbBtn');
    const down = e => {
      if (B.down) return; B.down = true; B.t0 = Date.now(); B.held = false; B.paired = B2.down; if (B2.down) B2.paired = true; b.classList.add('pr'); audioInit();
      B.swallow = touched();                                                        // stops an alarm, or only wakes the screen
      if (e) e.preventDefault(); tick(true);
    };
    const up = () => {
      if (!B.down) return; B.down = false; b.classList.remove('pr');
      if (B.swallow) B.swallow = false;
      else if (B.bright) { B.bright = false; D.save(); B.msg = 'Saved'; B.msgUntil = Date.now() + 800; }
      else if (!B.held && !B.paired && !B.both && B.on) { B.src = 1; back(); B.src = 0; }
      if (!B2.down) B.both = false;
      tick(true);
    };
    holdWire(b, 'lbHold1', down, up);
  }
  // The demo's "hold" toggle beside each button keeps it pressed, so one mouse or finger can hold a button and
  // turn the knob (BACK + the knob for brightness, BACK + SELECT for 3 s). Tap it again to let go. While it is on,
  // the button itself ignores clicks.
  function holdWire(b, id, down, up) {
    const h = document.getElementById(id); let on = false;
    const set = v => { on = v; h.classList.toggle('on', v); h.setAttribute('aria-pressed', v); h.textContent = v ? 'holding' : 'hold'; };
    h.addEventListener('click', e => { e.preventDefault(); if (on) { set(false); up(); } else { set(true); down(); } });
    const pd = e => { if (!on) down(e); else e.preventDefault(); }, pu = () => { if (!on) up(); };
    b.addEventListener('pointerdown', pd); b.addEventListener('pointerup', pu); b.addEventListener('pointerleave', pu); b.addEventListener('pointercancel', pu);
    b.addEventListener('keydown', e => { if ((e.key === ' ' || e.key === 'Enter') && !e.repeat) { if (!on) down(); e.preventDefault(); } });
    b.addEventListener('keyup', e => { if (e.key === ' ' || e.key === 'Enter') { if (!on) up(); e.preventDefault(); } });
    b.addEventListener('contextmenu', e => e.preventDefault());
  }
  // ---------- SELECT button: a press opens / ticks / starts what is highlighted; a hold turns the screen off ----------
  const HOLD_MS = 600;
  const B2 = {down: false, t0: 0, held: false, swallow: false, paired: false};
  function btn2Setup() {
    const b = document.getElementById('lbBtn2');
    const down = e => {
      if (B2.down) return; Object.assign(B2, {down: true, t0: Date.now(), held: false, paired: B.down}); if (B.down) B.paired = true; b.classList.add('pr'); audioInit();
      B2.swallow = touched(); if (e) e.preventDefault(); tick(true);
    };
    const up = () => {
      if (!B2.down) return; B2.down = false; b.classList.remove('pr');
      if (!B2.swallow && !B2.held && !B2.paired && !B.both && B.on) { B.src = 2; if (QM.on) qmRun(); else press(); B.src = 0; }
      B2.swallow = false;
      if (!B.down) B.both = false;
      tick(true);
    };
    holdWire(b, 'lbHold2', down, up);
  }
  // ---------- buzzer ----------
  let ac = null, osc = null, gain = null;
  function audioInit() {
    if (ac) { if (ac.state === 'suspended') ac.resume(); return; }
    try {
      ac = new (window.AudioContext || window.webkitAudioContext)(); osc = ac.createOscillator(); gain = ac.createGain();
      osc.type = 'square'; osc.frequency.value = 2650; gain.gain.value = 0; osc.connect(gain); gain.connect(ac.destination); osc.start();
    } catch (e) { ac = null; }
  }
  document.addEventListener('pointerdown', audioInit, { once: true, capture: true });
  let lastHz = -1;
  function buz(hz) {
    if (ui.mute) hz = 0;
    if (!ac || hz === lastHz) return; lastHz = hz;
    const t = ac.currentTime;
    if (hz) osc.frequency.setValueAtTime(hz, t);
    gain.gain.setTargetAtTime(hz ? 0.035 : 0, t, 0.004);
  }
  function soundHz(now) {
    const A = D.AL;
    if (A.bnStage === 2) { const s = now - A.bnT0, per = s < 30000 ? 2000 : s < 60000 ? 1000 : 400, on = s < 60000 ? 300 : 250, ph = s % per; return ph < on ? (s >= 60000 && Math.floor(s / per) % 2 ? 3100 : 2650) : 0; }
    if (D.timerRinging()) { const ph = now % 1500; return (ph < 120 || (ph >= 240 && ph < 360) || (ph >= 480 && ph < 600)) ? 2650 : 0; }
    if (A.find) { const ph = now % 1200; return ph < 150 ? 2650 : ph < 300 ? 3200 : 0; }
    if (tripBeeping()) { const ph = now % 3000; return ph < 120 || (ph >= 240 && ph < 360) ? 2650 : 0; }
    if (now < (B.beepUntil || 0)) return 2650;   // the alert check
    return 0;
  }
  // ---------- loop ----------
  let lastDraw = 0, lastVer = -1, lastSec = -1;
  const cv = () => document.getElementById('lbScr');
  function paint() {
    const c = cv(); if (!c) return;
    const g = c.getContext('2d'), im = g.createImageData(W, W);
    for (let i = 0; i < W * W; i++) {
      const v = awake() ? px[i] : 0, k = v ? Math.round(30 + 225 * v / 15) : 0;
      im.data[i * 4] = k; im.data[i * 4 + 1] = k; im.data[i * 4 + 2] = k; im.data[i * 4 + 3] = 255;
    }
    g.putImageData(im, 0, 0);
    c.style.filter = 'brightness(' + (0.35 + 0.75 * (D.ST.cfg.contrast || 128) / 255).toFixed(2) + ')';
  }
  // ---------- indicator lights (Settings › Indicator lights): the first true state of each light sets its pattern ----------
  const IND_COL = ['#3b82f6', '#ff4d4f'];   // light 1 blue, light 2 red
  const IND_LBL = {job: 'Job timer running', jpause: 'Job paused', trip: 'Work trip on', gpson: 'GPS on', fix: 'GPS has a fix', nofix: 'GPS on, no fix yet', batt: 'Battery under 20 %', chg: 'Charging or on USB', sd: 'No SD card', clk: 'Clock not set', ap: 'Hotspot on', wifi: 'On the house Wi-Fi', nowifi: 'Away from the house Wi-Fi'};
  let indAt = 0, indOn = {};
  function indTick(now, glow) {
    const pins = D.hwPins().ind || ['', ''], C = D.indCfg();
    if (now - indAt >= 1000) {   // the states, once a second, as the box does
      indAt = now; const st = D.status(), j = jobState();
      indOn = {job: !!j.run, jpause: !!j.pz, trip: WT.on, gpson: !st.gps.asleep, fix: !st.gps.asleep && st.gps.fix, nofix: !st.gps.asleep && !st.gps.fix, batt: !st.batt.chg && st.batt.pct < 20, chg: st.batt.chg,
        sd: !st.sdOK, clk: !st.timeValid, ap: st.ap.up, wifi: !!st.sta.up, nowifi: !st.sta.up};
    }
    const cur = [0, 1].map(i => {
      const w = document.getElementById('lbInd' + i + 'w'), e = document.getElementById('lbInd' + i), c = Object.assign({r: [], dim: true, slp: false}, C[i] || {});
      if (!e) return -1;
      w.classList.toggle('nopin', !pins[i]); e.style.setProperty('--ic', IND_COL[i]);
      if (!pins[i]) { e.className = ''; w.title = 'Indicator ' + (i + 1) + ': no pin chosen (Settings › Pin mapping)'; return -2; }
      const k = c.r.findIndex(r => indOn[r[0]]), p = k >= 0 ? c.r[k][1] : '';
      let lit = p === 's' || (p === 'b' && Math.floor(now / 500) % 2 === 0) || (p === 'f' && Math.floor(now / 125) % 2 === 0) || (p === 'p' && now % 2000 < 120);
      let lvl = !lit ? '' : B.on ? 'lit' : c.slp ? '' : c.dim ? 'faint' : 'lit';
      if (D.BOX.ltest) lvl = Math.floor((now - D.BOX.ltest) / 1000) === 2 + i ? 'lit' : '';
      e.className = lvl; e.style.filter = lvl === 'lit' && c.dim ? glow : '';
      w.title = 'Indicator ' + (i + 1) + ': ' + (k >= 0 ? IND_LBL[c.r[k][0]] : c.r.length ? 'nothing to show' : 'not set up (Settings › Indicator lights)');
      return k;
    });
    D.BOX.ind = cur;
  }
  function tick(force) {
    const now = Date.now();
    D.alarmsTick(); tripWatch();
    if (B.kDown && (!B.kSwallow || B.kWoke) && !B.kLong && !B.kTurned && !QM.on && !B.down && now - B.kT0 >= HOLD_MS) { B.kLong = true; Object.assign(QM, {on: true, sel: 0, top: 0}); B.lastInput = now; force = true; }   // hold the knob in: the quick menu
    if (B2.down && !B2.held && !B2.swallow && !B.down && now - B2.t0 >= OFF_MS) { B2.held = true; QM.on = false; B.on = false; force = true; }   // hold SELECT: screen off
    if (QM.on && B.down && !B.paired) { QM.on = false; B.paired = true; force = true; }   // BACK closes it, running nothing
    if (!B.on) QM.on = false;
    gpsSync();
    if (B.down && B2.down && !B.both && now - Math.max(B.t0, B2.t0) >= 3000) { B.both = true; B.held = B2.held = true; B.swallow = B2.swallow = false; B.src = 3; shortcut(btnCfg().b); B.src = 0; }   // hold both 3 s
    if (D.BOX.want != null) { B.on = D.BOX.want; B.lastInput = now; D.BOX.want = null; }                                 // from the dashboard
    const alarm = D.alarmActive() || tripBeeping();
    if (alarm) { B.on = true; B.lastInput = now; }
    else if (B.on && offMin() > 0 && now - B.lastInput > offMin() * 60000) B.on = false;                                  // sleeps when left alone
    D.BOX.screenOn = B.on;
    // button lights: the screen's brightness, dim while it sleeps, off if Settings says so; only the button used blinks; both flash for an alarm
    const lightsOn = D.ST.cfg.btnLed !== false, base = !lightsOn ? '' : B.on ? 'led' : 'dimled', L = [base, base];
    if (lightsOn && alarm) L[0] = L[1] = Math.floor(now / (D.AL.bnStage === 1 ? 500 : 150)) % 2 === 1 ? 'led' : '';
    else if (lightsOn && now < B.blinkUntil) { const v = Math.floor(now / 100) % 2 === 1 ? 'led' : ''; if (B.blinkMask & 1) L[0] = v; if (B.blinkMask & 2) L[1] = v; }
    const glow = 'brightness(' + (0.35 + 0.75 * (D.ST.cfg.contrast || 128) / 255).toFixed(2) + ')';
    if (D.BOX.ltest && now - D.BOX.ltest < 4000) { const k = Math.floor((now - D.BOX.ltest) / 1000); L[0] = k === 0 ? 'led' : ''; L[1] = k === 1 ? 'led' : ''; } else D.BOX.ltest = 0;
    ['lbBtn', 'lbBtn2'].forEach((id, i) => { const b = document.getElementById(id); if (b) { b.classList.toggle('led', L[i] === 'led'); b.classList.toggle('dimled', L[i] === 'dimled'); b.style.filter = L[i] ? glow : ''; } });
    indTick(now, glow);
    if (D.BOX.test) { B.beepUntil = D.BOX.test + 700; D.BOX.test = 0; }                                            // Test, from the dashboard
    const hz = soundHz(now), am = alertMode();
    const finding = D.AL.find && D.AL.bnStage !== 2 && !D.timerRinging();   // find my box always sounds
    buz(am === 1 && !finding ? 0 : hz);
    const lid = document.querySelector('#lbBox .lid'); if (lid) lid.classList.toggle('vib', !!hz && am !== 0);   // the vibration motor
    const sec = Math.floor(now / 1000);
    if (D.ST.docVer !== lastVer) { lastVer = D.ST.docVer; force = true; }
    if (force || sec !== lastSec || now - lastDraw > (alarm ? 250 : mqOn ? 30 : 1000)) { lastDraw = now; lastSec = sec; if (awake()) draw(); paint(); }
  }
  function start() { build(); tick(true); setInterval(() => tick(false), 30); }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start); else start();
})();
