# Working on lethe-bc250

Notes for an agent (or a human) starting a session in this repo. The documentation site (`docs/`,
published at https://lethevimlet.github.io/lethe-bc250/) is the source of truth for what the project
does, and the README is its short front door; this file is about how to change it without breaking the pieces that
depend on each other.

## What lives where

| Path | What | Runs on |
|------|------|---------|
| `decky-bc250-tune/bc250-tune` | one bash script: every tuning switch, the boot-time re-apply (`apply --boot`), the fan-curve daemon (`fan-daemon`), `status --json` | the console, as root |
| `decky-bc250-tune/decky-plugin/` | Decky plugin that only calls `bc250-tune`; `src/index.tsx` → `dist/index.js` (committed) | the console |
| `decky-bc250-sleep/` | Decky plugin: fake sleep, wake on input, quiet fans, control socket for the API; `main.py` + `src/index.tsx` → `dist/index.js` (committed) | the console |
| `bc250-api/` | REST service (Python stdlib) on port 8250: stats, fps from gamescope's stats pipe, tune switches, sleep/wake, poweroff; `panel.js` = the console part of the ESP32 page, served at `/panel.js` | the console |
| `esp32-power-control/` | ESP32-C3 sketch (`bc250_power_opto.ino`, the web page is a raw string inside it) and `flash.sh` | the ESP32; `flash.sh` on a laptop |
| `install.sh` | the `curl \| bash` guided installer; its `sunshine` item is the only one without code of its own here (Bazzite's brew build, plus the fixes in `step_sunshine`) | console or laptop |
| `docs/` | the documentation site (GitHub Pages, Jekyll + just-the-docs, built from this folder): one page per topic, images in `docs/images/` | |
| `docs/images/esp32-gui.png` | screenshot of the ESP32 page, used by the README and the docs | |
| `docs/images/schematic-esp32-pc817.svg` | wiring drawing: the SuperMini from the component side with its real pad order, the PC817 as the real package. Keep it matching the connections table in `docs/power-wiring.md` | |
| `.env/` (gitignored) | local notes: console address, ssh user, test log; **never commit** | |

## Rules of thumb

* **A switch touches five places.** Adding or changing a tune option means: the script (defaults,
  `load_conf`, `write_conf`, `validate_conf`, `cmd_set` keys, `cmd_apply`, the JSON and text status,
  the whiptail menu, `VERSION`), the Decky plugin (`index.tsx`, then rebuild `dist/index.js`), the
  console panel (`OPTS` and `cur()` in `bc250-api/panel.js`, bump its `VERSION`), `bc250-api`
  (`TUNE_KEYS` and `VALUE_OK`), and the docs (the switch tables in `docs/tuning.md` and
  `decky-bc250-tune/README.md`). Then the screenshot. None of this needs an ESP32 reflash.
* **The ESP32 sketch is for the ESP32's own things.** State row, the four buttons, the address
  editor, the offline notice, and the loader that pulls `panel.js` from the console. The contract
  between them (`bc250Panel.mount/update/busy`, documented in `bc250-api/README.md`) is what makes
  reflashes rare; do not put console-side UI back into the sketch.
* **Docs and installer move with the code.** Detail goes in `docs/`, not in the README: the README
  stays a short overview that links into the site. Any new service, script, option or install step gets
  its paragraph on the right docs page, its row in the tables, a line in `install.sh` if it changes what is installed,
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
* **The docs site must still build.** Pages use kramdown, not GitHub's renderer: titles with a colon
  are quoted in the front matter, callouts are `{: .warning }` above a blockquote (not `> [!WARNING]`),
  links between pages are relative `page.md#anchor`. Build it locally before pushing:
  `docker run --rm -e INPUT_SOURCE=docs -e INPUT_DESTINATION=_site -e GITHUB_WORKSPACE=/github/workspace -w /github/workspace -v "$PWD":/github/workspace ghcr.io/actions/jekyll-build-pages:v1.0.13`.
* **Addresses in docs are generic.** `<console-ip>`, `<esp32-ip>`, `http://YOUR_CONSOLE_IP:8250`.
  Prefer the reserved IP over `bc250.local` in prose; mDNS is a bonus many networks lack.

## Gotchas that cost time before

* **Arduino prototype generator vs the page.** The sketch preprocessor cannot see raw-string bounds
  and treats `//` as a comment, so a `//` inside `PAGE_HTML` flips its string tracking and it emits
  bogus prototypes for the page's JavaScript (`'function' does not name a type`). Write regex slashes
  as `[/][/]` and URL slashes as `&#47;&#47;`. Line-start `//` comments in the JS are tolerated only
  because they happen not to swallow a quote; do not rely on it.
* **A lone double quote in the page script breaks the build too.** Same generator, same cause: it
  tracks `"` across the whole raw string. `/[&<>"]/` or `'"'` in the JavaScript made every C function
  after the page "not declared in this scope". Keep double quotes paired inside HTML attributes only.
* **Wi-Fi changes on the ESP32 must never need a reboot and must be able to fail.** The firmware applies
  network settings without restarting, gives untested credentials three joins in 25 s, and rolls back
  with a full radio restart (`WIFI_OFF` then begin): on hardware, the running stack would not
  re-associate after a run of failures while a fresh radio joined at once, and the first version of
  this feature took the ESP32 off the network until it was power-cycled. The hotspot fallback and the
  console-OFF-only self-restart are the nets under that. Test network changes with the console OFF.
* **The ESP32's hotspot is a heat source and killed a board.** No modem sleep in AP mode: +17 °C on
  the chip sensor within minutes, all night fatal. Settings cannot fix it (minimum TX power, sparse beacons and one client
  bought 1-2 °C: the always-on receiver is the cost), only time can. The hotspot opens by itself once per power-up
  (5 min, only if Wi-Fi was never joined since boot) and otherwise only on demand (5 min); keep the 15 min
  cap with a client, the 70 °C cut-off, minimum TX power, no driver auto-reconnect scanning when the network
  is absent), and never add a path that reopens the hotspot in a loop (the self-heal restart marks the
  next boot as "not a power-up" via RTC memory for that reason). `/rest/status` carries `temp`, `btn` and
  `presses` for checks without a serial cable.
* **A button that does nothing is not always wiring or firmware.** One SuperMini had a pad `7` that never
  reached the chip: the firmware read its pull-up as high forever while the pad measured 0 V. The
  firmware reads the button on pad `7` or `10`, and `/rest/status` has `lows` (free pads pulled to ground
  since boot) and `pins` (live levels) to find such things remotely. Compare the meter with `pins` early.
* **Sunshine on this box is software-encoded, and Bazzite's recipe is not the whole install.** The GPU
  (`cyan_skillfish`) gets no VCN block from amdgpu, so there is no VAAPI or Vulkan encoder: Sunshine
  uses libx264, documented in `docs/streaming.md`; do not chase "Encoder [vaapi] failed". `ujust
  setup-sunshine enable-brew` installs and starts it but exits 1 afterwards (its unit override names
  `homebrew.sunshine`, the formula creates `app-dev.lizardbyte.app.Sunshine`), its `sudo` step needs a
  tty, and `ujust setup-sunshine status` mistakes the brew unit for the Flatpak. `step_sunshine` in
  `install.sh` therefore checks the Cellar, sets the capabilities itself and restarts the unit.
* **Every HTTP handler in the sketch starts with `if (!authGate()) return;`.** The optional login (off
  by default, NVS `auth_on`) is enforced per handler, so a new endpoint without that line is an open
  door. Digest is verified by the sketch's own `authCheck()`, not `WebServer::authenticate()`: that one
  remembers a single nonce, so two open browsers would re-challenge each other on every request and
  each miss would count as a wrong password. `/rest/auth` with the OTA password bypasses the gate on
  purpose (the way back from a forgotten password); keep it the only such path.
* **`flash.sh` deletes the old binary before compiling.** It once reported success after a failed
  compile because a stale `.bin` was still there.
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
* **No fan sensor at all.** The Super I/O can drop off the bus (nct6687 logs `chip ID 0xffff` and
  unloads, the BIOS shows 65535 rpm). The one case seen was the ESP32 sense wire on TPMS1 loading an
  LPC data line next to the 3.3 V pin; unplugging it brought the chip back. Every consumer must cope with "no hwmon": the HUD prints `FAN n/a`,
  `status --json` carries `fan.sensor` (`ok` / `no-driver` / `no-chip`), the fan daemon is not started,
  the panel and the plugin say why. Never hardcode `fan2` or a platform path; use the first
  spinning tach of whatever nct668x hwmon exists.
* **BIOS fan mode.** In *Full Speed* the EC ignores every PWM write; nothing in software can tell
  except that the rpm never moves. Fans that never see the PWM wire behave the same.
* **`curl | bash` reads the script from stdin.** bash executes a piped script command by command, so
  taking the keyboard from `/dev/tty` at top level leaves it waiting for the rest of the script to be
  typed, and the installer prints nothing. `install.sh` therefore runs everything inside `main()`,
  called on one final line with `exit`. Test the real path, a file run does not exercise it:
  `script -qec 'cat install.sh | bash -s -- --help' /dev/null`.
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
