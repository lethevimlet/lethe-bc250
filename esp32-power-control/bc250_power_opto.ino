// ---------------------------------------------------------------
// BC-250 soft power control  ·  wifi + web + REST + OTA
// ESP32-C3 SuperMini, powered from the PSU +5VSB rail
//
// GPIO 4 -> R3 220R -> PC817 pin 1 (HIGH = LED on = PS_ON low = PSU on)
// GPIO 6 <- R2 1k from BC250 TPMS1 pin 9 (3.3 V while board is up)
// GPIO 7 <- momentary button to ground, internal pull-up
//
// BENCH_MODE 1 = multimeter testing (state machine does not run)
// BENCH_MODE 0 = normal operation
//
// The MAC is printed at boot and exposed in /rest/status and the web
// page, so a DHCP reservation can be made on the router.
//
// OTA is gated on ST_OFF by design. Reflashing reboots the ESP32 and
// the optocoupler LED goes dark before any code runs, so an update
// while the BC-250 is up would hard-cut it.
//
// Arduino IDE: board "ESP32C3 Dev Module", USB CDC On Boot: Enabled.
// Without CDC enabled the sketch still runs but the serial monitor
// stays blank.
//
// No external libraries: WiFi, WebServer, ESPmDNS, ArduinoOTA and
// Preferences all ship with the ESP32 core.
// ---------------------------------------------------------------

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>

#define BENCH_MODE 0

// ---- WiFi configuration ----------------------------------------
const char *WIFI_SSID = "YOUR_SSID";
const char *WIFI_PASS = "YOUR_PASSWORD";   // WPA2 requires 8-63 chars.
                                           // Leave "" for an open network.
const char *MDNS_NAME = "bc250";           // -> http://bc250.local where mDNS
                                           // works; otherwise use the reserved IP

// bc250-api on the console (see ../bc250-api). The web page polls it from
// the *browser*, every 5 s while the machine is RUNNING, so the ESP32 does
// no extra work. This is only the default: the address can be changed from
// the page footer (or GET /rest/console?url=...) and is kept in NVS, so a
// new console IP does not need a reflash. "" hides the console panel.
const char *CONSOLE_API = "http://YOUR_CONSOLE_IP:8250";   // e.g. http://192.168.1.50:8250
Preferences prefs;
String consoleApi;                         // NVS value, else CONSOLE_API

const uint32_t WIFI_RETRY = 300000;        // reconnect attempt, 5 min

// ---- OTA -------------------------------------------------------
const char *OTA_PASS = "CHANGE_ME";        // Never ship empty: this
                                           // firmware owns the power path.
bool otaEnabled = false;                   // only armed while state == ST_OFF

// ---- Pins ------------------------------------------------------
const uint8_t PIN_OPTO   = 4;   // drives the PC817 LED through R3
const uint8_t PIN_SENSE  = 6;
const uint8_t PIN_BUTTON = 7;

// ---- Timing. All milliseconds ----------------------------------
const uint32_t BOOT_BLANKING  = 15000;  // ignore sense while the board boots
const uint32_t SENSE_LOW_HOLD = 10000;  // sense must stay low this long to count
                                        // (long enough to ride out a warm reboot)
const uint32_t DEBOUNCE       = 40;     // button debounce
const uint32_t LONG_PRESS     = 5000;   // hold this long to force power off
const uint32_t MIN_OFF        = 5000;   // dwell in STOPPING so PSU caps drain
const uint32_t START_TIMEOUT  = 45000;  // give up if the board never comes up

enum State { ST_OFF, ST_STARTING, ST_RUNNING, ST_STOPPING };

State    state         = ST_OFF;
uint32_t stateSince    = 0;
uint32_t senseLowSince = 0;

bool     btnStable    = HIGH;
bool     btnLastRead  = HIGH;
uint32_t btnChangedAt = 0;
uint32_t btnPressedAt = 0;
bool     longFired    = false;
bool     longArmed    = false;   // was the machine running when pressed?

// Requests arriving over the web. The loop consumes these flags;
// hardware is never touched from an HTTP handler.
volatile bool reqOn  = false;
volatile bool reqOff = false;

WebServer server(80);

uint32_t wifiLastAttempt = 0;
bool     wifiWasUp       = false;
bool     mdnsUp          = false;

static const char *stateName(State s) {
  switch (s) {
    case ST_OFF:      return "OFF";
    case ST_STARTING: return "STARTING";
    case ST_RUNNING:  return "RUNNING";
    case ST_STOPPING: return "STOPPING";
  }
  return "?";
}

void enterState(State s) {
  state = s;
  stateSince = millis();
  senseLowSince = 0;
  Serial.printf("[state] %s\n", stateName(s));
}

void psuOn() {
  digitalWrite(PIN_OPTO, HIGH);   // LED on -> phototransistor conducts -> PS_ON low
  Serial.println("[psu] PS_ON asserted");
}

void psuOff() {
  digitalWrite(PIN_OPTO, LOW);    // LED off -> phototransistor open -> PS_ON released
  Serial.println("[psu] PS_ON released");
}

// True when the BC250 is reporting that it is powered.
bool boardAlive() {
  return digitalRead(PIN_SENSE) == HIGH;
}

// ---- Web page --------------------------------------------------
// Self-contained, no CDN: the network may have no internet access.
// Note: the Arduino prototype generator cannot see raw-string bounds and
// treats "//" as a comment, so keep "//" out of the page: regex slashes as
// [/][/], URL slashes as &#47;&#47;.
// Fluid layout: single column on phones, wider card from 560px up, and a
// compact layout when vertical space is tight (phone in landscape).
// The console panel (stats + tune switches) talks to bc250-api on the
// BC-250 straight from the browser, only while the machine is RUNNING.
static const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!doctype html><html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="dark light">
<meta name="theme-color" content="#14161a">
<title>BC-250</title><style>
*{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
body{margin:0;min-height:100svh;display:flex;align-items:center;justify-content:center;
 padding:max(16px,env(safe-area-inset-top)) max(16px,env(safe-area-inset-right))
         max(16px,env(safe-area-inset-bottom)) max(16px,env(safe-area-inset-left));
 background:#14161a;color:#e6e8eb;
 font:clamp(15px,1.4vw + 12px,17px)/1.5 system-ui,-apple-system,"Segoe UI",sans-serif}
.card{width:100%;max-width:420px;background:#1c1f25;border:1px solid #2b3039;
 border-radius:16px;padding:clamp(20px,4vw,30px)}
h1{margin:0 0 4px;font-size:1.2em;font-weight:600;letter-spacing:.2px}
.sub{margin:0 0 20px;color:#8b929c;font-size:.82em}
.state{display:flex;align-items:center;gap:10px;padding:14px 16px;background:#22262e;
 border-radius:11px;margin-bottom:18px;min-width:0}
.dot{width:11px;height:11px;border-radius:50%;background:#555c66;flex:none;
 transition:background .25s,box-shadow .25s}
.dot.on{background:#3fbf63;box-shadow:0 0 9px #3fbf63}
.dot.mid{background:#e0a33a;box-shadow:0 0 9px #e0a33a}
.dot.zz{background:#5b8def;box-shadow:0 0 9px #5b8def}
.lbl{font-weight:600;letter-spacing:.4px;overflow:hidden;text-overflow:ellipsis;
 white-space:nowrap}
.meta{margin-left:auto;color:#8b929c;font-size:.78em;flex:none}
.btns{display:grid;gap:10px;grid-template-columns:1fr 1fr}
.pend .btns{grid-template-columns:repeat(auto-fit,minmax(140px,1fr))}
button{width:100%;min-height:48px;padding:13px 14px;border:0;border-radius:11px;
 font:inherit;font-weight:600;cursor:pointer;color:#fff;
 -webkit-tap-highlight-color:transparent;touch-action:manipulation;
 transition:background .15s,opacity .15s}
#on{background:#2f7d4a}#off{background:#8a3030}
/* Sleep/Wake joins Power on / Force off as a third button once bc250-api answers */
#slp{display:none;background:#2f4f7d}#slp.wake{background:#2f7d4a}
.card.ok .btns.main{grid-template-columns:1fr 1fr 1fr}.card.ok #slp{display:block}
button:disabled{opacity:.38;cursor:not-allowed}
.warn{margin:16px 0 0;color:#8b929c;font-size:.76em}
.net{margin-top:16px;padding-top:13px;border-top:1px solid #2b3039;
 color:#6d747e;font-size:.72em;display:flex;flex-wrap:wrap;gap:4px 14px;
 justify-content:space-between}
#capi{cursor:pointer;text-decoration:underline dotted;text-underline-offset:3px}
.id{margin-top:7px;color:#6d747e;font-size:.7em;text-align:center;
 font-family:ui-monospace,SFMono-Regular,Menlo,monospace;letter-spacing:.4px;
 word-break:break-all}
/* console panel */
#con{display:none;margin-top:18px;padding-top:16px;border-top:1px solid #2b3039}
#con.show{display:block}
.hd{display:flex;align-items:baseline;gap:8px;margin:0 0 10px}
.hd h2{margin:0;font-size:.95em;font-weight:600}
.hd .meta{font-size:.72em}
.tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(104px,1fr));gap:8px}
.tile{background:#22262e;border-radius:11px;padding:10px 12px;min-width:0}
.tile .k{color:#8b929c;font-size:.68em;text-transform:uppercase;letter-spacing:.6px}
.tile .v{font-size:1.25em;font-weight:600;line-height:1.25;white-space:nowrap;
 overflow:hidden;text-overflow:ellipsis;font-variant-numeric:tabular-nums}
.tile .v small{font-size:.62em;font-weight:500;color:#8b929c;margin-left:2px}
.tile .s{color:#8b929c;font-size:.68em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.pend{display:none;margin:12px 0 0;padding:10px 12px;border-radius:11px;background:#3a2f16;
 color:#f0c674;font-size:.8em}
.pend.show{display:block}
.pend .btns{margin-top:8px}
.pend button{min-height:40px;padding:8px 12px;background:#6b5420;font-size:.95em}
.tune{margin-top:14px;display:grid;grid-template-columns:minmax(0,1fr);gap:8px}
.row{display:flex;align-items:center;gap:10px;min-height:44px}
.row .n{flex:1;min-width:0}
.row .n b{font-weight:600;font-size:.9em;display:block}
.row .n i{font-style:normal;color:#8b929c;font-size:.7em;display:block;white-space:nowrap;
 overflow:hidden;text-overflow:ellipsis}
.seg{display:flex;background:#22262e;border-radius:9px;padding:3px;gap:3px;flex:none}
.seg button{width:auto;min-height:36px;padding:6px 14px;border-radius:7px;background:transparent;
 color:#8b929c;font-size:.85em}
.seg button.sel{background:#3a4150;color:#fff}
select{flex:none;min-height:42px;padding:6px 10px;border:1px solid #2b3039;border-radius:9px;
 background:#22262e;color:#e6e8eb;font:inherit;font-size:.85em;max-width:48%}
.busy .tune,.busy .pend{opacity:.5;pointer-events:none}
/* tiles and switches only after a successful poll; a notice with retry otherwise */
#con:not(.ok) .tiles,#con:not(.ok) .pend,#con:not(.ok) .tune{display:none}
#nocon{display:none;color:#8b929c;font-size:.8em}
#con:not(.ok) #nocon{display:block}
#nocon p{margin:0 0 8px}#nocon p:last-child{margin:0}
#nocon a{color:#aab2bd;text-decoration:underline dotted;text-underline-offset:3px}
#nocon b{color:#c5cad1;font-weight:600}
@media (min-width:560px){.card{max-width:520px}}
@media (max-height:460px) and (orientation:landscape){
 body{align-items:flex-start}
 .card{max-width:620px;padding:16px 20px}
 .sub{margin-bottom:12px}
 .state{margin-bottom:12px;padding:10px 14px}
 .warn{margin-top:11px}
 .net{margin-top:11px;padding-top:9px}
}
@media (hover:hover){#on:hover:not(:disabled){background:#389356}
 #off:hover:not(:disabled){background:#a03838}}
@media (prefers-reduced-motion:reduce){*{transition:none!important}}
</style></head><body>
<div class="card">
<h1>BC-250</h1><p class="sub">Power control</p>
<div class="state"><span class="dot" id="dot"></span>
<span class="lbl" id="st">...</span>
<span class="meta" id="up"></span></div>
<div class="btns main">
<button id="on">Power on</button>
<button id="off">Force off</button>
<button id="slp">Sleep</button>
</div>
<p class="warn">Force off is a hard cut, the same as holding the button for five
seconds. For a clean shutdown, use the operating system.</p>
<div id="con">
<div class="hd"><h2>Console</h2><span class="meta" id="cst">connecting</span></div>
<div id="nocon"><p id="nc"></p>
<p><a href="#" id="retry">retry</a> &nbsp;·&nbsp; <a href="#" id="chg">change address</a></p>
<p>Live stats and the tune switches need the <b>bc250-api</b> service on the BC-250: see
<a href="https:&#47;&#47;github.com/lethevimlet/lethe-bc250#46-stats-and-switches-over-the-network-bc250-api" target="_blank" rel="noopener">the README, §4.6</a>.</p></div>
<div class="tiles">
<div class="tile"><div class="k">FPS</div><div class="v" id="fps">–</div><div class="s" id="game"></div></div>
<div class="tile"><div class="k">GPU</div><div class="v" id="gmhz">–</div><div class="s" id="gtemp"></div></div>
<div class="tile"><div class="k">CPU</div><div class="v" id="ctemp">–</div><div class="s" id="cmhz"></div></div>
<div class="tile"><div class="k">Power</div><div class="v" id="soc">–</div><div class="s" id="tot"></div></div>
<div class="tile"><div class="k">Fan</div><div class="v" id="fan">–</div><div class="s" id="fans">rpm</div></div>
<div class="tile"><div class="k">VRAM</div><div class="v" id="vram">–</div><div class="s" id="vrams"></div></div>
</div>
<div class="pend" id="pend"><span id="pendt"></span>
<div class="btns"><button id="rb">Warm reboot</button><button id="rs">Restart session</button></div></div>
<div class="tune" id="tune"></div>
</div>
<div class="net"><span id="rssi"></span><span id="ota"></span><span id="sense"></span>
<span id="capi" title="tap to change the bc250-api address"></span></div>
<div class="id" id="id"></div>
</div>
<script>
const $=i=>document.getElementById(i);
function hms(s){const h=(s/3600|0),m=(s%3600/60|0);return h?h+"h "+m+"m":m+"m"}
let api=null,running=false,ct=null,busy=false,tuneBuilt=false,conState=null,asleep=false;
// Status line: the ESP32's power state, refined by the console once bc250-api answers:
// RUNNING = a game is running, IDLE = on with no game, SLEEP = the fake sleep holds it.
function showState(s){
 const c=conState&&s=='RUNNING'?conState:s;$('st').textContent=c;
 $('dot').className='dot'+(c=='SLEEP'?' zz':c=='RUNNING'||c=='IDLE'?' on':
  (c=='STARTING'||c=='STOPPING')?' mid':'');
}
async function poll(){
 try{const r=await fetch('/rest/status'),d=await r.json();
  showState(d.state);
  $('up').textContent=hms(d.uptime);
  $('rssi').textContent='RSSI '+d.rssi+' dBm';
  $('sense').textContent='sense '+(d.sense?'HIGH':'LOW');
  $('ota').textContent=d.ota?'OTA ready':'OTA locked';
  $('id').textContent=d.ip+'  ·  '+d.mac;
  $('on').disabled=(d.state!='OFF');
  $('off').disabled=(d.state=='OFF'||d.state=='STOPPING');
  api=new URLSearchParams(location.search).get('console')||d.console||null;
  $('capi').textContent='console '+(d.console?d.console.replace(/^https?:[/][/]/,''):'not set');
  setRunning(d.state=='RUNNING');
 }catch(e){$('st').textContent='offline';setRunning(false);}
}
$('on').onclick=async()=>{await fetch('/rest/on');setTimeout(poll,300)};
$('off').onclick=async()=>{if(!confirm('Hard-cut power to the machine?'))return;
 await fetch('/rest/off');setTimeout(poll,300)};
// The bc250-api address lives in the ESP32's NVS; empty restores the compiled default.
// Editable in every state, so the service can be installed on the console later.
async function editConsole(){const v=prompt('bc250-api address (empty = default)',api||'');
 if(v===null)return;const r=await fetch('/rest/console?url='+encodeURIComponent(v.trim()));
 if(!r.ok)alert('not saved: use http://host:8250');setRunning(false);poll()}
$('capi').onclick=editConsole;
$('chg').onclick=e=>{e.preventDefault();editConsole()};
$('retry').onclick=e=>{e.preventDefault();$('cst').textContent='connecting';cpoll()};
// ---- console panel: polled from the browser, 5 s, only while RUNNING and visible
function setOk(b){$('con').classList.toggle('ok',b);document.querySelector('.card').classList.toggle('ok',b)}
function setRunning(on){
 if(on==running)return;running=on;$('con').classList.toggle('show',on);
 if(ct){clearInterval(ct);ct=null}
 if(on){cpoll();ct=setInterval(cpoll,5000)}else setOk(false);
}
function noCon(msg){setOk(false);$('cst').textContent='offline';$('nc').textContent=msg;
 conState=null;if(running)showState('RUNNING')}
const fmt=(v,d=0)=>v==null?'–':Number(v).toFixed(d);
async function cpoll(){
 if(!running||document.hidden)return;
 if(!api){noCon('No bc250-api address is set.');return}
 try{
  const c=new AbortController(),tm=setTimeout(()=>c.abort(),4000);
  const r=await fetch(api+'/api/status',{cache:'no-store',signal:c.signal});clearTimeout(tm);
  const d=await r.json();render(d);setOk(true);
  $('cst').textContent=d.asleep?'asleep':'live';
 }catch(e){noCon('bc250-api is not answering at '+api.replace(/^https?:[/][/]/,'')+' (still booting, or not installed).')}
}
function render(d){
 asleep=!!d.asleep;conState=asleep?'SLEEP':d.game_running?'RUNNING':'IDLE';showState('RUNNING');
 const b=$('slp');b.textContent=asleep?'Wake':'Sleep';b.className=asleep?'wake':'';
 $('fps').textContent=d.fps==null?'–':Math.round(d.fps);
 $('game').textContent=d.game||(d.focus=='steam'?'Steam':d.fps==null?'idle':'app '+d.focus);
 $('gmhz').innerHTML=(d.gpu.mhz_estimated?'~':'')+fmt(d.gpu.mhz)+'<small>MHz</small>';
 $('gtemp').textContent=fmt(d.gpu.temp_c)+' °C';
 $('ctemp').innerHTML=fmt(d.cpu.temp_c)+'<small>°C</small>';
 $('cmhz').textContent=d.cpu.cores+' cores · '+fmt(d.cpu.mhz/1000,1)+'GHz';
 $('soc').innerHTML=fmt(d.power.soc_w)+'<small>W SoC</small>';
 $('tot').textContent='~'+fmt(d.power.total_w)+' W total';
 $('fan').textContent=fmt(d.fan.rpm);
 $('vram').innerHTML=fmt(d.gpu.vram_used_mb/1024,1)+'<small>GB</small>';
 $('vrams').textContent='of '+fmt(d.gpu.vram_total_mb/1024,0)+' GB';
 renderTune(d.tune);
}
// ---- tune switches (bc250-tune through bc250-api)
const OPTS={
 cu:{n:'Compute units',v:['24','32','40'],h:t=>t.cu.live+' routed'},
 cores:{n:'CPU cores',v:['6','8'],h:t=>t.cores.visible+' visible, needs a warm reboot'},
 'cores-auto-reboot':{n:'Auto reboot for 8 cores',v:['on','off'],h:t=>'cold boot gives 6, then one auto reboot'},
 hud:{n:'HUD line',v:['on','off'],h:t=>'MangoHud overlay level 1'},
 'gpu-min':{n:'GPU floor',s:['500','700','1000','1175'],u:' MHz',h:t=>'now '+(t.gpu.cur_mhz_estimated?'~':'')+t.gpu.cur_mhz+' MHz'},
 'gpu-max':{n:'GPU ceiling',s:['1500','1700','1850','2000'],u:' MHz',h:t=>'2000 runs hotter'},
 uma:{n:'VRAM (UMA)',s:['2048','4096','6144','8192','10240','12288'],u:' MB',h:t=>'CMOS, needs a reboot'},
 res:{n:'Resolution',s:['native','1280x720','1920x1080','2560x1440','3840x2160'],u:'',h:t=>'session '+(t.res.session||'?')}
};
const cur=(t,k)=>({cu:t.cu.config,cores:t.cores.config,'cores-auto-reboot':t.cores.auto_reboot,hud:t.hud.config,'gpu-min':t.gpu.config_min,
 'gpu-max':t.gpu.config_max,uma:t.uma.config,res:t.res.config})[k];
function buildTune(){
 const box=$('tune');box.innerHTML='';
 for(const k in OPTS){const o=OPTS[k];
  const row=document.createElement('div');row.className='row';
  row.innerHTML='<div class="n"><b>'+o.n+'</b><i id="h-'+k+'"></i></div>';
  if(o.v){const seg=document.createElement('div');seg.className='seg';seg.id='c-'+k;
   o.v.forEach(v=>{const b=document.createElement('button');b.textContent=v;b.dataset.v=v;
    b.onclick=()=>tset(k,v);seg.appendChild(b)});row.appendChild(seg);}
  else{const sel=document.createElement('select');sel.id='c-'+k;
   o.s.forEach(v=>{const e=document.createElement('option');e.value=v;e.textContent=v+o.u;sel.appendChild(e)});
   sel.onchange=()=>tset(k,sel.value);row.appendChild(sel);}
  box.appendChild(row);}
 $('rb').onclick=()=>tact('/api/tune/reboot','Warm reboot the console now?',3000);
 $('rs').onclick=()=>tact('/api/tune/restart-session','Restart the gaming session now? Steam will close.',3000);
 tuneBuilt=true;
}
function renderTune(t){
 if(!t){$('tune').style.display='none';$('pend').className='pend';return}
 if(!tuneBuilt)buildTune();$('tune').style.display='';
 if(busy)return;
 for(const k in OPTS){const o=OPTS[k],v=String(cur(t,k)),c=$('c-'+k);
  $('h-'+k).textContent=o.h(t);
  if(o.v){[...c.children].forEach(b=>b.className=b.dataset.v==v?'sel':'')}
  else if(document.activeElement!=c){
   if(![...c.options].some(e=>e.value==v)){const e=document.createElement('option');e.value=v;e.textContent=v+o.u;c.appendChild(e)}
   c.value=v}}
 const p=t.pending||{},msg=[p.reboot,p.session_restart,p.cold_boot].filter(x=>x&&x!=='0').map(x=>String(x).replace(/;\s*$/,'')).join(' · ');
 $('pend').className='pend'+(msg?' show':'');$('pendt').textContent='Pending: '+msg;
 $('rb').style.display=p.reboot&&p.reboot!=='0'?'':'none';
 $('rs').style.display=p.session_restart&&p.session_restart!=='0'?'':'none';
}
function setBusy(b){busy=b;$('con').classList.toggle('busy',b)}
async function tset(k,v){
 setBusy(true);$('cst').textContent='applying '+k+'='+v;
 try{const r=await fetch(api+'/api/tune/set',{method:'POST',headers:{'Content-Type':'application/json'},
   body:JSON.stringify({key:k,value:v})}),j=await r.json();
  if(!j.ok)alert('bc250-tune: '+(j.output||'failed'));
 }catch(e){alert('console unreachable')}
 setBusy(false);cpoll();
}
async function tact(path,q,wait){
 if(q&&!confirm(q))return;setBusy(true);$('cst').textContent=path.split('/').pop()+'…';
 try{const r=await fetch(api+path,{method:'POST'}),j=await r.json();if(!j.ok)alert(j.output||'failed')}
 catch(e){alert('console unreachable')}
 setBusy(false);setTimeout(cpoll,wait||800);
}
$('slp').onclick=()=>tact(asleep?'/api/wake':'/api/sleep');
// Poll only while the tab is visible, to save battery on the phone
// and needless radio wakeups on the ESP32.
let t=null;
function run(on){if(t)clearInterval(t);t=on?setInterval(poll,2000):null;if(on){poll();cpoll()}}
document.addEventListener('visibilitychange',()=>run(!document.hidden));
run(true);
</script></body></html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", PAGE_HTML);
}

void handleStatus() {
  char buf[384];
  snprintf(buf, sizeof(buf),
    "{\"state\":\"%s\",\"sense\":%s,\"uptime\":%lu,\"rssi\":%d,"
    "\"heap\":%lu,\"ota\":%s,\"ip\":\"%s\",\"mac\":\"%s\",\"console\":\"%s\"}",
    stateName(state),
    boardAlive() ? "true" : "false",
    (unsigned long)(millis() / 1000),
    (int)WiFi.RSSI(),
    (unsigned long)ESP.getFreeHeap(),
    otaEnabled ? "true" : "false",
    WiFi.localIP().toString().c_str(),
    WiFi.macAddress().c_str(),
    consoleApi.c_str());
  server.send(200, "application/json", buf);
}

void loadConsoleApi() {
  // Read-only open fails until the namespace exists; the default covers that.
  prefs.begin("bc250", true);
  consoleApi = prefs.getString("console", CONSOLE_API);
  prefs.end();
}

// GET /rest/console?url=http://<console-ip>:8250   ("" = back to the compiled default)
void handleConsole() {
  if (!server.hasArg("url")) {
    server.send(400, "application/json", "{\"error\":\"url missing\"}");
    return;
  }
  String url = server.arg("url");
  url.trim();
  if (url.length() > 96 ||
      (url.length() && !url.startsWith("http://") && !url.startsWith("https://"))) {
    server.send(400, "application/json", "{\"error\":\"url must start with http:// and be short\"}");
    return;
  }
  prefs.begin("bc250", false);
  if (url.length()) prefs.putString("console", url);
  else              prefs.remove("console");
  prefs.end();
  loadConsoleApi();
  Serial.printf("[web] console api -> %s\n", consoleApi.c_str());
  server.send(200, "application/json", "{\"ok\":true,\"console\":\"" + consoleApi + "\"}");
}

void handleOn() {
  reqOn = true;
  Serial.println("[web] ON requested");
  server.send(200, "application/json", "{\"ok\":true,\"action\":\"on\"}");
}

void handleOff() {
  reqOff = true;
  Serial.println("[web] OFF requested (hard cut)");
  server.send(200, "application/json", "{\"ok\":true,\"action\":\"off\"}");
}

void handleNotFound() {
  server.send(404, "application/json", "{\"error\":\"not found\"}");
}

// ---- WiFi ------------------------------------------------------
// Everything non-blocking: the state machine takes priority and the
// radio sorts itself out along the way.
void wifiStart() {
  wifiLastAttempt = millis();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  Serial.printf("[wifi] MAC %s  <- reserve this one on the router\n",
                WiFi.macAddress().c_str());
  WiFi.setSleep(true);                      // modem sleep, cuts average draw
  WiFi.setTxPower(WIFI_POWER_8_5dBm);       // lower current peaks, less heat
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(MDNS_NAME);
  if (strlen(WIFI_PASS) >= 8) WiFi.begin(WIFI_SSID, WIFI_PASS);
  else                        WiFi.begin(WIFI_SSID);   // open network
  Serial.printf("[wifi] connecting to %s\n", WIFI_SSID);
}

void wifiTick() {
  bool up = (WiFi.status() == WL_CONNECTED);

  if (up && !wifiWasUp) {
    Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
    if (!mdnsUp && MDNS.begin(MDNS_NAME)) {
      MDNS.addService("http", "tcp", 80);
      mdnsUp = true;
      Serial.printf("[wifi] http://%s.local\n", MDNS_NAME);
    }
    wifiWasUp = true;
  } else if (!up && wifiWasUp) {
    Serial.println("[wifi] disconnected");
    wifiWasUp = false;
  }

  // Periodic retry. Never blocks.
  if (!up && millis() - wifiLastAttempt >= WIFI_RETRY) {
    Serial.println("[wifi] retrying");
    wifiStart();
  }
}

// ---- OTA -------------------------------------------------------
// Callbacks only. Arming happens in the loop, gated on ST_OFF.
void otaSetup() {
  ArduinoOTA.setHostname(MDNS_NAME);
  ArduinoOTA.setPassword(OTA_PASS);

  ArduinoOTA.onStart([]() {
    // Belt and braces: refuse mid-flight if the machine came up between
    // the arm check and the first packet. Restarting aborts the update
    // before anything is written to the inactive partition.
    if (state != ST_OFF) {
      Serial.println("[ota] refused, machine is not OFF");
      ESP.restart();
    }
    Serial.println("[ota] update starting");
  });

  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    static uint8_t last = 255;
    uint8_t pct = total ? (uint8_t)((uint32_t)done * 100 / total) : 0;
    if (pct != last && pct % 10 == 0) {
      Serial.printf("[ota] %u%%\n", pct);
      last = pct;
    }
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("[ota] done, rebooting");
  });

  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("[ota] error %u\n", (unsigned)e);
  });
}

void setup() {
  // Drive the LED pin low before anything else so a reset never
  // leaves the PSU latched on.
  pinMode(PIN_OPTO, OUTPUT);
  digitalWrite(PIN_OPTO, LOW);

  // Internal pulldown replaces the second divider resistor: the pin
  // reads LOW on its own as soon as TPMS1 stops driving 3.3 V.
  pinMode(PIN_SENSE, INPUT_PULLDOWN);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  // 80 MHz is plenty here and trims both draw and heat.
  setCpuFrequencyMhz(80);

  Serial.begin(115200);
  // USB CDC takes a moment to enumerate; without this the first
  // lines are lost.
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);
  delay(200);

  loadConsoleApi();
  wifiStart();
  otaSetup();

  server.on("/",            handleRoot);
  server.on("/rest/status", handleStatus);
  server.on("/rest/on",     handleOn);
  server.on("/rest/off",    handleOff);
  server.on("/rest/console", handleConsole);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("[web] server up on port 80");

#if BENCH_MODE
  Serial.println("\n[bench] BENCH MODE - the state machine does NOT run");
  Serial.println("[bench] commands:  g = LED ON     |   l = LED OFF");
  Serial.println("[bench]            t = toggle     |   ? = current status");
  Serial.println("[bench] the output starts OFF\n");
#else
  Serial.println("\n[boot] BC250 power controller ready");
  // Note: we cannot "adopt" a running machine after a reset. The LED goes
  // dark the instant GPIO 4 stops driving, so the PSU has already dropped
  // before this code runs. That is the deliberate trade: the circuit fails
  // safe-off, which means an ESP32 crash hard-cuts a running BC-250.
  enterState(ST_OFF);
#endif
}

// Fires once on the press itself, not on release, so holding the button
// down from OFF still starts the machine.
bool buttonDown() {
  bool raw = digitalRead(PIN_BUTTON);
  uint32_t now = millis();

  if (raw != btnLastRead) {
    btnLastRead = raw;
    btnChangedAt = now;
  }
  if (now - btnChangedAt < DEBOUNCE) return false;
  if (raw == btnStable) return false;

  btnStable = raw;
  if (btnStable == LOW) {          // pressed
    btnPressedAt = now;
    longFired = false;
    // Only a press that starts while the machine is up can force it off.
    // Without this, holding the button from OFF starts the PSU and then
    // kills it five seconds later.
    longArmed = (state == ST_RUNNING);
    return true;
  }
  return false;                    // released
}

bool longPressHeld() {
  if (btnStable == LOW && longArmed && !longFired &&
      millis() - btnPressedAt >= LONG_PRESS) {
    longFired = true;
    return true;
  }
  return false;
}

#if BENCH_MODE
// ---- Bench loop -------------------------------------------------------
bool optoState = false;

void setOpto(bool on) {
  optoState = on;
  digitalWrite(PIN_OPTO, on ? HIGH : LOW);
  Serial.printf("[bench] output -> %s  (GPIO4 to ground: %s)\n",
                on ? "ON" : "OFF", on ? "~3.3 V" : "~0 V");
}

void loop() {
  server.handleClient();
  wifiTick();

  // The state machine is not running here, so OTA stays armed whenever
  // the radio is up. Nothing is powered from the bench.
  if (WiFi.status() == WL_CONNECTED) {
    if (!otaEnabled) {
      ArduinoOTA.begin();
      otaEnabled = true;
      Serial.println("[ota] armed (bench mode)");
    }
    ArduinoOTA.handle();
  }

  if (reqOn)  { reqOn  = false; setOpto(true);  }
  if (reqOff) { reqOff = false; setOpto(false); }

  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'g' || c == 'G') setOpto(true);
    else if (c == 'l' || c == 'L') setOpto(false);
    else if (c == 't' || c == 'T') setOpto(!optoState);
    else if (c == '?') {
      Serial.printf("[bench] output=%s  sense=%s  button=%s  ip=%s\n",
                    optoState ? "ON" : "OFF",
                    digitalRead(PIN_SENSE) == HIGH ? "HIGH" : "LOW",
                    digitalRead(PIN_BUTTON) == LOW ? "PRESSED" : "released",
                    WiFi.localIP().toString().c_str());
    }
  }

  // Sample sense and button at the same rate as the real firmware.
  uint32_t now = millis();
  static int lastSense = -1, lastBtn = -1;
  static uint32_t nPress = 0, tDown = 0;

  int s = digitalRead(PIN_SENSE);
  int b = digitalRead(PIN_BUTTON);

  if (s != lastSense) {
    Serial.printf("[bench] sense -> %s\n", s == HIGH ? "HIGH" : "LOW");
    lastSense = s;
  }
  if (b != lastBtn) {
    if (b == LOW) {
      tDown = now;
      nPress++;
      Serial.printf("[bench] button -> PRESSED  (#%lu)\n", (unsigned long)nPress);
    } else {
      Serial.printf("[bench] button -> released (%lu ms)\n",
                    (unsigned long)(now - tDown));
    }
    lastBtn = b;
  }

  delay(10);
}

#else
// ---- Normal loop ------------------------------------------------------
void loop() {
  server.handleClient();
  wifiTick();

  // Arm OTA only while the PSU is off, and only once the radio is up.
  // Reflashing reboots the board and the LED goes dark before any code
  // runs, so an update with the BC-250 live would hard-cut it.
  bool shouldArm = (state == ST_OFF) && (WiFi.status() == WL_CONNECTED);
  if (shouldArm && !otaEnabled) {
    ArduinoOTA.begin();
    otaEnabled = true;
    Serial.println("[ota] armed (machine is OFF)");
  } else if (!shouldArm && otaEnabled) {
    ArduinoOTA.end();
    otaEnabled = false;
    Serial.println("[ota] disarmed");
  }
  if (otaEnabled) ArduinoOTA.handle();

  uint32_t now = millis();
  bool pressed = buttonDown();

  // A web OFF request is equivalent to the long press.
  bool webOff = reqOff;
  if (webOff) reqOff = false;

  if (longPressHeld() || (webOff && state != ST_OFF && state != ST_STOPPING)) {
    Serial.println("[force] forcing off");
    psuOff();
    enterState(ST_STOPPING);
    reqOn = false;              // drop an ON that arrived at the same time
    return;
  }
  if (webOff) reqOn = false;    // OFF while already off: just clear requests

  // A web ON request is equivalent to the short press.
  bool webOn = reqOn;
  if (webOn) reqOn = false;

  switch (state) {
    case ST_OFF:
      // STOPPING already served the cool-down, so respond immediately.
      if (pressed || webOn) {
        psuOn();
        enterState(ST_STARTING);
      }
      break;

    case ST_STARTING:
      // Sense is meaningless until the board has had time to come up.
      if (now - stateSince < BOOT_BLANKING) break;
      if (boardAlive()) {
        enterState(ST_RUNNING);
      } else if (now - stateSince > START_TIMEOUT) {
        Serial.println("[warn] board never reported alive, aborting");
        psuOff();
        enterState(ST_STOPPING);
      }
      break;

    case ST_RUNNING:
      if (pressed) {
        // A tap while running is a no-op on purpose: shut the BC-250
        // down in software instead. Hold 5 s to force it.
        Serial.println("[button] ignored, shut down in software");
      }
      if (boardAlive()) {
        senseLowSince = 0;
      } else {
        if (senseLowSince == 0) senseLowSince = now ? now : 1;  // 0 is the sentinel
        if (now - senseLowSince >= SENSE_LOW_HOLD) {
          Serial.println("[sense] board is down, cutting the PSU");
          psuOff();
          enterState(ST_STOPPING);
        }
      }
      break;

    case ST_STOPPING:
      if (now - stateSince >= MIN_OFF) enterState(ST_OFF);
      break;
  }

  delay(10);
}
#endif
