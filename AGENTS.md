# Working on lethe-bc250

Notes for an agent (or a human) starting a session in this repo. The README is the source of truth
for what the project does; this file is about how to change it without breaking the pieces that
depend on each other.

## What lives where

| Path | What | Runs on |
|------|------|---------|
| `decky-bc250-tune/bc250-tune` | one bash script: every tuning switch, the boot-time re-apply (`apply --boot`), the fan-curve daemon (`fan-daemon`), `status --json` | the console, as root |
| `decky-bc250-tune/decky-plugin/` | Decky plugin that only calls `bc250-tune`; `src/index.tsx` → `dist/index.js` (committed) | the console |
| `decky-bc250-sleep/` | Decky plugin: fake sleep, wake on input, quiet fans, control socket for the API; `main.py` + `src/index.tsx` → `dist/index.js` (committed) | the console |
| `bc250-api/` | REST service (Python stdlib) on port 8250: stats, fps from gamescope's stats pipe, tune switches, sleep/wake, poweroff; `panel.js` = the console part of the ESP32 page, served at `/panel.js` | the console |
| `esp32-power-control/` | ESP32-C3 sketch (`bc250_power_opto.ino`, the web page is a raw string inside it) and `flash.sh` | the ESP32; `flash.sh` on a laptop |
| `install.sh` | the `curl \| bash` guided installer | console or laptop |
| `images/esp32-gui.png` | screenshot of the ESP32 page, embedded in the README | |
| `.env/` (gitignored) | local notes: console address, ssh user, test log; **never commit** | |

## Rules of thumb

* **A switch touches five places.** Adding or changing a tune option means: the script (defaults,
  `load_conf`, `write_conf`, `validate_conf`, `cmd_set` keys, `cmd_apply`, the JSON and text status,
  the whiptail menu, `VERSION`), the Decky plugin (`index.tsx`, then rebuild `dist/index.js`), the
  console panel (`OPTS` and `cur()` in `bc250-api/panel.js`, bump its `VERSION`), `bc250-api`
  (`TUNE_KEYS` and `VALUE_OK`), and the docs (the switch tables in `README.md` and
  `decky-bc250-tune/README.md`). Then the screenshot. None of this needs an ESP32 reflash.
* **The ESP32 sketch is for the ESP32's own things.** State row, the four buttons, the address
  editor, the offline notice, and the loader that pulls `panel.js` from the console. The contract
  between them (`bc250Panel.mount/update/busy`, documented in `bc250-api/README.md`) is what makes
  reflashes rare; do not put console-side UI back into the sketch.
* **Docs and installer move with the code.** Any new service, script, option or install step gets its
  README paragraph, its row in the tables, a line in `install.sh` if it changes what is installed,
  and a mention in the installer's closing hand-off list if a human has to do something.
* **Validate before writing.** `bc250-tune set` validates the whole config before `write_conf`; a bad
  value that reaches the file makes every later `apply` (including the boot-time one) die at that
  key. The API checks values per key before calling the script. Keep both.
* **Guards, not trust, on hardware.** Anything that takes manual control of the fan header (Sleep
  plugin, fan daemon) hands it back on wake/exit, on a stalled fan, on a sensor that stops reading, and
  when the fans do not follow PWM. Anything that reboots at boot (`CORES_AUTO_REBOOT`) has a persistent
  stamp so it can never loop.
* **One owner per knob.** The fan daemon leaves the header alone while the Sleep plugin's state file
  exists; the Sleep plugin restores what it found. `bc250-tune` disables the image's own CU service
  and owns the CU table. Do not add a second writer for the governor config, the CU table, the core
  mask or the fan header.
* **Rebuild `dist/`.** Plugin frontends are prebuilt and committed. After editing `src/index.tsx`:
  `pnpm i && pnpm run build` in that plugin folder, commit `dist/index.js`. Do not commit
  `pnpm-lock.yaml` or `dist/*.map`.
* **The sketch stays generic.** `WIFI_SSID`, `WIFI_PASS`, `OTA_PASS` and `CONSOLE_API` in the repo are
  placeholders (`YOUR_SSID`, `YOUR_PASSWORD`, `CHANGE_ME`, `http://YOUR_CONSOLE_IP:8250`). Real values
  go into `esp32-power-control/config.local` (gitignored) and `flash.sh` substitutes them in a build
  copy. Before every commit: `git diff --cached | grep -i` for the local values; the count must be 0.
* **Addresses in docs are generic.** `<console-ip>`, `<esp32-ip>`, `http://YOUR_CONSOLE_IP:8250`.
  Prefer the reserved IP over `bc250.local` in prose; mDNS is a bonus many networks lack.

## Gotchas that cost time before

* **Arduino prototype generator vs the page.** The sketch preprocessor cannot see raw-string bounds
  and treats `//` as a comment, so a `//` inside `PAGE_HTML` flips its string tracking and it emits
  bogus prototypes for the page's JavaScript (`'function' does not name a type`). Write regex slashes
  as `[/][/]` and URL slashes as `&#47;&#47;`. Line-start `//` comments in the JS are tolerated only
  because they happen not to swallow a quote; do not rely on it.
* **systemd ordering cycles.** `bc250-tune.service` is `After=multi-user.target` and wanted by it. A
  unit that is `After=bc250-tune.service` and also wanted by multi-user is a cycle, and systemd
  silently drops it from the boot. Check with `systemd-analyze verify`.
* **OTA only with the console OFF.** The ESP32 arms OTA only in its `OFF` state; reflashing it while
  the console runs would hard-cut the power. `flash.sh ota` refuses otherwise. The ESP32 can never be
  updated from the console itself.
* **Warm reboots and USB.** With `CORES_AUTO_REBOOT` every power-on ends in a warm reboot. Controller
  dongles behind a hub on the xHCI controller can hang on a warm reboot; a board USB 2.0 port works.
* **`pkill -f` over ssh.** A pattern that also appears in the remote command line kills the remote
  shell (exit 255). Anchor with `^` or use `-x`.
* **Decky does not reload a plugin on a plain file replace.** `sudo systemctl restart plugin_loader`
  after installing; unloading the Sleep plugin wakes a sleeping console.
* **gamescope stats pipe.** `$XDG_RUNTIME_DIR/gamescope-stats/stats.pipe` gets an `fps=` and `focus=`
  line every 300 composited frames, and nothing while the Steam UI sits idle. Nobody else reads it.
* **BIOS fan mode.** In *Full Speed* the EC ignores every PWM write; nothing in software can tell
  except that the rpm never moves. Fans that never see the PWM wire behave the same.
* **Sudo over ssh in tests.** sudo's no-tty timestamp is keyed by the parent pid, so a piped
  `curl | bash` cannot reuse a credential cached in the ssh shell; source the script instead, or use
  a real terminal.

## Testing on the real hardware

* The console's address, ssh user and the test log are in `.env/dev.md` (gitignored). Passwords are
  never stored; ask the owner. Nothing from `.env/` goes into a commit or a doc.
* Deploy = copy the file, install as root, restart the unit or `plugin_loader`; then verify through
  `http://<console-ip>:8250/api/status` and the journal (`journalctl -u bc250-fan`,
  `PluginLoader` lines for the plugins).
* Power off and on through the ESP32 (`/rest/status`, `/rest/on`, `/rest/off`), never with the
  case button from a script. After a power-on with `CORES_AUTO_REBOOT` on, expect one automatic
  warm reboot and about 90 s until the API answers.
* Fan or PWM experiments: change one thing, read the rpm for a few seconds, restore auto mode
  (`echo 2 > pwmN_enable`) before moving on.
* Launching a game on the console for a test shows on the owner's TV; say so, and close it after.

## Commits

Small, one topic each, message body explains the why and what was verified on hardware. Screenshots
of the ESP32 page are re-rendered with the real console data when the page changes.
