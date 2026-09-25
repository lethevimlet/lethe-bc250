---
title: Stream with Moonlight
nav_order: 7
---

# Stream with Moonlight

The console can stream Gaming Mode to a phone, laptop, handheld or TV box running
[Moonlight](https://moonlight-stream.org/), through [Sunshine](https://app.lizardbyte.dev/Sunshine/)
on the console. Handy for a game on the sofa's other screen, or for playing from another room while
the TV is used for something else. The installer's **Sunshine** item does the install; this page is
what it does and what to expect.

## Install

Tick **Sunshine** in the installer ([Quick start](index.md#quick-start)), or:

```bash
curl -fsSL https://raw.githubusercontent.com/lethevimlet/lethe-bc250/main/install.sh | bash -s -- --only sunshine
```

It runs Bazzite's own recipe, `ujust setup-sunshine enable-brew`, then finishes what the recipe
leaves undone. The brew build is the one that works in Gaming Mode: the session there is gamescope
drawing straight to the GPU (KMS), and only Sunshine's KMS capture sees that. The Flatpak cannot,
which is why Bazzite steers deck images to brew. The recipe writes `capture = kms` into
`~/.config/sunshine/sunshine.conf` and starts Sunshine as a user service that comes up with the
session; the installer then sets the capabilities KMS capture needs on the binary (without them the
log says `Failed to gain CAP_SYS_ADMIN` and there is no encoder), restarts the service and opens the
ports in the firewall. Bazzite's recipe alone exits with an error after a good install (its last step
names a unit the current brew formula does not create) and skips the capabilities when run without a
terminal, so the installer does not trust its exit code and checks the result instead. Re-running the
installer keeps the install and only repeats those checks; `ujust setup-sunshine update` updates
Sunshine, after which a re-run of the installer puts the capabilities back.

Then, once, from another device on the LAN:

1. Open `https://<console-ip>:47990` (the certificate is self-signed; accept it) and set a username
   and password. Do this soon: until it is set, the first visitor gets to.
2. Install Moonlight on the client, add the console by IP, and type the PIN it shows into the
   Sunshine page under **PIN**.
3. In Moonlight, **Desktop** streams whatever Gaming Mode shows. Ignore Sunshine's stock *Steam Big
   Picture* entry; Desktop is the one.

## The encoder is the CPU

The BC-250's GPU has no video encoder under Linux. The chip is a cut-down PS5 APU (`cyan_skillfish`
to the kernel), and amdgpu brings up no VCN block for it: no VAAPI, no Vulkan video, and `vainfo`
fails on the box. Sunshine tries them, logs `Encoder [vaapi] failed`, and settles on **libx264 in
software**. That works, with two consequences:

* Encoding costs CPU while streaming: a couple of cores' worth at 1080p60 with Sunshine's default
  preset, taken from the same six (or eight, with the [core unlock](tuning.md)) the game has. Lighter
  games do not notice; a CPU-bound one loses some frames while streamed.
* H.264 only. Leave Moonlight on H.264 (HEVC and AV1 have no software encoder in Sunshine), 1080p,
  and 60 fps if the game holds it, otherwise 30. A bitrate of 10 to 20 Mbit/s is plenty over Wi-Fi 5
  or wired.

## What it captures, and the screen question

Sunshine captures the console's real output: the frames gamescope sends to the TV. There is no second
head and no virtual display, so:

* **The TV must be connected.** gamescope needs an output to run at all; with nothing on the
  DisplayPort/HDMI there is no Gaming Mode session, and nothing to capture. A TV that is *connected
  but switched off* usually still counts as connected (the link stays up on most sets), but some
  drop the link when off, and then gamescope loses its output. A cheap **HDMI dummy plug** (an EDID
  emulator, a few euros) in the second port is the usual fix for a headless box, and is the fallback if
  your TV misbehaves.
* **Resolution follows the TV.** The stream is what the TV gets (1080p by default here, see
  [Tuning: RES](tuning.md)). Moonlight's resolution setting only scales the client side; it does not
  change what the console renders. Sunshine's own "change resolution to match the client" needs a
  virtual display and does nothing in this setup.
* **No stream while asleep.** [Fake sleep](sleep.md) turns the TV output off, and an output that is
  off has no frames to capture. Wake the console first: any controller button, the Sleep button on the
  ESP32 page, or one click of the case button ([The button](power-page.md#the-button)), then connect.
* **The stream shares the TV.** Whoever is in front of the TV sees exactly what the Moonlight client
  sees, and the controller in Moonlight is a real controller on the console. It is remote play of the
  one session, not a second one.

A virtual display, so the console could stream with the TV off and at the client's resolution, is
not part of this project yet.

## Notes

* Sunshine listens on TCP 47984, 47989, 47990 and 48010, and UDP 47998 to 48000, 48002 and 48010; the
  installer opens them in firewalld. It is a LAN service: keep it off the internet, or reach it over a
  VPN or Tailscale.
* Sunshine logs with `journalctl --user -u app-dev.lizardbyte.app.Sunshine` (the unit brew creates;
  `sunshine.service` is an alias). The management page has a Troubleshooting tab with the same log.
* `ujust setup-sunshine status` reports the brew install as the Flatpak one (`enable`); the recipe
  checks the unit name, which brew now shares with the Flatpak. Harmless, but do not act on it.
* If pairing works but the stream shows nothing, check that `capture = kms` is in
  `~/.config/sunshine/sunshine.conf` and that the binary has its capabilities:
  `getcap "$(readlink -f "$(command -v sunshine)")"` should show `cap_sys_admin,cap_sys_nice`. A brew
  update installs a new binary without them; re-run the installer's Sunshine item.
