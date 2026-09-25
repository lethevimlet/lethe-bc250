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
// WIFI_SSID / WIFI_PASS / CONSOLE_API below are only the first-boot
// defaults. The page's Settings section changes Wi-Fi, DHCP or a static
// IP, and the console address; the values live in NVS and win over the
// compiled ones. Network changes are applied WITHOUT rebooting (a reboot
// would hard-cut a running console) and are only made permanent once
// they are proven. Untested credentials get three join attempts within
// 25 s and are then dropped (minutes of bad-password attempts get a MAC
// locked out by some routers); settings that join but cannot be reached
// revert after two minutes.
//
// Hotspot fallback: after a power-up that cannot join Wi-Fi (three
// failed joins or 30 s) the ESP32 opens the WPA2 network "BC250-AP" (password: the OTA
// password) and serves the same page, power buttons included, at
// 192.168.4.1. Holding the case button 10 s opens it on demand. The
// hotspot runs hot (no modem sleep), so it opens by itself only once,
// for 5 min after a power-up that cannot join Wi-Fi, and otherwise only
// on demand; 15 min at most with a phone attached, minimum transmit
// power, closed at once above 70 C on the chip sensor.
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
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <MD5Builder.h>
#include <esp_wifi.h>

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

// ---- Network settings (NVS over the compiled defaults) ----------
struct NetCfg {
  String    ssid, pass;
  bool      staticIp = false;
  IPAddress ip, gw, mask, dns;
};
NetCfg   netCur, netPrev;
bool     netPending      = false;   // applied, not yet proven reachable
bool     netPendingReset = false;   // the pending change is "back to defaults"
uint32_t netPendingSince = 0;
uint32_t netApplyAt      = 0;       // deferred so the HTTP answer gets out first
const uint32_t NET_ASSOC_MS   = 25000;    // must JOIN the network within this, or revert at once:
                                          // minutes of bad-password attempts get a MAC locked
                                          // out by some routers
const uint8_t  NET_MAX_TRIES  = 3;        // association attempts with untested credentials
const uint32_t NET_CONFIRM_MS = 120000;   // joined but never reached this long -> previous settings
const uint32_t AP_AFTER_MS    = 30000;    // no Wi-Fi this long, or AP_AFTER_FAILS failed joins,
const uint8_t  AP_AFTER_FAILS = 3;        //   -> open the hotspot with the same page
const uint32_t AP_RETRY_MS    = 60000;    // while the hotspot is up: one quiet rejoin attempt per
                                          //   minute, and none while somebody is connected to it
const uint32_t AP_FORCE_MS    = 300000;   // hotspot opened on demand (button, page) stays this long
// Heat. An access point cannot use modem sleep: the receiver is on all the time, on a tiny
// regulator fed from 5 V inside a case. A board left all night with wrong Wi-Fi data sat in
// hotspot mode the whole time and died. So the hotspot has a budget:
// Measured: 45 C in station mode, 61 C within 90 s of hotspot whatever the transmit power or
// beacon rate (the always-on receiver is the cost), back to 46 C within 30 s of closing. The
// sensor follows instantaneous power; what ages the board is the long-run average, so:
// The hotspot therefore opens by itself ONCE, at power-up, when Wi-Fi cannot be joined, and
// otherwise only on demand (button held 10 s, or the page). Wi-Fi lost later is only retried.
const uint32_t AP_OPEN_MS     = 300000;   // open this long (5 min)...
const uint32_t AP_HARD_MS     = 900000;   // ...never longer than this, even with a phone attached
const float    AP_MAX_TEMP_C  = 70.0;     // chip sensor; hotter than this closes the hotspot at once
const uint16_t AP_BEACON_MS   = 400;      // sparse beacons (default 100 ms); phones still find it
const uint32_t JOIN_GRACE_MS  = 20000;    // after this without joining, stop the driver's own
                                          //   reconnect loop (constant scanning) and pace retries
const uint32_t SETUP_PRESS    = 10000;    // hold the button this long to open the hotspot
const uint32_t SELF_HEAL_MS   = 3600000;  // Wi-Fi down this long with the console OFF -> restart the
                                          //   ESP32. Never while the console runs: a restart drops
                                          //   GPIO 4 and would hard-cut it.
const char    *AP_SSID        = "BC250-AP";
volatile uint8_t staFails = 0;      // disconnect events since the last apply
volatile uint8_t staReason = 0;
bool     netJoined     = false;     // the pending settings got onto the network at least once
uint8_t  netTries      = 0;
String   netLastError;              // why the last change was rolled back, shown on the page
bool     netViaAp      = false;     // the pending change was saved over the hotspot
uint32_t apForceUntil  = 0;         // hotspot forced by the button, regardless of Wi-Fi
bool     setupFired    = false;
bool     apUp          = false;
uint32_t apSince       = 0;         // when the hotspot opened
bool     bootApUsed    = false;     // the one automatic opening after power-up has been spent
bool     everUp        = false;     // Wi-Fi has been joined at least once since boot
uint32_t btnPresses    = 0;         // presses seen since boot, shown on the page for bench checks
// Bench aid: every free pad is read with a pull-up and remembered if it was ever pulled to ground,
// so a button soldered to the wrong pad shows up in /rest/status ("lows") instead of doing nothing.
const uint8_t PROBE_PINS[] = {0, 1, 2, 3, 5, 8, 9, 20, 21};
uint32_t probeLows = 0;             // bit i set: PROBE_PINS[i] has been low since boot
RTC_NOINIT_ATTR uint32_t healMagic; // survives ESP.restart(): "this boot follows a self-heal"
const uint32_t HEAL_MAGIC = 0xB250A9E5;
uint32_t wifiDownSince = 0;
uint32_t wifiUpSince   = 0;

// ---- OTA -------------------------------------------------------
const char *OTA_PASS = "CHANGE_ME";        // Never ship empty: this
                                           // firmware owns the power path.
bool otaEnabled = false;                   // only armed while state == ST_OFF
String otaPass;                            // OTA_PASS, or what the page set (NVS "ota_pass")

// ---- Login (off by default) ------------------------------------
// For a page that is reachable from outside the home network. HTTP digest, so the
// password never travels, though the page itself is not encrypted (no HTTPS on the
// ESP32): a VPN home is the better way in, this is the lock for those who forward
// the port anyway. Verified here rather than by WebServer::authenticate(), which
// remembers only the last nonce it issued: with two browsers open, each request
// would fail and be re-challenged, and every such failure would look like a wrong
// password. Nonces are kept in a short ring instead, and only a wrong response to a
// nonce we issued counts as a failed login.
const char    *AUTH_REALM     = "BC250";
const uint8_t  AUTH_MAX_FAILS = 5;         // then AUTH_LOCK_MS of 429 for everyone
const uint32_t AUTH_LOCK_MS   = 60000;
const uint32_t NONCE_LIFE_MS  = 600000;    // a browser reuses a nonce until told it is stale
bool     authOn = false;
String   authUser, authPass;
uint8_t  authFails = 0;
uint32_t authLockUntil = 0;
enum AuthRes { AUTH_OK, AUTH_NONE, AUTH_STALE, AUTH_BAD };   // up here: the sketch generator puts prototypes before this point
struct Nonce { String v; uint32_t at; };
Nonce    nonces[4];
uint8_t  nonceNext = 0;

// ---- Pins ------------------------------------------------------
const uint8_t PIN_OPTO   = 4;   // drives the PC817 LED through R3
const uint8_t PIN_SENSE  = 6;
const uint8_t PIN_BUTTON = 7;
// Second button input, same wiring (pad to button to G). A SuperMini turned up whose pad 7
// was not connected to the chip: the pad measured 0 V while the firmware read its pull-up
// as high, and no press ever arrived. Either pad works; an unused one just stays pulled up.
const uint8_t PIN_BUTTON_ALT = 10;

// ---- Timing. All milliseconds ----------------------------------
const uint32_t BOOT_BLANKING  = 15000;  // ignore sense while the board boots
const uint32_t SENSE_LOW_HOLD = 10000;  // sense must stay low this long to count
                                        // (long enough to ride out a warm reboot)
const uint32_t DEBOUNCE       = 40;     // button debounce
const uint32_t LONG_PRESS     = 5000;   // hold this long to force power off
const uint32_t CLICK_GAP      = 450;    // second click within this = double click
const uint32_t CLICK_HTTP_MS  = 8000;   // the console call gets this long to answer
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
// Clicks while the console runs go to bc250-api: one click sleeps or wakes it, two clicks
// shut it down cleanly. Decided on release, so the 5 s and 10 s holds stay what they are.
uint32_t clickReleasedAt = 0;    // a click is waiting for a possible second one (0: none)
bool     doubleClick     = false; // the second press arrived in time; act on it
bool     secondPress     = false; // ...and its release is not a new click
volatile bool clickBusy  = false; // the console call is in flight (its own task)
String   clickResult;             // last outcome, in /rest/status "click" for bench checks

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
// Note: the Arduino prototype generator cannot see raw-string bounds. It
// treats "//" as a comment, so keep "//" out of the page (regex slashes as
// [/][/], URL slashes as &#47;&#47;), and it tracks double quotes, so never
// put a lone double quote in the JavaScript (no /["]/ and no '"').
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
/* Once bc250-api answers the row grows to: Power on | Shut down | Sleep | Force off */
#slp,#sd{display:none}#slp{background:#2f4f7d}#slp.wake{background:#2f7d4a}#sd{background:#5a5f6b}
.card.ok .btns.main{grid-template-columns:repeat(4,1fr)}.card.ok #slp,.card.ok #sd{display:block}
.card.ok .btns.main button{padding:13px 2px;font-size:.84em;white-space:nowrap}
button:disabled{opacity:.38;cursor:not-allowed}
.warn{margin:16px 0 0;color:#8b929c;font-size:.76em}
.net{margin-top:16px;padding-top:13px;border-top:1px solid #2b3039;
 color:#6d747e;font-size:.72em;display:flex;flex-wrap:wrap;gap:4px 14px;
 justify-content:space-between}
#capi{cursor:pointer;text-decoration:underline dotted;text-underline-offset:3px}
/* settings: one clearly marked place for Wi-Fi, IP and the console address */
#set{margin-top:16px;border-top:1px solid #2b3039;padding-top:6px}
#set summary{cursor:pointer;list-style:none;display:flex;align-items:center;gap:10px;min-height:46px;
 font-weight:600;font-size:.95em}
#set summary::-webkit-details-marker{display:none}
#set summary:before{content:'';width:9px;height:9px;border-right:2px solid #8b929c;border-bottom:2px solid #8b929c;
 transform:rotate(-45deg);flex:none}
#set[open] summary:before{transform:rotate(45deg)}
#set summary .meta{font-weight:400;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;flex:1;text-align:right}
.grp{background:#22262e;border-radius:11px;padding:14px;margin-top:10px}
.grp h3{margin:0 0 4px;font-size:.88em;font-weight:600}
.grp h3+h3,.grp .gap{margin-top:16px}
.grp label{display:block;color:#8b929c;font-size:.72em;margin:10px 0 4px}
.grp label.chk{display:flex;align-items:center;gap:8px;color:#c5cad1;font-size:.8em}
.grp input[type=text],.grp input[type=password]{width:100%;min-height:42px;padding:8px 10px;border:1px solid #2b3039;
 border-radius:9px;background:#14161a;color:#e6e8eb;font:inherit;font-size:.9em}
.grp input[type=checkbox]{width:18px;height:18px;flex:none}
.seg2{display:flex;background:#14161a;border-radius:9px;padding:3px;gap:3px;margin-top:6px}
.seg2 button{min-height:38px;padding:6px 8px;background:transparent;color:#8b929c;font-size:.85em}
.seg2 button.sel{background:#3a4150;color:#fff}
button.save{margin-top:14px;background:#3a4150;min-height:44px}
.hint{color:#8b929c;font-size:.72em;margin:10px 0 0}
.hint a{color:#aab2bd;text-decoration:underline dotted;text-underline-offset:3px}
.msg{font-size:.78em;margin-top:8px;color:#f0c674;min-height:1.2em}
.apb{display:none;margin:0 0 16px;padding:11px 13px;border-radius:11px;background:#16324a;color:#a9d4ff;font-size:.8em}
.apb.show{display:block}.apb b{color:#d6ebff}
.id{margin-top:7px;color:#6d747e;font-size:.7em;text-align:center;
 font-family:ui-monospace,SFMono-Regular,Menlo,monospace;letter-spacing:.4px;
 word-break:break-all}
/* console panel: header, notice and buttons are here; the tiles and switches come from
   bc250-api's panel.js so they update with the console, not with a reflash */
#con{display:none;margin-top:18px;padding-top:16px;border-top:1px solid #2b3039}
#con.show{display:block}
.hd{display:flex;align-items:baseline;gap:8px;margin:0 0 10px}
.hd h2{margin:0;font-size:.95em;font-weight:600}
.hd .meta{font-size:.72em}
#con:not(.ok) #panel{display:none}
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
<div class="apb" id="apb"></div>
<div class="state"><span class="dot" id="dot"></span>
<span class="lbl" id="st">...</span>
<span class="meta" id="up"></span></div>
<div class="btns main">
<button id="on">Power on</button>
<button id="sd">Shut down</button>
<button id="slp">Sleep</button>
<button id="off">Force off</button>
</div>
<p class="warn">Force off is a hard cut, the same as holding the button for five
seconds. Shut down (shown while the console answers) is the clean way: the OS
powers down and the PSU is cut once the board is off.</p>
<div id="con">
<div class="hd"><h2>Console</h2><span class="meta" id="cst">connecting</span></div>
<div id="nocon"><p id="nc"></p>
<p><a href="#" id="retry">retry</a> &nbsp;·&nbsp; <a href="#" id="chg">change address</a></p>
<p>Live stats and the tune switches need the <b>bc250-api</b> service on the BC-250: see
<a href="https:&#47;&#47;github.com/lethevimlet/lethe-bc250#46-stats-and-switches-over-the-network-bc250-api" target="_blank" rel="noopener">the README, §4.6</a>.</p></div>
<div id="panel"></div>
</div>
<details id="set"><summary>Settings<span class="meta" id="setsum"></span></summary>
<div class="grp"><h3>Wi-Fi</h3>
<label for="ssid">Network name (SSID)</label>
<input id="ssid" type="text" maxlength="32" autocomplete="off" autocapitalize="none" spellcheck="false">
<label for="wpass">Password</label>
<input id="wpass" type="password" maxlength="63" placeholder="leave empty to keep the current one" autocomplete="new-password">
<label class="chk"><input id="wopen" type="checkbox">Open network, no password</label>
<h3 class="gap">IP address</h3>
<div class="seg2"><button id="mdhcp" type="button">Automatic (DHCP)</button><button id="mstat" type="button">Static</button></div>
<div id="stat">
<label for="sip">IP address</label><input id="sip" type="text" inputmode="decimal" placeholder="192.168.1.60">
<label for="sgw">Gateway (your router)</label><input id="sgw" type="text" inputmode="decimal" placeholder="192.168.1.1">
<label for="smask">Subnet mask</label><input id="smask" type="text" inputmode="decimal" placeholder="255.255.255.0">
<label for="sdns">DNS (optional, the gateway if empty)</label><input id="sdns" type="text" inputmode="decimal">
</div>
<button class="save" id="netsave" type="button">Save Wi-Fi and IP</button>
<div class="msg" id="netmsg"></div>
<p class="hint">Applied without restarting the ESP32, so a running console is not affected. If it cannot
join the network it keeps the previous settings. If it cannot join Wi-Fi after being powered up, it
opens its own hotspot <b>BC250-AP</b> (password: your OTA password) with this same page at
192.168.4.1 for five minutes. Holding the case button for 10 s opens it for five minutes at any time,
and so does the link below. It is kept short because hotspot mode runs the board hot.
<a href="#" id="hotspot">Open the hotspot for 5 minutes</a> ·
<a href="#" id="netreset">Reset Wi-Fi and IP to the firmware defaults</a></p>
</div>
<div class="grp"><h3>Console (bc250-api)</h3>
<label for="capi2">Address of bc250-api on the BC-250</label>
<input id="capi2" type="text" placeholder="http:&#47;&#47;192.168.1.50:8250" autocapitalize="none" spellcheck="false">
<button class="save" id="capisave" type="button">Save console address</button>
<div class="msg" id="capimsg"></div>
<p class="hint">Port 8250 unless you changed it. Empty restores the address compiled into the firmware.</p>
</div>
<div class="grp"><h3>Access</h3>
<label class="chk"><input id="aon" type="checkbox">Ask for a username and password</label>
<div id="abox">
<label for="auser">Username</label>
<input id="auser" type="text" maxlength="32" autocomplete="username" autocapitalize="none" spellcheck="false">
<label for="apass">Password</label>
<input id="apass" type="password" maxlength="63" autocomplete="new-password">
<label for="apass2">Password again</label>
<input id="apass2" type="password" maxlength="63" autocomplete="new-password">
</div>
<button class="save" id="asave" type="button">Save access</button>
<div class="msg" id="amsg"></div>
<p class="hint">Off by default: at home nothing asks. Turn it on before forwarding this page's port
through your router. The login is HTTP digest, so the password itself never travels, but the page is
not encrypted (the ESP32 does no HTTPS): a VPN into your home is the better way in. Five wrong logins
lock the page for a minute. Forgotten? From your own network: <b>curl -X POST http:&#47;&#47;&lt;esp32-ip&gt;/rest/auth
-d on=0 -d ota=&lt;OTA password&gt;</b>. Never forward port 8250 (bc250-api) or 3232 (OTA).</p>
</div>
<div class="grp"><h3>OTA password</h3>
<label for="ocur">Current OTA password</label>
<input id="ocur" type="password" autocomplete="off">
<label for="onew">New OTA password</label>
<input id="onew" type="password" maxlength="63" autocomplete="new-password">
<label for="onew2">New OTA password again</label>
<input id="onew2" type="password" maxlength="63" autocomplete="new-password">
<button class="save" id="otasave" type="button">Change OTA password</button>
<div class="msg" id="otamsg"></div>
<p class="hint">The password for firmware updates over the air, for the <b>BC250-AP</b> hotspot, and the
way back in when the login above is forgotten. Put the new one into config.local on the PC you update
from. If it is lost, only a USB flash with <b>flash.sh usb --erase</b> resets it.</p>
</div>
</details>
<div class="net"><span id="rssi"></span><span id="ota"></span><span id="sense"></span>
<span id="capi" title="tap to change the bc250-api address"></span><span id="lock"></span><span id="chip"></span></div>
<div class="id" id="id"></div>
</div>
<script>
const $=i=>document.getElementById(i);
function hms(s){const h=(s/3600|0),m=(s%3600/60|0);return h?h+"h "+m+"m":m+"m"}
let api=null,running=false,ct=null,conState=null,asleep=false,panel=null,panelApi=null;
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
  if(d.temp!=null)$('chip').textContent='chip '+Math.round(d.temp)+' °C · button '+(d.btn?'DOWN':'up')+' ×'+d.presses+(d.lows?' · pads seen low: '+d.lows:'');
  $('id').textContent=d.ip+'  ·  '+d.mac;
  $('lock').textContent=d.auth?'login on':'';
  $('on').disabled=(d.state!='OFF');
  $('off').disabled=(d.state=='OFF'||d.state=='STOPPING');
  api=new URLSearchParams(location.search).get('console')||d.console||null;
  $('capi').textContent='console '+(d.console?d.console.replace(/^https?:[/][/]/,''):'not set');
  const viaAp=location.hostname=='192.168.4.1';apNow=!!d.ap;
  $('hotspot').textContent=apNow?'Close the hotspot':'Open the hotspot for 5 minutes';
  $('apb').className='apb'+(d.ap?' show':'');
  if(d.ap)$('apb').innerHTML=viaAp?('You are on the ESP32\'s own hotspot'+(d.wifi?'.':', because it cannot join <b>'+esc(d.ssid||'Wi-Fi')+'</b>.')+
   ' Set the Wi-Fi under Settings below, or simply use the buttons.'):'The hotspot <b>BC250-AP</b> is open.';
  if(viaAp&&!apOpened){apOpened=true;$('set').open=true;loadNet()}
  if(!$('set').open)$('setsum').textContent=(d.ssid||'')+(d.wifi===false?' · not connected':'')+(d.ap?' · hotspot on':'')+(d.net_pending?' · trying new settings':'');
  setRunning(d.state=='RUNNING');
 }catch(e){$('st').textContent='offline';setRunning(false);}
}
$('on').onclick=async()=>{await fetch('/rest/on');setTimeout(poll,300)};
$('off').onclick=async()=>{if(!confirm('Hard-cut power to the machine?'))return;
 await fetch('/rest/off');setTimeout(poll,300)};
// The bc250-api address lives in the ESP32's NVS; empty restores the compiled default.
// Editable in every state, so the service can be installed on the console later.
function editConsole(){$('set').open=true;loadNet();$('capi2').value=api||'';$('capi2').focus();
 $('capi2').scrollIntoView({block:'center'})}
const SL='/';let statMode=false,apOpened=false,apNow=false;
const esc=t=>String(t).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));
function setMode(on){statMode=on;$('mstat').className=on?'sel':'';$('mdhcp').className=on?'':'sel';
 $('stat').style.display=on?'':'none'}
$('mdhcp').onclick=()=>setMode(false);$('mstat').onclick=()=>setMode(true);
async function loadNet(){
 try{const r=await fetch('/rest/net'),n=await r.json();
  $('ssid').value=n.ssid;$('wopen').checked=n.open;setMode(n.static);
  $('sip').value=n.static?n.ip:n.cur_ip;$('sgw').value=n.static?n.gw:n.cur_gw;
  $('smask').value=n.static?n.mask:n.cur_mask;$('sdns').value=n.static?n.dns:n.cur_dns;
  $('setsum').textContent=n.ssid+' · '+(n.cur_ip=='0.0.0.0'?'not connected':(n.static?'static ':'DHCP ')+n.cur_ip)+(n.ap?' · hotspot on':'');
 }catch(e){}
}
$('set').addEventListener('toggle',()=>{if($('set').open){loadNet();loadAuth();$('capi2').value=api||''}});
let authWas=false;
function abox(){const on=$('aon').checked;$('abox').style.display=on?'':'none';
 $('apass').placeholder=authWas?'leave empty to keep the current one':'';$('apass2').placeholder=$('apass').placeholder}
$('aon').onchange=abox;
async function loadAuth(){
 try{const r=await fetch('/rest/auth'),a=await r.json();authWas=a.on;$('aon').checked=a.on;$('auser').value=a.user||'';abox()}catch(e){}
}
$('asave').onclick=async()=>{const m=$('amsg');
 if($('aon').checked&&$('apass').value!=$('apass2').value){m.textContent='The two passwords differ.';return}
 const b=new URLSearchParams();b.set('on',$('aon').checked?'1':'0');b.set('user',$('auser').value.trim());
 b.set('pass',$('apass').value);m.textContent='Saving…';
 try{const r=await fetch('/rest/auth',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b.toString()}),j=await r.json();
  if(!j.ok){m.textContent='Not saved: '+(j.error||'error');return}
  $('apass').value=$('apass2').value='';
  if(j.on&&j.changed){m.textContent='Saved. The page will now ask you to log in.';setTimeout(()=>location.reload(),1200)}
  else{m.textContent=j.on?'Saved.':'Saved. The page no longer asks for a login.';loadAuth();poll()}
 }catch(e){m.textContent='No answer from the ESP32.'}};
$('capisave').onclick=async()=>{const r=await fetch('/rest/console?url='+encodeURIComponent($('capi2').value.trim()));
 $('capimsg').textContent=r.ok?'Saved.':'Not saved: it must start with http:'+SL+SL+' and include the port, for example :8250';
 setRunning(false);poll()};
/* After a save the ESP32 tries the new settings. Reaching it, here or on its new address, makes
   them permanent; otherwise it returns to the previous ones by itself. */
function watchNet(newIp,secs){
 const m=$('netmsg'),t0=Date.now();let done=false,rolled='';
 const tick=async()=>{
  if(done)return;const left=secs-((Date.now()-t0)/1000|0);
  if(left<=-15){m.textContent='Not reachable on the new settings. The ESP32 went back to the previous ones.';loadNet();return}
  if(rolled){done=true;m.textContent='Not saved: the ESP32 '+rolled+'. It is back on the previous settings.';loadNet();return}
  m.textContent='Trying the new settings. Waiting for the ESP32, '+Math.max(left,0)+' s left…';
  try{const r=await fetch('/rest/net',{cache:'no-store'}),n=await r.json();
   if(!n.pending&&n.last_error){rolled=n.last_error;return tick()}
   if(!n.pending&&location.hostname=='192.168.4.1'&&n.cur_ip!='0.0.0.0'&&Date.now()-t0>4000){done=true;
    m.textContent='Joined '+n.ssid+'. The ESP32 is at '+n.cur_ip+' there: put this phone back on that network and open that address. The hotspot closes a minute after you leave it.';
    loadNet();return}
   if(!n.pending&&Date.now()-t0>4000){done=true;m.textContent='Saved. The ESP32 is on the new settings.';loadNet();return}}catch(e){}
  if(newIp&&newIp!=location.hostname){
   try{await fetch('http:'+SL+SL+newIp+'/rest/status',{mode:'no-cors',cache:'no-store'});
    done=true;m.textContent='Saved. Moving to the new address…';
    setTimeout(()=>{location.href='http:'+SL+SL+newIp+SL},1500);return}catch(e){}}
  setTimeout(tick,3000)};
 setTimeout(tick,2500);
}
async function postNet(body){
 const m=$('netmsg');m.textContent='Saving…';
 try{const r=await fetch('/rest/net',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
   body:body.toString()}),j=await r.json();
  if(!j.ok){m.textContent='Not saved: '+(j.error||'error');return}
  $('wpass').value='';watchNet(j.ip,j.confirm_s);
 }catch(e){m.textContent='No answer from the ESP32.'}
}
$('netsave').onclick=()=>{const b=new URLSearchParams();
 b.set('ssid',$('ssid').value.trim());b.set('pass',$('wpass').value);b.set('open',$('wopen').checked?'1':'0');
 b.set('mode',statMode?'static':'dhcp');b.set('ip',$('sip').value.trim());b.set('gw',$('sgw').value.trim());
 b.set('mask',$('smask').value.trim());b.set('dns',$('sdns').value.trim());postNet(b)};
$('hotspot').onclick=async e=>{e.preventDefault();const m=$('netmsg'),b=new URLSearchParams();
 if(apNow)b.set('off','1');
 try{const r=await fetch('/rest/hotspot',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b.toString()}),j=await r.json();
  m.textContent=!j.ok?'Not done: '+(j.error||'error'):
   j.open?'Hotspot '+j.ssid+' is open for '+j.minutes+' minutes. Wi-Fi stays connected.':'Hotspot closed.';poll()}
 catch(x){m.textContent='No answer from the ESP32.'}};
$('netreset').onclick=e=>{e.preventDefault();
 if(!confirm('Go back to the Wi-Fi and IP settings compiled into the firmware?'))return;
 const b=new URLSearchParams();b.set('reset','1');postNet(b)};
$('otasave').onclick=async()=>{const m=$('otamsg');
 if($('onew').value!=$('onew2').value){m.textContent='The two new passwords differ.';return}
 const b=new URLSearchParams();b.set('cur',$('ocur').value);b.set('pass',$('onew').value);m.textContent='Saving…';
 try{const r=await fetch('/rest/otapass',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b.toString()}),j=await r.json();
  m.textContent=j.ok?'Changed. Use it for the next update and in config.local.':'Not changed: '+(j.error||'error');
  if(j.ok)$('ocur').value=$('onew').value=$('onew2').value='';
 }catch(e){m.textContent='No answer from the ESP32.'}};
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
async function cpoll(){
 if(!running||document.hidden)return;
 if(!api){noCon('No bc250-api address is set.');return}
 try{
  const c=new AbortController(),tm=setTimeout(()=>c.abort(),4000);
  const r=await fetch(api+'/api/status',{cache:'no-store',signal:c.signal});clearTimeout(tm);
  const d=await r.json();render(d);
  if(!panel||panelApi!=api){await loadPanel();}
  if(panel)panel.update(d);
  setOk(true);$('cst').textContent=d.asleep?'asleep':'live';
 }catch(e){noCon('bc250-api is not answering at '+api.replace(/^https?:[/][/]/,'')+' (still booting, or not installed).')}
}
// The tiles and switches are bc250-api's panel.js: loaded once per page load, from the console.
function loadPanel(){return new Promise(res=>{
 const s=document.createElement('script');s.src=api+'/panel.js?t='+Date.now();
 s.onload=()=>{try{panel=window.bc250Panel.mount($('panel'),{api:api,refresh:cpoll});panelApi=api}catch(e){panel=null;$('panel').textContent='panel error: '+e}res()};
 s.onerror=()=>{panel=null;$('panel').innerHTML='<p class="warn">bc250-api answers but has no panel.js: update bc250-api on the console (installer).</p>';res()};
 document.head.appendChild(s)})}
function render(d){
 asleep=!!d.asleep;conState=asleep?'SLEEP':d.game_running?'RUNNING':'IDLE';showState('RUNNING');
 const b=$('slp');b.textContent=asleep?'Wake':'Sleep';b.className=asleep?'wake':'';
}
function setBusy(b){if(panel)panel.busy(b)}
async function tact(path,q,wait){
 if(q&&!confirm(q))return;setBusy(true);$('cst').textContent=path.split('/').pop()+'…';
 try{const r=await fetch(api+path,{method:'POST'}),j=await r.json();if(!j.ok)alert(j.output||'failed')}
 catch(e){alert('console unreachable')}
 setBusy(false);setTimeout(cpoll,wait||800);
}
$('slp').onclick=()=>tact(asleep?'/api/wake':'/api/sleep');
$('sd').onclick=()=>tact('/api/poweroff','Shut the console down cleanly?',3000);
// Poll only while the tab is visible, to save battery on the phone
// and needless radio wakeups on the ESP32.
let t=null;
function run(on){if(t)clearInterval(t);t=on?setInterval(poll,2000):null;if(on){poll();cpoll()}}
document.addEventListener('visibilitychange',()=>run(!document.hidden));
run(true);
</script></body></html>
)rawliteral";

String jsonEsc(const String &in) {
  String out;
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if ((uint8_t)c < 0x20) out += ' ';
    else out += c;
  }
  return out;
}

// Live level of every pad, e.g. "0:1 1:1 ... 7:0": 0 = at ground right now.
String pinLevels() {
  const uint8_t all[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 21};
  String out;
  for (size_t i = 0; i < sizeof(all); i++) {
    if (out.length()) out += " ";
    out += String(all[i]) + ":" + String(digitalRead(all[i]));
  }
  return out;
}

String probeList() {
  String out;
  for (size_t i = 0; i < sizeof(PROBE_PINS); i++)
    if (probeLows & (1UL << i)) { if (out.length()) out += ","; out += String(PROBE_PINS[i]); }
  return out;
}

void probeTick() {
  if (millis() < 3000) return;               // let the strapping pins settle after boot
  for (size_t i = 0; i < sizeof(PROBE_PINS); i++)
    if (digitalRead(PROBE_PINS[i]) == LOW) probeLows |= (1UL << i);
}

uint32_t netPendingLeft() {
  if (!netPending) return 0;
  uint32_t el = millis() - netPendingSince;
  return el >= NET_CONFIRM_MS ? 0 : (NET_CONFIRM_MS - el) / 1000;
}

void netError(const char *msg);
bool cleanText(const String &s);

String md5(const String &s) {
  MD5Builder m; m.begin(); m.add(s); m.calculate(); return m.toString();
}

// value of key=... in a Digest header: quoted or bare
String dparam(const String &h, const char *key) {
  String k = String(key) + "=";
  int i = h.indexOf(k);
  while (i > 0 && h[i - 1] != ' ' && h[i - 1] != ',') i = h.indexOf(k, i + 1);   // whole key only
  if (i < 0) return "";
  i += k.length();
  if (i < (int)h.length() && h[i] == '"') { int e = h.indexOf('"', i + 1); return e < 0 ? "" : h.substring(i + 1, e); }
  int e = i; while (e < (int)h.length() && h[e] != ',' && h[e] != ' ') e++;
  return h.substring(i, e);
}

String newNonce() {
  char b[17];
  snprintf(b, sizeof(b), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
  nonces[nonceNext] = { String(b), millis() };
  nonceNext = (nonceNext + 1) % 4;
  return String(b);
}

bool nonceKnown(const String &n) {
  if (!n.length()) return false;
  for (auto &x : nonces) if (x.v == n && millis() - x.at < NONCE_LIFE_MS) return true;
  return false;
}

void authChallenge(bool stale) {
  String h = String("Digest realm=\"") + AUTH_REALM + "\", qop=\"auth\", nonce=\"" + newNonce() + "\"";
  if (stale) h += ", stale=true";
  server.sendHeader("WWW-Authenticate", h);
  server.send(401, "text/plain", "login required");
}


AuthRes authCheck() {
  if (!server.hasHeader("Authorization")) return AUTH_NONE;
  String h = server.header("Authorization");
  if (!h.startsWith("Digest ")) return AUTH_BAD;
  String nonce = dparam(h, "nonce");
  if (!nonceKnown(nonce)) return AUTH_STALE;
  String uri = dparam(h, "uri");
  if (dparam(h, "username") != authUser || dparam(h, "realm") != AUTH_REALM ||
      !uri.startsWith(server.uri())) return AUTH_BAD;
  const char *method = server.method() == HTTP_POST ? "POST" : server.method() == HTTP_PUT ? "PUT" :
                       server.method() == HTTP_DELETE ? "DELETE" : "GET";
  String ha1 = md5(authUser + ":" + AUTH_REALM + ":" + authPass);
  String ha2 = md5(String(method) + ":" + uri);
  String qop = dparam(h, "qop"), want;
  if (qop.length()) want = md5(ha1 + ":" + nonce + ":" + dparam(h, "nc") + ":" + dparam(h, "cnonce") + ":" + qop + ":" + ha2);
  else              want = md5(ha1 + ":" + nonce + ":" + ha2);
  return want.equalsIgnoreCase(dparam(h, "response")) ? AUTH_OK : AUTH_BAD;
}

// Every handler starts with this. True: go on. False: the answer has been sent.
bool authGate() {
  if (!authOn) return true;
  if (authLockUntil && (int32_t)(millis() - authLockUntil) < 0) {
    server.send(429, "application/json", "{\"error\":\"too many wrong logins, try again in a minute\"}");
    return false;
  }
  authLockUntil = 0;
  AuthRes r = authCheck();
  if (r == AUTH_OK) { authFails = 0; return true; }
  if (r == AUTH_BAD) {
    delay(300);                                  // slows guessing; the power loop tolerates it
    if (++authFails >= AUTH_MAX_FAILS) {
      authFails = 0;
      authLockUntil = millis() + AUTH_LOCK_MS;
      Serial.printf("[auth] %u wrong logins from %s: locked for a minute\n", AUTH_MAX_FAILS, server.client().remoteIP().toString().c_str());
    }
  }
  authChallenge(r == AUTH_STALE);
  return false;
}

void otaLoad() {
  prefs.begin("bc250", true);
  otaPass = prefs.getString("ota_pass", OTA_PASS);
  prefs.end();
}

// POST /rest/otapass: cur, pass. The one save that asks for the current OTA password,
// because this password is what an update, the hotspot and a forgotten login fall back
// on. It moves to NVS: the compiled one is only the default, and flash.sh usb --erase
// is the way back when it is lost.
void handleOtaPass() {
  if (!authGate()) return;
  if (server.arg("cur") != otaPass) {
    delay(500);
    server.send(403, "application/json", "{\"ok\":false,\"error\":\"wrong current OTA password\"}");
    return;
  }
  String np = server.arg("pass");
  if (np.length() < 8 || np.length() > 63 || !cleanText(np)) { netError("new password must be 8 to 63 plain characters, no quotes or colons"); return; }
  otaPass = np;
  prefs.begin("bc250", false);
  prefs.putString("ota_pass", otaPass);
  prefs.end();
  ArduinoOTA.setPassword(otaPass.c_str());     // applies to the next update, armed or not
  Serial.println("[ota] password changed from the page");
  server.send(200, "application/json", "{\"ok\":true}");
}

void authLoad() {
  prefs.begin("bc250", true);
  authOn   = prefs.getBool("auth_on", false);
  authUser = prefs.getString("auth_user", "");
  authPass = prefs.getString("auth_pass", "");
  prefs.end();
  if (authOn && (!authUser.length() || !authPass.length())) authOn = false;   // never lock with no key
}

void authStore() {
  prefs.begin("bc250", false);
  prefs.putBool("auth_on", authOn);
  if (authOn) { prefs.putString("auth_user", authUser); prefs.putString("auth_pass", authPass); }
  else        { prefs.remove("auth_user"); prefs.remove("auth_pass"); }
  prefs.end();
}

bool cleanText(const String &s) {
  for (size_t i = 0; i < s.length(); i++) if (s[i] < 0x20 || s[i] > 0x7e || s[i] == '"' || s[i] == ':') return false;
  return true;
}

void handleAuthGet() {
  if (!authGate()) return;
  server.send(200, "application/json", String("{\"on\":") + (authOn ? "true" : "false") + ",\"user\":\"" + jsonEsc(authUser) + "\"}");
}

// POST /rest/auth: on=0|1, user, pass (empty = keep, when already on). No password
// to save; with the login on, being logged in is the protection. The OTA password
// gets past the login here, which is the way back in for a forgotten one: on=0 with ota=... .
void handleAuthPost() {
  bool otaOk = server.hasArg("ota") && server.arg("ota") == otaPass;
  if (!otaOk && !authGate()) return;
  bool on = server.arg("on") == "1";
  String user = server.arg("user"); user.trim();
  String pass = server.arg("pass");
  bool changed = false;
  if (on) {
    if (user.length() < 1 || user.length() > 32 || !cleanText(user)) { netError("username must be 1 to 32 plain characters, no quotes or colons"); return; }
    if (!pass.length() && authOn) pass = authPass;                         // keep it
    if (pass.length() < 8 || pass.length() > 63 || !cleanText(pass)) { netError("password must be 8 to 63 plain characters, no quotes or colons"); return; }
    changed = !authOn || user != authUser || pass != authPass;
    authUser = user; authPass = pass;
  }
  authOn = on;
  if (!on) { authUser = ""; authPass = ""; }   // off means gone, also from GET /rest/auth
  authFails = 0; authLockUntil = 0;
  authStore();
  Serial.printf("[auth] login %s (user %s)\n", authOn ? "on" : "off", authOn ? authUser.c_str() : "-");
  server.send(200, "application/json", String("{\"ok\":true,\"on\":") + (authOn ? "true" : "false") +
    ",\"user\":\"" + jsonEsc(authUser) + "\",\"changed\":" + (changed ? "true" : "false") + "}");
}

void handleRoot() {
  if (!authGate()) return;
  netSeen();
  server.send_P(200, "text/html; charset=utf-8", PAGE_HTML);
}

void handleStatus() {
  if (!authGate()) return;
  netSeen();
  char buf[900];
  snprintf(buf, sizeof(buf),
    "{\"state\":\"%s\",\"sense\":%s,\"uptime\":%lu,\"rssi\":%d,"
    "\"heap\":%lu,\"ota\":%s,\"ip\":\"%s\",\"mac\":\"%s\",\"console\":\"%s\","
    "\"ssid\":\"%s\",\"ap\":%s,\"wifi\":%s,\"net_pending\":%lu,"
    "\"temp\":%.1f,\"btn\":%s,\"presses\":%lu,\"click\":\"%s\",\"lows\":\"%s\",\"pins\":\"%s\",\"auth\":%s}",
    stateName(state),
    boardAlive() ? "true" : "false",
    (unsigned long)(millis() / 1000),
    (int)WiFi.RSSI(),
    (unsigned long)ESP.getFreeHeap(),
    otaEnabled ? "true" : "false",
    WiFi.localIP().toString().c_str(),
    WiFi.macAddress().c_str(),
    consoleApi.c_str(),
    jsonEsc(netCur.ssid).c_str(),
    apUp ? "true" : "false",
    WiFi.status() == WL_CONNECTED ? "true" : "false",
    (unsigned long)netPendingLeft(),
    temperatureRead(),
    buttonRaw() == LOW ? "true" : "false",
    (unsigned long)btnPresses,
    jsonEsc(clickResult).c_str(),
    probeList().c_str(),
    pinLevels().c_str(),
    authOn ? "true" : "false");
  server.send(200, "application/json", buf);
}

void loadConsoleApi() {
  // Read-only open fails until the namespace exists; the default covers that.
  prefs.begin("bc250", true);
  consoleApi = prefs.getString("console", CONSOLE_API);
  prefs.end();
}

// GET /rest/console?url=http://<console-ip>:8250   ("" = back to the compiled default)
// POST /rest/hotspot: open the hotspot for AP_FORCE_MS even though Wi-Fi works,
// e.g. to check it from a phone. Wi-Fi stays connected alongside it.
void handleHotspot() {
  if (!authGate()) return;
  netSeen();
  if (server.arg("off") == "1") {            // close it now
    apForceUntil = 0;
    apStop("closed on request", false);
    server.send(200, "application/json", "{\"ok\":true,\"open\":false}");
    return;
  }
  uint32_t secs = server.hasArg("secs") ? (uint32_t)server.arg("secs").toInt() : AP_FORCE_MS / 1000;
  if (secs < 30) secs = 30;
  if (secs > AP_FORCE_MS / 1000) secs = AP_FORCE_MS / 1000;
  apForceUntil = millis() + secs * 1000UL;
  server.send(200, "application/json", String("{\"ok\":true,\"open\":true,\"ssid\":\"") + AP_SSID +
    "\",\"minutes\":" + String((secs + 59) / 60) + ",\"secs\":" + String(secs) + "}");
}

void handleConsole() {
  if (!authGate()) return;
  netSeen();
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
  if (!authGate()) return;
  reqOn = true;
  Serial.println("[web] ON requested");
  server.send(200, "application/json", "{\"ok\":true,\"action\":\"on\"}");
}

void handleOff() {
  if (!authGate()) return;
  reqOff = true;
  Serial.println("[web] OFF requested (hard cut)");
  server.send(200, "application/json", "{\"ok\":true,\"action\":\"off\"}");
}

void handleNotFound() {
  if (!authGate()) return;
  server.send(404, "application/json", "{\"error\":\"not found\"}");
}

// ---- WiFi ------------------------------------------------------
// Everything non-blocking: the state machine takes priority and the
// radio sorts itself out along the way.
void netDefaults(NetCfg &c) {
  c.ssid = WIFI_SSID; c.pass = WIFI_PASS; c.staticIp = false;
  c.ip = c.gw = c.mask = c.dns = IPAddress((uint32_t)0);
}

void netLoad() {
  netDefaults(netCur);
  prefs.begin("bc250", true);      // read-only open fails until the namespace exists
  if (prefs.isKey("ssid")) {
    netCur.ssid     = prefs.getString("ssid", WIFI_SSID);
    netCur.pass     = prefs.getString("pass", "");
    netCur.staticIp = prefs.getBool("static", false);
    netCur.ip.fromString(prefs.getString("ip", "0.0.0.0"));
    netCur.gw.fromString(prefs.getString("gw", "0.0.0.0"));
    netCur.mask.fromString(prefs.getString("mask", "255.255.255.0"));
    netCur.dns.fromString(prefs.getString("dns", "0.0.0.0"));
  }
  prefs.end();
}

void netStore(const NetCfg &c) {
  prefs.begin("bc250", false);
  prefs.putString("ssid", c.ssid);
  prefs.putString("pass", c.pass);
  prefs.putBool("static", c.staticIp);
  prefs.putString("ip", c.ip.toString());
  prefs.putString("gw", c.gw.toString());
  prefs.putString("mask", c.mask.toString());
  prefs.putString("dns", c.dns.toString());
  prefs.end();
}

void netForget() {
  prefs.begin("bc250", false);
  const char *keys[] = {"ssid", "pass", "static", "ip", "gw", "mask", "dns"};
  for (const char *k : keys) prefs.remove(k);
  prefs.end();
}

// (Re)associate with the given settings. Never reboots: GPIO 4 keeps the PSU
// latched the whole time, so this is safe with the console running.
void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    staFails++;
    staReason = info.wifi_sta_disconnected.reason;
  }
}

void wifiApply(const NetCfg &c) {
  wifiLastAttempt = millis();
  staFails = 0;
  WiFi.disconnect(false);
  WiFi.mode(apUp ? WIFI_AP_STA : WIFI_STA);
  WiFi.setSleep(!apUp);                     // modem sleep only works without the AP
  WiFi.setTxPower(WIFI_POWER_8_5dBm);       // lower current peaks, less heat
  // Untested credentials are tried a few times by netTick(), never in an endless loop.
  WiFi.setAutoReconnect(!netPending && !apUp);
  WiFi.setHostname(MDNS_NAME);
  if (c.staticIp) WiFi.config(c.ip, c.gw, c.mask, c.dns);
  else            WiFi.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0), IPAddress((uint32_t)0));  // DHCP
  if (c.pass.length() >= 8) WiFi.begin(c.ssid.c_str(), c.pass.c_str());
  else                      WiFi.begin(c.ssid.c_str());   // open network
  Serial.printf("[wifi] connecting to %s (%s)\n", c.ssid.c_str(),
                c.staticIp ? c.ip.toString().c_str() : "DHCP");
}

// Seen on hardware: after a run of failed joins the running Wi-Fi stack would not
// re-associate even with the right credentials, while a fresh boot joined at once.
// So a rollback restarts the radio completely. This does not touch GPIO 4.
void wifiRestart(const NetCfg &c) {
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  apUp = false;
  delay(250);
  wifiApply(c);
}

void wifiStart() {
  Serial.printf("[wifi] MAC %s  <- reserve this one on the router\n",
                WiFi.macAddress().c_str());
  wifiApply(netCur);
}

// A request that arrives over the station interface proves the pending
// settings work: only then are they written to NVS. A power loss before
// that boots the previous, known-good settings.
void netSeen() {
  if (!netPending || WiFi.status() != WL_CONNECTED) return;
  if (server.client().localIP() != WiFi.localIP()) return;   // came in over the setup AP
  if (netPendingReset) netForget(); else netStore(netCur);
  netPending = false;
  netLastError = "";
  WiFi.setAutoReconnect(!apUp);
  Serial.println("[net] new settings confirmed and saved");
}

void netTick() {
  uint32_t now = millis();
  if (netApplyAt && now >= netApplyAt) {     // deferred apply, after the HTTP answer
    netApplyAt = 0;
    netPending = true;
    netPendingSince = now;
    netJoined = false;
    netTries = 1;
    wifiApply(netCur);
  }
  if (!netPending) return;
  bool up = (WiFi.status() == WL_CONNECTED);
  if (up) netJoined = true;
  if (up && netViaAp) {                   // saved over the hotspot: joining is the proof
    if (netPendingReset) netForget(); else netStore(netCur);
    netPending = false; netViaAp = false; netLastError = "";
    Serial.println("[net] joined with the settings saved over the hotspot, stored");
    return;
  }
  uint32_t el = now - netPendingSince;
  const char *why = nullptr;
  if (!netJoined && (staFails >= NET_MAX_TRIES || el >= NET_ASSOC_MS))
    why = "could not join that network (name or password wrong?)";
  else if (el >= NET_CONFIRM_MS)
    why = "joined the network but could not be reached on it (IP settings wrong?)";
  if (why) {
    Serial.printf("[net] %s, back to the previous settings (reason %u)\n", why, (unsigned)staReason);
    netLastError = why;
    netCur = netPrev;
    netPending = false;
    wifiDownSince = now ? now : 1;       // the hotspot waits its full delay after this
    wifiRestart(netCur);
  } else if (!up && !netJoined && staFails >= netTries && netTries < NET_MAX_TRIES &&
             now - wifiLastAttempt >= 4000) {
    netTries++;
    wifiLastAttempt = now;
    WiFi.reconnect();
  }
}

// ---- Hotspot fallback --------------------------------------------
// Whatever goes wrong with Wi-Fi, the ESP32 stays reachable: it opens the
// WPA2 network AP_SSID (password: the OTA password) and serves the same
// page at 192.168.4.1, power buttons included. The station keeps trying in
// the background, slowly, because every join attempt makes the radio leave
// the hotspot's channel for a moment.
void apStart(const char *why) {
  if (apUp) return;
  apUp = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);                     // modem sleep and an AP do not mix
  WiFi.setAutoReconnect(false);             // rejoin attempts are paced by wifiTick()
  // Low-power access point: one client, sparse beacons, minimum transmit power. The receiver
  // still has to stay on, which is where most of the heat comes from.
  bool ok = WiFi.softAP(AP_SSID, otaPass.length() >= 8 ? otaPass.c_str() : "bc250setup", 1, 0, 1);
  wifi_config_t conf;
  if (esp_wifi_get_config(WIFI_IF_AP, &conf) == ESP_OK) {
    conf.ap.beacon_interval = AP_BEACON_MS;
    esp_wifi_set_config(WIFI_IF_AP, &conf);
  }
  WiFi.setTxPower(WIFI_POWER_2dBm);         // a phone next to the console needs no range
  apSince = millis();
  Serial.printf("[wifi] %s: hotspot %s %s at %s\n", why, AP_SSID, ok ? "up" : "FAILED",
                WiFi.softAPIP().toString().c_str());
}

void apStop(const char *why, bool rest) {
  if (!apUp) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.setAutoReconnect(WiFi.status() == WL_CONNECTED);
  apUp = false;
  (void)rest;
  Serial.printf("[wifi] hotspot closed: %s\n", why);
}

void wifiTick() {
  bool up = (WiFi.status() == WL_CONNECTED);
  uint32_t now = millis();
  netTick();

  if (up) { wifiDownSince = 0; if (!wifiUpSince) wifiUpSince = now ? now : 1; }
  else    { wifiUpSince = 0;   if (!wifiDownSince) wifiDownSince = now ? now : 1; }

  bool forced = apForceUntil && now < apForceUntil;
  if (apForceUntil && !forced) apForceUntil = 0;
  bool hot = temperatureRead() >= AP_MAX_TEMP_C;
  if (up) everUp = true;

  if (!apUp) {
    if (hot) { /* no hotspot while the chip is hot, not even on demand */ }
    else if (forced) apStart("on demand");
    else if (!bootApUsed && !everUp && !up && !netPending && !netApplyAt &&
             (staFails >= AP_AFTER_FAILS || now - wifiDownSince >= AP_AFTER_MS)) {
      bootApUsed = true;                     // once per power-up, never again by itself
      apStart("cannot join Wi-Fi after power-up");
    }
  } else {
    uint32_t open = now - apSince;
    int clients = WiFi.softAPgetStationNum();
    if (hot)                                                   apStop("chip too hot", true);
    else if (open >= AP_HARD_MS)                               apStop("30 min limit", true);
    else if (!forced && clients == 0 && open >= AP_OPEN_MS)    apStop("5 min window over", true);
    else if (up && !forced && clients == 0 && now - wifiUpSince >= 60000) apStop("back on Wi-Fi", false);
    else if (up && !forced && open >= AP_OPEN_MS)                          apStop("on-demand window over", false);
  }

  // The driver's own auto-reconnect scans without pause when the network is not there, which
  // keeps the radio busy for nothing. Give a join JOIN_GRACE_MS, then pace the retries below.
  if (up) { if (!WiFi.getAutoReconnect() && !apUp) WiFi.setAutoReconnect(true); }
  else if (!netPending && WiFi.getAutoReconnect() && now - wifiLastAttempt >= JOIN_GRACE_MS)
    WiFi.setAutoReconnect(false);

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

  // Last resort, only with the console OFF and nobody on the hotspot: a restart boots
  // the known-good settings from NVS with a fresh radio.
  if (!up && state == ST_OFF && !netPending && !netApplyAt &&
      now - wifiDownSince >= SELF_HEAL_MS && WiFi.softAPgetStationNum() == 0) {
    Serial.println("[wifi] down for an hour with the console OFF, restarting the ESP32");
    healMagic = HEAL_MAGIC;                 // the next boot is not a power-up: no hotspot by itself
    delay(100);
    ESP.restart();
  }

  // Rejoin attempts. Never blocks. With the hotspot up: one per minute, and none while a
  // phone is connected to it, so the page stays usable. Without it: the old 5 minute retry
  // on top of the driver's own auto-reconnect.
  if (!up && !netPending && !netApplyAt) {
    if (apUp) {
      if (WiFi.softAPgetStationNum() == 0 && now - wifiLastAttempt >= AP_RETRY_MS) {
        wifiLastAttempt = now;
        WiFi.reconnect();
      }
    } else if (now - wifiLastAttempt >= WIFI_RETRY) {
      Serial.println("[wifi] retrying");
      wifiApply(netCur);
    }
  }
}

// ---- /rest/net: Wi-Fi and IP settings ---------------------------
// GET shows them (never the password). POST changes them; a change that does not
// work is rolled back (below), which is the protection against locking oneself out.
void handleNetGet() {
  if (!authGate()) return;
  netSeen();
  String j = "{\"ssid\":\"" + jsonEsc(netCur.ssid) + "\",\"open\":" + (netCur.pass.length() ? "false" : "true") +
    ",\"static\":" + (netCur.staticIp ? "true" : "false") +
    ",\"ip\":\"" + netCur.ip.toString() + "\",\"gw\":\"" + netCur.gw.toString() +
    "\",\"mask\":\"" + netCur.mask.toString() + "\",\"dns\":\"" + netCur.dns.toString() +
    "\",\"cur_ip\":\"" + WiFi.localIP().toString() + "\",\"cur_gw\":\"" + WiFi.gatewayIP().toString() +
    "\",\"cur_mask\":\"" + WiFi.subnetMask().toString() + "\",\"cur_dns\":\"" + WiFi.dnsIP().toString() +
    "\",\"ap\":" + (apUp ? "true" : "false") + ",\"pending\":" + String(netPendingLeft()) +
    ",\"last_error\":\"" + jsonEsc(netLastError) + "\"}";
  server.send(200, "application/json", j);
}

void netError(const char *msg) {
  server.send(400, "application/json", String("{\"ok\":false,\"error\":\"") + msg + "\"}");
}

void handleNetPost() {
  if (!authGate()) return;
  if (netPending || netApplyAt) { netError("a change is still being tried, wait for it"); return; }
  NetCfg n = netCur;
  bool reset = server.arg("reset") == "1";
  if (reset) {
    netDefaults(n);
  } else {
    String ssid = server.arg("ssid"); ssid.trim();
    if (ssid.length() < 1 || ssid.length() > 32) { netError("network name must be 1 to 32 characters"); return; }
    n.ssid = ssid;
    String pass = server.arg("pass");
    if (server.arg("open") == "1") n.pass = "";
    else if (pass.length()) {
      if (pass.length() < 8 || pass.length() > 63) { netError("password must be 8 to 63 characters"); return; }
      n.pass = pass;
    }                                            // empty = keep the current password
    n.staticIp = server.arg("mode") == "static";
    if (n.staticIp) {
      if (!n.ip.fromString(server.arg("ip")) || n.ip == IPAddress((uint32_t)0)) { netError("IP address is not valid"); return; }
      if (!n.gw.fromString(server.arg("gw")))     { netError("gateway is not valid"); return; }
      if (!n.mask.fromString(server.arg("mask")) || n.mask == IPAddress((uint32_t)0)) { netError("subnet mask is not valid"); return; }
      if (!server.arg("dns").length() || !n.dns.fromString(server.arg("dns"))) n.dns = n.gw;
    }
  }
  netPrev = netCur;
  netCur = n;
  netViaAp = apUp && server.client().localIP() == WiFi.softAPIP();
  netLastError = "";
  netPendingReset = reset;
  netApplyAt = millis() + 600;
  Serial.printf("[net] trying %s, %s\n", n.ssid.c_str(), n.staticIp ? n.ip.toString().c_str() : "DHCP");
  server.send(200, "application/json", String("{\"ok\":true,\"confirm_s\":") + String(NET_CONFIRM_MS / 1000) +
    ",\"ip\":\"" + (n.staticIp ? n.ip.toString() : String("")) + "\"}");
}

// ---- OTA -------------------------------------------------------
// Callbacks only. Arming happens in the loop, gated on ST_OFF.
void otaSetup() {
  ArduinoOTA.setHostname(MDNS_NAME);
  ArduinoOTA.setPassword(otaPass.c_str());

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
  pinMode(PIN_BUTTON_ALT, INPUT_PULLUP);
  for (size_t i = 0; i < sizeof(PROBE_PINS); i++) pinMode(PROBE_PINS[i], INPUT_PULLUP);

  // 80 MHz is plenty here and trims both draw and heat.
  setCpuFrequencyMhz(80);

  Serial.begin(115200);
  // USB CDC takes a moment to enumerate; without this the first
  // lines are lost.
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);
  delay(200);

  // A boot that follows a self-heal restart is not a power-up: it must not spend a hotspot
  // window, or a box with no Wi-Fi would open one every hour after all.
  if (healMagic == HEAL_MAGIC) bootApUsed = true;
  healMagic = 0;

  loadConsoleApi();
  otaLoad();
  authLoad();
  netLoad();
  WiFi.onEvent(onWifiEvent);
  wifiStart();
  otaSetup();

  server.on("/",            handleRoot);
  server.on("/rest/status", handleStatus);
  server.on("/rest/on",     handleOn);
  server.on("/rest/off",    handleOff);
  server.on("/rest/console", handleConsole);
  server.on("/rest/net", HTTP_GET,  handleNetGet);
  server.on("/rest/net", HTTP_POST, handleNetPost);
  server.on("/rest/hotspot", HTTP_POST, handleHotspot);
  server.on("/rest/auth", HTTP_GET,  handleAuthGet);
  server.on("/rest/auth", HTTP_POST, handleAuthPost);
  server.on("/rest/otapass", HTTP_POST, handleOtaPass);
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

// LOW while the button is down, on either button pad.
int buttonRaw() {
  return (digitalRead(PIN_BUTTON) == LOW || digitalRead(PIN_BUTTON_ALT) == LOW) ? LOW : HIGH;
}

// Fires once on the press itself, not on release, so holding the button
// down from OFF still starts the machine.
bool buttonDown() {
  bool raw = buttonRaw();
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
    setupFired = false;
    btnPresses++;
    // Second press soon after a click: a double click, decided on this press.
    if (clickReleasedAt && now - clickReleasedAt < CLICK_GAP) {
      clickReleasedAt = 0;
      doubleClick = true;
      secondPress = true;
    }
    return true;
  }
  // Released. A short release while the console runs is a click; a hold that
  // reached LONG_PRESS has already forced it off and is not one.
  if (secondPress) {
    secondPress = false;
  } else if (state == ST_RUNNING && longArmed && !longFired && !setupFired) {
    clickReleasedAt = now ? now : 1;
  }
  return false;
}

// POST to the console, off the main loop so a slow answer never stalls the
// state machine or the page. The result is only for the status page.
void clickTask(void *arg) {
  const char *path = (const char *)arg;
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(CLICK_HTTP_MS);
  String url = consoleApi + path;
  String out;
  if (http.begin(url)) {
    int code = http.POST("");
    if (code > 0) out = String(path) + " " + String(code) + " " + http.getString().substring(0, 80);
    else          out = String(path) + " " + http.errorToString(code);
    http.end();
  } else {
    out = String(path) + " bad url";
  }
  out.replace("\n", " ");
  Serial.printf("[click] %s\n", out.c_str());
  clickResult = out;
  clickBusy = false;
  vTaskDelete(NULL);
}

void consolePost(const char *path) {
  if (WiFi.status() != WL_CONNECTED) { clickResult = String(path) + " no wifi"; return; }
  if (clickBusy) { clickResult = String(path) + " busy"; return; }
  clickBusy = true;
  if (xTaskCreate(clickTask, "click", 6144, (void *)path, 1, NULL) != pdPASS) {
    clickBusy = false;
    clickResult = String(path) + " no task";
  }
}

// One click while running: sleep or wake. Two clicks: clean shutdown. Both through
// bc250-api on the console; without it the button only has the 5 s hold.
void clickTick() {
  if (state != ST_RUNNING) { doubleClick = false; clickReleasedAt = 0; return; }
  if (doubleClick) {
    doubleClick = false;
    Serial.println("[button] double click: shutdown");
    consolePost("/api/poweroff");
  } else if (clickReleasedAt && millis() - clickReleasedAt >= CLICK_GAP) {
    clickReleasedAt = 0;
    Serial.println("[button] click: sleep/wake");
    consolePost("/api/sleep/toggle");
  }
}

// Holding the button SETUP_PRESS opens the hotspot for AP_FORCE_MS, whatever the
// Wi-Fi state: the way back in when the ESP32 joined a network but cannot be
// reached on it (wrong static IP). From OFF the same press has already started the
// console; from RUNNING it has already forced it off at LONG_PRESS, so only do this
// on purpose.
void setupPressTick() {
  if (btnStable == LOW && !setupFired && millis() - btnPressedAt >= SETUP_PRESS) {
    setupFired = true;
    apForceUntil = millis() + AP_FORCE_MS;
    Serial.println("[button] held 10 s: hotspot forced open");
  }
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
  setupPressTick();
  probeTick();

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
                    buttonRaw() == LOW ? "PRESSED" : "released",
                    WiFi.localIP().toString().c_str());
    }
  }

  // Sample sense and button at the same rate as the real firmware.
  uint32_t now = millis();
  static int lastSense = -1, lastBtn = -1;
  static uint32_t nPress = 0, tDown = 0;

  int s = digitalRead(PIN_SENSE);
  int b = buttonRaw();

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
  setupPressTick();
  probeTick();
  clickTick();

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
      // A press while running does nothing by itself: its release becomes a
      // click (sleep/wake) or a double click (shutdown), see clickTick.
      // Hold 5 s to force the PSU off.
      (void)pressed;
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
