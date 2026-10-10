'use strict';
// Weather, system Telemetry, Sensors and Modules (hardware) pages.

const SENSOR_CH = [[0, 'NONE']].concat([1, 2, 3, 4, 5, 6, 7, 8, 9, 10].map((n) => [n, 'SENSOR#' + n]));
const LOWHIGH = [[false, 'LOW'], [true, 'HIGH']];
const gpio = (k, l, meta, h) => ({ k, l, t: 'num', min: -1, max: meta.gpioMax, h: h || '-1 = not used' });
const baud = (k, l, meta) => ({ k, l: l || 'Baudrate', t: 'sel', o: meta.baudrates.map((b) => [b, String(b)]) });

// ---- Weather (old WX tab) ----
pages.weather = (main) => formPage(main, (cfg, meta) => {
  const wx = meta.wxSensors.map((name, i) => ({
    name,
    en: { k: 'wxSenEn', i, t: 'sw' },
    ch: { k: 'wxSenCH', i, t: 'sel', o: SENSOR_CH },
    avg: { k: 'wxSenAvg', i, t: 'sel', o: [[false, 'Sample'], [true, 'Average']] },
  }));
  return {
    title: 'Weather', enable: 'wxEn',
    sections: [
      {
        title: 'Station',
        fields: [
          { k: 'wxEn', l: 'Enable', t: 'sw' },
          { k: 'wxMycall', l: 'Callsign', t: 'txt', max: 7, w: 9, up: true, re: CALL_RE, reMsg: '3 to 7 letters/digits' },
          { k: 'wxSSID', l: 'SSID', t: 'sel', o: ssidOpts(13), w: 30 },
          { k: 'wxObject', l: 'Object name', t: 'txt', max: 9, w: 11, h: 'Leave empty if not used (3 to 9 characters)' },
          pathSel('wxPath', meta),
          { k: 'wxComment', l: 'Comment', t: 'txt', max: 24, w: 30 },
          { k: 'wxTime', l: 'Time stamp', t: 'sw' },
        ],
      },
      {
        title: 'Position',
        fields: [
          { k: 'wxInv', l: 'Interval', t: 'num', min: 0, max: 3600, unit: 's' },
          { k: 'wxGPS', l: 'Location', t: 'sel', o: [[false, 'Fixed'], [true, 'GPS']] },
          { k: 'wxTx2rf', l: 'Send to RF', t: 'sw' },
          { k: 'wxTx2inet', l: 'Send to Internet', t: 'sw' },
          { k: 'wxLAT', l: 'Latitude', t: 'num', min: -90, max: 90, step: 0.00001, unit: '&deg; (+N / &minus;S)' },
          { k: 'wxLON', l: 'Longitude', t: 'num', min: -180, max: 180, step: 0.00001, unit: '&deg; (+E / &minus;W)' },
          { k: 'wxALT', l: 'Altitude', t: 'num', min: 0, max: 10000, step: 0.1, unit: 'm above sea level' },
        ],
      },
      {
        title: 'Sensors',
        fields: wx.flatMap((r) => [r.en, r.ch, r.avg]),
        html: '<div class="scroll"><table class="list"><thead><tr><th>Value</th><th>Use</th><th>Sensor</th><th>Reading</th></tr></thead><tbody>' +
          wx.map((r) => '<tr><td>' + esc(r.name) + '</td><td>' + control(r.en) + '</td><td>' + control(r.ch) + '</td><td>' + control(r.avg) + '</td></tr>').join('') +
          '</tbody></table></div>',
        note: 'Each weather value is taken from one of the sensors set up on the Sensors page.',
      },
    ],
  };
});

// ---- System telemetry (old TLM tab) ----
pages.telemetry = (main) => formPage(main, (cfg, meta) => {
  const analog = [], bits = [];
  for (let x = 0; x < 5; x++) {
    analog.push({
      src: { k: 'tlmDataCH', i: x, t: 'sel', o: meta.tlmSystem.map((n, v) => [v, n]) },
      name: { k: 'tlmPARM', i: x, t: 'txt', max: 9, w: 10 },
      unit: { k: 'tlmUNIT', i: x, t: 'txt', max: 7, w: 7 },
      a: { k: 'tlmEQNS', i: x * 3, t: 'num', step: 'any', w: 8 },
      b: { k: 'tlmEQNS', i: x * 3 + 1, t: 'num', step: 'any', w: 8 },
      c: { k: 'tlmEQNS', i: x * 3 + 2, t: 'num', step: 'any', w: 8 },
    });
  }
  for (let x = 0; x < 8; x++) {
    bits.push({
      src: { k: 'tlmDataCH', i: x + 5, t: 'sel', o: meta.tlmBits.map((n, v) => [v, n]) },
      name: { k: 'tlmPARM', i: x + 5, t: 'txt', max: 9, w: 10 },
      unit: { k: 'tlmUNIT', i: x + 5, t: 'txt', max: 7, w: 7 },
      act: { k: 'tlmBIT', t: 'bit', bit: 1 << x },
    });
  }
  const cells = (o, keys) => keys.map((k) => '<td>' + control(o[k]) + '</td>').join('');
  return {
    title: 'Telemetry', enable: 'tlmEn',
    intro: 'Telemetry of the station itself (packet counters and modes). Each mode has its own sensor telemetry on its page.',
    sections: [
      {
        title: 'Station',
        fields: [
          { k: 'tlmEn', l: 'Enable', t: 'sw' },
          { k: 'tlmMycall', l: 'Callsign', t: 'txt', max: 7, w: 9, up: true, re: CALL_RE, reMsg: '3 to 7 letters/digits' },
          { k: 'tlmSSID', l: 'SSID', t: 'sel', o: ssidOpts(), w: 30 },
          pathSel('tlmPath', meta),
          { k: 'tlmComment', l: 'Comment', t: 'txt', max: 24, w: 30, h: 'Now saved (it was lost on every reboot)' },
          { k: 'tlmInfoInv', l: 'Info interval', t: 'num', min: 0, max: 3600, unit: 's', h: 'Parameter names, units and equations' },
          { k: 'tlmDataInv', l: 'Data interval', t: 'num', min: 0, max: 3600, unit: 's' },
          { k: 'tlmTx2rf', l: 'Send to RF', t: 'sw' },
          { k: 'tlmTx2inet', l: 'Send to Internet', t: 'sw' },
        ],
      },
      {
        title: 'Analog channels',
        fields: analog.flatMap((o) => Object.values(o)),
        html: '<div class="scroll"><table class="list tlm"><thead><tr><th>CH</th><th>Source</th><th>Name</th><th>Unit</th><th>a</th><th>b</th><th>c</th></tr></thead><tbody>' +
          analog.map((o, x) => '<tr><td>A' + (x + 1) + '</td>' + cells(o, ['src', 'name', 'unit', 'a', 'b', 'c']) + '</tr>').join('') +
          '</tbody></table></div><div class="hint pad">Value = a&middot;v&sup2; + b&middot;v + c</div>',
      },
      {
        title: 'Digital channels',
        fields: bits.flatMap((o) => Object.values(o)),
        html: '<div class="scroll"><table class="list tlm"><thead><tr><th>CH</th><th>Source</th><th>Name</th><th>Unit</th><th>Active HIGH</th></tr></thead><tbody>' +
          bits.map((o, x) => '<tr><td>B' + (x + 1) + '</td>' + cells(o, ['src', 'name', 'unit', 'act']) + '</tr>').join('') +
          '</tbody></table></div>',
      },
    ],
  };
});

// ---- Sensors (old SENSOR tab) ----
// Defaults the old page filled in when the type or the port changed
const SENSOR_DEFAULTS = [null, ['Co2', 'ppm'], ['CH2O', 'μg/m³'], ['TVOC', 'μg/m³'], ['PM2.5', 'μg/m³'], ['PM10.0', 'μg/m³'],
  ['Temperature', '°C'], ['Humidity', '%RH'], ['Pressure', 'hPa'], ['WindSpeed', 'kPh'], ['WindCourse', '°'], ['Rain', 'mm'],
  ['Luminosity', 'W/m³'], ['SoilTemp', '°C'], ['SoilMoisture', '%VWC'], ['WaterTemp', '°C'], ['WaterTDS', ' '], ['WaterLevel', 'mm'],
  ['WaterFlow', 'L/min'], ['Voltage', 'V'], ['Current', 'A'], ['Power', 'W'], ['Energy', 'Wh'], ['Frequency', 'Hz'], ['PF', ' '],
  ['Satellite', ' '], ['HDOP', ' '], ['Battery', 'V'], ['BattLevel', '%']];
const portAddress = (p) => (p >= 10 && p <= 13 ? 118 : p === 16 || p === 17 ? 90 : p === 23 ? 1 : p === 24 || p === 25 ? 1000 : 0);

pages.sensors = async (main) => {
  let timer = 0;
  await formPage(main, (cfg, meta) => {
    const n = meta.sensorCount, S = [];
    for (let x = 0; x < n; x++) {
      const b = x * 11; // "Sensor" holds 11 values per sensor
      S.push({
        en: { k: 'Sensor', i: b, t: 'sw' },
        port: { k: 'Sensor', i: b + 1, t: 'sel', o: meta.sensorPorts.map((p, v) => [v, p]) },
        addr: { k: 'Sensor', i: b + 2, t: 'num', min: 0, max: 6500, w: 6, l: 'Address' },
        sample: { k: 'Sensor', i: b + 3, t: 'num', min: 0, max: 9999, w: 6, l: 'Sample' },
        avg: { k: 'Sensor', i: b + 4, t: 'num', min: 0, max: 999, w: 5, l: 'Average' },
        a: { k: 'Sensor', i: b + 5, t: 'num', step: 'any', w: 8 },
        bb: { k: 'Sensor', i: b + 6, t: 'num', step: 'any', w: 8 },
        c: { k: 'Sensor', i: b + 7, t: 'num', step: 'any', w: 8 },
        type: { k: 'Sensor', i: b + 8, t: 'sel', o: meta.sensorTypes.map((t, v) => [v, t]) },
        parm: { k: 'Sensor', i: b + 9, t: 'txt', max: 14, w: 12 },
        unit: { k: 'Sensor', i: b + 10, t: 'txt', max: 9, w: 7 },
      });
    }
    const td = (f) => '<td>' + control(f) + '</td>';
    return {
      title: 'Sensors',
      sections: [
        {
          title: 'Readings',
          fields: [],
          html: '<div class="scroll"><table class="list"><thead><tr><th>#</th><th>Name</th><th>Sample</th><th>Average</th></tr></thead><tbody id="senLive">' +
            S.map((s, x) => '<tr><td>' + (x + 1) + '</td><td>' + esc(cfg.Sensor[x * 11 + 9]) + '</td><td data-s="' + x + '">-</td><td data-a="' + x + '">-</td></tr>').join('') +
            '</tbody></table></div><div class="hint pad">Refreshed every 5 s</div>',
        },
        {
          title: 'Setup',
          fields: S.flatMap((s) => Object.values(s)),
          html: '<div class="scroll"><table class="list tlm"><thead><tr><th>#</th><th>On</th><th>Type</th><th>Name</th><th>Unit</th><th>Port</th>' +
            '<th>Addr/Reg/GPIO</th><th>Sample s</th><th>Average s</th><th>a</th><th>b</th><th>c</th></tr></thead><tbody>' +
            S.map((s, x) => '<tr><td>' + (x + 1) + '</td>' + [s.en, s.type, s.parm, s.unit, s.port, s.addr, s.sample, s.avg, s.a, s.bb, s.c].map(td).join('') + '</tr>').join('') +
            '</tbody></table></div><div class="hint pad">Value = a&middot;v&sup2; + b&middot;v + c. Choosing a type fills name and unit; choosing a port fills the usual address.</div>',
          wire: (form) => S.forEach((s) => {
            form.querySelector('#' + fid(s.type)).addEventListener('change', (e) => {
              const d = SENSOR_DEFAULTS[e.target.selectedIndex];
              if (d) { form.querySelector('#' + fid(s.parm)).value = d[0]; form.querySelector('#' + fid(s.unit)).value = d[1]; }
            });
            form.querySelector('#' + fid(s.port)).addEventListener('change', (e) => {
              form.querySelector('#' + fid(s.addr)).value = portAddress(e.target.selectedIndex);
            });
          }),
        },
      ],
    };
  });
  const live = async () => {
    try {
      const v = await api('/api/sensors');
      v.forEach((r, x) => {
        const on = CFG.Sensor[x * 11];
        const s = main.querySelector('[data-s="' + x + '"]'), a = main.querySelector('[data-a="' + x + '"]');
        if (s) s.textContent = on ? r.sample + ' ' + CFG.Sensor[x * 11 + 10] : 'off';
        if (a) a.textContent = on ? r.average + ' ' + CFG.Sensor[x * 11 + 10] : '';
      });
    } catch (e) { /* keep the last values */ }
  };
  live();
  timer = setInterval(live, 5000);
  return () => clearInterval(timer);
};

// ---- Modules: hardware wiring (old MOD tab) ----
pages.modules = (main) => formPage(main, (cfg, meta) => {
  const uart = (n) => ({
    title: 'UART' + n,
    fields: [
      { k: 'uart' + n + 'En', l: 'Enable', t: 'sw' },
      gpio('uart' + n + 'RX', 'RX GPIO', meta), gpio('uart' + n + 'TX', 'TX GPIO', meta), gpio('uart' + n + 'RTS', 'RTS/DE GPIO', meta),
      baud('uart' + n + 'BR', 'Baudrate', meta),
    ],
  });
  const i2c = (p, title) => ({
    title,
    fields: [
      { k: p + 'En', l: 'Enable', t: 'sw' }, gpio(p + 'SDA', 'SDA GPIO', meta), gpio(p + 'SCK', 'SCK GPIO', meta),
      { k: p + 'Freq', l: 'Frequency', t: 'num', min: 1000, max: 800000, unit: 'Hz', w: 9 },
    ],
  });
  const counter = (n) => ({
    title: 'Counter ' + n,
    fields: [{ k: 'cnt' + n + 'En', l: 'Enable', t: 'sw' }, gpio('cnt' + n + 'IO', 'Input GPIO', meta), { k: 'cnt' + n + 'Act', l: 'Active', t: 'sel', o: LOWHIGH }],
  });
  const sections = [
    uart(0), uart(1),
    { title: '1-Wire bus', fields: [{ k: 'oneWireEn', l: 'Enable', t: 'sw' }, gpio('oneWireIO', 'GPIO', meta)] },
    {
      title: 'Radio wiring',
      fields: [
        { k: 'adcAtten', l: 'ADC attenuation', t: 'sel', o: meta.adcAtten.map((a, v) => [v, a]) },
        { k: 'adcOffset', l: 'ADC DC offset', t: 'num', min: 0, max: 2500, unit: 'mV' },
        baud('rfBaudrate', 'Module UART baudrate', meta),
        gpio('rfRx', 'Module UART RX GPIO', meta), gpio('rfTx', 'Module UART TX GPIO', meta),
        gpio('rfPD', 'PD GPIO', meta), { k: 'rfPDAct', l: 'PD active', t: 'sel', o: LOWHIGH },
        gpio('rfPWR', 'H/L power GPIO', meta), { k: 'rfPWRAct', l: 'H/L active', t: 'sel', o: LOWHIGH },
        gpio('rfSQL', 'SQL GPIO', meta), { k: 'rfSQLAct', l: 'SQL active', t: 'sel', o: LOWHIGH },
        gpio('rfPTT', 'PTT GPIO', meta), { k: 'rfPTTAct', l: 'PTT active', t: 'sel', o: LOWHIGH },
      ],
    },
    i2c('i2c', 'I2C 0 (display)'), i2c('i2c1', 'I2C 1'), counter(0), counter(1),
    {
      title: 'GNSS',
      fields: [
        { k: 'gnssEn', l: 'Enable', t: 'sw' },
        { k: 'gnssCH', l: 'Port', t: 'sel', o: meta.gnssPorts.map((p, v) => [v, p]) },
        { k: 'gnssAT', l: 'AT command', t: 'txt', max: 29, w: 30, h: 'Sent to the receiver at start; leave empty if not needed' },
        { k: 'gnssTCPHost', l: 'TCP host', t: 'txt', max: 19, w: 22, h: 'For port TCP' },
        { k: 'gnssTCPPort', l: 'TCP port', t: 'num', min: 0, max: 65535, h: 'Usually 1024 or above' },
      ],
    },
    {
      title: 'Modbus',
      fields: [
        { k: 'modbusEn', l: 'Enable', t: 'sw' },
        { k: 'modbusCh', l: 'Port', t: 'sel', o: meta.gnssPorts.map((p, v) => [v, p]) },
        { k: 'modbusAddr', l: 'Address', t: 'num', min: 0, max: 247 },
        gpio('modbusDE', 'DE GPIO', meta),
      ],
    },
    {
      title: 'External TNC',
      fields: [
        { k: 'extTNCEn', l: 'Enable', t: 'sw' },
        { k: 'extTNCCh', l: 'Port', t: 'sel', o: meta.tncPorts.map((p, v) => [v, p]) },
        { k: 'extTNCMode', l: 'Mode', t: 'sel', o: meta.tncModes.map((m, v) => [v, m]) },
      ],
    },
    {
      title: 'AT command channels',
      fields: [
        { k: 'cmdOnMqtt', l: 'MQTT', t: 'sw' },
        { k: 'cmdOnMsg', l: 'APRS message', t: 'sw' },
        { k: 'cmdOnBluetooth', l: 'Bluetooth', t: 'sw' },
        { k: 'cmdOnUart', l: 'UART port', t: 'sel', o: meta.tncPorts.map((p, v) => [v, p]) },
      ],
      note: 'Where the station accepts configuration commands. Keep off what you do not use.',
    },
  ];
  if (meta.features.ppp) {
    sections.push({
      title: 'Cellular modem (PPPoS)',
      fields: [
        { k: 'pppEn', l: 'Enable', t: 'sw' },
        { k: 'pppGNSS', l: 'Modem GNSS', t: 'sw' },
        { k: 'pppNAPT', l: 'NAPT (share to WiFi)', t: 'sw' },
        { k: 'pppAPN', l: 'APN', t: 'txt', max: 31, w: 32 },
        { k: 'pppPin', l: 'SIM PIN', t: 'pass', max: 7, w: 10, h: 'Stored; leave as is to keep it' },
        { k: 'pppSerial', l: 'Port', t: 'sel', o: [[0, 'UART0'], [1, 'UART1']], h: 'That UART is turned off for other uses' },
        baud('pppSerialBaudrate', 'Baudrate', meta),
        gpio('pppRX', 'RX GPIO', meta), gpio('pppTX', 'TX GPIO', meta),
        gpio('pppRST', 'Reset GPIO', meta), { k: 'pppRSTAct', l: 'Reset active', t: 'sel', o: LOWHIGH },
        { k: 'pppRSTDelay', l: 'Reset delay', t: 'num', min: 0, max: 999999, unit: 'ms' },
      ],
    });
  }
  return {
    title: 'Modules',
    intro: 'How the hardware is wired. Wrong GPIO numbers can stop a function from working; changes take effect after a reboot.',
    sections,
    rebootToApply: 'Saved. Hardware wiring changes take effect after a reboot.',
  };
});
