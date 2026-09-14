# bc250-tune

Part of [lethe-bc250](../README.md); this folder is self-contained and can be copied to the BC-250 on its own.

One script to switch every worthwhile tweak on an **AMD BC-250** running **Bazzite** (the
[62fixolab patched images](https://github.com/62fixolab/Latest-Bazzite-AMD-BC-250-Patched-Images)),
plus a **Decky Loader plugin** so the same switches live in the Steam Quick Access menu (the `…`
button / Xbox guide + A) and can be flipped without leaving Gaming Mode.

```
$ sudo bc250-tune status
bc250-tune 1.0.0 — config: /etc/bc250-tune/config   user: deck
  UMA        config 6144 MB   CMOS 6144     live VRAM 6144 MB
  GPU        config 500-1850  governor file 500-1850 MHz, now 500 MHz, 47°C
  CU         config 24        live 24/40 routed
  CORES      config 6         mask 0x77, 6 cores visible, 54°C
  HUD        config on        installed: on
  RES        config 1920x1080 session: 1920x1080
  POWER      SoC 36 W, TOTAL ~81 W
```

## What it can toggle

| Key | Values | What it does | Takes effect |
|-----|--------|--------------|--------------|
| `UMA_SIZE` | MB, ≥ 256, 16 MB steps (512 … 12288) | VRAM / system-RAM split of the 16 GB GDDR6. Written into battery-backed CMOS with [bc250memcfg](https://github.com/fanoush/bc250_memcfg) (built from source at install). Survives reinstalls and BIOS flashes; only a CMOS clear resets it. | next reboot |
| `GPU_MIN` / `GPU_MAX` | MHz | `[frequency-range]` of the image's `cyan-skillfish-governor-smu`. 500 MHz floor saves ~10 W at idle; 1850 is the image default ceiling; 2000 is hotter (~140 W). | live |
| `CU` | `24` / `40` | Routes all 20 WGPs (40 CUs) through the image's `bc250-cu-live-manager`, or back to the stock 24. Compute scales ~1.6x; games gain only a few % (they are fill-rate bound). ~+30 W. | live, re-applied at boot |
| `CORES` | `6` / `8` | Enables the two dormant Zen 2 cores by sending SMU message `0x98` (core-mask register `0x0115A870`), the technique from [GabriWar/bc250-core-cu-unlock](https://github.com/GabriWar/bc250-core-cu-unlock). Nothing is flashed. | **warm reboot** to appear; a **cold boot** (power removed) always reverts to 6 |
| `HUD` | `on` / `off` | Installs a one-line MangoHud layout as **Performance Overlay level 1** in Gaming Mode (and as the Desktop-Mode `MangoHud.conf`). | next game launch |
| `RES` | `WxH` or `native` | Gaming Mode (gamescope) output resolution via `~/.config/environment.d/`. Forces the Steam UI and games to that mode, e.g. 1080p on a 4K panel. | gaming session restart |

The HUD line looks like this (FPS from MangoHud, the rest from `bc250-tune hud-stats`):

```
· 47 FPS | GPU 48°C 1300MHz  CPU 54°C  SoC 47W  TOTAL ~92W  FAN 1587  24CU 6C
```

* `SoC` is the measured APU package power (CPU + GPU + memory controller, amdgpu `PPT`).
* `TOTAL ~` is `SoC + TOTAL_OFFSET_W` (default 45 W), a rough estimate of what the PCIe power
  cable / wall delivers (GDDR6 chips, VRM losses, fan, SSD). The board has no 12 V sensor; if you
  have a wall meter, adjust `TOTAL_OFFSET_W` in the config.
* `24CU 6C` shows how many compute units are routed and how many CPU cores are enabled.

## Install

Requirements: a 62fixolab Bazzite image (the `-40cu` variants ship `bc250-cu-live-manager` and `umr`;
on other variants the `CU` toggle is skipped), internet access once (to build `bc250memcfg`).

```bash
git clone https://github.com/lethevimlet/lethe-bc250.git
cd lethe-bc250/decky-bc250-tune
sudo ./bc250-tune install        # copies itself to /usr/local/bin, writes /etc/bc250-tune/config,
                                 # builds bc250memcfg, enables bc250-tune.service, applies the config
```

The defaults it applies are: UMA 6144 MB, GPU 500–1850 MHz, 24 CU, 6 cores, HUD on, 1920x1080.
Change them before the first apply with `sudo ./bc250-tune set KEY VALUE --no-apply` or afterwards
with any of the methods below.

### Steam Quick Access menu (Decky plugin)

1. Install Decky Loader: `ujust setup-decky install` (Bazzite) or the
   [official installer](https://github.com/SteamDeckHomebrew/decky-loader).
2. Copy the plugin (it is prebuilt, `decky-plugin/dist/index.js` is in this repo):
   ```bash
   sudo mkdir -p ~/homebrew/plugins/bc250-tune
   sudo cp -r decky-plugin/{plugin.json,package.json,main.py,LICENSE,dist} ~/homebrew/plugins/bc250-tune/
   sudo chown -R root:root ~/homebrew/plugins/bc250-tune
   sudo systemctl restart plugin_loader
   ```
3. Restart Steam (or the gaming session). Open the Quick Access menu → Decky (plug icon) →
   **BC-250 Tune**. Every control there runs `bc250-tune set …` as root through the plugin backend.

To rebuild the frontend after editing `decky-plugin/src/index.tsx`: `cd decky-plugin && pnpm i && pnpm run build`.

## Use

```bash
sudo bc250-tune menu                       # whiptail TUI (works over ssh or in a Desktop-Mode terminal)
sudo bc250-tune set cu 40                  # keys: uma gpu-min gpu-max cu cores hud res  (applies immediately)
sudo bc250-tune set cores 8 uma 8192       # several at once; prints what still needs a reboot
sudo bc250-tune apply                      # re-apply /etc/bc250-tune/config (idempotent)
sudo bc250-tune status [--json]            # everything, incl. pending reboot / session restart
sudo bc250-tune reboot                     # WARM reboot (needed for 8 cores; keeps the SMU mask)
sudo bc250-tune restart-session            # restart gamescope/Steam (applies RES; closes games)
bc250-tune hud-stats                       # the HUD line (what MangoHud's exec= calls)
sudo bc250-tune uninstall
```

`/etc/bc250-tune/config` is a plain `KEY=VALUE` file; edit it and run `apply` if you prefer.

## How the pieces persist

| Piece | Where it lives | Survives |
|-------|----------------|----------|
| UMA split | CMOS (RTC RAM) | reboot, reinstall, BIOS flash (not a CMOS clear) |
| GPU range | `/etc/cyan-skillfish-governor-smu/config.toml` (original saved as `*.bc250-tune.orig`) | reboot, image update |
| CU routing | SPI registers (volatile) → `bc250-tune.service` re-applies at boot. The image's own `bc250-cu-live-manager.service` is disabled to avoid two owners of the table. | reboot via the service |
| Core mask | SMU register (volatile) → the service re-arms it after a cold boot; cores then appear on the *next* reboot. The service never reboots by itself. | warm reboot |
| HUD | `~/.config/MangoHud/presets.conf` + `MangoHud.conf` (marked `# bc250-tune managed`; unmarked files are left alone) | reboot, image update |
| Resolution | `~/.config/environment.d/bc250-tune-gamescope.conf` | reboot, image update |

`bc250-tune.service` runs `bc250-tune apply --boot` after `multi-user.target` on every boot.

## How it fits with what the 62fixolab image already ships

The `-40cu` images ship `cyan-skillfish-governor-smu`, `umr`, `bc250-cu-live-manager`, the
`ujust bc250-cu-*` recipes and `cyan-skillfish-performance-mode`. Nothing is enabled at first boot
(40 CU is off, no Decky, no HUD preset, no UMA or 8-core tooling). bc250-tune builds on those tools
instead of replacing them:

| Image piece | Relationship |
|-------------|--------------|
| `cyan-skillfish-governor-smu.service` | kept as is; bc250-tune only edits `[frequency-range]` in its config and restarts it |
| `bc250-cu-live-manager`, `umr` | bc250-tune calls them for `CU=40`/`24`; `ujust bc250-cu-status`/`-menu` keep working |
| `ujust bc250-cu-save-boot` (creates `bc250-cu-live-manager.service`) | do not use together with `CU=40`: two owners of the same table. bc250-tune disables that unit the next time it applies and re-routes the WGPs itself at boot |
| `ujust bc250-cu-sweet-spot` / `bc250-governor-profile` | writes `max = 1500` into the same governor config; the next `bc250-tune apply` puts `GPU_MAX` back. Use one or the other |
| `cyan-skillfish-performance-mode` | per-game wrapper (`cyan-skillfish-performance-mode %command%` in a game's launch options) that pins the governor over D-Bus while the game runs. Independent of bc250-tune; useful to test whether a game is GPU-bound |

## Things worth knowing before flipping switches

* **8 cores**: upstream validated the unlock on boards whose stock mask is `0x77`. Some boards report
  a different 6-core mask (e.g. `0x7E`); the script warns but proceeds, because a cold boot always
  restores the factory state. After unlocking, the 4 new threads have no ACPI C-states (slightly higher
  idle power) and **every kernel GPU-clock readout becomes garbage** (`pp_dpm_sclk`, hwmon `freq1`,
  `amdgpu_pm_info` all read the same broken SMU metrics table; values wander between ~10 and ~1200 MHz).
  The GPU itself clocks normally and the governor still works. While 8 cores are enabled, bc250-tune
  therefore *estimates* the clock from the GFX voltage along the governor's `[[safe-points]` curve
  and shows it with a `~` prefix (HUD `~1415MHz`, plugin `~1415 MHz`, JSON `cur_mhz_estimated`).
  Six cores already reach ~90 °C under all-core load on a stock cooler, so watch temperatures.
* **Decky and `LD_LIBRARY_PATH`**: Decky Loader is a PyInstaller bundle and exports its own library
  path to child processes, which makes `systemctl` fail with "version OPENSSL_x not found". The script
  scrubs its environment on start and the plugin backend spawns it with a clean one; keep that if you
  fork either part.
* **40 CU + 8 cores + 2000 MHz** all at once pushes the package toward 180 W. Make sure the PSU and
  the PCIe cable are up to it. In games the combined gain is typically 5–12 %.
* **Sleep/suspend does not work on the BC-250** (no S3, s2idle hangs). This tool does not touch it;
  just never use Sleep.
* MangoHud layout rule (0.8.x, `legacy_layout=0`): every option listed in a config becomes a column
  with a `|` separator, even `something=0`. That is why the shipped preset lists only `fps` and `exec`.

## Repository layout

```
bc250-tune              the script (bash, single file)
mangohud/               the HUD files exactly as bc250-tune installs them, for reference
decky-plugin/           Decky Loader plugin (src/index.tsx, main.py, prebuilt dist/index.js, screenshot.png)
```

## Credits

* [fanoush/bc250_memcfg](https://github.com/fanoush/bc250_memcfg) — CMOS memory configuration tool (built at install).
* [GabriWar/bc250-core-cu-unlock](https://github.com/GabriWar/bc250-core-cu-unlock) (MIT) — SMU core-unlock sequence.
* [62fixolab](https://github.com/62fixolab/Latest-Bazzite-AMD-BC-250-Patched-Images), `bc250-cu-live-manager`
  and the `bc250-40cu-unlock` research it vendors — CU routing and the GPU governor.
* [Decky Loader](https://github.com/SteamDeckHomebrew/decky-loader) — plugin framework.

MIT licensed. Use at your own risk: this pokes SMU and CMOS registers on a board that was never meant
to be a PC.
