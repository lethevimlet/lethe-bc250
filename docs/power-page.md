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
The footer's `console …` entry shows the `bc250-api` address; tap it to change it (`GET
/rest/console?url=http://host:8250`, stored in NVS, empty restores the compiled default).

* A short press when off starts the machine. A short press while running does nothing on purpose:
  shut down in software so the filesystem is clean. Use Shutdown, not Sleep: sleep and hibernation
  do not work on the BC-250 ([Things we learned](lessons.md)), and a sleeping board would leave the PSU on with no way to wake.
* Hold the button five seconds to force the PSU off. This only arms once the firmware has reached
  RUNNING, which needs the sense line connected.
* The web page and `/rest/on`, `/rest/off`, `/rest/status` do the same over the network. A web off is
  a hard cut. `/rest/status` also carries `console`, the `bc250-api` address the page polls;
  `/rest/console?url=…` changes it without a reflash.
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
| HUD shows `FAN n/a`, BIOS hardware monitor shows 65535 rpm, no fan curve | The sense wire sits on, or touches, an LPC signal pin of TPMS1 and knocks the Super I/O off the bus. Unplug it from TPMS1 and the reading comes back; then seat it on the real 3.3 V pin, clear of its neighbours (build step 8) |
| Shuts down during a warm reboot | Raise `SENSE_LOW_HOLD` above your reboot time |
| Cuts power ~25 s after every boot | R2 too large, or the sense pin is short of margin |
| Serial monitor stays blank | USB CDC On Boot not enabled, or not recompiled after changing it |
