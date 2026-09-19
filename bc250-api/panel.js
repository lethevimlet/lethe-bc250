/* bc250-api console panel — served by bc250-api at /panel.js and loaded by the ESP32 power page
 * once the API answers, so this part of the page updates with the console, not with a reflash.
 *
 * Contract with the page (keep it stable; the firmware only knows this):
 *   window.bc250Panel.version                 string
 *   window.bc250Panel.mount(root, opts) -> p  root: element to render into
 *                                             opts.api: base URL of bc250-api (no trailing slash)
 *                                             opts.refresh(): ask the page to poll /api/status now
 *   p.update(status)                          called after every successful /api/status poll
 *   p.busy(bool)                              page-level busy (sleep/shutdown in flight)
 * The page keeps: state row, Power on / Shut down / Sleep / Force off, the address editor and the
 * offline notice. Everything below the "Console" header is this file. No "//" restriction here.
 */
(function () {
  'use strict';
  var VERSION = '1.1.0';
  var CSS = [
    '.p-tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(104px,1fr));gap:8px}',
    '.p-tile{background:#22262e;border-radius:11px;padding:10px 12px;min-width:0}',
    '.p-tile .k{color:#8b929c;font-size:.68em;text-transform:uppercase;letter-spacing:.6px}',
    '.p-tile .v{font-size:1.25em;font-weight:600;line-height:1.25;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;font-variant-numeric:tabular-nums}',
    '.p-tile .v small{font-size:.62em;font-weight:500;color:#8b929c;margin-left:2px}',
    '.p-tile .s{color:#8b929c;font-size:.68em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}',
    '.p-pend{display:none;margin:12px 0 0;padding:10px 12px;border-radius:11px;background:#3a2f16;color:#f0c674;font-size:.8em}',
    '.p-pend.show{display:block}.p-pend .b{display:grid;gap:8px;margin-top:8px;grid-template-columns:repeat(auto-fit,minmax(140px,1fr))}',
    '.p-pend button{width:100%;min-height:40px;padding:8px 12px;border:0;border-radius:11px;background:#6b5420;color:#fff;font:inherit;font-weight:600;font-size:.95em;cursor:pointer}',
    '.p-tune{margin-top:14px;display:grid;grid-template-columns:minmax(0,1fr);gap:8px}',
    '.p-row{display:flex;align-items:center;gap:10px;min-height:44px}.p-row .n{flex:1;min-width:0}',
    '.p-row .n b{font-weight:600;font-size:.9em;display:block}',
    '.p-row .n i{font-style:normal;color:#8b929c;font-size:.7em;display:block;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}',
    '.p-seg{display:flex;background:#22262e;border-radius:9px;padding:3px;gap:3px;flex:none}',
    '.p-seg button{width:auto;min-height:36px;padding:6px 14px;border:0;border-radius:7px;background:transparent;color:#8b929c;font:inherit;font-weight:600;font-size:.85em;cursor:pointer}',
    '.p-seg button.sel{background:#3a4150;color:#fff}',
    '.p-tune select{flex:none;min-height:42px;padding:6px 10px;border:1px solid #2b3039;border-radius:9px;background:#22262e;color:#e6e8eb;font:inherit;font-size:.85em;max-width:48%}',
    '.p-busy .p-tune,.p-busy .p-pend{opacity:.5;pointer-events:none}',
    '.p-ver{margin-top:10px;color:#6d747e;font-size:.66em;text-align:right}'
  ].join('\n');

  var TILES = [
    ['fps', 'FPS'], ['gmhz', 'GPU'], ['ctemp', 'CPU'], ['soc', 'Power'], ['fan', 'Fan'], ['vram', 'VRAM']
  ];
  var OPTS = {
    cu: { n: 'Compute units', s: ['24', '26', '28', '30', '32', '34', '36', '38', '40'], u: ' CU', h: function (t) { return t.cu.live + ' routed · 32/40 symmetric'; } },
    cores: { n: 'CPU cores', v: ['6', '8'], h: function (t) { return t.cores.visible + ' visible, needs a warm reboot'; } },
    'cores-auto-reboot': { n: 'Auto reboot for 8 cores', v: ['on', 'off'], h: function () { return 'cold boot gives 6, then one auto reboot'; } },
    hud: { n: 'HUD line', v: ['on', 'off'], h: function () { return 'MangoHud overlay level 1'; } },
    'fan-curve': { n: 'Fan curve', v: ['on', 'off'], h: function (t) {
      if (!t.fan) return 'needs a newer bc250-tune';
      if (t.fan.sensor && t.fan.sensor != 'ok') return t.fan.sensor == 'no-driver' ? 'no fan sensor: nct6687 driver missing' : 'no fan sensor: Super I/O not answering';
      return t.fan.curve == 'on' ? (t.fan.active ? 'software, ' : 'guard stopped it, ') + t.fan.rpm + ' rpm' : 'BIOS curve, ' + t.fan.rpm + ' rpm';
    } },
    'fan-min': { n: 'Fan idle duty', s: ['5', '10', '15', '20', '30', '40'], u: ' %', h: function (t) { return t.fan ? 'below ' + t.fan.low_c + ' °C, 100 % at ' + t.fan.high_c + ' °C' : ''; } },
    'gpu-min': { n: 'GPU floor', s: ['500', '700', '1000', '1175'], u: ' MHz', h: function (t) { return 'now ' + (t.gpu.cur_mhz_estimated ? '~' : '') + t.gpu.cur_mhz + ' MHz'; } },
    'gpu-max': { n: 'GPU ceiling', s: ['1500', '1700', '1850', '2000'], u: ' MHz', h: function () { return '2000 runs hotter'; } },
    uma: { n: 'VRAM (UMA)', s: ['2048', '4096', '6144', '8192', '10240', '12288'], u: ' MB', h: function () { return 'CMOS, needs a reboot'; } },
    res: { n: 'Resolution', s: ['native', '1280x720', '1920x1080', '2560x1440', '3840x2160'], u: '', h: function (t) { return 'session ' + (t.res.session || '?'); } }
  };
  function cur(t, k) {
    switch (k) {
      case 'cu': return t.cu.config; case 'cores': return t.cores.config; case 'cores-auto-reboot': return t.cores.auto_reboot;
      case 'hud': return t.hud.config; case 'fan-curve': return t.fan && t.fan.curve; case 'fan-min': return t.fan && t.fan.min;
      case 'gpu-min': return t.gpu.config_min; case 'gpu-max': return t.gpu.config_max; case 'uma': return t.uma.config; case 'res': return t.res.config;
    }
  }
  var fmt = function (v, d) { return v == null ? '–' : Number(v).toFixed(d || 0); };
  var on = function (x) { return x && x !== '0' && x !== 0; };

  function mount(root, opts) {
    var api = opts.api, refresh = opts.refresh || function () {};
    var busy = false, pageBusy = false, el = {};
    if (!document.getElementById('bc250-panel-css')) {
      var st = document.createElement('style'); st.id = 'bc250-panel-css'; st.textContent = CSS; document.head.appendChild(st);
    }
    root.innerHTML = '';
    var tiles = document.createElement('div'); tiles.className = 'p-tiles';
    TILES.forEach(function (t) {
      var d = document.createElement('div'); d.className = 'p-tile';
      d.innerHTML = '<div class="k">' + t[1] + '</div><div class="v" id="p-' + t[0] + '">–</div><div class="s" id="p-' + t[0] + 's"></div>';
      tiles.appendChild(d);
    });
    var pend = document.createElement('div'); pend.className = 'p-pend';
    pend.innerHTML = '<span id="p-pendt"></span><div class="b"><button id="p-rb">Warm reboot</button><button id="p-rs">Restart session</button></div>';
    var tune = document.createElement('div'); tune.className = 'p-tune';
    var ver = document.createElement('div'); ver.className = 'p-ver'; ver.textContent = 'panel ' + VERSION + ' from the console';
    root.appendChild(tiles); root.appendChild(pend); root.appendChild(tune); root.appendChild(ver);
    var $ = function (id) { return root.querySelector('#' + id); };

    Object.keys(OPTS).forEach(function (k) {
      var o = OPTS[k], row = document.createElement('div'); row.className = 'p-row';
      row.innerHTML = '<div class="n"><b>' + o.n + '</b><i id="p-h-' + k + '"></i></div>';
      if (o.v) {
        var seg = document.createElement('div'); seg.className = 'p-seg'; seg.id = 'p-c-' + k;
        o.v.forEach(function (v) { var b = document.createElement('button'); b.textContent = v; b.dataset.v = v; b.onclick = function () { set(k, v); }; seg.appendChild(b); });
        row.appendChild(seg);
      } else {
        var sel = document.createElement('select'); sel.id = 'p-c-' + k;
        o.s.forEach(function (v) { var e = document.createElement('option'); e.value = v; e.textContent = v + o.u; sel.appendChild(e); });
        sel.onchange = function () { set(k, sel.value); }; row.appendChild(sel);
      }
      tune.appendChild(row);
    });
    $('p-rb').onclick = function () { act('/api/tune/reboot', 'Warm reboot the console now?'); };
    $('p-rs').onclick = function () { act('/api/tune/restart-session', 'Restart the gaming session now? Steam will close.'); };

    function setBusy(b) { busy = b; root.classList.toggle('p-busy', busy || pageBusy); }
    function set(k, v) {
      setBusy(true);
      fetch(api + '/api/tune/set', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ key: k, value: v }) })
        .then(function (r) { return r.json(); }).then(function (j) { if (!j.ok) alert('bc250-tune: ' + (j.output || 'failed')); })
        .catch(function () { alert('console unreachable'); })
        .then(function () { setBusy(false); refresh(); });
    }
    function act(path, q) {
      if (!confirm(q)) return; setBusy(true);
      fetch(api + path, { method: 'POST' }).then(function (r) { return r.json(); }).then(function (j) { if (!j.ok) alert(j.output || 'failed'); })
        .catch(function () {}).then(function () { setBusy(false); setTimeout(refresh, 3000); });
    }
    function tilesUpdate(d) {
      $('p-fps').textContent = d.fps == null ? '–' : Math.round(d.fps);
      $('p-fpss').textContent = d.game_running ? (d.game_running.name || 'app ' + d.game_running.appid) : (d.focus == 'steam' ? 'Steam' : 'idle');
      $('p-gmhz').innerHTML = (d.gpu.mhz_estimated ? '~' : '') + fmt(d.gpu.mhz) + '<small>MHz</small>';
      $('p-gmhzs').textContent = fmt(d.gpu.temp_c) + ' °C';
      $('p-ctemp').innerHTML = fmt(d.cpu.temp_c) + '<small>°C</small>';
      $('p-ctemps').textContent = d.cpu.cores + ' cores · ' + fmt(d.cpu.mhz / 1000, 1) + 'GHz';
      $('p-soc').innerHTML = fmt(d.power.soc_w) + '<small>W SoC</small>';
      $('p-socs').textContent = '~' + fmt(d.power.total_w) + ' W total';
      var noFan = d.fan.rpm == null;
      $('p-fan').textContent = noFan ? 'n/a' : fmt(d.fan.rpm); $('p-fans').textContent = noFan ? 'no fan sensor' : 'rpm';
      $('p-vram').innerHTML = fmt(d.gpu.vram_used_mb / 1024, 1) + '<small>GB</small>';
      $('p-vrams').textContent = 'of ' + fmt(d.gpu.vram_total_mb / 1024, 0) + ' GB';
    }
    function tuneUpdate(t) {
      if (!t) { tune.style.display = 'none'; pend.className = 'p-pend'; return; }
      tune.style.display = '';
      if (busy) return;
      Object.keys(OPTS).forEach(function (k) {
        var o = OPTS[k], v = String(cur(t, k)), c = $('p-c-' + k);
        $('p-h-' + k).textContent = o.h(t);
        if (o.v) { Array.prototype.forEach.call(c.children, function (b) { b.className = b.dataset.v == v ? 'sel' : ''; }); }
        else if (document.activeElement != c) {
          if (!Array.prototype.some.call(c.options, function (e) { return e.value == v; })) { var e = document.createElement('option'); e.value = v; e.textContent = v + o.u; c.appendChild(e); }
          c.value = v;
        }
      });
      var p = t.pending || {}, parts = [];
      if (on(p.reboot)) parts.push(String(p.reboot).replace(/;\s*$/, ''));
      if (on(p.session_restart)) parts.push('gaming session restart (resolution)');
      if (on(p.cold_boot)) parts.push('cold boot (power off) to return to 6 cores');
      pend.className = 'p-pend' + (parts.length ? ' show' : ''); $('p-pendt').textContent = 'Pending: ' + parts.join(' · ');
      $('p-rb').style.display = on(p.reboot) ? '' : 'none'; $('p-rs').style.display = on(p.session_restart) ? '' : 'none';
    }
    return {
      update: function (d) { tilesUpdate(d); tuneUpdate(d.tune); },
      busy: function (b) { pageBusy = !!b; root.classList.toggle('p-busy', busy || pageBusy); }
    };
  }
  window.bc250Panel = { version: VERSION, mount: mount };
})();
