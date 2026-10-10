'use strict';
// ESP32APRS web UI: plain JS, no libraries. A page renders into #main and may return a cleanup
// function (stop its timers) that runs when the user leaves it.
const $ = (s) => document.querySelector(s);
const esc = (v) => String(v ?? '').replace(/[&<>"']/g, (c) => '&#' + c.charCodeAt(0) + ';');
const ICONS = 'https://aprs.p00lack.cc/symbols/icons/';

async function api(path, opts) {
  const r = await fetch(path, opts);
  if (!r.ok) throw new Error(path + ': HTTP ' + r.status);
  return r.json();
}

function toast(msg) {
  const t = $('#toast');
  t.textContent = msg;
  t.hidden = false;
  clearTimeout(toast.t);
  toast.t = setTimeout(() => (t.hidden = true), 4000);
}

function setConn(ok) {
  $('#conn').className = 'dot ' + (ok ? 'on' : 'off');
  $('#conn').title = ok ? 'Connected' : 'No answer from the device';
}

const fmtUptime = (s) => {
  const d = Math.floor(s / 86400), h = Math.floor(s / 3600) % 24, m = Math.floor(s / 60) % 60;
  return (d ? d + 'd ' : '') + h + 'h ' + String(m).padStart(2, '0') + 'm';
};
const kb = (b) => (b / 1024).toFixed(1) + ' KB';
const rows = (list) => '<table class="kv">' + list.filter(Boolean).map(([k, v]) => '<tr><td>' + k + '</td><td>' + v + '</td></tr>').join('') + '</table>';
const card = (title, body, cls) => '<div class="card' + (cls ? ' ' + cls : '') + '"><h2>' + title + '</h2>' + body + '</div>';
const bar = (used, total) => '<div class="bar"><i style="width:' + Math.min(100, (100 * used) / (total || 1)).toFixed(0) + '%"></i></div>';
const tag = (name, on) => '<span class="tag' + (on ? ' on' : '') + '">' + name + '</span>';
const tzLabel = (tz) => 'UTC' + (tz >= 0 ? '+' : '') + tz;
const dBV = (mV) => (mV > 0 ? 20 * Math.log10(mV / 1000) : -Infinity);
// Repeats fn every ms (after each run ends) until the returned stop function is called. Live data is
// pulled this way: the firmware never pushes into the web server from its other tasks (see webfeed.h).
function poller(fn, ms, onState) {
  let t = 0, stopped = false;
  const run = async () => {
    if (stopped) return;
    let ok = true;
    try { await fn(); } catch (e) { ok = false; }
    if (stopped) return; // the page is gone: do not touch its elements
    if (onState) onState(ok);
    t = setTimeout(run, ms);
  };
  run();
  return () => { stopped = true; clearTimeout(t); };
}

// Scrolling text log with a line limit, so a long session does not eat the browser's memory
function makeLog(el, max) {
  const lines = [];
  return {
    add(text) {
      lines.push(text);
      if (lines.length > max) lines.splice(0, lines.length - max);
      const stick = el.scrollTop + el.clientHeight >= el.scrollHeight - 20;
      el.textContent = lines.join('\n');
      if (stick) el.scrollTop = el.scrollHeight;
    },
    clear() { lines.length = 0; el.textContent = ''; },
    text: () => lines.join('\n'),
  };
}

// ---- Dashboard: everything the old Dashboard tab showed (sysinfo, sidebar, radio, WiFi, BT, last heard) ----
const LH_COLS = [
  ['time', 'Time'], ['icon', ''], ['callsign', 'Callsign'], ['path', 'Via last path'],
  ['dx', 'DX'], ['packet', 'Packets'], ['audio', 'Audio'],
];
const LH_SORT = {
  time: (r) => { const m = /^(\d+) (\d+):(\d+):(\d+)$/.exec(r.time); return m ? +m[1] * 86400 + +m[2] * 3600 + +m[3] * 60 + +m[4] : 0; },
  callsign: (r) => r.callsign,
  path: (r) => r.path,
  dx: (r) => { const v = parseFloat(r.dx); return isNaN(v) ? null : v; },
  packet: (r) => parseInt(r.packet, 10) || 0,
  audio: (r) => { const v = parseFloat(r.audio); return isNaN(v) ? null : v; },
};

async function dashboard(main) {
  // Fixed order, most watched first: status strip, last heard, then detail cards in even rows
  // Station panel: status strip on top, last heard as the main column, details stacked on the side
  main.innerHTML = '<h1>Dashboard</h1><div class="strip" id="strip"></div>' +
    '<div class="dash"><div class="card lhcard"><h2>Last heard <a href="#terminal">[RAW]</a></h2><div class="scroll lhscroll"><table class="list" id="lh"></table></div></div>' +
    '<aside class="side"><div id="ov"></div><div class="actions"><button class="btn danger" id="reboot">Reboot</button></div></aside></div>';
  $('#reboot').onclick = async () => {
    if (!confirm('Reboot the device?')) return;
    try { await api('/api/reboot', { method: 'POST' }); toast('Rebooting...'); } catch (e) { toast(e.message); }
  };

  let tz = 0, data = [], sortKey = 'time', sortUp = false;
  const drawLH = () => {
    const get = LH_SORT[sortKey];
    const sorted = data.slice().sort((a, b) => {
      const x = get(a), y = get(b);
      if (x === null) return 1;
      if (y === null) return -1;
      return (x < y ? -1 : x > y ? 1 : 0) * (sortUp ? 1 : -1);
    });
    const head = LH_COLS.map(([k, t]) => {
      const label = k === 'time' ? 'Time (' + tzLabel(tz) + ')' : t;
      return k === 'icon' ? '<th></th>' : '<th data-k="' + k + '">' + label + (k === sortKey ? (sortUp ? ' &#9650;' : ' &#9660;') : '') + '</th>';
    }).join('');
    const body = sorted.map((r) => {
      const icon = /^(\d{2,3}-[12]|dot)\.png$/.test(r.icon) ? '<img src="' + ICONS + r.icon + '" alt="" width="20" height="20" loading="lazy">' : '';
      const audio = r.audio && r.audio !== '-' ? '<span class="ok">' + esc(r.audio) + ' dBV</span>' : '-';
      return '<tr><td>' + esc(r.time) + '</td><td class="ic">' + icon + '</td><td><b>' + esc(r.callsign) + '</b></td><td class="path">' +
        esc(r.path) + '</td><td>' + esc(r.dx || '-') + '</td><td>' + esc(r.packet) + '</td><td>' + audio + '</td></tr>';
    }).join('');
    $('#lh').innerHTML = '<thead><tr>' + head + '</tr></thead><tbody>' +
      (body || '<tr><td colspan="7" class="muted">No stations heard yet</td></tr>') + '</tbody>';
  };
  $('#lh').onclick = (e) => {
    const k = e.target.closest('th')?.dataset.k;
    if (!k) return;
    sortUp = k === sortKey ? !sortUp : k !== 'time';
    sortKey = k;
    drawLH();
  };
  drawLH();

  let lhText = '';
  const stopLH = poller(async () => {
    const r = await fetch('/api/lastheard');
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const txt = await r.text();
    if (txt !== lhText) { lhText = txt; data = JSON.parse(txt); drawLH(); }
  }, 5000);

  const draw = async () => {
    const i = await api('/api/info');
    const w = i.wifi, s = i.stats, r = i.radio, n = i.net, g = i.gps;
    if (i.tz !== tz) { tz = i.tz; drawLH(); }
    $('#call').textContent = i.callsign + (i.ssid ? '-' + i.ssid : '');
    const tile = (label, value, cls) => '<div class="tile"><span>' + label + '</span><b' + (cls ? ' class="' + cls + '"' : '') + '>' + value + '</b></div>';
    const heapPct = Math.round((100 * i.heap) / i.heapSize);
    $('#strip').innerHTML =
      '<div class="tiles">' +
      tile('Uptime', fmtUptime(i.uptime)) +
      tile('RAM free', heapPct + '%', heapPct < 15 ? 'bad' : heapPct < 30 ? 'warn' : '') +
      tile('CPU temp', i.temp !== undefined ? i.temp + '&deg;C' : 'N/A') +
      tile('WiFi', w.sta ? w.rssi + ' dBm' : 'AP only', w.sta ? '' : 'warn') +
      tile('APRS-IS', n.aprsis ? 'connected' : i.modes.igate ? 'down' : 'off', n.aprsis ? 'ok' : i.modes.igate ? 'bad' : '') +
      tile('Packets RX / TX', s.pkt + ' / ' + s.tx) +
      '</div><div class="tags">' +
      tag('IGATE', i.modes.igate) + tag('DIGI', i.modes.digi) + tag('WX', i.modes.wx) + tag('TRACKER', i.modes.tracker) +
      '<span class="gap"></span>' +
      tag('APRS-IS', n.aprsis) + tag('VPN', n.vpn) + tag('PPPoS', n.ppp) + (n.mqtt !== undefined ? tag('MQTT', n.mqtt) : '') +
      tag('FX.25', r.fx25 !== 'NONE') + '</div>';
    $('#ov').innerHTML =
      card('Statistics', rows([
        ['Radio RX', s.rx],
        ['Packet RX', s.pkt],
        ['Packet TX', s.tx],
        ['RF &rarr; INET', s.rf2inet],
        ['INET &rarr; RF', s.inet2rf],
        ['Digi', s.digi + ' (duplicates dropped ' + s.dup + ')'],
        ['Drop / error', s.drop + ' / ' + s.error],
      ])) +
      card('Radio', rows([
        r.rf && ['Freq TX', r.txFreq + ' MHz'],
        r.rf && ['Freq RX', r.rxFreq + ' MHz'],
        r.rf && ['TX power', r.power],
        r.rf && ['Module', r.version ? '<span class="ok">' + esc(r.version) + '</span>' : '<span class="warn">no answer</span>'],
        ['Modem', esc(r.modem)],
        ['FX.25', esc(r.fx25)],
      ])) +
      card('Network', rows([
        ['WiFi mode', w.mode],
        ['Client', w.sta ? '<span class="ok">' + esc(w.ssid) + '</span> ' + esc(w.ip) : '<span class="warn">disconnected</span>'],
        ['RSSI', w.sta ? w.rssi + ' dBm' : '-'],
        ['Access point', esc(w.apIp) + ' (' + w.apClients + ' clients)'],
        ['APRS-IS server', esc(n.aprsHost) + ':' + n.aprsPort],
      ])) +
      card('System', rows([
        ['Firmware', esc(i.version)],
        ['Chip', esc(i.chip) + ' @ ' + i.cpuMhz + ' MHz'],
        ['RAM free', kb(i.heap) + ' / ' + kb(i.heapSize) + ' (min ' + kb(i.heapMin) + ')' + bar(i.heapSize - i.heap, i.heapSize)],
        i.psramSize && ['PSRAM free', kb(i.psram) + ' / ' + kb(i.psramSize)],
        ['Storage', kb(i.fsUsed) + ' / ' + kb(i.fsTotal) + bar(i.fsUsed, i.fsTotal)],
        i.vbat !== undefined && ['Battery', i.vbat + ' V'],
      ])) +
      (g.en ? card('GPS <a href="#gps">[View]</a>', rows(g.lat !== undefined ? [
        ['Lat', g.lat], ['Lon', g.lng], ['Alt', g.alt + ' m'], ['Sat', g.sat],
      ] : [['Fix', '<span class="warn">no fix</span>']])) : '') +
      (i.bt ? card('Bluetooth', rows([
        ['Master', i.bt.master ? '<span class="ok">enabled</span>' : 'disabled'],
        ['Name', esc(i.bt.name)],
        ['Mode', esc(i.bt.mode)],
      ])) : '');
  };
  await draw();
  const timer = setInterval(() => draw().then(() => setConn(true), () => setConn(false)), 10000);
  return () => { clearInterval(timer); stopLH(); };
}

// ---- Terminal: TNC2 monitor of every packet decoded on RF, with the audio level (old /tnc2 page) ----
function terminal(main) {
  main.innerHTML = '<h1>TNC2 terminal</h1>' +
    '<div class="card"><h2>RX audio level</h2><div class="vu"><div class="vu-scale"><i id="vuMark"></i></div>' +
    '<div class="vu-ticks"><span>-40</span><span>-30</span><span>-20</span><span>-10</span><span>0 dBV</span></div>' +
    '<div id="vuText" class="muted">Waiting for packets...</div></div></div>' +
    '<div class="card wide"><h2>Monitor <span id="wsState" class="muted"></span></h2><pre class="log" id="log"></pre></div>' +
    '<div class="actions"><button class="btn" id="clear">Clear</button><button class="btn" id="save">Save as text</button></div>';
  const log = makeLog($('#log'), 1000);
  $('#clear').onclick = () => log.clear();
  $('#save').onclick = () => {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([log.text()], { type: 'text/plain' }));
    a.download = 'tnc2-' + new Date().toISOString().slice(0, 19).replace(/:/g, '') + '.txt';
    a.click();
  };
  let seq = 0;
  return poller(async () => {
    const d = await api('/api/monitor?after=' + seq);
    seq = d.seq;
    d.items.forEach((m) => {
      const mV = m.mv || 0, db = dBV(mV);
      const shown = Math.max(-40, Math.min(0, db));
      $('#vuMark').style.left = ((shown + 40) * 2.5) + '%';
      $('#vuText').textContent = (mV / 1000).toFixed(3) + ' Vrms, ' + (isFinite(db) ? db.toFixed(1) : '-') + ' dBV';
      log.add(new Date(m.t * 1000).toLocaleString() + ' [' + (mV / 1000).toFixed(3) + ' Vrms, ' + (isFinite(db) ? db.toFixed(1) : '-') + ' dBV]\n' + m.raw);
    });
  }, 1000, (ok) => { $('#wsState').textContent = ok ? '(live)' : '(no answer, retrying...)'; });
}

// ---- GPS: live fix and raw NMEA (old /gnss page) ----
function gpsPage(main) {
  main.innerHTML = '<h1>GPS</h1><div class="grid"><div class="card"><h2>GNSS information</h2><div id="fix">' +
    rows([['Status', '<span class="muted">waiting for data from the GPS...</span>']]) + '</div></div></div>' +
    '<div class="card wide"><h2>NMEA <span id="wsState" class="muted"></span></h2><pre class="log" id="log"></pre></div>' +
    '<div class="actions"><button class="btn" id="clear">Clear</button></div>';
  const log = makeLog($('#log'), 500);
  $('#clear').onclick = () => log.clear();
  let seq = 0;
  return poller(async () => {
    const m = await api('/api/gnss?after=' + seq);
    seq = m.seq;
    const t = String(parseInt(m.time, 10) || 0).padStart(8, '0');
    $('#fix').innerHTML = rows([
      ['Enabled', m.en ? '<span class="ok">yes</span>' : 'no'],
      ['Fix', m.valid ? '<span class="ok">valid</span>' : '<span class="warn">no fix</span>'],
      ['Latitude', esc(m.lat)],
      ['Longitude', esc(m.lng)],
      ['Altitude', esc(m.alt) + ' m'],
      ['Speed', esc(m.spd) + ' km/h'],
      ['Course', esc(m.csd) + '&deg;'],
      ['HDOP', esc(m.hdop)],
      ['Satellites', esc(m.sat)],
      ['Time (UTC)', t.slice(0, 2) + ':' + t.slice(2, 4) + ':' + t.slice(4, 6)],
    ]);
    m.lines.forEach((l) => log.add(l));
  }, 2000, (ok) => { $('#wsState').textContent = ok ? '(live)' : '(no answer, retrying...)'; });
}

// ---- Router ----
// Other files (forms.js) add their pages to this object before the first route() runs.
const pages = { dashboard, terminal, gps: gpsPage };
let cleanup = null, current = '', guard = null; // guard(): false keeps the user on a page with unsaved changes
function setGuard(fn) { guard = fn; }
async function route() {
  const name = pages[location.hash.slice(1)] ? location.hash.slice(1) : 'dashboard';
  if (guard && name !== current && !guard()) { history.replaceState(null, '', '#' + current); return; }
  guard = null;
  if (cleanup) { cleanup(); cleanup = null; }
  current = name;
  document.querySelectorAll('nav a').forEach((a) => a.classList.toggle('act', a.dataset.page === name));
  $('#nav').classList.remove('open');
  const main = $('#main');
  try {
    cleanup = (await pages[name](main)) || null;
    setConn(true);
  } catch (e) {
    setConn(false);
    main.innerHTML = '<h1>Error</h1><p class="bad">' + esc(e.message) + '</p>';
  }
}
$('#menu').onclick = () => $('#nav').classList.toggle('open');
window.addEventListener('hashchange', route);
window.addEventListener('beforeunload', (e) => { if (guard && !guard(true)) e.preventDefault(); });
window.addEventListener('DOMContentLoaded', route);
