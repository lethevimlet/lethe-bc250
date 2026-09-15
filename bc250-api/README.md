# bc250-api

Part of [lethe-bc250](../README.md). A small REST service for the BC-250 that exposes what the
one-line HUD shows, plus the `bc250-tune` switches, as JSON over the LAN. The ESP32 power
controller's web page polls it from the phone's browser, so the console's stats and tuning live on
the same page as the power button, with no display needed and nothing extra running on the ESP32.

```
GET  /api/status                 live stats (sampled every second) + fps + cached tune status
GET  /api/tune[?refresh=1]       `bc250-tune status --json`, cached 30 s
POST /api/tune/set               {"key":"cu","value":"40"}  -> `bc250-tune set cu 40`
POST /api/tune/reboot            -> `bc250-tune reboot`          (warm reboot, for 8 cores)
POST /api/tune/restart-session   -> `bc250-tune restart-session` (for a resolution change)
POST /api/sleep                  fake sleep through the BC-250 Sleep plugin (its control socket)
POST /api/wake                   wake from it
```

Port 8250, JSON, CORS open (`Access-Control-Allow-Origin: *`) so a page served by another host
can read it. Standard library only, one file, runs as root under systemd because the tune
switches need it.

## `/api/status`

```json
{
  "fps": 60.0, "fps_samples": 13, "focus": "251470", "game": "TowerFall Ascension",
  "gpu":   {"mhz": 500, "mhz_estimated": false, "temp_c": 44, "vram_used_mb": 449, "vram_total_mb": 6144},
  "cpu":   {"temp_c": 49, "mhz": 3194, "cores": 6, "threads": 12, "load1": 0.39},
  "power": {"soc_w": 31.8, "total_w": 77, "total_offset_w": 45},
  "fan":   {"rpm": 1595},
  "mem":   {"used_mb": 2601, "total_mb": 9650},
  "uptime_s": 3436, "asleep": false, "sleep": {"asleep": false, "since": null},
  "game_running": {"appid": "251470", "name": "TowerFall Ascension"}, "ts": 1789491944.1,
  "tune":  { "...": "the bc250-tune status --json object: uma, gpu, cu, cores, hud, res, pending" }
}
```

* **fps / focus / game** come from gamescope's own stats pipe (`gamescope -T …/stats.pipe`, linked
  from `$XDG_RUNTIME_DIR/gamescope-stats`). SteamOS creates it and nothing else reads it; gamescope
  writes an `fps=` and a `focus=` line every 300 composited frames, i.e. every ~5 s in a 60 fps
  game. While the Steam UI sits idle no frames are composited and `fps` is `null`. `focus` is the
  Steam app id (or `steam`); `game` is looked up in the `appmanifest_<id>.acf` files.
* **gpu.mhz** is read the way `bc250-tune` reads it: from the SMU clock table, or estimated from
  VDDGFX along the governor's safe-points curve once 8 cores are enabled (the table is garbage
  then; `mhz_estimated` says so).
* **power.total_w** is `soc_w + TOTAL_OFFSET_W` from `/etc/bc250-tune/config`, the same estimate
  the HUD prints.
* **fan.rpm** is the first spinning tach of the nct6687 driver (the "Pump Fan" header on this board).
* **game_running** is the game Steam has launched (its `reaper SteamLaunch AppId=N` process),
  regardless of what gamescope has in focus; `focus` can flip to `steam` while the overlay is open.
* **asleep** / **sleep** report the BC-250 Sleep plugin's fake sleep (`since` is the epoch it started).
  `POST /api/sleep` and `/api/wake` go through the plugin's control socket
  (`/run/bc250-sleep/ctl.sock`) and answer 503 when the plugin is not loaded.
* **tune** is `bc250-tune status --json`, refreshed every 30 s and right after every `set`. It is
  `null` when `bc250-tune` is not installed; the POST endpoints answer 503 then.

`/api/status` answers in a few milliseconds: the sysfs sampler, the pipe reader and the tune
refresh run in their own threads and the request only copies the latest snapshot.

## Install

```bash
sudo ./install.sh        # copies bc250-api to /usr/local/bin, enables bc250-api.service
curl http://localhost:8250/api/status
```

Then point the ESP32 page at it: tap the `console …` entry in the page footer and enter
`http://<console-ip>:8250` (the IP the router reserves for the BC-250), or set `CONSOLE_API` in the
sketch as the compiled default. The power page grows a **Console** panel while the machine is
`RUNNING`. Bazzite's default firewall zone already allows ports above 1024; if you changed it:
`sudo firewall-cmd --add-port=8250/tcp --permanent && sudo firewall-cmd --reload`.

## Security

There is no authentication, by design: it is meant for the same trusted LAN as the ESP32, whose
own `/rest/off` hard-cuts the power without asking either. Anyone on that network can read the
stats and flip the tune switches, including a warm reboot. Do not expose port 8250 beyond the LAN.

## Files

| File | What |
|------|------|
| `bc250-api` | the service (Python 3, standard library only) |
| `bc250-api.service` | systemd unit, `Restart=always` |
| `install.sh` | copy + enable |
