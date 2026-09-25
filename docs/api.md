---
title: Stats and switches over the network
nav_order: 8
---

# Stats and switches over the network
[`bc250-api/`](https://github.com/lethevimlet/lethe-bc250/tree/main/bc250-api/) is a one-file REST service (Python, standard library only) that runs as a
systemd unit on the BC-250 and answers on port 8250:

```
GET  /api/status                 fps + game, GPU MHz/°C, CPU °C/MHz/cores, SoC and ~total W, fan rpm, VRAM, RAM, asleep, tune
GET  /api/tune[?refresh=1]       bc250-tune status --json (cached 30 s)
POST /api/tune/set               {"key":"cu","value":"40"}  → bc250-tune set cu 40
POST /api/tune/reboot            warm reboot (8 cores)
POST /api/tune/restart-session   gaming session restart (resolution)
POST /api/sleep, /api/wake       fake sleep / wake (through the BC-250 Sleep plugin's control socket)
POST /api/sleep/toggle           sleep if awake, wake if asleep (one click of the case button)
```

```bash
sudo bc250-api/install.sh                       # on the BC-250
curl http://<console-ip>:8250/api/status         # from anywhere on the LAN
```

FPS and the focused game come from gamescope's own stats pipe, which SteamOS creates and nothing
reads (a line every 300 composited frames, so `fps` is `null` while the Steam UI sits idle). The
rest is the same sysfs the HUD line reads, sampled once a second in a background thread, so a
request answers in milliseconds. The ESP32 page uses it for its **Console** panel ([Behaviour and the web page](power-page.md#behaviour-and-the-web-page)); anything
else on the LAN can too. There is no authentication, like the ESP32's own power API: keep port 8250
on the trusted LAN. Details in [bc250-api/README.md](https://github.com/lethevimlet/lethe-bc250/tree/main/bc250-api/README.md).
