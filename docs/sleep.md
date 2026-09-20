---
title: Fake sleep
nav_order: 6
---

# Fake sleep
The board cannot sleep, and Steam's **Sleep** entry hangs it (logind suspend → suspend-to-idle → never
wakes; only a power cut recovers). [`decky-bc250-sleep/`](https://github.com/lethevimlet/lethe-bc250/tree/main/decky-bc250-sleep/) is a second Decky
plugin that gives you the thing you actually wanted from Sleep, a quick pause-and-resume, and makes the
real Sleep harmless:

| Step | What happens |
|------|--------------|
| Sleep | the running game's whole process tree is frozen (`SIGSTOP`, nothing rendered, GPU drops to its idle clock), audio is muted, and the TV is put to sleep through gamescope (`drm_sleep_external_screen`, real DPMS off) |
| While asleep | the board stays fully powered at its idle package power (~32 W SoC, ~75 W at the wall): the **board LEDs stay lit** and only the TV is dark. The board's own fan curve barely slows down when the chip idles, so the plugin drops the **fan header to a low duty** itself (through the nct6687 driver; 15 % by default, about 770 rpm on the P12 Pro, adjustable in the plugin) and gives it back on wake, or at once if a die passes 65 °C, the fan stalls or does not respond (BIOS fan mode *Full Speed* ignores it; use *Default* or *Customize*). It is a pause, not a power saving |
| Wake | the first button press on any controller, keyboard or mouse turns the TV back on, thaws the game and unmutes; you are back exactly where you left off. **Do not use the case power button to wake:** the ESP32 sees the board as running, so a short press does nothing and a five-second hold hard-cuts the power ([Soft power control](power.md)) |

How to trigger it:

* **Steam's own Sleep entry** in the power menu (on by default, option *Use Steam's Sleep button*).
  The plugin replaces the `SuspendPC` call Steam makes and, as a safety net, masks the systemd sleep
  units so logind refuses any suspend request ("Unit suspend.target is masked, refusing operation")
  instead of hanging the board. Verified on this build: Steam's Sleep runs the fake sleep, nothing
  reaches logind, and Sleep stays listed in the power menu with the units masked. If Decky ever fails
  to inject on a boot, the menu's Sleep falls through to the masked units and simply does nothing,
  which is still harmless. **Leave the option on:** switching it off removes both the replacement and
  the mask, and Steam's Sleep becomes the real, board-hanging suspend again.
  Steam plays its own suspend animation and goes black before it calls the OS, and it only comes back
  on the resume event a real suspend would produce; on wake the plugin raises that event itself
  (`SuspendResumeStore.OnResumeFromSuspend`), so the home screen returns instead of staying black.
* The **Sleep now** button in the plugin's own panel (Quick Access → Decky → BC-250 Sleep).

Install like the tuning plugin (it is prebuilt):

```bash
cd lethe-bc250/decky-bc250-sleep
sudo mkdir -p ~/homebrew/plugins/bc250-sleep
sudo cp -r plugin.json package.json main.py LICENSE dist ~/homebrew/plugins/bc250-sleep/
sudo chown -R root:root ~/homebrew/plugins/bc250-sleep
sudo systemctl restart plugin_loader
```

Notes: it runs as root (it reads `/dev/input` and signals the game's processes). The game is found by
Steam's launcher shape (`reaper SteamLaunch AppId=N`), so only Steam-launched games are frozen; the
Steam UI itself keeps running, which is what makes the wake button work. Some games dislike being
frozen for very long (network sessions time out, anti-cheat may complain), same as with the store's
Pause Games plugin. The freezing is built in; Pause Games is not required. If Decky is ever restarted or the plugin reloaded while asleep, it wakes everything
first so nothing stays frozen. The HDMI audio sink disappears while the TV is asleep, so the unmute is
retried for a moment after wake.

---
