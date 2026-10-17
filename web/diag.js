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
