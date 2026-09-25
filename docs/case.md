---
title: 3D-printed case
nav_order: 10
---

# 3D-printed case
The [`printed-case/`](https://github.com/lethevimlet/lethe-bc250/tree/main/printed-case/) folder holds the STL files this build prints: the **"ASRock BC-250 case
Steam Machine" by MrLarva** ([Thingiverse](https://www.thingiverse.com/thing:7304454), also on
[Printables](https://www.printables.com/model/1618501-asrock-bc-250-case-steam-machine-by-mrlarva)
and [MakerWorld](https://makerworld.com/en/models/2453965-asrock-bc-250-case-steam-machine-by-mrlarva)),
licensed **CC BY-SA** (see [`printed-case/LICENSE.txt`](https://github.com/lethevimlet/lethe-bc250/tree/main/printed-case/LICENSE.txt)). It is a remix of
[Arthrimus's console-style BC-250 case](https://www.thingiverse.com/thing:7165679) with a larger
compartment for a Flex-ATX power supply (the Metalfish 500 W in [Parts](parts.md) fits), ventilation holes under it, a round front button, and a
bracket for 120 mm fans. Not our design; all credit to MrLarva and Arthrimus.

| File | Size (mm) | What it is |
|------|-----------|------------|
| `Case_Back.stl` | 183 × 96 × 240 | Main body: board tray, PSU compartment |
| `Case_Front.stl` | 183 × 96 × 97 | Front section of the body |
| `Front_Panel_with_USB.stl` | 167 × 62 × 10 | Front panel with the round power-button hole and a USB cut-out |
| `Left_Big_Panel.stl`, `Right_Big_Panel.stl` | 225 × 158 × 4 | Large side panels |
| `Left_Small_Panel.stl`, `Right_Small_Panel.stl` | 84 × 158 × 4 | Small side panels |
| `Single_120mm_Fan_Shroud.stl` | 230 × 133 × 11 | Shroud for one 120 mm fan |
| `Double_120mm_Fan_Shroud.stl` | 130 × 11 × 249 | Shroud for two 120 mm fans |
| `PEG.stl` | 39 × 10 × 6 | Peg that locks the panels |

Hardware used in this build:

| Part | Qty | Spec | Link |
|------|-----|------|------|
| Heat-set threaded inserts | 12 | M3 × 3.5 × 4.6 × 6 mm (M3 thread, 4.6 mm outer diameter, 6 mm long) | [AliExpress](https://es.aliexpress.com/item/1005005920120561.html) |
| Screws | 12 | M3 × 6 mm | [AliExpress](https://es.aliexpress.com/item/1005008082257314.html) |

**Fans.** Print the **double** shroud and fit two **ARCTIC P12 Pro PST** 120 mm fans blowing onto the
heatsink fins (lid removed, [Remove the heatsink lid](hardware.md#remove-the-heatsink-lid)). One fan works but runs hotter and louder; two at low rpm are quieter
for the same airflow. The BC-250 has a single 4-pin fan header: plug the first fan into it and chain
the second from the first one's PST pass-through connector, so both receive the header's PWM signal.
Avoid a Y-splitter that carries only power to one or both plugs: a fan that never sees PWM runs flat
out and the Sleep plugin cannot slow it ([Fake sleep](sleep.md), and set the BIOS fan mode to *Default* or *Customize*,
[Before the OS](os.md#before-the-os)). The board's curve drives them while awake; the speed shows up as `FAN` in the HUD and on the
ESP32 page.

The momentary button from [Soft power control](power.md) goes in the front panel's round hole. TODO: print settings
(material, orientation, supports).

---
