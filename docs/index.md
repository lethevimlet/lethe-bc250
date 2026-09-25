---
title: Home
nav_order: 1
---

# lethe-bc250

<p align="center">
  <img src="images/printed-case.jpg" alt="The finished BC-250 build in its 3D-printed case, power button on the front" width="720">
</p>

Notes, scripts, firmware and case files for turning an **AMD BC-250** mining board into a small
living-room gaming PC running **Bazzite** (SteamOS-like, Gaming Mode by default). Everything here is
what one particular build actually uses, so it favours "this works" over "every option". The repo is
self-contained: the tuning script, its Decky plugin, the ESP32 firmware and the case STLs are all in
here.

> **Status:** the build is complete and in daily use; the guide is still being polished.

## What this repo brings to the table

Three things you will not find in the base images, plus the build notes to put them together:

**1. Robust power management with an ESP32** ([Soft power control](power.md)). Out of the
box the BC-250 has no power button and no way to switch itself off: it boots when the PSU comes up
and, after a shutdown, the PSU keeps running. A small optocoupler circuit on an ESP32-C3 fixes that:

* **Shut down like a normal computer.** Pick Shutdown in Steam or the desktop and the machine actually
  goes dark: the ESP32 sees the board halt and cuts the PSU to standby, under a watt.
* **A real power button.** One press on the front of the case starts it; hold five seconds to force
  it off.
* **Remote on/off over the web.** The ESP32 serves a small page on your LAN (and a REST API) to power
  the console on from the sofa or another room, and to hard-cut it if it ever hangs.
* **The console's stats and switches on the same page.** A tiny REST service on the BC-250
  (`bc250-api`, [Stats and switches over the network](api.md)) exposes what the HUD shows
  (FPS and game, GPU clock and temperature, CPU, SoC and total power, fan rpm, CU and core counts)
  and every `bc250-tune` switch. The ESP32 page polls it from the phone's browser every 5 s while
  the machine runs, so the ESP32 itself does no extra work.

**2. Tuning from the Steam menu, no custom BIOS: the BC-250 Tune Decky plugin**
([Tuning: bc250-tune](tuning.md)). Everything the modded-BIOS crowd flashes for, done from Linux on the
stock firmware and switchable from the Quick Access menu without leaving Gaming Mode: VRAM/RAM split
(written to CMOS), GPU clock range, the 40 CU unlock, the 8-core unlock, output resolution, and a
one-line MangoHud HUD showing what is enabled. A boot service re-applies it all, and a Bazzite reinstall
does not lose the VRAM split.

**3. Pseudo sleep: the BC-250 Sleep Decky plugin** ([Fake sleep](sleep.md)).
Real suspend and hibernation do not work on the BC-250 (the S3 path has not been reverse-engineered,
suspend-to-idle hangs, hibernation is buggy), and Steam's Sleep entry would hang the board. The plugin
gives you what you wanted from Sleep instead: it freezes the game, mutes, turns the TV off and wakes on
any controller or keyboard button exactly where you left off (not the case power button: the board
stays powered, fans and LEDs on, and a long press there cuts the power), and it takes over Steam's
Sleep entry so the real, board-hanging suspend can never be triggered.

Recommended companion from the Decky store: **Pause Games** (freeze and resume individual games with
SIGSTOP/SIGCONT, like the Steam Deck's quick suspend) for pausing one game while the console stays
awake, for example to hop into another game and come back. It is optional: BC-250 Sleep does its own
freezing and does not depend on it. It works on the BC-250 after the small permission fix in
[Install](tuning.md#install).

For everything about the board itself (BIOS, pinouts, VRAM, power, kernel, governor), the
[AMD BC-250 community documentation](https://elektricm.github.io/amd-bc250-docs/) by elektricM is the
reference this guide leans on. Start there if something here is not covered.

**Contents**

0. [Quick start](index.md#quick-start)
1. [Components](parts.md)
2. [Hardware preparation](hardware.md) — heatsink lid, power cable
3. [Install Bazzite and the 62fixolab BC-250 image](os.md)
4. [Tuning: `bc250-tune`](tuning.md) — VRAM split, clocks, 40 CU, 8 cores, HUD, resolution, Steam menu plugin, fake sleep plugin
5. [Stream with Moonlight](streaming.md) — Sunshine on the console, and what it can capture
6. [Soft power control with an ESP32](power.md) — circuit, flashing, OTA, web page
7. [3D-printed case](case.md)
8. [Things we learned](lessons.md)
9. [Credits](credits.md)

---

## Quick start

The software side has a guided installer. On the BC-250 (once Bazzite is on it, [Install Bazzite](os.md)) it clones the
repo into `~/lethe-bc250` and lets you tick what to install: `bc250-tune` with its boot service,
Decky Loader, the Tune and Sleep plugins, `bc250-api`. Run the same line on your laptop or desktop
and it offers the ESP32 firmware helper instead (build, first flash over USB, later updates over the
air). Re-running it later is the update path.

```bash
curl -fsSL https://raw.githubusercontent.com/lethevimlet/lethe-bc250/main/install.sh | bash
# prefer to read it first?  git clone https://github.com/lethevimlet/lethe-bc250 && cd lethe-bc250 && ./install.sh
# scripted:                 ./install.sh --all   |   ./install.sh --only tune,api
```

The whole build, in order. Each step links to the section with the details.

1. **Parts** — get everything in [Parts](parts.md). The EPS12V → 2× Micro-Fit power cable is not
   optional.
2. **Board prep** — take the lid off the heatsink and renew the paste and pads
   ([Remove the heatsink lid](hardware.md#remove-the-heatsink-lid)), cut the tabs off the power plugs and fit the cable
   ([Fit the power cable and the auto power-on jumper](hardware.md#fit-the-power-cable-and-the-auto-power-on-jumper)). Set the AUTO_PWRON1 jumper to auto power-on, disable IOMMU and set the fan mode to
   Default or Customize, not Full Speed, in the BIOS ([Before the OS](os.md#before-the-os)).
3. **OS** — install stock Bazzite (deck), then rebase to the 62fixolab `-40cu` image and reboot
   ([Install Bazzite](os.md)).
4. **Tune** — run the installer above on the BC-250 and tick `bc250-tune`, Decky Loader, the two
   plugins and `bc250-api`, and Sunshine if you want to play it from another screen (or do it by hand:
   [Tuning: bc250-tune](tuning.md), [Stats and switches over the network](api.md),
   [Stream with Moonlight](streaming.md)).
   Set the Performance Overlay slider to level 1 for the HUD. Treat 40 CU and 8 cores as optional
   experiments: each board is a silicon lottery.
5. **Power button** — build the ESP32 optocoupler wiring, flash the firmware (the installer's ESP32
   helper on a laptop, or the Arduino IDE), and test with the multimeter at each step
   ([Soft power control](power.md)).
6. **Case** — print the STLs, fit the inserts, mount the two fans on the double shroud, assemble
   ([3D-printed case](case.md)).
7. **Use** — Shutdown from Steam powers the PSU down by itself; press the button to start. Never use
   Sleep ([Things we learned](lessons.md)).


