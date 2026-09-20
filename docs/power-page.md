---
title: Behaviour, web page, troubleshooting
nav_order: 3
parent: Soft power control
---

# Behaviour, web page, troubleshooting
## Behaviour and the web page

<p align="center">
  <img src="images/esp32-gui.png" alt="The ESP32's web page: state RUNNING with uptime, Power on and Force off buttons, then the Console panel with FPS and game, GPU, CPU, power, fan and VRAM tiles, a pending warm-reboot notice and the bc250-tune switches" width="380">
</p>

The page at `http://<esp32-ip>` (the IP the router reserves for the ESP32; `http://bc250.local` also
works where the network resolves mDNS) is served by the ESP32 itself, with no
internet dependency. It shows the state (`OFF`, `STARTING`, `RUNNING`, `STOPPING`) with the ESP32's
uptime, a **Power on** button that is enabled only when off, and a **Force off** button that is enabled
only when running. The footer shows the Wi-Fi signal, whether OTA is armed (`OTA ready` only while
off), the sense line (`HIGH` while the BC-250 reports alive), and the IP and MAC, the latter for the
router's DHCP reservation. It polls `/rest/status` every two seconds while the tab is visible.

While the state is `RUNNING` the page grows a **Console** panel, fetched by the phone's browser
straight from `bc250-api` on the BC-250 ([Stats and switches over the network](api.md)) every five seconds; the ESP32 only hands the browser
the address (`console` in `/rest/status`, from `CONSOLE_API` in the sketch). Once the console
answers, the status line refines the ESP32's `RUNNING` into **RUNNING** (a game is running),
**IDLE** (on, no game) or **SLEEP** (the fake sleep of [Fake sleep](sleep.md) holds it, blue dot), and the button row
grows to **Power on · Shut down · Sleep · Force off**: Shut down is a clean `systemctl poweroff`
through `bc250-api` (the ESP32 cuts the PSU once the board reports down), Sleep / Wake drives the
fake sleep. Everything below the **Console** header (the tiles, the pending notice, the switches)
is `panel.js`, served by `bc250-api` and loaded by the page once the API answers, so that part
updates with the console and never needs a reflash; the firmware keeps only the state row, the four
buttons, the address editor and the offline notice. Tiles show FPS and the
running game, GPU clock and temperature, CPU temperature with cores and clock, SoC and estimated
total power, fan rpm and VRAM use. Below them are the `bc250-tune` switches (compute units, cores,
HUD, GPU floor and ceiling, VRAM split, resolution): a tap runs `bc250-tune set` on the console, and
when a change still needs a warm reboot or a session restart a notice appears with the button for
it. The tiles and switches only appear once `bc250-api` answers; until then (the OS still booting,
or the service not installed) the panel shows a short notice with **retry** and **change address**
links and a pointer to [Stats and switches over the network](api.md), and it says `asleep` during a fake sleep.
The footer's `console …` entry shows the `bc250-api` address; tapping it opens **Settings**.

## Settings: Wi-Fi, IP address, console address

<p align="center">
  <img src="images/esp32-settings.png" alt="The Settings section of the ESP32 page: Wi-Fi name and password, DHCP or static IP with address, gateway, mask and DNS, the OTA password to confirm, and a separate box for the bc250-api address" width="300">
</p>

Everything that used to need a reflash lives in one collapsible **Settings** section at the bottom of
the page. The values compiled into the sketch are only the first-boot defaults; what you save here is
kept in the ESP32's flash and wins over them.

* **Wi-Fi**: network name and password (leave the password empty to keep the current one, or tick
  *open network*).
* **IP address**: *Automatic (DHCP)* or *Static* with address, gateway, subnet mask and optional DNS.
  The static fields come pre-filled with what DHCP currently gives, so switching to static keeps the
  same address unless you change it.
* **Console (bc250-api)**: the address the page polls for stats and switches, in its own box with
  its own save button. No password needed for this one.

Saving Wi-Fi or IP settings asks for the **OTA password**, because losing them can lock you out. The
read endpoint never returns the Wi-Fi password.

**A change cannot cut a running console.** It is applied without restarting the ESP32, so the PSU
stays latched. It is also only made permanent once it has proven itself:

1. New credentials get three join attempts within 25 seconds. If the ESP32 cannot join, it restarts
   its radio, returns to the previous settings and the page says why (*could not join that network,
   name or password wrong?*). On the test bench a wrong password cost eleven seconds.
2. If it joins but nothing reaches it within two minutes (a wrong static IP), it returns to the
   previous settings too. Reaching it, on the old address or the new one, is what saves the change;
   the page polls the new static address for you and moves there.
3. A power loss in between boots the previous, known-good settings.

**Hotspot fallback.** If the ESP32 cannot join Wi-Fi after being powered up (three failed joins or
30 seconds), it opens its own WPA2 network **`BC250-AP`** (password: your OTA password) for five
minutes and serves the same page at `http://192.168.4.1`, power buttons included. So a console that
moved house, or a router that changed its password, needs no reflash: power it up, join the hotspot,
fix the Wi-Fi under Settings, or simply use the buttons. That automatic opening happens once per
power-up. Wi-Fi that drops later is only retried, quietly; to get the hotspot then, **hold the case
button for ten seconds** (five minutes, at any time; this is also the way back in when the ESP32
joined a network but cannot be reached on it), use *Open the hotspot* in Settings, or power-cycle the
ESP32. Mind that the button hold first does what a press does: it starts the console from off, and
forces it off after five seconds when running. While the hotspot is up the ESP32 retries Wi-Fi only
once a minute and not at all while a phone is connected, and it closes the hotspot a minute after
Wi-Fi is back.

**The hotspot is budgeted, because it runs hot.** An access point cannot use modem sleep: the
receiver is on all the time. Measured on the board, the chip sensor reads about 45 °C in normal Wi-Fi
mode and 61 °C within ninety seconds of hotspot. Minimum transmit power, beacons four times sparser
and a single allowed client bought one or two degrees, no more: the always-on receiver is the cost,
and no setting keeps an active hotspot in the 40s. The same log shows the reading back at 46 °C
within thirty seconds of closing, so what matters for the hardware is the long-run average, and that
is what the firmware limits: the hotspot opens by itself only once, for five minutes after a power-up
without Wi-Fi, otherwise only on demand for five minutes, and never longer than fifteen with a phone
attached. One board that was left overnight with wrong Wi-Fi data, on the first firmware
without this budget, sat in hotspot mode all night and died. On top of the budget, the driver's own
non-stop reconnect scanning is switched off after twenty seconds without a join in favour of paced
retries, and above 70 °C on the chip sensor the hotspot closes at once and will not open even on
demand. *Open the hotspot* in Settings becomes *Close the hotspot* while it is up. The footer shows
the chip temperature. *Open the hotspot
now* in Settings, or **holding the case button for ten seconds**, opens it on demand for five minutes;
the button route is the way back in when the ESP32 joined a network but cannot be reached on it.
Mind that the same hold first does what a press does: it starts the console from off, and forces it
off after five seconds when running.

As a last resort, if Wi-Fi stays down for an hour **while the console is off**, the ESP32 restarts
itself to come back with a fresh radio; that restart does not count as a power-up, so it opens no
hotspot. It never restarts while the console runs.

| Endpoint | What |
|----------|------|
| `GET /rest/net` | current and saved network settings, without the password; `last_error` after a rollback |
| `POST /rest/net` | `ssid`, `pass`, `open`, `mode=dhcp\|static`, `ip`, `gw`, `mask`, `dns`, `ota`; `reset=1` returns to the compiled defaults |
| `POST /rest/hotspot` | `ota`; opens the hotspot for five minutes (`secs=30…300`), `off=1` closes it |
| `GET /rest/console?url=…` | the `bc250-api` address; empty restores the compiled default |

* A short press when off starts the machine. A short press while running does nothing on purpose:
  shut down in software so the filesystem is clean. Use Shutdown, not Sleep: sleep and hibernation
  do not work on the BC-250 ([Things we learned](lessons.md)), and a sleeping board would leave the PSU on with no way to wake.
* Hold the button five seconds to force the PSU off. This only arms once the firmware has reached
  RUNNING, which needs the sense line connected.
* The web page and `/rest/on`, `/rest/off`, `/rest/status` do the same over the network. A web off is
  a hard cut. `/rest/status` also carries `console`, the `bc250-api` address the page polls;
  `/rest/console?url=…` changes it without a reflash, and `/rest/net` does the same for Wi-Fi and IP
  (see Settings above). For bench checks without a serial cable it also has `temp` (chip, °C), `btn` and
  `presses` (the button, on pad `7` or `10`), `lows` (other free pads pulled to ground since boot) and
  `pins` (the live level of every pad).
* After a mains outage the machine stays off and waits for a press. To change that, call `psuOn()` at
  the end of `setup()` instead of entering `ST_OFF`.
* An ESP32 crash or watchdog reset cuts a running machine: the LED goes dark before any code runs.
  The circuit always fails off rather than on. Treat the ESP32 as part of the power path and do not
  reflash it while the BC-250 is up; OTA is gated on the OFF state for exactly this reason.

## If something misbehaves

| Symptom | Cause |
|---------|-------|
| PSU clicks on then straight off | `BOOT_BLANKING` too short, or the sense wire is miswired |
| PSU never starts | PC817 pins 1 and 2 swapped; the LED only conducts one way |
| PSU starts on its own | Pins 3 and 4 swapped, or the emitter tied to the logic ground node instead of pin 17 |
| ESP32 won't boot with sense connected | Sense on a strapping pin; keep it on GPIO 6 |
| Stays on after shutdown | TPMS1 pin 9 not actually dropping; remeasure |
| The case button does nothing, the web page's Power on works | Watch the page footer while pressing: it shows `button up ×N` and counts every press the ESP32 sees. If the count does not move, the button is not reaching pad `7` and ground (wrong pad, the mirrored underside labels, or a broken return to the star node). If the wiring checks out, measure pad `7` against the USB-C shell with the ESP32 powered: it must read 3.3 V at rest. 0 V there, with nothing connected that could pull it down, is a pad that never reached the chip (seen on one SuperMini): move the button wire to pad `10`, which the firmware reads as well. The footer also lists `pads seen low`, any other free pad that was pulled to ground since boot, which finds a wire on the wrong pad. If it counts but nothing starts, the state is not `OFF`: a press only starts the console from `OFF` |
| HUD shows `FAN n/a`, BIOS hardware monitor shows 65535 rpm, no fan curve | The sense wire sits on, or touches, an LPC signal pin of TPMS1 and knocks the Super I/O off the bus. Unplug it from TPMS1 and the reading comes back; then seat it on the real 3.3 V pin, clear of its neighbours (build step 8) |
| Shuts down during a warm reboot | Raise `SENSE_LOW_HOLD` above your reboot time |
| Cuts power ~25 s after every boot | R2 too large, or the sense pin is short of margin |
| Serial monitor stays blank | USB CDC On Boot not enabled, or not recompiled after changing it |
