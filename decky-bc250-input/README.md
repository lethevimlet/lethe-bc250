# BC-250 Input (Decky plugin)

Phone keyboard and touchpad for the BC-250: type or dictate on the phone and the console types it into the
game through a virtual uinput keyboard; use the phone's screen as a touchpad for a virtual uinput mouse.
Documentation: [Phone keyboard and touchpad](https://lethevimlet.github.io/lethe-bc250/input.html).

Port **8251**, no pairing (LAN / tailnet only; API calls carry the fixed header `X-BC250-Input: 1`), devices `BC-250 Input Keyboard` and `BC-250 Input Mouse`.

| Path | What |
|------|------|
| `main.py` | Decky `Plugin` class: lifecycle and the callables the panel uses |
| `py_modules/bc250input/uinput.py` | virtual keyboard and mouse (pure Python, legacy uinput setup), `Typer` |
| `py_modules/bc250input/keymap.py` | text to keys (US layout), the named keys of the touchpad's key row |
| `py_modules/bc250input/server.py` | stdlib HTTP server: the page, `/api/state`, `/api/send`, `/api/list`, `/api/ws` |
| `py_modules/bc250input/ws.py`, `remote.py` | minimal WebSocket framing; one phone's touchpad session and its protocol |
| `py_modules/bc250input/core.py` | send logic, connected phones, live sessions |
| `py_modules/bc250input/security.py`, `settings.py`, `netinfo.py` | rate limit, settings file, addresses for the QR code |
| `py_modules/bc250input/web/index.html` | the phone page (Chat and Touchpad tabs) |
| `src/index.tsx` → `dist/index.js` | the Quick Access panel (QR code via `uqr`) |

The touchpad protocol (one WebSocket text frame can carry several commands, one per line):
`m dx dy` move, `s v h` scroll in hi-res units (120 = one notch, v > 0 up), `b l|r|m 1|0` button,
`c l|r|m` click, `k name` key tap, `p` keep-alive. The server answers with one
`{"hello": 1, "pointer": bool}` and releases every button the session pressed when it ends.

Build: `pnpm i && pnpm run build`, commit `dist/index.js` (not the lock file or `.map`).
Install: `./install.sh --only input-plugin` on the console, or copy `plugin.json package.json main.py
LICENSE dist py_modules` to `~/homebrew/plugins/bc250-input/` as root and `sudo systemctl restart plugin_loader`.

Licence: MIT (see `LICENSE`, which also carries the original author's notice and the BSD-3 notice of the
decky-plugin-template build scaffolding).
