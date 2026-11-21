---
title: Phone keyboard and touchpad
nav_order: 6.5
---

# Phone keyboard and touchpad
[`decky-bc250-input/`](https://github.com/lethevimlet/lethe-bc250/tree/main/decky-bc250-input/) is a
third Decky plugin, **BC-250 Input**: your phone becomes a keyboard and a touchpad for the console. Type
or dictate and the console types it into the game through a virtual USB keyboard; swipe and tap and it
moves, clicks and scrolls a virtual USB mouse. It was made for MMO chat and menus in Gaming Mode (WoW's
channel commands are the default chips), where the on-screen keyboard is slow and a controller is a poor
mouse, but it works with anything that takes keyboard and mouse input.

## Connect a phone

1. Quick Access → **BC-250 Input**, make sure **Phone input** is on.
2. On a phone on the same network (or the same tailnet; the panel lists the Tailscale address too)
   open `http://<console-ip>:8251/`, or scan the QR code. There is no pairing code, so a bookmark or a
   home-screen icon keeps working across restarts; use the console's reserved address.
   The [ESP32 power page](power-page.md) also shows a **Phone keyboard & touchpad** button while the
   plugin is on.
3. **Close the Quick Access menu**: keys and clicks go to whatever has focus.

Once a phone is connected the panel says so (with its address) and hides the QR code; **Show QR code**
brings it back for a second phone.

## Chat tab

Type and send. The console presses Enter to open chat, types the text and presses Enter again. Channel
chips add a prefix (`/g `, `/p `, ...), and quick phrases send in one tap. **Edit** adds or removes chips and phrases; they are stored on the console.

## Touchpad tab

| Gesture | Does |
|---------|------|
| one finger | moves the pointer |
| tap | left click |
| two-finger tap | right click |
| three-finger tap | middle click |
| two fingers | scroll, up/down and sideways |
| tap, then touch and move | drag with the left button held; tap twice for a double click |
| **Left** / **•** / **Right** | mouse buttons, held while your finger is on them (hold Left and move on the pad to drag) |
| key row | Esc, Tab, arrows, Backspace, Enter; arrows and Backspace repeat while held |

**Settings** (top right on the touchpad tab) sets pointer speed, acceleration, scroll speed, natural
scrolling, tap to click, tap-then-drag and vibration. These are kept on the phone, so each phone has its
own. With tap-then-drag on, a tap clicks about a quarter second late (the page waits to see whether you
are starting a drag); turn it off for instant clicks.

The touchpad uses a WebSocket on the same port, so motion is sent once per screen frame without a
request per move. If the phone locks or the page goes to the background the link closes and the
console releases any button that was held.

## Panel settings

| Panel setting | What it does |
|---------------|--------------|
| Phone input | starts or stops the web server on port 8251 |
| Show / Hide QR code | the page's address; shown by itself until a phone has connected |
| Mouse control | lets the touchpad move the pointer and click (on by default); off removes the virtual mouse and the touchpad only shows a notice, the key row keeps working |
| Open chat with | `Enter` (default), `/` (only for messages that start with `/`), or none |
| Key delay | per-key delay, raise it if letters go missing (default 12 ms) |
| Wait after opening chat | pause between opening chat and typing (default 120 ms) |

## Security

There is no login and no pairing code: **anyone who can reach port 8251 can type and move the mouse on
the console**, the same as a USB keyboard and mouse plugged into it. That is meant for a home LAN and a
tailnet; never forward the port from the internet, and turn **Phone input** off on networks you do not
trust (a hotel, a LAN party).

What it does guard against is other web sites open on a phone in that network:

* API calls need a fixed request header that the page sends. A page from another site can only send a
  custom header after a CORS preflight, which the console never answers, so it cannot type.
* The touchpad's WebSocket is refused when the request comes from a page on another origin.
* Requests must name the console by an IP address, a plain host name, a `.local` or a `.ts.net` name,
  so a site cannot point its own domain at the console (DNS rebinding) and talk to it as itself.

What a phone can do: type text (one message a second, 255 characters, 8 KB bodies) followed by an
optional Enter, press the named keys of the key row (no modifiers, so no shortcuts like Alt+F4), and,
while **Mouse control** is on, move, click and scroll the mouse (about 800 commands a second at most).

## Limits

ASCII and a US layout only: phone punctuation is converted, accents are stripped, anything else is typed
as `?` and the page warns before sending. About 40 characters a second at the default delay. The pointer
moves relative to where it is (like any mouse), and the console's own pointer acceleration adds to the
page's; lower the speed or turn the page's acceleration off if it feels too fast.
