---
title: "Firmware: flashing and OTA"
nav_order: 2
parent: Soft power control
---

# Firmware: flashing and OTA
## Flashing the firmware with the Arduino IDE

The scripted way, from a laptop or desktop (Linux or macOS), needs no Arduino IDE:

```bash
git clone https://github.com/lethevimlet/lethe-bc250 && cd lethe-bc250
./esp32-power-control/flash.sh usb        # first flash over USB; asks for Wi-Fi, OTA password, console address once
./esp32-power-control/flash.sh ota        # later updates over Wi-Fi; refuses unless the console is OFF and OTA is armed
```

It installs `arduino-cli` and the ESP32 core under your home, keeps your values in
`esp32-power-control/config.local` (gitignored) and builds from a copy of the sketch, so the repo's
`bc250_power_opto.ino` keeps its placeholders. The same helper is what [Quick start](index.md#quick-start)'s installer offers when it
is run on a machine that is not a BC-250. The manual IDE route:

1. **Install the ESP32 core.** Arduino IDE 2.x → File → Preferences → *Additional boards manager
   URLs*: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`. Then Tools → Board →
   Boards Manager, search `esp32`, install **esp32 by Espressif Systems**. No other libraries are
   needed: WiFi, WebServer, ESPmDNS and ArduinoOTA ship with the core.
2. **Board.** Tools → Board → ESP32 Arduino → **ESP32C3 Dev Module**.
3. **Enable the serial monitor.** Tools → **USB CDC On Boot → Enabled**. This routes the serial console
   over the USB-C connector. Without it the sketch runs normally but the serial monitor stays blank
   and you lose half the diagnostics. The setting is compiled into the binary, so if you change it you
   must recompile and re-upload; reopening the monitor is not enough.
4. **Configure the sketch.** Open `esp32-power-control/bc250_power_opto.ino` and set:
   These are first-boot defaults: Wi-Fi, a static IP and the console address can all be changed later
   from the page's [Settings](power-page.md#settings-wi-fi-ip-address-console-address) without a
   reflash, and a board that cannot join Wi-Fi opens its own hotspot for that.
   * `WIFI_SSID` / `WIFI_PASS` — WPA2 needs an 8–63 character passphrase; leave `WIFI_PASS` as `""`
     for an open network.
   * `OTA_PASS` — **never leave it empty**; this firmware owns the machine's power path.
   * `MDNS_NAME` — the mDNS name (`bc250` → `http://bc250.local`). Treat it as a bonus: many home
     routers and phones do not resolve `.local`, so the reliable address is the ESP32's IP, fixed with a
     DHCP reservation (below).
   * `CONSOLE_API` — default `http://<console-ip>:8250` where `bc250-api` runs ([Stats and switches over the network](api.md)).
     It can be changed later from the page's Settings (kept in the ESP32's NVS), so a new console IP
     needs no reflash. `""` hides the Console panel.
   * `BENCH_MODE` — `1` for bench testing (see below), `0` for normal use.
5. **Disconnect the +5VSB wire before plugging in USB.** Most SuperMini clones tie USB VBUS straight to
   the 5V pin with no blocking diode, so leaving it connected ties the PSU standby rail to your
   computer's USB port.
6. **Connect and select the port.** Plug the ESP32 in with a data-capable USB-C cable and pick the new
   port under Tools → Port (`/dev/ttyACM*` on Linux, `COMx` on Windows). If no port appears, hold the
   board's **BOOT** button while plugging it in to force download mode, then try again.
7. **Upload.** Sketch → Upload. Then Tools → Serial Monitor at **115200 baud**. If the monitor is empty,
   press the board's **RESET** button with the monitor already open; USB CDC takes a moment to enumerate
   and the monitor sometimes attaches after the first lines have gone out. Expected:
   ```
   [wifi] MAC xx:xx:xx:xx:xx:xx  <- reserve this one on the router
   [wifi] connecting to <ssid>
   [web] server up on port 80
   [boot] BC250 power controller ready
   [state] OFF
   ```
8. **Bench mode.** With `BENCH_MODE 1` the state machine is held back and you drive the output from the
   serial monitor: `g` on, `l` off, `t` toggle, `?` status. Use it for steps 3–4 of the build order,
   then set it back to `0` and re-upload.

The MAC address printed at boot (also shown on the web page and in `/rest/status`) lets you reserve a
fixed IP on the router.

## Updating the firmware over the air (OTA)

Scripted: `./esp32-power-control/flash.sh ota` builds, checks that the ESP32 reports `OFF` with OTA
armed (and refuses otherwise), uploads, and waits for it to come back. By hand:

After the first USB flash, later versions can be sent over Wi-Fi. **The console must be powered
off first.** OTA only arms while the firmware is in the `OFF` state, on purpose: an update reboots the
ESP32, the optocoupler LED goes dark before any code runs, and a running BC-250 would be hard-cut.

1. Shut the BC-250 down from Steam or the desktop. Wait for the PSU to drop to standby; the web page
   or `http://<esp32-ip>/rest/status` must show `"state":"OFF"` and `"ota":true` ("OTA ready" on
   the page). If it says "OTA locked", the machine is not off yet.
2. Arduino IDE → Tools → Port: pick the network port `bc250 at <ip>` (it appears a few seconds after
   the ESP32 reports OTA ready). If it is missing, check that your computer is on the same network
   and that mDNS works; you can also read the IP from the page footer.
3. Upload as usual. When asked, enter the `OTA_PASS` you compiled into the sketch.
   The serial log (if connected) shows `[ota] update starting`, progress every 10 %, then
   `[ota] done, rebooting`.
4. The ESP32 reboots into the new firmware in state `OFF`. Press the button or use the web page to
   start the console again.

Notes:

* Keep `OTA_PASS` the same across versions, or you will need to remember which one the running
  firmware has. Never leave it empty.
* If the machine is switched on while an upload is starting, the firmware refuses the update and
  restarts itself; nothing is written. Power the console off and try again.
* `BENCH_MODE 1` builds arm OTA whenever Wi-Fi is up, because nothing is powered from the bench.
* Do not flash over USB with the +5VSB wire connected (see [Flashing the firmware with the Arduino IDE](power-firmware.md#flashing-the-firmware-with-the-arduino-ide)). OTA has no such restriction, which is
  the main reason to use it once the wiring is finished.
