'use strict';
// APRS messages as a two-pane chat, and the VPN page.

// ack: >0 retries left, 0 no acknowledgement, -1 received, -2 acknowledged (or sent without retries)
const msgState = (m, retry) => m.rx ? '' :
  m.ack === -2 ? '<span class="ok" title="Acknowledged">&#10003;&#10003;</span>' :
  m.ack === 0 ? '<span class="bad" title="No acknowledgement">&#10007; not acknowledged</span>' :
  '<span class="warn" title="Waiting for the acknowledgement">&#8635; try ' + Math.max(1, retry - m.ack + 1) + '/' + retry + '</span>';
const hhmm = (t) => new Date(t * 1000).toLocaleString([], { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' });

pages.messages = async (main) => {
  main.innerHTML = '<h1>Messages</h1>' +
    '<div class="chat card"><div class="convs"><div class="newc"><input id="newCall" class="up" placeholder="New: callsign" maxlength="9" autocomplete="off">' +
    '<button class="btn sm" id="newGo">Open</button></div><div id="convList"></div></div>' +
    '<div class="thread"><div class="thead" id="tHead">Select a conversation</div><div class="tbody" id="tBody"></div>' +
    '<form class="compose" id="compose"><input id="msgText" maxlength="67" placeholder="Message" autocomplete="off" disabled>' +
    '<span class="muted" id="cnt">0/67</span><button class="btn primary" id="sendBtn" disabled>Send</button></form></div></div>' +
    '<div id="msgCfg"></div>';

  let list = [], sel = '';
  await loadCfg(true);
  const retry = () => CFG.msgRetry || 0;
  const convs = () => {
    const by = new Map();
    list.forEach((m) => { if (!by.has(m.call) || by.get(m.call).t < m.t) by.set(m.call, m); });
    return [...by.values()].sort((a, b) => b.t - a.t);
  };
  const draw = () => {
    const cs = convs();
    if (sel && !cs.some((c) => c.call === sel)) cs.unshift({ call: sel, text: '', t: 0 });
    $('#convList').innerHTML = cs.map((c) => '<a href="#" class="conv' + (c.call === sel ? ' act' : '') + '" data-c="' + esc(c.call) + '"><b>' +
      esc(c.call) + '</b><span class="muted">' + (c.t ? hhmm(c.t) : 'new') + '</span><div class="prev">' + esc(c.text) + '</div></a>').join('') ||
      '<div class="muted pad">No messages yet</div>';
    $('#convList').querySelectorAll('.conv').forEach((a) => a.onclick = (e) => { e.preventDefault(); open(a.dataset.c); });
    $('#tHead').textContent = sel || 'Select a conversation';
    const th = list.filter((m) => m.call === sel).sort((a, b) => a.t - b.t);
    $('#tBody').innerHTML = th.map((m) => '<div class="bub ' + (m.rx ? 'in' : 'out') + '"><div>' + esc(m.text) + '</div><small>' +
      hhmm(m.t) + ' #' + m.id + ' ' + msgState(m, retry()) + '</small></div>').join('') ||
      (sel ? '<div class="muted pad">No messages with ' + esc(sel) + ' yet</div>' : '');
    $('#tBody').scrollTop = $('#tBody').scrollHeight;
    $('#msgText').disabled = $('#sendBtn').disabled = !sel;
  };
  const open = (c) => { sel = c.toUpperCase(); draw(); $('#msgText').focus(); };
  $('#newGo').onclick = () => { const c = $('#newCall').value.trim().toUpperCase(); if (/^[A-Z0-9-]{3,9}$/.test(c)) { $('#newCall').value = ''; open(c); } else toast('Callsign: 3 to 9 letters, digits or -'); };
  $('#msgText').oninput = () => { $('#cnt').textContent = $('#msgText').value.length + '/67'; };
  $('#compose').onsubmit = async (e) => {
    e.preventDefault();
    const text = $('#msgText').value;
    if (!text.trim()) return;
    if (/[|~{]/.test(text)) return toast('These characters cannot go in an APRS message: | ~ {');
    $('#sendBtn').disabled = true;
    try {
      await post('/api/messages/send', { to: sel, text });
      refresh().catch(() => {}); // show it at once
      $('#msgText').value = '';
      $('#cnt').textContent = '0/67';
    } catch (err) { toast('Not sent: ' + err.message); }
    $('#sendBtn').disabled = false;
  };

  let listText = '';
  const refresh = async () => {
    const r = await fetch('/api/messages');
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const txt = await r.text();
    if (txt !== listText) { listText = txt; list = JSON.parse(txt); draw(); }
  };
  const stopPoll = poller(refresh, 3000);

  // Settings of the old MSG tab, below the chat
  const cfgBox = $('#msgCfg');
  await formPage(cfgBox, (cfg, meta) => ({
    title: 'Message settings', enable: 'msgEnable',
    intro: cfg.msgEnable ? '' : 'Messaging is disabled: incoming messages are not handled until you enable it.',
    sections: [{
      title: 'Settings',
      fields: [
        { k: 'msgEnable', l: 'Enable', t: 'sw' },
        { k: 'msgMycall', l: 'My callsign', t: 'txt', max: 9, w: 11, up: true, h: 'With SSID, e.g. LU6EWB-9' },
        { k: 'msgRf', l: 'Send over RF', t: 'sw' },
        { k: 'msgInet', l: 'Send over Internet', t: 'sw' },
        pathSel('msgPath', meta),
        { k: 'msgRetry', l: 'Retries', t: 'num', min: 0, max: 99, h: '0 = send once, no acknowledgement expected' },
        { k: 'msgInterval', l: 'Retry interval', t: 'num', min: 1, max: 9999, unit: 's' },
        { k: 'msgEncrypt', l: 'Encryption', t: 'sw', h: 'Hiding the content of amateur radio traffic is not allowed in most countries. Use it only for remote AT commands.' },
        { k: 'msgAESKey', l: 'AES key', t: 'pass', max: 32, w: 34, h: '32 hex characters. Stored; leave as is to keep it' },
      ],
    }],
  }));
  return () => stopPoll();
};

// ---- VPN (old VPN tab, WireGuard) ----
pages.vpn = (main) => formPage(main, (cfg, meta) => ({
  title: 'VPN', enable: 'vpnEn',
  intro: 'WireGuard client. Changes take effect after a reboot.',
  sections: [{
    title: 'WireGuard',
    fields: [
      { k: 'vpnEn', l: 'Enable', t: 'sw' },
      { k: 'vpnPeer', l: 'Server address', t: 'txt', max: 31, w: 32 },
      { k: 'vpnPort', l: 'Server port', t: 'num', min: 1, max: 65535 },
      { k: 'vpnLocal', l: 'Local address', t: 'txt', max: 15, w: 17 },
      { k: 'vpnNetmark', l: 'Netmask', t: 'txt', max: 15, w: 17 },
      { k: 'vpnGW', l: 'Gateway', t: 'txt', max: 15, w: 17 },
      { k: 'vpnPubKey', l: 'Server public key', t: 'txt', max: 44, w: 46 },
      { k: 'vpnPriKey', l: 'Client private key', t: 'pass', max: 44, w: 46, h: 'Stored; leave as is to keep it' },
    ],
  }],
}));
