// Diagnostics: what the station keeps in memory to find problems, in one place. It only reads: nothing is
// stored (the logs are small fixed buffers in RAM, the oldest line is dropped) and they are fetched only
// while this page is open.
pages.diag = async (main) => {
  main.innerHTML = '<h1>Diagnostics</h1><div class="grid" id="dg"></div>';
  const pre = (lines, empty) => lines && lines.length ?
    '<pre class="mono" style="white-space:pre-wrap;margin:0">' + lines.map(esc).join('\n') + '</pre>' :
    '<span class="muted">' + empty + '</span>';
  const draw = async () => {
    const [i, rlog] = await Promise.all([api('/api/info'), api('/api/radiolog')]);
    const r = i.radio || {}, k = i.stats && i.stats.kiss;
    $('#dg').innerHTML =
      card('Start and health', rows([
        ['Firmware', 'V' + esc(i.version)],
        ['Uptime', fmtUptime(i.uptime)],
        ['Last start', esc(i.resetCause || '-')],
        ['Supervisor restart', i.lastRestart ? '<span class="warn">' + esc(i.lastRestart) + '</span>' : 'none'],
        ['Loop stage', esc(i.loopStage || '-')],
        ['BOOT button', esc(i.bootPin || '-')],
        ['RAM free', kb(i.heap) + ' (lowest ' + kb(i.heapMin) + ')'],
        i.heapBlock !== undefined && ['Largest block', kb(i.heapBlock) + ' (lowest ' + kb(i.heapBlockMin) + '; small = memory in pieces)'],
      ])) +
      card('Radio module', rows([
        ['Module', r.rf ? 'enabled' : 'off'],
        r.version && ['Answer to AT+VERSION', esc(r.version)],
        r.initError !== undefined && ['Setup', r.initError ? '<span class="bad">' + esc(r.initError) + '</span>' : '<span class="ok">every setting accepted</span>'],
        r.rssiRaw !== undefined && ['Last RSSI answer', esc(r.rssiRaw)],
        ['Audio samples', r.adcCount],
      ]) + '<h3>Last setup commands and answers</h3>' + pre(rlog, 'No command sent to the module since start'), 'wide') +
      (i.i2c ? card('I2C 0 (display)', rows([
        ['Bus', i.i2c.en ? 'on, SDA ' + i.i2c.sda + ' / SCL ' + i.i2c.scl : 'off'],
        ['Display', !i.i2c.display ? 'off' : i.i2c.driver === false ?
          '<span class="bad">enabled, but this firmware has no display driver: flash a build with a display (SH1106 or SSD1306)</span>' : 'enabled'],
        i.i2c.en && ['Found at start', i.i2c.found.length ?
          i.i2c.found.map(a => '0x' + a.toString(16).toUpperCase().padStart(2, '0') + (a === 0x3C || a === 0x3D ? ' (display)' : '')).join(', ') :
          '<span class="bad">nothing: check wiring and power</span>'],
      ])) : '') +
      (i.tempDiag ? card('CPU temperature sensor', rows([
        ['Chip', esc(i.chip || '-')],
        ['Result', i.tempDiag.state === 'ok' ? '<span class="ok">gives readings</span>' + (i.tempChip !== undefined ? ': ' + i.tempChip + '&deg;C' : '') :
          i.tempDiag.state === 'unavailable' ? '<span class="warn">no reading on this chip</span>' : 'still trying'],
        ['Reads', i.tempDiag.reads + (i.tempDiag.invalid ? ' (' + i.tempDiag.invalid + ' without a reading)' : '')],
        i.tempDiag.raw !== undefined && ['Last raw value', i.tempDiag.raw + ' by registers, ' + i.tempDiag.rawCore + ' by the core function (128 = no reading)'],
        i.tempDiag.skipped && ['Put off', i.tempDiag.skipped + ' times, a packet was coming in'],
      ]) + '<p class="muted">Classic ESP32: the sensor is undocumented and works on some chips only. ' +
        'ESP32-S3/C3/C6: documented sensor. See README, CPU temperature.</p>') : '') +
      (i.net ? card('APRS-IS', rows([
        ['Now', i.net.aprsis ? '<span class="ok">connected</span> to ' + esc(i.net.aprsHost) : '<span class="bad">not connected</span>'],
        ['Logins since start', i.net.isConnects],
        ['Connection lost', i.net.isDrops ? '<span class="warn">' + i.net.isDrops + ' times</span>' : 'never'],
        i.net.isDropAgo !== undefined && ['Last time', fmtUptime(i.net.isDropAgo) + ' ago: ' + esc(i.net.isDropWhy)],
      ])) : '') +
      (i.echo ? card('Own RF echo', (i.echo.tx ? rows([
        ['Sent on RF since start', i.echo.tx],
        ['Repeated by a digipeater', i.echo.txEchoed + ' of ' + i.echo.tx],
        ['Last repeat', i.echo.ago !== undefined ?
          fmtUptime(i.echo.ago) + ' ago by ' + esc(i.echo.by) + ', ' + i.echo.db + ' dBV' : 'none heard yet'],
        i.echo.txSinceEcho >= 3 && ['Warning', '<span class="warn">no repeat for the last ' + i.echo.txSinceEcho +
          ' transmissions: check the radio, cable and antenna, or the digipeater</span>'],
      ]) : '<span class="muted">Nothing sent on RF yet.</span>') +
        '<p class="muted">When a digipeater repeats a packet of this station, it is heard here: proof that the RF ' +
        'transmission reached it, even when aprs.fi shows the Internet copy. Needs a path (WIDE1-1, WIDE2-1) and a ' +
        'digipeater in range.</p>' + pre(i.echo.log && i.echo.log.slice().reverse(), 'No repeat heard since start'), 'wide') : '') +
      card('Internet &rarr; RF decisions',
        i.i2rLog ? pre(i.i2rLog.slice().reverse(), 'Nothing from the Internet to decide yet') :
          '<span class="muted">Off. Turn on "Internet &rarr; RF test" on the iGate page to see what would go to RF and why, without transmitting.</span>', 'wide') +
      card('KISS over TCP', k ? rows([
        ['Client', k.client ? '<span class="ok">connected</span>' : 'none'],
        ['Frames from the client', k.in],
        ['Of them sent on RF', k.tx],
      ]) : '<span class="muted">Off (Modules page)</span>');
  };
  await draw();
  const timer = setInterval(() => draw().then(() => setConn(true), () => setConn(false)), 10000);
  return () => clearInterval(timer);
};
