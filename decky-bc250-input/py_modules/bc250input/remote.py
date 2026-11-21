"""One phone's live connection: touchpad motion, buttons, wheel and single key presses.

Protocol (text frames, commands separated by newlines, fields by spaces):
  m dx dy     move the pointer (pixels, clamped)
  s v h       scroll, hi-res units (120 = one notch); v > 0 up, h > 0 right
  b l|r|m 1|0 button down / up
  c l|r|m     click
  k name      tap a key from keymap.NAMED_KEYS
  p           keep-alive
Pointer commands are ignored unless mouse control is on. Every button is released when the
connection ends, however it ends.
"""

from __future__ import annotations

import time
from typing import Callable, List

from .keymap import NAMED_KEYS
from .uinput import BUTTONS

MAX_MOVE = 2000
MAX_SCROLL = 2400
MAX_COMMANDS_PER_S = 800


def _int(s: str, lim: int) -> int:
    return max(-lim, min(lim, int(s)))


class RemoteSession:
    def __init__(self, core, clock: Callable[[], float] = time.monotonic) -> None:
        self.core = core
        self.clock = clock
        self.pointer = core.pointer_allowed()
        self.mouse = core.mouse
        self._window = clock()
        self._count = 0
        self.dropped = 0
        self.buttons: set = set()

    def _allow(self) -> bool:
        now = self.clock()
        if now - self._window >= 1.0:
            self._window, self._count = now, 0
        self._count += 1
        return self._count <= MAX_COMMANDS_PER_S

    def handle(self, text: str) -> None:
        for line in text.split("\n"):
            parts = line.split()
            if not parts:
                continue
            if not self._allow():
                self.dropped += 1
                continue
            try:
                self._one(parts)
            except (ValueError, IndexError, KeyError):
                self.dropped += 1  # malformed command: ignore, keep the connection

    def _one(self, p: List[str]) -> None:
        op = p[0]
        if op == "p":
            return
        if op == "k":
            code = NAMED_KEYS[p[1]]
            self.core.typer.tap_key(code, self.core.settings.get("key_delay_ms"))
            return
        if not self.pointer:
            return
        if op == "m":
            self.mouse.move(_int(p[1], MAX_MOVE), _int(p[2], MAX_MOVE))
        elif op == "s":
            self.mouse.scroll(_int(p[1], MAX_SCROLL), _int(p[2], MAX_SCROLL) if len(p) > 2 else 0)
        elif op == "b":
            code = BUTTONS[p[1]]
            down = p[2] == "1"
            self.mouse.button(code, down)
            (self.buttons.add if down else self.buttons.discard)(code)
        elif op == "c":
            code = BUTTONS[p[1]]
            self.mouse.button(code, True)
            self.mouse.button(code, False)
            self.buttons.discard(code)

    def close(self) -> None:
        if not self.pointer:
            return
        for code in list(self.buttons):
            try:
                self.mouse.button(code, False)
            except Exception:
                pass
        self.buttons.clear()
