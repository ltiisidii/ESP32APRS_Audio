'use strict';
// Station administration: System (form), About (information, credits, firmware update) and Files.

const post = (path, body) => api(path, body === undefined ? { method: 'POST' } :
  { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
const decodeEnt = (t) => String(t).replace(/&amp;/g, '&'); // a few firmware strings carry HTML entities

// ---- System (old System tab) ----
pages.system = (main) => formPage(main, (cfg, meta) => {
  const sections = [
    {
      title: 'General',
      fields: [
        { k: 'hostName', l: 'Host name', t: 'txt', max: 31, w: 32, h: 'Name of the device on the network and in the browser tab' },
        { k: 'resetTimeout', l: 'Automatic reboot', t: 'num', min: 0, max: 65535, unit: 'minutes', h: '0 = never (recommended; a healthy station does not need it)' },
      ],
    },
    {
      title: 'Clock',
      fields: [
        { k: 'timeZone', l: 'Time zone', t: 'sel', o: meta.tz.map(([tz, name]) => [tz, decodeEnt(name)]), h: 'Applied at once' },
        { k: 'ntpHost', l: 'NTP server', t: 'txt', max: 19, w: 22, h: 'Applied at once' },
      ],
      extra: '<div class="frow"><label>Device time</label><div><b id="devTime">-</b>' +
        '<div class="actions inline"><button type="button" class="btn sm" id="syncTime">Set to this computer\'s time</button>' +
        '<input type="datetime-local" id="manTime" step="1"><button type="button" class="btn sm" id="setTime">Set</button></div>' +
        '<div class="hint">Normally set by NTP or GPS; set it by hand when the station has neither</div></div></div>',
      wire: (form) => {
        const show = async () => {
          try {
            const i = await api('/api/info');
            const t = new Date((i.time + i.tz * 3600) * 1000); // device local time = UTC + its time zone
            form.querySelector('#devTime').textContent = i.time > 1600000000 ? t.toISOString().slice(0, 19).replace('T', ' ') + ' (' + tzLabel(i.tz) + ')' : 'not set';
          } catch (e) { /* shown as - */ }
        };
        const setEpoch = async (epoch) => {
          try { await post('/api/time', { epoch: Math.round(epoch) }); toast('Clock set'); show(); } catch (e) { toast(e.message); }
        };
        form.querySelector('#syncTime').onclick = () => setEpoch(Date.now() / 1000);
        form.querySelector('#setTime').onclick = () => {
          const v = form.querySelector('#manTime').value; // read as device local time
          if (!v) return toast('Pick a date and time first');
          setEpoch(Date.parse(v + 'Z') / 1000 - cfg.timeZone * 3600);
        };
        show();
      },
    },
    {
      title: 'Web login',
      fields: [
        { k: 'httpUser', l: 'User', t: 'txt', max: 31, w: 32 },
        { k: 'httpPass', l: 'Password', t: 'pass', max: 63, w: 32, h: 'Stored; leave as is to keep it' },
      ],
      note: 'After changing them the browser asks you to log in again with the new ones.',
    },
    {
      title: 'User defined paths',
      fields: [0, 1, 2, 3].map((n) => ({ k: 'path', i: n, l: 'UserDefine ' + (n + 1), t: 'txt', max: 71, w: 40, up: true, ph: 'e.g. WIDE1-1,WIDE2-1' })),
      note: 'Selectable as PATH in iGate, Digipeater and Tracker.',
    },
    {
      title: 'Power save',
      fields: [
        { k: 'pwrEn', l: 'Enable', t: 'sw' },
        { k: 'pwrIO', l: 'PWR GPIO', t: 'num', min: -1, max: 50, h: '-1 = not used' },
        { k: 'pwrIOAct', l: 'Output active', t: 'sel', o: [[false, 'LOW'], [true, 'HIGH']] },
        { k: 'pwrSleep', l: 'Sleep interval', t: 'num', min: 0, max: 9999, unit: 's' },
        { k: 'pwrStanby', l: 'Standby delay', t: 'num', min: 0, max: 9999, unit: 's' },
        { k: 'pwrMode', l: 'Mode', t: 'sel', o: meta.pwrModes.map((m, n) => [n, m]), h: 'A = reduce speed (PWR off), B = light sleep (WiFi/PWR off), C = deep sleep (all off)' },
        { k: 'pwrSleepAct', l: 'Wake events (mode C)', t: 'bits', o: [[1, 'Tracker'], [64, 'Status'], [16, 'Telemetry'], [8, 'Weather'], [2, 'iGate'], [4, 'Digi'], [32, 'Query'], [128, 'WiFi']] },
      ],
    },
  ];
  if (meta.features.logFile) {
    sections.push({ title: 'Log file', fields: [{ k: 'logFile', l: 'Log events', t: 'bits', o: [[1, 'Tracker'], [2, 'iGate'], [4, 'Digi'], [8, 'Weather']] }] });
  }
  if (meta.features.display) {
    sections.push({
      title: 'Display',
      fields: [
        { k: 'dspEn', l: 'OLED/TFT enable', t: 'sw' },
        { k: 'dspFlip', l: 'Flip / rotate', t: 'sw' },
        { k: 'dspTX', l: 'Show TX packets', t: 'sw', h: 'Every transmitted packet, after the filter' },
        { k: 'dspRX', l: 'Show RX packets', t: 'sw', h: 'Every received packet, after the filter' },
        { k: 'dspHUp', l: 'Heading up', t: 'sw', h: 'The compass turns with the direction of movement' },
        { k: 'dspBright', l: 'TFT brightness', t: 'sel', o: Array.from({ length: 11 }, (_, n) => [n * 25, String(n * 25)]) },
        { k: 'dspDelay', l: 'Popup time', t: 'sel', o: Array.from({ length: 16 }, (_, n) => [n, n + ' s']) },
        { k: 'dspTOut', l: 'Screen sleep', t: 'sel', o: Array.from({ length: 21 }, (_, n) => [n * 30, n ? n * 30 + ' s' : 'never']) },
        { k: 'dspRF', l: 'Popup from RF', t: 'sw' },
        { k: 'dspINET', l: 'Popup from Internet', t: 'sw' },
        { k: 'dspDxFilter', l: 'Max distance', t: 'num', min: 0, max: 9999, unit: 'km', h: '0 = any distance' },
        { k: 'dspFilter', l: 'Popup types', t: 'bits', o: FILTERS },
      ],
    });
  }
  sections.push({
    title: 'System control',
    fields: [],
    html: '<div class="frow"><label>Reboot</label><div><button type="button" class="btn danger" id="sysReboot">Reboot now</button></div></div>' +
      '<div class="frow"><label>Reload saved settings</label><div><button type="button" class="btn" id="sysReload">Reload</button>' +
      '<div class="hint">Reads the configuration file again, dropping changes that were applied but not saved</div></div></div>' +
      '<div class="frow"><label>Factory reset</label><div><button type="button" class="btn danger" id="sysFactory">Factory reset&hellip;</button>' +
      '<div class="hint">All settings back to defaults, including WiFi and the web login. The station restarts as an access point.</div></div></div>',
    wire: (form) => {
      form.querySelector('#sysReboot').onclick = async () => {
        if (!confirm('Reboot the station now?')) return;
        try { await post('/api/reboot'); toast('Rebooting...'); setConn(false); } catch (e) { toast(e.message); }
      };
      form.querySelector('#sysReload').onclick = async () => {
        try { await post('/api/reload'); toast('Saved settings reloaded'); route(); } catch (e) { toast(e.message); }
      };
      form.querySelector('#sysFactory').onclick = async () => {
        if (prompt('This erases every setting. Type RESET to confirm.') !== 'RESET') return;
        try { await post('/api/factory'); toast('Factory reset done, restarting...'); setConn(false); } catch (e) { toast(e.message); }
      };
    },
  });
  return { title: 'System', sections };
});

// ---- About (old About tab) ----
pages.about = async (main) => {
  const [a, meta] = await Promise.all([api('/api/about'), loadMeta()]);
  const link = (u) => '<a href="' + u + '" target="_blank" rel="noopener">' + u.replace(/^https?:\/\//, '') + '</a>';
  const w = a.wifi;
  main.innerHTML = '<h1>About</h1><div class="grid">' +
    card('System', rows([
      ['Board', esc(a.board)],
      ['Firmware', 'V' + esc(a.version)],
      ['RF module', esc(a.rfModule)],
      ['Chip', esc(a.chip) + ' rev ' + a.revision],
      ['Chip ID', esc(a.chipId)],
      ['Flash', kb(a.flash)],
      a.psramSize && ['PSRAM free', kb(a.psram) + ' / ' + kb(a.psramSize)],
      ['File system', kb(a.fsUsed) + ' / ' + kb(a.fsTotal)],
    ])) +
    card('Credits', rows([
      ['Original author', 'Mr. Somkiat Nakhonthai &mdash; HS5TQA (Atten, Nakhonthai), Bangkok, Thailand'],
      ['GitHub', link('https://github.com/nakhonthai')],
      ['YouTube', link('https://www.youtube.com/@HS5TQA')],
      ['Facebook', link('https://www.facebook.com/atten')],
      ['Chat', 'Telegram ' + link('https://t.me/HS5TQA') + ', WeChat HS5TQA'],
      ['Sponsors', link('https://github.com/sponsors/nakhonthai')],
      ['Donate', link('https://www.paypal.me/0hs5tqa0')],
      ['This version', 'Modified fork by Jonatan &mdash; LU6EWB'],
      ['Source', link('https://github.com/ltiisidii/ESP32APRS_Audio')],
      ['Changes', 'Stability and recovery, tests, digipeater duplicate suppression, new web interface (2026)'],
      ['License', 'GNU General Public License v3; source code on GitHub'],
    ])) +
    card('WiFi', rows([
      ['Mode', esc(w.mode) + ' (' + esc(w.protocol) + ')'],
      ['MAC', esc(w.mac)],
      ['Channel', w.channel],
      ['TX power', w.txPower + ' dBm'],
      ['SSID', esc(w.ssid) || '-'],
      ['IP / gateway', esc(w.ip) + ' / ' + esc(w.gateway)],
      ['DNS', esc(w.dns)],
    ])) +
    (a.ppp ? card('PPPoS (cellular)', rows([
      ['Modem', esc(a.ppp.manufacturer) + ' ' + esc(a.ppp.model)],
      ['IMEI / IMSI', esc(a.ppp.imei) + ' / ' + esc(a.ppp.imsi)],
      ['Operator', esc(a.ppp.operator)],
      ['RSSI', a.ppp.rssi + ' dBm'],
      ['IP / gateway', esc(a.ppp.ip) + ' / ' + esc(a.ppp.gateway)],
    ])) : '') +
    '</div>' +
    '<div class="card wide form"><h2>Firmware update</h2>' +
    '<div class="frow"><label>Firmware file</label><div><input type="file" id="fwFile" accept=".bin">' +
    '<div class="hint">The .bin for this board from ' + link('https://github.com/ltiisidii/ESP32APRS_Audio/releases') +
    '. If the new firmware fails to start, the station goes back to this one by itself.</div></div></div>' +
    '<div class="frow"><label>Progress</label><div><div class="bar big"><i id="fwBar" style="width:0"></i></div><span id="fwMsg" class="muted">Ready</span></div></div>' +
    '<div class="frow"><label></label><div><button type="button" class="btn primary" id="fwGo">Upload and install</button></div></div>' +
    (meta.otaServer ? '<div class="frow"><label>Online update</label><div><button type="button" class="btn" id="otaCheck">Check for a new version</button> <span id="otaMsg" class="muted"></span></div></div>' : '') +
    '</div>';

  $('#fwGo').onclick = () => {
    const f = $('#fwFile').files[0];
    if (!f) return toast('Choose the firmware file first');
    if (!/\.bin$/i.test(f.name)) return toast('The firmware is a .bin file');
    if (!confirm('Install ' + f.name + ' (' + kb(f.size) + ')? The station restarts when it finishes.')) return;
    const fd = new FormData();
    fd.append('update', f, f.name);
    const x = new XMLHttpRequest();
    x.open('POST', '/update');
    x.upload.onprogress = (e) => {
      if (!e.lengthComputable) return;
      const pc = Math.round((100 * e.loaded) / e.total);
      $('#fwBar').style.width = pc + '%';
      $('#fwMsg').textContent = pc + '%';
    };
    x.onload = () => { $('#fwMsg').textContent = x.status === 200 ? 'Done, restarting... the page reconnects by itself' : 'Failed: HTTP ' + x.status; };
    x.onerror = () => { $('#fwMsg').textContent = 'Connection lost (normal if it restarted)'; };
    $('#fwGo').disabled = true;
    x.send(fd);
  };
  if (meta.otaServer) {
    $('#otaCheck').onclick = async () => {
      $('#otaMsg').textContent = 'Checking...';
      try {
        const d = await api('/check_version');
        $('#otaMsg').textContent = d.update_available ? 'New version V' + d.latest_version + d.latest_build + ' (' + d.latest_date + ')' : 'This is the latest version';
      } catch (e) { $('#otaMsg').textContent = e.message; }
    };
  }
};

// ---- Files (old File tab) ----
pages.files = async (main) => {
  const d = await api('/api/files');
  const list = d.files.slice().sort((x, y) => x.name.localeCompare(y.name));
  main.innerHTML = '<h1>Files</h1>' +
    '<div class="card wide"><h2>Storage</h2><div class="pad">' + kb(d.used) + ' used of ' + kb(d.total) + bar(d.used, d.total) + '</div></div>' +
    '<div class="card wide"><h2>Files</h2><div class="scroll"><table class="list"><thead><tr><th>Name</th><th>Size</th><th></th></tr></thead><tbody>' +
    (list.map((f) => '<tr><td>' + esc(f.name) + (f.dir ? '/' : '') + '</td><td>' + (f.dir ? '-' : kb(f.size)) + '</td><td class="acts">' +
      (f.dir ? '' : '<a class="btn sm" href="/api/files/get?name=' + encodeURIComponent(f.name) + '">Download</a> ' +
        '<button class="btn sm danger" data-del="' + esc(f.name) + '">Delete</button>') + '</td></tr>').join('') ||
      '<tr><td colspan="3" class="muted">Empty</td></tr>') +
    '</tbody></table></div></div>' +
    '<div class="card wide form"><h2>Upload</h2><div class="frow"><label>File</label><div><input type="file" id="upFile">' +
    '<div class="hint">Names with letters, digits, dot, dash or underscore, up to 31 characters</div></div></div>' +
    '<div class="frow"><label></label><div><button class="btn primary" id="upGo">Upload</button></div></div></div>' +
    '<div class="card wide form"><h2>Format</h2><div class="frow"><label>Erase everything</label><div>' +
    '<button class="btn danger" id="fmt">Format&hellip;</button><div class="hint">Deletes every file; the running settings are written back afterwards</div></div></div></div>';

  main.querySelectorAll('[data-del]').forEach((b) => b.onclick = async () => {
    const n = b.dataset.del;
    const warn = /^default\.cfg/.test(n) ? '\n\nThis is the configuration file: the running settings stay until the next save or reboot.' : '';
    if (!confirm('Delete ' + n + '?' + warn)) return;
    try { await post('/api/files/delete?name=' + encodeURIComponent(n)); toast('Deleted'); route(); } catch (e) { toast(e.message); }
  });
  $('#upGo').onclick = async () => {
    const f = $('#upFile').files[0];
    if (!f) return toast('Choose a file first');
    if (!/^[A-Za-z0-9._-]{1,31}$/.test(f.name)) return toast('Rename the file: letters, digits, . _ - only, up to 31 characters');
    if (f.size > d.total - d.used - 4096) return toast('Not enough space');
    const fd = new FormData();
    fd.append('file', f, f.name);
    try {
      await apiFetch('/api/files/upload', { method: 'POST', body: fd });
      toast('Uploaded');
      route();
    } catch (e) { toast('Upload failed: ' + e.message); }
  };
  $('#fmt').onclick = async () => {
    if (prompt('Every file is erased. Type FORMAT to confirm.') !== 'FORMAT') return;
    try { await post('/api/files/format'); toast('Formatted'); route(); } catch (e) { toast(e.message); }
  };
};
