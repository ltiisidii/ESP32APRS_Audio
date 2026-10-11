'use strict';
// APRS objects of this station (repeaters, events, points of interest). The firmware sends them
// (objects.cpp): decay after a change, its own slot, real UTC time stamps, kill when turned off.

const OBJ_N = 5;
const OBJ_PATHS = [[0, 'Direct (no digipeater)'], [1, 'WIDE1-1'], [2, 'WIDE2-1'], [3, 'WIDE2-2']];
const OBJ_NAME_RE = /^(?=.*[^ ])[\x20-\x7b\x7d]{3,9}$/; // 3..9 printable, not only spaces, no | or ~
const OBJ_TEXT_RE = /^[\x20-\x7b\x7d]*$/;

pages.objects = async (main) => {
  let timer = 0;
  await formPage(main, (cfg, meta) => {
    const sections = [];
    for (let i = 0; i < OBJ_N; i++) {
      sections.push({
        title: 'Object ' + (i + 1) + (cfg.objName[i] ? ' &middot; ' + esc(cfg.objName[i]) : ''),
        fields: [
          { k: 'objEn', i, l: 'On the air', t: 'sw', h: 'Turning it off sends it as killed, so maps and radios remove it' },
          { k: 'objName', i, l: 'Name', t: 'txt', max: 9, w: 11, re: /^$|^(?=.*[^ ])[\x20-\x7b\x7d]{3,9}$/, reMsg: '3 to 9 characters, no | or ~',
            h: 'Unique on the network. Repeaters usually use the frequency, e.g. 147.030xx or 145375KHz' },
          { k: 'objSym', i, l: 'Symbol', t: 'sym', h: 'r = repeater, E = event, ; = campground, h = hospital' },
          { k: 'objLat', i, l: 'Latitude', t: 'num', min: -90, max: 90, step: 0.00001, unit: '&deg; (+N / &minus;S)' },
          { k: 'objLon', i, l: 'Longitude', t: 'num', min: -180, max: 180, step: 0.00001, unit: '&deg; (+E / &minus;W)' },
          { k: 'objCmt', i, l: 'Comment', t: 'txt', max: 43, w: 45, re: OBJ_TEXT_RE, reMsg: 'printable characters, no | or ~',
            h: 'Up to 43 characters. Repeaters: tone and offset first, e.g. T088 -060 Club repeater' },
          { k: 'objInv', i, l: 'Interval', t: 'num', min: 10, max: 120, unit: 'min', h: 'After a change it also goes out at 30 s, 1, 2, 4, 8 and 16 min' },
          { k: 'objPath', i, l: 'PATH', t: 'sel', o: OBJ_PATHS, h: 'Local information: WIDE2-1 is usually enough' },
          { k: 'objRF', i, l: 'Send to RF', t: 'sw' },
          { k: 'objIS', i, l: 'Send to Internet', t: 'sw' },
          { k: 'objExp', i, l: 'Remove after', t: 'num', min: 0, max: 8760, unit: 'h', h: '0 = never. For temporary objects (events): killed by itself' },
        ],
        extra: '<div class="frow"><label>Status</label><div><span data-obj-st="' + i + '" class="muted">-</span>' +
          '<div class="hint mono" data-obj-pk="' + i + '"></div></div></div>' +
          '<div class="frow"><label></label><div><button type="button" class="btn sm" data-kill="' + (i + 1) + '">Kill now</button>' +
          '<div class="hint">Turns it off and removes it from maps and radios (sent as killed 3 times)</div></div></div>',
      });
    }
    sections[0].wire = (form) => form.querySelectorAll('[data-kill]').forEach((b) => b.onclick = async () => {
      const n = b.dataset.kill;
      if (!confirm('Kill object ' + n + '? It is turned off and removed from maps and radios.')) return;
      try {
        await api('/api/objects/kill?i=' + n, { method: 'POST' });
        setGuard(null);
        toast('Object ' + n + ' killed');
        route();
      } catch (e) { toast('Kill failed: ' + e.message); }
    });
    return {
      title: 'Objects',
      intro: 'Objects this station announces on APRS: repeaters, events, places. They go out with the station callsign ' +
        '(iGate page) and the real UTC time; nothing is sent until the clock is set. A new or edited object is repeated ' +
        'quickly at first and then every interval; objects are never sent together.',
      sections,
      validate: (patch) => {
        for (let i = 0; i < OBJ_N; i++) {
          const on = (patch.objEn || cfg.objEn)[i], name = (patch.objName || cfg.objName)[i];
          if (on && !OBJ_NAME_RE.test(name || '')) return 'Object ' + (i + 1) + ': a name of 3 to 9 characters is needed to put it on the air';
        }
        return null;
      },
    };
  });
  const ago = (t) => { const s = Math.max(0, Math.round(Date.now() / 1000 - t)); return s < 90 ? s + ' s ago' : s < 5400 ? Math.round(s / 60) + ' min ago' : Math.round(s / 3600) + ' h ago'; };
  const live = async () => {
    try {
      const st = await api('/api/objects');
      st.forEach((o, i) => {
        const el = main.querySelector('[data-obj-st="' + i + '"]'), pk = main.querySelector('[data-obj-pk="' + i + '"]');
        if (!el) return;
        const on = o.state === 'on the air';
        el.className = on ? 'ok' : o.state === 'off' ? 'muted' : 'warn';
        el.textContent = o.state + (o.sent ? ' · sent ' + o.sent + ' (last ' + ago(o.last) + ')' : '') +
          (o.next && o.state !== 'off' ? ' · next in ' + (o.next < 90 ? o.next + ' s' : Math.round(o.next / 60) + ' min') : '') +
          (o.expires ? ' · removed ' + new Date(o.expires * 1000).toLocaleString() : '');
        pk.textContent = o.packet && o.state !== 'off' ? o.packet : '';
      });
    } catch (e) { /* keep the last values */ }
  };
  live();
  timer = setInterval(live, 5000);
  return () => clearInterval(timer);
};
