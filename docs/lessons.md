---
title: Things we learned
nav_order: 10
---

# Things we learned
* **Cabling is a fire-safety item.** The BC-250 peaks at 200–250 W on 12 V. Split it over both
  Micro-Fit inputs from an EPS12V cable; never hang the board off one PCIe 8-pin on 18 AWG wire.
* **Sleep and hibernation are not supported.** The BC-250 firmware exposes no S3 state, and the S3
  sleep path has not been reverse-engineered yet, so there is nothing for the kernel to use; the only
  option it offers, suspend-to-idle, hangs the board and needs a power cut. Hibernation (S4) is
  advertised but buggy and is not usable either. Without the BC-250 Sleep plugin the Sleep entry in
  Steam's power menu hangs the board; with it installed ([Fake sleep](sleep.md)) that entry runs the fake sleep and the
  real suspend is masked. For powering down, always use **Shutdown** and let the ESP32 circuit cut
  the PSU.
* **The BIOS fan curve idles at 50 % duty.** Harmless on fans that ignore PWM, loud on ARCTIC P12 Pro
  (~1700 rpm at a 50 °C idle). `FAN_CURVE=on` in `bc250-tune` ([Tuning: bc250-tune](tuning.md)) runs the header from the die
  temperature instead, ~600 rpm at idle, and the Sleep plugin takes it lower still during a fake sleep.
* **`FAN n/a` and 65535 rpm in the BIOS: look at the TPMS1 sense wire first.** Fan speed, the fan
  curve and the Sleep plugin's quiet fans all go through the NCT6686D via the `nct6687` driver. On a
  second, identical build the chip answered on no address: the driver logged `chip ID 0xffff` and
  unloaded, raw reads of its config ports and its `0xa20` window returned all ones, the BIOS
  hardware monitor showed 65535 rpm, and a side-by-side with a working board showed identical
  firmware settings but a DSDT whose `IOST` (Super I/O devices found at POST) was 0: the BIOS
  could not see the chip either, while the fans still followed its curve. The cause was the ESP32
  sense wire on the TPMS1 header. That header is the LPC bus, the pins next to the 3.3 V pin are LPC
  data lines that also sit at 3.3 V, and a wire loading one of them leaves the power logic working
  while every LPC read fails. Unplugging it brought the chip back at once ([Build order](power-wiring.md#build-order) step 8, [If something misbehaves](power-page.md#if-something-misbehaves)).
  Until then the project shows `FAN n/a`, `bc250-tune status` says why, and the fan curve is not
  started; the fans follow the BIOS curve.
* **Plug controller dongles into a board USB port, not a hub.** The Xbox 360 wireless receiver hung
  on every warm reboot while it sat behind a hub on the board's xHCI controller: it stalled its first
  descriptor read and stopped answering until physically unplugged, and nothing in software could
  power-cycle it (the root ports have no power switching and the hub's ganged switching never drops
  VBUS). On one of the board's own USB 2.0 ports (the OHCI controller) it survives warm reboots. This
  matters more than it sounds: with `CORES_AUTO_REBOOT` every power-on ends in a warm reboot.
* **The image ships no VRAM tool.** `bc250memcfg` writing the split into CMOS is the way on the stock
  BIOS; the alternative is a modded BIOS.
* **40 CU and 8 cores are a silicon lottery.** They work on this board; they will not work on every
  board. Test before trusting them, one unlock at a time.
* **40 CU is not a gaming upgrade.** Measured by the 40 CU researchers: +4.4 % in a graphics benchmark,
  1.6x in compute. Games on this chip are fill-rate bound.
* **The GPU governor cap matters more.** `ujust bc250-cu-sweet-spot` caps the governor at 1500 MHz and
  leaves it there; the image default is 1850 MHz. `bc250-tune` puts it back.
* **8 cores work but blind the GPU clock readouts.** With the core unlock active every kernel GPU
  clock value is garbage; the GPU is fine. `bc250-tune` estimates the clock from the GFX voltage and
  marks it with `~`.
* **No CPU idle states.** The BIOS tables name the processors differently from the C-state table,
  so the kernel drops it. Idle package power is ~32 W with the GPU at 500 MHz; the fixed-clock GDDR6
  is the floor.
* **Steam's Sleep is just a logind suspend.** With `sleep.target`/`suspend.target` masked, logind
  refuses it harmlessly and Steam carries on; that is the safety net the sleep plugin relies on. The
  suspend hooks Pause Games registers (`RegisterForOnSuspendRequest`) do not exist on this Steam
  build; wrapping `SteamClient.System.SuspendPC` from a Decky plugin does work.
* **MangoHud in Gaming Mode ignores `MangoHud.conf`.** Steam selects preset 1–4 from the Performance
  Overlay slider; user presets live in `~/.config/MangoHud/presets.conf`.
* **Decky Loader and `LD_LIBRARY_PATH`.** Decky's PyInstaller bundle leaks its library path to child
  processes; `systemctl` then fails with an OpenSSL version error. Scrub the environment in anything
  Decky spawns.
