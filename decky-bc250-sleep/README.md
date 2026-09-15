# BC-250 Sleep (Decky plugin)

Part of [lethe-bc250](../README.md). A fake sleep for the AMD BC-250, which has no working suspend:

1. **Sleep** freezes the running Steam game (`SIGSTOP` on its whole process tree), mutes audio and puts
   the TV to sleep through gamescope (`drm_sleep_external_screen 1`, real DPMS off).
2. **Wake** on the first button press of any controller/keyboard/mouse: TV on, game thawed
   (`SIGCONT`), audio back.
3. **Steam's Sleep entry is taken over** (default on): the plugin replaces `SteamClient.System.SuspendPC`
   with the fake sleep and masks the systemd sleep units, so the real suspend, which hangs the board,
   can never run. Steam still shows its suspend animation and goes black; on wake the backend emits
   `bc250_sleep_woke` and the frontend calls `SuspendResumeStore.OnResumeFromSuspend()`, which is the
   event a real resume would have raised, so Steam's UI comes back.

The board itself stays fully powered at its idle power: fans keep spinning and the board LEDs stay lit;
only the TV is dark. This is a pause with quick resume, not a power saving. **Wake with a controller,
keyboard or mouse button, never the case power button:** with the ESP32 power circuit the board reads
as running, so a short press is ignored and a five-second hold hard-cuts the PSU.

Sleep stays listed in Steam's power menu while the units are masked. If Decky fails to inject on a
boot, the menu's Sleep falls through to the masked units and does nothing (harmless). Turning the
*Use Steam's Sleep button* option off restores both the original call and the units, i.e. the real,
board-hanging suspend: leave it on.

## Install

```bash
sudo mkdir -p ~/homebrew/plugins/bc250-sleep
sudo cp -r plugin.json package.json main.py LICENSE dist ~/homebrew/plugins/bc250-sleep/
sudo chown -R root:root ~/homebrew/plugins/bc250-sleep
sudo systemctl restart plugin_loader   # then Quick Access → Decky → BC-250 Sleep
```

Requires Decky Loader. On Bazzite make Decky's binary readable first:
`sudo chmod a+rx ~/homebrew/services ~/homebrew/services/PluginLoader`.

## Options

| Option | Default | Effect |
|--------|---------|--------|
| Freeze the running game | on | `SIGSTOP`/`SIGCONT` the game found under Steam's `reaper SteamLaunch AppId=N` |
| Mute audio | on | `wpctl set-mute` on the default sink; on wake the same sink is unmuted **by node name** (the HDMI sink disappears while the TV sleeps and returns with a new id), re-asserted for a few seconds because WirePlumber may re-apply the saved muted state |
| Wake on any input | on | backend watches every `/dev/input/event*` for a key/button press (2 s grace after sleeping); rescans once a second so a controller that Steam powered off on sleep and that comes back as a new node still wakes |
| Use Steam's Sleep button | on | wraps `SuspendPC` + `systemctl mask sleep.target suspend.target …`; off restores both |

Settings live in Decky's settings dir (`~/homebrew/settings/bc250-sleep/settings.json`). State while asleep is in
`/run/bc250-sleep/state.json`; if the plugin is reloaded while asleep it wakes everything first.

## Build

```bash
pnpm i && pnpm run build      # dist/index.js (prebuilt copy is committed)
```

`main.py` needs no build. The plugin runs as root (`"flags": ["root"]`).
