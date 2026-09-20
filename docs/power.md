---
title: Soft power control
nav_order: 8
has_children: true
---

# Soft power control
Turns a standard ATX PSU on from a button or over the network, and off again by itself when the
BC-250 halts. Replaces the green-wire jumper, so the PSU sits under a watt when the machine is off
instead of running continuously.

Firmware: [`esp32-power-control/bc250_power_opto.ino`](https://github.com/lethevimlet/lethe-bc250/tree/main/esp32-power-control/bc250_power_opto.ino).

<p align="center">
  <img src="images/schematic-esp32-pc817.svg" alt="Wiring: ESP32-C3 SuperMini drawn from the component side with USB-C up (left pads 5, 6, 7, 8, 9, 10, 20, 21; right pads 5V, G, 3.3, 4, 3, 2, 1, 0). 5V from ATX pin 9, G to the logic ground star node and ATX pin 15, pad 6 through R2 1k to TPMS1 pin 9, pad 7 through the push button to the star node, pad 4 through R3 220 to PC817 pin 1. The PC817 is drawn as the real four-pin package seen from the top, dot at pin 1: 1 anode, 2 cathode to the star node, 4 collector to ATX pin 16 PS_ON, 3 emitter to ATX pin 17 ground, with the isolation barrier through its middle" width="720">
</p>

## How it works

* The ESP32 runs from the PSU's **+5 V standby** rail, so it stays awake with the main rails dead.
  That is what lets it switch the PSU back on.
* A button press lights the optocoupler's LED. The phototransistor on the other side pulls **PS_ON**
  to ground, the PSU starts, and the BC-250 auto-boots.
* **TPMS1 pin 9** on the BC-250 carries 3.3 V only while the board is powered. The ESP32 watches it
  through one series resistor, using its internal pulldown. TPMS1 is the LPC bus brought out to
  pins, and pin 9's neighbours are LPC data lines that also idle at 3.3 V: a sense wire that is one
  pin off, or whose connector touches a neighbour, still makes the power logic work but knocks the
  Super I/O off the bus (no fan reading, see step 8 below and [If something misbehaves](power-page.md#if-something-misbehaves)).
* On software shutdown that 3.3 V disappears, the ESP32 releases PS_ON, and the PSU returns to standby.

The optocoupler keeps the two halves electrically separate: its input is an LED and its output a
phototransistor, with no conductive path between them. The ESP32's ground and the PSU's switching
ground never meet inside the circuit, so the only way PS_ON can reach ground is by the LED being lit.

Detailed parts for this circuit:

| Part | Qty | Notes |
|------|-----|-------|
| ESP32-C3 SuperMini | 1 | Recommended. Any ESP32 works; check your silkscreen pin names |
| PC817 optocoupler | 1 | 4N35 also works. A dot marks pin 1 |
| 220 Ω resistor | 1 | R3, sets the LED current. 150–470 Ω all fine |
| 1 kΩ resistor | 1 | R2, sense series. Not 100 k, see the design notes |
| Momentary push button | 1 | Normally open. This build: [Gebildet 16 mm stainless momentary button with white LED ring](https://www.amazon.es/dp/B07Z4PHKJX), 1NO1NC, comes with a plug lead. Only the NO contact pair is used; the LED ring is optional (it needs its own supply) |
| ATX 24-pin taps | 4 | This build: four **female Molex-style crimp terminals** (the same pin type the 24-pin uses) pushed onto the PSU plug's pins 9, 15, 16 and 17 from the wire-entry side and fixed with **hot glue**. A male + female passthrough extension works too |
| Hookup wire | — | 24–26 AWG |
| Heatshrink | — | Two short sleeves for the two capsules |

Everything here is low voltage, but you are working next to a PSU. Unplug it from the wall before
touching connectors, and never open the PSU case: the primary side holds a lethal charge long after
it is unplugged.

## Design notes

**R3** sets the LED current. At 3.3 V with a forward drop near 1.2 V, 220 Ω gives about 9.5 mA,
inside what a C3 pin will source and well past the PC817's threshold. 150–470 Ω all work.

**R2 must be 1 kΩ.** It limits fault current, it does not divide. GPIO 6's internal pulldown is only
specified as a range and can be as low as 10 kΩ, while the C3 needs about 2.5 V to read HIGH. At 1 kΩ
the pin sees at least 3.0 V even in the worst case; at 4.7 kΩ it falls to 2.2 V and at 100 kΩ to
1.0 V, either of which reads LOW with the board running and cuts the power shortly after every boot.
If your pin measures short of margin, a 10 kΩ resistor from GPIO 6 to ground makes the divider
deterministic at 2.94 V.

**Saturation.** PS_ON sources well under a milliamp, so the phototransistor saturates around
0.1–0.2 V, far below the 0.8 V the PSU needs to see. Step 7 confirms it on your own hardware.

**No shared ground.** If the two grounds are bridged anywhere in your wiring, the optocoupler still
switches, but you have given up the isolation that is the whole point of the part.

**One assumption worth testing.** TPMS1 pin 9 carrying 3.3 V only while the board is powered comes
from community BC-250 pinout documentation, not an official AMD source. Measure it on your own board
before relying on it. Fallbacks are an INA219 current sensor on the 12 V feed, or a systemd shutdown
hook that notifies the ESP32 over Wi-Fi. Pinouts vary between board revisions and clones.

---
