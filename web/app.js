'use strict';
// ESP32APRS web UI: plain JS, no libraries. Pages are functions that render into #main.
const $ = (s) => document.querySelector(s);
const esc = (v) => String(v ?? '').replace(/[&<>"']/g, (c) => '&#' + c.charCodeAt(0) + ';');

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
const onOff = (v) => (v ? '<span class="ok">enabled</span>' : '<span class="muted">disabled</span>');
const rows = (list) => '<table class="kv">' + list.map(([k, v]) => '<tr><td>' + k + '</td><td>' + v + '</td></tr>').join('') + '</table>';
const card = (title, body) => '<div class="card"><h2>' + title + '</h2>' + body + '</div>';
const bar = (used, total) => '<div class="bar"><i style="width:' + Math.min(100, (100 * used) / (total || 1)).toFixed(0) + '%"></i></div>';

// ---- Overview ----
let cfg = null;
async function overview(main) {
  main.innerHTML = '<h1>Overview</h1><div class="grid" id="ov"></div>' +
    '<div class="actions"><button class="btn danger" id="reboot">Reboot</button></div>';
  $('#reboot').onclick = async () => {
    if (!confirm('Reboot the device?')) return;
    try { await api('/api/reboot', { method: 'POST' }); toast('Rebooting...'); } catch (e) { toast(e.message); }
  };
  if (!cfg) cfg = await api('/api/config');
  const draw = async () => {
    const i = await api('/api/info');
    const w = i.wifi, s = i.stats;
    $('#call').textContent = i.callsign + (i.ssid ? '-' + i.ssid : '');
    $('#ov').innerHTML =
      card('System', rows([
        ['Firmware', esc(i.version)],
        ['Chip', esc(i.chip)],
        ['Uptime', fmtUptime(i.uptime)],
        ['Free heap', kb(i.heap) + ' / ' + kb(i.heapSize) + ' (min ' + kb(i.heapMin) + ')' + bar(i.heapSize - i.heap, i.heapSize)],
        ['Storage', kb(i.fsUsed) + ' / ' + kb(i.fsTotal) + bar(i.fsUsed, i.fsTotal)],
      ])) +
      card('Network', rows([
        ['WiFi client', w.sta ? '<span class="ok">' + esc(w.ssid) + '</span> ' + esc(w.ip) + ' (' + w.rssi + ' dBm)' : '<span class="warn">not connected</span>'],
        ['Access point', esc(w.apIp) + ' (' + w.apClients + ' clients)'],
        ['APRS-IS', i.igate ? '<span class="ok">connected</span> ' + esc(cfg.igateHost) + ':' + cfg.igatePort : (cfg.igateEn ? '<span class="warn">disconnected</span>' : 'off')],
      ])) +
      card('Services', rows([
        ['iGate', onOff(cfg.igateEn)],
        ['Digipeater', onOff(cfg.digiEn)],
        ['Tracker', onOff(cfg.trkEn)],
        ['Weather', onOff(cfg.wxEn)],
      ])) +
      card('Packets', rows([
        ['RX / TX', s.rx + ' / ' + s.tx],
        ['Digipeated', s.digi + ' (duplicates dropped ' + s.dup + ')'],
        ['RF &rarr; Internet', s.rf2inet],
        ['Internet &rarr; RF', s.inet2rf],
        ['Dropped / errors', s.drop + ' / ' + s.error],
      ]));
  };
  await draw();
  return setInterval(() => draw().then(() => setConn(true), () => setConn(false)), 5000);
}

// ---- Router ----
const pages = { overview };
let timer = 0;
async function route() {
  clearInterval(timer);
  const name = location.hash.slice(1) || 'overview';
  const page = pages[name] || overview;
  document.querySelectorAll('nav a').forEach((a) => a.classList.toggle('act', a.dataset.page === name));
  $('#nav').classList.remove('open');
  const main = $('#main');
  try {
    timer = await page(main);
    setConn(true);
  } catch (e) {
    setConn(false);
    main.innerHTML = '<h1>Error</h1><p class="bad">' + esc(e.message) + '</p>';
  }
}
$('#menu').onclick = () => $('#nav').classList.toggle('open');
window.addEventListener('hashchange', route);
route();
