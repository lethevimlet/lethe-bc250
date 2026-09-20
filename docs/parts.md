---
title: Parts
nav_order: 2
---

# Parts
Everything used in this build, with the exact parts where it matters.

| Part | Notes |
|------|-------|
| **AMD BC-250 mining card** | Cyan Skillfish / Oberon APU: 6 of 8 Zen 2 cores and 24 of 40 RDNA2 CUs enabled from the factory, 16 GB GDDR6 shared between CPU and GPU. Stock ASRock BIOS P3.00. Needs an NVMe SSD (M.2 2280) for the OS. |
| **PSU: Metalfish 500 W, Flex ATX** | [AliExpress](https://es.aliexpress.com/item/1005009609601844.html). **Flex ATX** form factor, which is what the printed case's PSU compartment is sized for ([3D-printed case](case.md)); a standard ATX or SFX unit will not fit. Standard ATX pinout otherwise: 24-pin and an EPS12V 8-pin for the board cable. The BC-250 draws ~125–180 W at the extremes, all from the 12 V rail. |
| **Power cable: 8-pin EPS12V → 2× Micro-Fit 8-pin** — **REQUIRED, fire safety** | [moddiy ASRock BC-250 cable](https://www.moddiy.com/products/6837/Standard-8-Pin-EPS12V-to-2-x-MicroFit-8-Pin-Cable-for-ASRock-BC250.html). The board has two Micro-Fit 8-pin power inputs. Feeding it through a single PCIe 8-pin plug forces the whole 200–250 W peak draw down one 18 AWG lead set, which is beyond what that gauge is rated for and heats the cable and connector. This adapter takes the PSU's EPS12V (CPU) 8-pin, whose four 12 V conductors are rated for it, and splits it across both board inputs. Do not run the board on a PCIe cable alone. The plugs' tabs must be cut for them to seat; see [Fit the power cable and the auto power-on jumper](hardware.md#fit-the-power-cable-and-the-auto-power-on-jumper). |
| **Soft power control** | **ESP32-C3 SuperMini** (recommended: tiny, USB-C, runs happily from the PSU's 5 V standby rail; any ESP32 works), [16 mm momentary push button](https://www.amazon.es/dp/B07Z4PHKJX), PC817 optocoupler, resistors (220 Ω and 1 kΩ), hookup wire, solder, heatshrink tube. Full parts list and build in [Soft power control](power.md). |
| **Cooling** | 2× **ARCTIC P12 Pro PST** 120 mm fans on the double fan shroud (recommended over a single fan). Daisy-chain the second from the first one's PST pass-through, so both hang off the board's one 4-pin header and both get its PWM signal; that is what lets the Sleep plugin slow them down ([Fake sleep](sleep.md)). More airflow and static pressure than the plain P12, and they go down to ~500 rpm at low duty. **Thermalright TFX** thermal paste for the APU (a full tube's worth is not excessive: the die sits ~1 mm below the heatsink base, [Remove the heatsink lid](hardware.md#remove-the-heatsink-lid)) and new **2 mm thermal pads** for the GDDR6 and VRMs; the factory ones are dry. |
| **Case** (optional) | Access to a 3D printer for the case in [3D-printed case](case.md), plus 12× [M3 heat-set inserts](https://es.aliexpress.com/item/1005005920120561.html) and 12× [M3 × 6 mm screws](https://es.aliexpress.com/item/1005008082257314.html). |

Tools: a fine-tipped soldering iron, side cutters, wire strippers, a multimeter (not optional for the
power circuit), thin sharp scissors for the heatsink lid, a crimping tool for the Molex terminals, and a hot-glue gun for
fixing the taps.

---
