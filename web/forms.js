'use strict';
// Configuration pages. Each page is data: sections with fields bound to keys of GET /api/config.
// The engine draws them, and on Save sends only what changed to POST /api/config, which applies it
// the same way the old pages did (RF module, modem, APRS-IS reconnect, WiFi power, beacon timers).
//
// Field: { k: JSON key, i: array index, l: label, t: type, ...options }
//   sw    switch (bool)                 txt   text {max, up: upper case, ph}
//   pass  password (kept if untouched)  num   number {min, max, step, unit}
//   sel   select {o: [[value, label]]}  range slider {min, max}
//   bits  checkboxes on an int {o: [[bit, label]]}   bit  switch on one bit of an int {bit}
//   sym   APRS symbol (2 chars: table + code) with picker
// Any field: h (hint), w (input width in ch), on (called with the form after a change)

let CFG = null, META = null;
const loadCfg = async (force) => (CFG = !CFG || force ? await api('/api/config') : CFG);
const loadMeta = async () => (META = META || (await api('/api/meta')));
const MASK = '********';
const cfg0 = (k) => CFG[k];

const fid = (f) => 'f_' + f.k + (f.i !== undefined ? '_' + f.i : '') + (f.bit !== undefined ? '_b' + f.bit : '');
const orig = (f) => (f.i !== undefined ? CFG[f.k][f.i] : CFG[f.k]);
const iconUrl = (sym) => {
  const code = sym ? sym.charCodeAt(1) : 0;
  return code > 32 && code < 127 ? ICONS + code + '-' + (sym[0] === '\\' ? 2 : 1) + '.png' : ICONS + 'dot.png';
};

function control(f) {
  const id = fid(f), v = orig(f), w = f.w ? ' style="width:' + f.w + 'ch"' : '';
  switch (f.t) {
    case 'sw':
      return '<label class="sw"><input type="checkbox" id="' + id + '"' + (v ? ' checked' : '') + '><i></i></label>';
    case 'bit':
      return '<label class="sw"><input type="checkbox" id="' + id + '"' + (v & f.bit ? ' checked' : '') + '><i></i></label>';
    case 'txt':
    case 'pass':
      return '<input type="' + (f.t === 'pass' ? 'password' : 'text') + '" id="' + id + '" value="' + esc(v) + '"' +
        (f.max ? ' maxlength="' + f.max + '"' : '') + (f.ph ? ' placeholder="' + esc(f.ph) + '"' : '') +
        (f.up ? ' class="up"' : '') + w + ' autocomplete="off" spellcheck="false">';
    case 'num':
      return '<input type="number" id="' + id + '" value="' + esc(v) + '"' +
        (f.min !== undefined ? ' min="' + f.min + '"' : '') + (f.max !== undefined ? ' max="' + f.max + '"' : '') +
        ' step="' + (f.step || 1) + '"' + w + '>';
    case 'range':
      return '<input type="range" id="' + id + '" min="' + f.min + '" max="' + f.max + '" value="' + v +
        '" oninput="this.nextElementSibling.textContent=this.value"><b class="rv">' + v + '</b>';
    case 'sel':
      return '<select id="' + id + '"' + w + '>' + f.o.map(([val, label], n) =>
        '<option value="' + n + '"' + (val === v || String(val) === String(v) ? ' selected' : '') + '>' + esc(label) + '</option>').join('') + '</select>';
    case 'bits':
      return '<div class="bits" id="' + id + '">' + f.o.map(([bit, label]) =>
        '<label><input type="checkbox" data-bit="' + bit + '"' + (v & bit ? ' checked' : '') + '> ' + esc(label) + '</label>').join('') + '</div>';
    case 'sym':
      return '<span class="sym" id="' + id + '"><input class="t" maxlength="1" value="' + esc(v[0] || '/') + '" title="Table (/ or \\ or overlay)">' +
        '<input class="c" maxlength="1" value="' + esc(v[1] || '') + '" title="Symbol code"><img src="' + iconUrl(v) + '" alt="" width="24" height="24">' +
        '<button type="button" class="btn sm">Pick&hellip;</button></span>';
  }
  return '';
}

function read(f, form) {
  const el = form.querySelector('#' + fid(f)), v = orig(f);
  switch (f.t) {
    case 'sw': return el.checked;
    case 'bit': return el.checked ? v | f.bit : v & ~f.bit;
    case 'txt': return f.up ? el.value.trim().toUpperCase() : el.value;
    case 'pass': return el.value;
    case 'num': case 'range': { const n = Number(el.value); return el.value === '' || isNaN(n) ? v : n; }
    case 'sel': return f.o[el.selectedIndex][0];
    case 'bits': {
      let r = v;
      el.querySelectorAll('input').forEach((c) => { const b = +c.dataset.bit; r = c.checked ? r | b : r & ~b; });
      return r;
    }
    case 'sym': return (el.querySelector('.t').value || '/') + (el.querySelector('.c').value || ' ');
  }
  return v;
}

function check(f, val) {
  if (f.t === 'num' || f.t === 'range') {
    if ((f.min !== undefined && val < f.min) || (f.max !== undefined && val > f.max)) return f.l + ': must be ' + f.min + '..' + f.max;
  }
  if (f.re && !f.re.test(val)) return f.l + ': ' + f.reMsg;
  return null;
}

const row = (f) => '<div class="frow' + (f.t === 'bits' ? ' top' : '') + '"><label for="' + fid(f) + '">' + f.l + '</label><div>' +
  control(f) + (f.unit ? ' <span class="unit">' + f.unit + '</span>' : '') + (f.h ? '<div class="hint">' + f.h + '</div>' : '') + '</div></div>';

// Telemetry block shared by iGate, Digi and Tracker: interval + 5 analog channels
function tlmSection(p, title) {
  const fields = [{ k: p + 'TlmInv', l: 'Interval', t: 'num', min: 0, max: 1000, unit: 'packets', h: '0 = do not send, 1 = with every packet' }];
  const sensors = [[0, 'NONE']].concat([1, 2, 3, 4, 5, 6, 7, 8, 9, 10].map((n) => [n, 'SENSOR#' + n]));
  const ch = [];
  for (let x = 0; x < 5; x++) {
    const c = {
      sen: { k: p + 'TlmSen', i: x, t: 'sel', o: sensors },
      name: { k: p + 'TlmPARM', i: x, t: 'txt', max: 9, w: 9 },
      unit: { k: p + 'TlmUNIT', i: x, t: 'txt', max: 7, w: 6 },
      prec: { k: p + 'TlmPrec', i: x, t: 'num', min: 0, max: 5, w: 4, l: 'Precision' },
      off: { k: p + 'TlmOffset', i: x, t: 'num', step: 'any', w: 8 },
      a: { k: p + 'TlmEQNS', i: x * 3, t: 'num', step: 'any', w: 8 },
      b: { k: p + 'TlmEQNS', i: x * 3 + 1, t: 'num', step: 'any', w: 8 },
      c: { k: p + 'TlmEQNS', i: x * 3 + 2, t: 'num', step: 'any', w: 8 },
    };
    ch.push(c);
    fields.push(...Object.values(c));
  }
  const html = row(fields[0]) + '<div class="scroll"><table class="list tlm"><thead><tr><th>CH</th><th>Sensor</th><th>Name</th><th>Unit</th>' +
    '<th>Precision</th><th>Offset</th><th>a</th><th>b</th><th>c</th></tr></thead><tbody>' +
    ch.map((c, x) => '<tr><td>A' + (x + 1) + '</td>' + ['sen', 'name', 'unit', 'prec', 'off', 'a', 'b', 'c'].map((n) => '<td>' + control(c[n]) + '</td>').join('') + '</tr>').join('') +
    '</tbody></table></div><div class="hint">Value = a&middot;v&sup2; + b&middot;v + c (v = 0..8280). Changing precision sets b = 1/10<sup>precision</sup>; changing offset sets c = &minus;offset (as before).</div>';
  // the old page filled b and c from precision and offset
  const wire = (form) => ch.forEach((c) => {
    form.querySelector('#' + fid(c.prec)).addEventListener('change', (e) => { form.querySelector('#' + fid(c.b)).value = 1 / Math.pow(10, +e.target.value); });
    form.querySelector('#' + fid(c.off)).addEventListener('change', (e) => { form.querySelector('#' + fid(c.c)).value = -e.target.value; });
  });
  return { title: title || 'Telemetry', fields, html, wire };
}

// PHG calculator (same formula as the old pages); writes into the PHG text field
function phgSection(key) {
  const f = { k: key, l: 'PHG text', t: 'txt', max: 7, w: 8, h: 'Power-Height-Gain-Directivity; leave empty to not send it' };
  const html = row(f) + '<div class="frow"><label>Calculator</label><div class="phg">' +
    '<select class="p">' + [1, 5, 10, 15, 25, 35, 50, 65, 80].map((w) => '<option>' + w + '</option>').join('') + '</select> W ' +
    '<select class="h">' + [10, 20, 40, 80, 160, 320, 640, 1280, 2560, 5120].map((h) => '<option>' + h + '</option>').join('') + '</select> ft ' +
    '<input class="g" type="number" min="0" max="100" step="0.1" value="6" style="width:6ch"> dBi ' +
    '<select class="d">' + ['Omni', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW', 'N'].map((d) => '<option>' + d + '</option>').join('') + '</select> ' +
    '<button type="button" class="btn sm">Calculate</button></div></div>';
  const wire = (form) => {
    const box = form.querySelector('.phg');
    box.querySelector('button').onclick = () => {
      const pw = +box.querySelector('.p').value, ht = +box.querySelector('.h').value, gn = +box.querySelector('.g').value;
      const p = pw < 1 ? 0 : Math.min(9, Math.floor(Math.sqrt(pw)));
      const h = String.fromCharCode(48 + Math.round(Math.log2(ht / 10)));
      const g = gn > 9 ? 9 : gn < 0 ? 0 : Math.round(gn);
      form.querySelector('#' + fid(f)).value = 'PHG' + p + h + g + box.querySelector('.d').selectedIndex;
    };
  };
  return { fields: [f], html, wire };
}

// Symbol picker: both tables as a grid of icons
function pickSymbol(onPick) {
  const dlg = document.createElement('div');
  dlg.className = 'modal';
  let html = '<div class="card"><h2>Select symbol <a href="#" class="x">&times;</a></h2><div class="picker">';
  [['/', 1, 'Primary table /'], ['\\', 2, 'Alternate table \\']].forEach(([t, n, title]) => {
    html += '<div class="sep2">' + title + '</div><div class="icons">';
    for (let c = 33; c < 127; c++) html += '<img src="' + ICONS + c + '-' + n + '.png" data-s="' + esc(t + String.fromCharCode(c)) + '" title="' + esc(t + String.fromCharCode(c)) + '" width="24" height="24" loading="lazy">';
    html += '</div>';
  });
  dlg.innerHTML = html + '</div></div>';
  dlg.onclick = (e) => {
    if (e.target.dataset.s) onPick(e.target.dataset.s);
    if (e.target.dataset.s || e.target === dlg || e.target.classList.contains('x')) { e.preventDefault(); dlg.remove(); }
  };
  document.body.appendChild(dlg);
}

// Draws a page and handles Save. spec = { title, intro, sections: [{ title, fields, html?, wire?, note? }] }
async function formPage(main, specFn) {
  await Promise.all([loadCfg(true), loadMeta()]);
  const spec = specFn(CFG, META);
  const all = spec.sections.flatMap((s) => s.fields);
  const pill = spec.enable ? '<span class="pill' + (cfg0(spec.enable) ? ' on">Enabled' : '">Disabled') + '</span>' : '';
  const tabs = spec.sections.length > 2 ? '<div class="tabs"><a href="#" data-t="all" class="act">All</a>' +
    spec.sections.map((s, n) => '<a href="#" data-t="' + n + '">' + s.title + '</a>').join('') + '</div>' : '';
  main.innerHTML = '<div class="phead"><h1>' + spec.title + '</h1>' + pill + '</div>' + (spec.intro ? '<p class="muted intro">' + spec.intro + '</p>' : '') + tabs +
    '<form id="cfgForm" autocomplete="off">' + spec.sections.map((s, n) => '<div class="card form" data-s="' + n + '"><h2>' + s.title + '</h2>' +
      (s.html || s.fields.map(row).join('')) + (s.note ? '<div class="hint pad">' + s.note + '</div>' : '') + '</div>').join('') +
    '<div class="savebar"><span id="dirty" class="muted"></span><button type="button" class="btn" id="undo">Undo changes</button>' +
    '<button type="submit" class="btn primary" id="save">Save</button></div></form>';
  const form = $('#cfgForm');
  // tabs only filter what is shown; Save always covers every section
  main.querySelectorAll('.tabs a').forEach((a) => a.onclick = (e) => {
    e.preventDefault();
    main.querySelectorAll('.tabs a').forEach((x) => x.classList.toggle('act', x === a));
    form.querySelectorAll('.card.form').forEach((c) => { c.hidden = a.dataset.t !== 'all' && c.dataset.s !== a.dataset.t; });
  });
  spec.sections.forEach((s) => s.wire && s.wire(form));
  all.forEach((f) => f.on && form.querySelector('#' + fid(f)).addEventListener('change', () => f.on(form)));
  all.forEach((f) => f.on && f.on(form));
  form.querySelectorAll('.sym').forEach((box) => {
    const upd = () => { box.querySelector('img').src = iconUrl(box.querySelector('.t').value + box.querySelector('.c').value); };
    box.querySelectorAll('input').forEach((i) => i.addEventListener('input', upd));
    box.querySelector('button').onclick = () => pickSymbol((s) => {
      box.querySelector('.t').value = s[0];
      box.querySelector('.c').value = s[1];
      upd();
      form.dispatchEvent(new Event('input'));
    });
  });

  const diff = () => {
    const patch = {};
    for (const f of all) {
      const nv = read(f, form);
      if (f.i !== undefined) {
        if (nv !== orig(f)) (patch[f.k] = patch[f.k] || CFG[f.k].slice())[f.i] = nv;
      } else if (f.t === 'bit' || f.t === 'bits') {
        const cur = patch[f.k] !== undefined ? patch[f.k] : CFG[f.k];
        const merged = f.t === 'bit' ? (form.querySelector('#' + fid(f)).checked ? cur | f.bit : cur & ~f.bit) : nv;
        if (merged !== CFG[f.k] || patch[f.k] !== undefined) patch[f.k] = merged;
      } else if (nv !== orig(f)) patch[f.k] = nv;
    }
    for (const k of Object.keys(patch)) if (JSON.stringify(patch[k]) === JSON.stringify(CFG[k])) delete patch[k];
    return patch;
  };
  const showDirty = () => {
    const n = Object.keys(diff()).length;
    $('#dirty').textContent = n ? 'Unsaved changes' : '';
    $('#save').disabled = !n;
  };
  form.addEventListener('input', showDirty);
  form.addEventListener('change', showDirty);
  showDirty();
  setGuard((silent) => !Object.keys(diff()).length || (!silent && confirm('Leave without saving your changes?')));

  $('#undo').onclick = () => route();
  form.onsubmit = async (e) => {
    e.preventDefault();
    const patch = diff();
    for (const f of all) {
      const err = check(f, read(f, form));
      if (err) { toast(err); form.querySelector('#' + fid(f)).focus(); return; }
    }
    if (spec.validate) { const err = spec.validate(patch, form); if (err) { toast(err); return; } }
    $('#save').disabled = true;
    try {
      const r = await api('/api/config', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(patch) });
      setGuard(null);
      await formPage(main, specFn);
      if (r.restart) restartBanner(main);
      else toast(r.changed ? 'Saved and applied' : 'Nothing changed');
    } catch (err) {
      toast('Save failed: ' + err.message);
      $('#save').disabled = false;
    }
  };
}

function restartBanner(main) {
  const b = document.createElement('div');
  b.className = 'banner';
  b.innerHTML = 'Saved. WiFi and Bluetooth changes take effect after a reboot. <button class="btn primary sm">Reboot now</button>';
  b.querySelector('button').onclick = async () => {
    try { await api('/api/reboot', { method: 'POST' }); b.textContent = 'Rebooting... the page reconnects by itself.'; setConn(false); } catch (e) { toast(e.message); }
  };
  main.prepend(b);
}

// ---- shared field groups ----
const CALL_RE = /^[A-Z0-9]{3,7}$/;
// APRS SSID convention (aprs.org/aprs11/SSIDs.txt); the usual one for the page gets a star
const SSID_USE = ['primary station', 'generic / digi', 'generic', 'generic', 'generic', 'other networks / phone',
  'special activity', 'handheld', 'boat / RV / 2nd mobile', 'mobile', 'iGate / Internet', 'balloon / aircraft',
  'one-way tracker', 'weather station', 'truck', 'generic'];
const ssidOpts = (star) => SSID_USE.map((u, n) => [n, (n ? '-' + n : '0 (none)') + '  ' + u + (n === star ? '  ★' : '')]);
const pathSel = (k, meta) => ({ k, l: 'PATH', t: 'sel', o: meta.paths.map((p, n) => [n, p]) });
const station = (p, meta, extra, star) => [
  { k: p + 'Mycall', l: 'Callsign', t: 'txt', max: 7, w: 9, up: true, re: CALL_RE, reMsg: '3 to 7 letters/digits' },
  { k: p + 'SSID', l: 'SSID', t: 'sel', o: ssidOpts(star), w: 30 },
  { k: p + 'Symbol', l: 'Symbol', t: 'sym' },
].concat(extra || [], [pathSel(p + 'Path', meta)]);
const position = (p, altKey) => [
  { k: p + 'Bcn', l: 'Beacon', t: 'sw' },
  { k: p + 'INV', l: 'Interval', t: 'num', min: 0, max: 3600, unit: 's' },
  { k: p + 'GPS', l: 'Location', t: 'sel', o: [[false, 'Fixed'], [true, 'GPS']] },
  { k: p + 'Pos2rf', l: 'Send to RF', t: 'sw' },
  { k: p + 'Pos2inet', l: 'Send to Internet', t: 'sw' },
  { k: p + 'LAT', l: 'Latitude', t: 'num', min: -90, max: 90, step: 0.00001, unit: '&deg; (+N / &minus;S)' },
  { k: p + 'LON', l: 'Longitude', t: 'num', min: -180, max: 180, step: 0.00001, unit: '&deg; (+E / &minus;W)' },
  { k: altKey || p + 'ALT', l: 'Altitude', t: 'num', min: 0, max: 10000, step: 0.1, unit: 'm', h: '0 = altitude not sent' },
];
const texts = (p, stsKey) => [
  { k: p + 'Comment', l: 'Comment', t: 'txt', max: 25, w: 30 },
  { k: p + 'Status', l: 'Status text', t: 'txt', max: 50, w: 50 },
  { k: stsKey, l: 'Status interval', t: 'num', min: 0, max: 3600, unit: 's', h: '0 = do not send' },
];
const FILTERS = [[4, 'Message'], [64, 'Status'], [16, 'Telemetry'], [8, 'Weather'], [1, 'Object'], [2, 'Item'], [32, 'Query'], [256, 'Buoy'], [128, 'Position']];

// ---- Radio (old Radio tab: RF module + AFSK/TNC) ----
const RF_RANGE = { 1: [134, 174], 4: [136, 174], 7: [136, 174], 2: [400, 470], 5: [400, 470], 8: [400, 470], 3: [320, 400], 6: [350, 390] };
pages.radio = (main) => formPage(main, (cfg, meta) => ({
  title: 'Radio', enable: 'rfEnable',
  sections: [
    {
      title: 'RF module',
      fields: [
        { k: 'rfEnable', l: 'Enable', t: 'sw' },
        {
          k: 'rfType', l: 'Module type', t: 'sel', o: meta.rfTypes.map((t, n) => [n, t]),
          on: (form) => { // frequency range of the module, as the old page did
            const [lo, hi] = RF_RANGE[read({ k: 'rfType', t: 'sel', o: meta.rfTypes.map((t, n) => [n, t]) }, form)] || [134, 500];
            ['rfFreqTX', 'rfFreqRX'].forEach((k) => { const el = form.querySelector('#f_' + k); el.min = lo; el.max = hi; });
            form.querySelector('#rfRange').textContent = lo + ' - ' + hi + ' MHz';
          },
        },
        { k: 'rfFreqTX', l: 'TX frequency', t: 'num', step: 0.0001, unit: 'MHz <span class="muted" id="rfRange"></span>' },
        { k: 'rfFreqRX', l: 'RX frequency', t: 'num', step: 0.0001, unit: 'MHz' },
        { k: 'rfToneTX', l: 'TX CTCSS', t: 'sel', o: meta.ctcss.map((v, n) => [n, n ? v + ' Hz' : 'Off']) },
        { k: 'rfToneRX', l: 'RX CTCSS', t: 'sel', o: meta.ctcss.map((v, n) => [n, n ? v + ' Hz' : 'Off']) },
        { k: 'rfBand', l: 'Narrow / wide', t: 'sel', o: [[0, '12.5 kHz'], [1, '25 kHz']] },
        { k: 'rfPwr', l: 'TX power', t: 'sel', o: [[true, 'HIGH'], [false, 'LOW']] },
        { k: 'rfVolume', l: 'Volume', t: 'range', min: 1, max: 8 },
        { k: 'rfSql', l: 'Squelch level', t: 'range', min: 0, max: 8 },
      ],
      note: 'Applied at once: the module is re-programmed after saving.',
    },
    {
      title: 'AFSK / TNC',
      fields: [
        { k: 'rfModem', l: 'Modem', t: 'sel', o: meta.modems.map((m, n) => [n, m]), h: meta.modems.length < 4 ? '9600 G3RUH is only available on ESP32-S3 boards' : '' },
        { k: 'fx25Mode', l: 'FX.25', t: 'sel', o: meta.fx25.map((m, n) => [n, m]), h: 'FX.25 = AX.25 + forward error correction' },
        { k: 'audioLPF', l: 'De-emphasis', t: 'sw', h: 'Audio low pass filter 1 Hz - 2.5 kHz' },
        { k: 'txTimeSlot', l: 'TX time slot', t: 'num', min: 0, max: 99999, step: 100, unit: 'ms' },
        { k: 'rfPreamble', l: 'Preamble', t: 'sel', o: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10].map((n) => [n, n * 100 + ' ms']) },
      ],
      note: 'Applied at once: the modem restarts with the new settings.',
    },
  ],
}));

// ---- iGate (old IGATE tab) ----
pages.igate = (main) => formPage(main, (cfg, meta) => ({
  title: 'iGate', enable: 'igateEn',
  sections: [
    {
      title: 'Station',
      fields: [{ k: 'igateEn', l: 'Enable', t: 'sw' }].concat(station('igate', meta, [
        { k: 'igateObject', l: 'Item/Object name', t: 'txt', max: 9, w: 11, h: 'Leave empty if not used (3 to 9 characters)' },
      ], 10)),
    },
    {
      title: 'APRS-IS server',
      fields: [
        { k: 'igateHost', l: 'Server', t: 'txt', max: 19, w: 22, h: 'For example rotate.aprs2.net' },
        { k: 'igatePort', l: 'Port', t: 'num', min: 1, max: 65535, w: 8, h: '14580 = filtered port (uses the filter below)' },
        { k: 'igateFilter', l: 'Server filter', t: 'txt', max: 29, w: 32, h: 'Syntax: aprs-is.net/javAPRSFilter.aspx' },
      ],
      note: 'Changing callsign, server, port, filter or the enable switch reconnects to APRS-IS.',
    },
    { title: 'Text', fields: texts('igate', 'igateSTSIntv') },
    {
      title: 'Gateway',
      fields: [
        { k: 'rf2inet', l: 'RF &rarr; Internet', t: 'sw' },
        { k: 'rf2inetFilter', l: 'RF &rarr; Internet types', t: 'bits', o: FILTERS.concat([[32768, 'ALL']]) },
        { k: 'inet2rf', l: 'Internet &rarr; RF', t: 'sw' },
        { k: 'inet2rfFiltger', l: 'Internet &rarr; RF types', t: 'bits', o: FILTERS },
        { k: 'igateTime', l: 'Time stamp', t: 'sw' },
      ],
    },
    { title: 'Position', fields: position('igate') },
    Object.assign(phgSection('igatePHG'), { title: 'PHG' }),
    tlmSection('igate'),
  ],
}));

// ---- Digipeater (old DIGI tab) ----
pages.digi = (main) => formPage(main, (cfg, meta) => ({
  title: 'Digipeater', enable: 'digiEn',
  sections: [
    {
      title: 'Station',
      fields: [
        { k: 'digiEn', l: 'Enable', t: 'sw' },
        { k: 'digiAuto', l: 'Auto enable', t: 'sw', h: 'Turn the digipeater on by itself while APRS-IS is disconnected' },
      ].concat(station('digi', meta, [], 1)),
    },
    {
      title: 'Repeating',
      fields: [
        { k: 'digiDelay', l: 'Repeat delay', t: 'num', min: 0, max: 10000, unit: 'ms', h: '0 = auto; any other value = random delay up to it' },
        { k: 'digiFilter', l: 'Repeat types', t: 'bits', o: FILTERS },
      ],
    },
    { title: 'Text', fields: texts('digi', 'digiSTSIntv') },
    { title: 'Position', fields: position('digi', 'digiAlt').concat([{ k: 'digiTime', l: 'Time stamp', t: 'sw' }]) },
    Object.assign(phgSection('digiPHG'), { title: 'PHG' }),
    tlmSection('digi'),
  ],
}));

// ---- Tracker (old TRACKER tab) ----
pages.tracker = (main) => formPage(main, (cfg, meta) => ({
  title: 'Tracker', enable: 'trkEn',
  sections: [
    {
      title: 'Station',
      fields: [{ k: 'trkEn', l: 'Enable', t: 'sw' }].concat(station('trk', meta, [
        { k: 'trkItem', l: 'Item/Object name', t: 'txt', max: 9, w: 11, h: 'Leave empty if not used (3 to 9 characters)' },
      ], 9)),
    },
    { title: 'Text', fields: texts('trk', 'trkSTSIntv') },
    {
      title: 'Position',
      fields: [
        { k: 'trkINV', l: 'Interval', t: 'num', min: 0, max: 3600, unit: 's' },
        { k: 'trkGPS', l: 'Location', t: 'sel', o: [[false, 'Fixed'], [true, 'GPS']] },
        { k: 'trkPos2rf', l: 'Send to RF', t: 'sw' },
        { k: 'trkPos2inet', l: 'Send to Internet', t: 'sw' },
        { k: 'trkLAT', l: 'Latitude', t: 'num', min: -90, max: 90, step: 0.00001, unit: '&deg; (+N / &minus;S)' },
        { k: 'trkLON', l: 'Longitude', t: 'num', min: -180, max: 180, step: 0.00001, unit: '&deg; (+E / &minus;W)' },
        { k: 'trkALT', l: 'Altitude', t: 'num', min: 0, max: 10000, step: 0.1, unit: 'm', h: '0 = altitude not sent' },
        { k: 'trkTime', l: 'Time stamp', t: 'sw' },
        { k: 'trkCompress', l: 'Compressed', t: 'sw' },
        { k: 'trkMicEType', l: 'Mic-E type', t: 'sel', o: meta.micE.map((m, n) => [n, m]), h: 'Used when compressed is on and no Item/Object or time stamp is used' },
        { k: 'trkLog', l: 'Telemetry', t: 'sw' },
        { k: 'trkOptAlt', l: 'Altitude', t: 'sw' },
        { k: 'trkOptRSSI', l: 'Audio request', t: 'sw' },
      ],
    },
    {
      title: 'Smart beacon',
      fields: [
        { k: 'trkSmart', l: 'Enable', t: 'sw' },
        { k: 'trkSymbolMove', l: 'Moving symbol', t: 'sym' },
        { k: 'trkSymbolStop', l: 'Stopped symbol', t: 'sym' },
        { k: 'trkHSpeed', l: 'High speed', t: 'num', min: 10, max: 1000, unit: 'km/h' },
        { k: 'trkLSpeed', l: 'Low speed', t: 'num', min: 1, max: 250, unit: 'km/h' },
        { k: 'trkSlowInv', l: 'Slow interval', t: 'num', min: 60, max: 3600, unit: 's' },
        { k: 'trkMaxInv', l: 'Max interval', t: 'num', min: 10, max: 255, unit: 's' },
        { k: 'trkMinInv', l: 'Min interval', t: 'num', min: 1, max: 100, unit: 's' },
        { k: 'trkMinDir', l: 'Min angle', t: 'num', min: 1, max: 359, unit: '&deg;' },
      ],
    },
    tlmSection('trk'),
  ],
}));

// ---- WiFi and Bluetooth (old WiFi tab) ----
pages.wifi = (main) => formPage(main, (cfg, meta) => {
  const sta = [];
  for (let n = 0; n < 5; n++) {
    sta.push(
      { k: 'WiFiSTA', i: n * 3, l: 'Network ' + (n + 1), t: 'sw' },
      { k: 'WiFiSTA', i: n * 3 + 1, l: 'SSID', t: 'txt', max: 31, w: 32 },
      { k: 'WiFiSTA', i: n * 3 + 2, l: 'Password', t: 'pass', max: 62, w: 32, h: cfg.WiFiSTA[n * 3 + 2] === MASK ? 'Stored; leave as is to keep it' : '' },
    );
  }
  const sections = [
    {
      title: 'Access point',
      fields: [
        { k: 'WiFiMode', l: 'Enable', t: 'bit', bit: 1 },
        { k: 'WiFiAP_SSID', l: 'SSID', t: 'txt', max: 31, w: 32 },
        { k: 'WiFiAP_PASS', l: 'Password', t: 'pass', max: 62, w: 32, h: 'At least 8 characters. Stored; leave as is to keep it' },
      ],
    },
    {
      title: 'Client (multi station)',
      fields: [
        { k: 'WiFiMode', l: 'Enable', t: 'bit', bit: 2 },
        { k: 'WiFiPwr', l: 'WiFi TX power', t: 'sel', o: meta.wifiPwr.map(([v, dbm]) => [v, dbm + ' dBm']), h: 'Applied at once' },
      ].concat(sta),
      note: 'The device connects to the strongest enabled network in range.',
    },
  ];
  if (meta.features.bt) {
    sections.push({
      title: 'Bluetooth (BLE)',
      fields: [
        { k: 'btMaster', l: 'Enable', t: 'sw' },
        { k: 'btName', l: 'Name', t: 'txt', max: 19, w: 20 },
        { k: 'btPin', l: 'PIN', t: 'num', min: 0, max: 999999, h: '0 = no authentication' },
        { k: 'btMode', l: 'Mode', t: 'sel', o: [[0, 'NONE'], [1, 'TNC2'], [2, 'KISS']] },
      ].concat(meta.features.btUuid ? [
        { k: 'btUUID', l: 'UUID', t: 'txt', max: 36, w: 38 },
        { k: 'btUUIDRx', l: 'UUID RX', t: 'txt', max: 36, w: 38 },
        { k: 'btUUIDTx', l: 'UUID TX', t: 'txt', max: 36, w: 38 },
      ] : []),
    });
  }
  return {
    title: 'WiFi',
    intro: 'Mode and network changes take effect after a reboot; you will be offered one after saving.',
    sections,
    validate: (patch) => {
      if (patch.WiFiAP_PASS !== undefined && patch.WiFiAP_PASS !== MASK && patch.WiFiAP_PASS.length < 8) return 'Access point password: at least 8 characters';
      const mode = patch.WiFiMode !== undefined ? patch.WiFiMode : cfg.WiFiMode;
      if (!(mode & 3)) return 'Keep the access point or the client enabled, or you lose access to this page';
      return null;
    },
  };
});
