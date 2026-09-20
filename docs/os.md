---
title: Install Bazzite
nav_order: 4
---

# Install Bazzite
The BC-250 has no power button header, so read the whole section before starting.

## Before the OS

* **BIOS.** This build runs the **stock ASRock P3.00 BIOS**. The 62fixolab images recommend a modded
  BIOS with "512 MB dynamic VRAM" and IOMMU disabled; the VRAM split is instead set from Linux with
  `bc250memcfg` ([Tuning: bc250-tune](tuning.md)), which works on the stock BIOS. **IOMMU must be disabled** in BIOS.
* **BIOS fan mode: not Full Speed.** The BIOS offers *Default*, *Full Speed* and *Customize* for the
  fan header. In **Full Speed** the embedded controller pins the header at 100 % and ignores every
  PWM write from Linux, so the Sleep plugin's *Quiet the fans* option ([Fake sleep](sleep.md)) can never slow the
  fans (its guard notices the rpm not dropping and gives the header back). Set **Default** or
  **Customize**; both leave the board's own curve in charge until the plugin takes the header for a
  fake sleep. The fans on this board run at a near-constant ~1570 rpm on the stock curve anyway,
  idle or loaded, so Full Speed buys nothing.
* **Auto power-on jumper.** Set the board's **AUTO_PWRON1** jumper to the auto-power-on position
  (pins 1–2). The BC-250 then boots by itself as soon as 12 V appears, which is what both the ESP32
  circuit ([Soft power control](power.md)) and a plain PS_ON switch rely on; there is no power button header to press otherwise.
* **Power switch.** Without the ESP32 circuit in [Soft power control](power.md), put a latching switch on the ATX PS_ON pin
  (green wire, pin 16, to ground) or jumper it permanently.
* **Boot media.** A USB stick with the Bazzite installer and a keyboard.
* **No sleep.** Neither suspend (S3 is not reverse-engineered yet; suspend-to-idle hangs) nor
  hibernation works on the BC-250; always shut down ([Things we learned](lessons.md)).

## Install stock Bazzite (deck variant)

1. Download the Bazzite ISO from <https://bazzite.gg> — choose **AMD**, and the **Steam Deck /
   handheld (Gaming Mode) desktop**. The deck variant boots straight into Steam's Gaming Mode.
2. Write it to a USB stick, boot the BC-250 from it and install to the NVMe.
3. First boot: finish the Steam setup, then switch to **Desktop Mode** (Power menu → Switch to Desktop)
   and open a terminal for the next steps.

## Rebase to the 62fixolab patched image (40 CU experimental variant)

The [62fixolab images](https://github.com/62fixolab/Latest-Bazzite-AMD-BC-250-Patched-Images) are
stock Bazzite plus the BC-250 kernel patches, the `cyan-skillfish-governor-smu` GPU governor, and, in
the `-40cu` variant, `umr`, `bc250-cu-live-manager` and the `ujust bc250-cu-*` helpers. Nothing is
enabled automatically at first boot (40 CU is off).

```bash
# deck variant with the 40 CU tooling (what this build uses)
rpm-ostree rebase ostree-image-signed:docker://ghcr.io/62fixolab/bazzite-bc250-patched-deck-40cu:latest
systemctl reboot
```

Other variants: replace `deck` with `gnome` or `kde`; drop `-40cu` for the plain image. Update channels
are `:latest` (stable, recommended), `-testing:latest` and `-unstable:latest`, e.g.
`…/bazzite-bc250-patched-deck-40cu-testing:latest`. Do not rebase between desktop environments.

## Verify

```bash
rpm-ostree status                                        # shows the 62fixolab image
systemctl status cyan-skillfish-governor-smu --no-pager  # active
for f in /sys/class/drm/card*/device/pp_dpm_sclk; do echo "$f"; cat "$f"; done
```

Updates from then on: `ujust update`. Rollback: `rpm-ostree rollback && systemctl reboot`.

---
