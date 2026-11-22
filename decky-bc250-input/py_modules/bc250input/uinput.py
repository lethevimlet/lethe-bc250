"""Minimal pure-Python virtual keyboard and mouse over /dev/uinput (no python-evdev needed).

Uses the legacy uinput setup path (write struct uinput_user_dev, then
UI_DEV_CREATE), which every kernel that has uinput supports.
"""

from __future__ import annotations

import os
import struct
import threading
import time
from typing import Callable, Iterable, List, Optional

from .keymap import ALL_KEYCODES, KEY_BACKSPACE, KEY_ENTER, KEY_LEFTSHIFT, KEY_SLASH, Stroke

EV_SYN = 0x00
EV_KEY = 0x01
EV_REL = 0x02
SYN_REPORT = 0
REL_X, REL_Y, REL_HWHEEL, REL_WHEEL = 0x00, 0x01, 0x06, 0x08
REL_WHEEL_HI_RES, REL_HWHEEL_HI_RES = 0x0B, 0x0C
BTN_LEFT, BTN_RIGHT, BTN_MIDDLE = 0x110, 0x111, 0x112
BUTTONS = {"l": BTN_LEFT, "r": BTN_RIGHT, "m": BTN_MIDDLE}
BUS_USB = 0x03

# ioctl numbers from linux/uinput.h ('U' = 0x55)
UI_DEV_CREATE = 0x5501
UI_DEV_DESTROY = 0x5502
UI_SET_EVBIT = 0x40045564
UI_SET_KEYBIT = 0x40045565
UI_SET_RELBIT = 0x40045566

UINPUT_MAX_NAME_SIZE = 80
ABS_CNT = 64
# struct input_event on 64-bit Linux: struct timeval (2 x long), u16 type, u16 code, s32 value
INPUT_EVENT = struct.Struct("@llHHi")
USER_DEV = struct.Struct("@%dsHHHHI%di" % (UINPUT_MAX_NAME_SIZE, ABS_CNT * 4))

DEVICE_NAME = b"BC-250 Input Keyboard"
MOUSE_NAME = b"BC-250 Input Mouse"
VENDOR = 0x1209  # pid.codes
KEYBOARD_PRODUCT, MOUSE_PRODUCT = 0xC4A8, 0xC4A9


def _make_ioctl() -> Callable[[int, int, int], None]:
    try:
        import fcntl

        def _ioctl(fd: int, req: int, arg: int) -> None:
            fcntl.ioctl(fd, req, arg)

        return _ioctl
    except Exception:  # pragma: no cover - fallback for frozen runtimes without fcntl
        import ctypes

        libc = ctypes.CDLL(None, use_errno=True)

        def _ioctl(fd: int, req: int, arg: int) -> None:
            if libc.ioctl(fd, ctypes.c_ulong(req), ctypes.c_int(arg)) != 0:
                err = ctypes.get_errno()
                raise OSError(err, os.strerror(err))

        return _ioctl


def _create(path: str, name: bytes, product: int, bits: dict) -> int:
    """Open /dev/uinput and create a device. bits: {EV_KEY: [codes], EV_REL: [codes]}."""
    ioctl = _make_ioctl()
    setbit = {EV_KEY: UI_SET_KEYBIT, EV_REL: UI_SET_RELBIT}
    fd = os.open(path, os.O_WRONLY | os.O_NONBLOCK)
    try:
        ioctl(fd, UI_SET_EVBIT, EV_SYN)
        for ev, codes in bits.items():
            ioctl(fd, UI_SET_EVBIT, ev)
            for code in codes:
                ioctl(fd, setbit[ev], code)
        os.write(fd, USER_DEV.pack(name, BUS_USB, VENDOR, product, 1, 0, *([0] * (ABS_CNT * 4))))
        ioctl(fd, UI_DEV_CREATE, 0)
    except Exception:
        os.close(fd)
        raise
    return fd


def _destroy(fd: int) -> None:
    try:
        _make_ioctl()(fd, UI_DEV_DESTROY, 0)
    except Exception:
        pass
    os.close(fd)


def _event(etype: int, code: int, value: int) -> bytes:
    now = time.time()
    sec = int(now)
    return INPUT_EVENT.pack(sec, int((now - sec) * 1e6), etype, code, value)


class UInputKeyboard:
    """A real virtual keyboard. Requires write access to /dev/uinput (root)."""

    def __init__(self, path: str = "/dev/uinput", keycodes: Iterable[int] = ALL_KEYCODES,
                 settle_s: float = 0.5) -> None:
        self.path = path
        self.keycodes = list(keycodes)
        self.settle_s = settle_s
        self.fd: Optional[int] = None

    def open(self) -> None:
        if self.fd is not None:
            return
        self.fd = _create(self.path, DEVICE_NAME, KEYBOARD_PRODUCT, {EV_KEY: self.keycodes})
        # Give udev / the compositor time to notice the new device.
        time.sleep(self.settle_s)

    def key(self, code: int, down: bool) -> None:
        if self.fd is None:
            raise RuntimeError("keyboard not open")
        os.write(self.fd, _event(EV_KEY, code, 1 if down else 0) + _event(EV_SYN, SYN_REPORT, 0))

    def close(self) -> None:
        fd, self.fd = self.fd, None
        if fd is not None:
            _destroy(fd)


class UInputMouse:
    """Virtual relative mouse: motion, three buttons, vertical and horizontal wheel.

    Scroll amounts are in hi-res units (120 = one wheel notch); the classic wheel events are
    emitted whenever a whole notch has accumulated, as real hi-res mice do."""

    def __init__(self, path: str = "/dev/uinput", settle_s: float = 0.3) -> None:
        self.path = path
        self.settle_s = settle_s
        self.fd: Optional[int] = None
        self.pressed: set = set()
        self._acc = {REL_WHEEL: 0, REL_HWHEEL: 0}
        self._lock = threading.Lock()

    def open(self) -> None:
        if self.fd is not None:
            return
        self.fd = _create(self.path, MOUSE_NAME, MOUSE_PRODUCT, {
            EV_KEY: list(BUTTONS.values()),
            EV_REL: [REL_X, REL_Y, REL_WHEEL, REL_HWHEEL, REL_WHEEL_HI_RES, REL_HWHEEL_HI_RES],
        })
        time.sleep(self.settle_s)

    def _write(self, events: List[bytes]) -> None:
        if self.fd is None:
            raise RuntimeError("mouse not open")
        events.append(_event(EV_SYN, SYN_REPORT, 0))
        os.write(self.fd, b"".join(events))

    def move(self, dx: int, dy: int) -> None:
        ev = []
        if dx:
            ev.append(_event(EV_REL, REL_X, dx))
        if dy:
            ev.append(_event(EV_REL, REL_Y, dy))
        if ev:
            with self._lock:
                self._write(ev)

    def scroll(self, v: int, h: int) -> None:
        """v > 0 scrolls up (away from the user), h > 0 scrolls right."""
        ev = []
        with self._lock:
            for amount, hi, lo in ((v, REL_WHEEL_HI_RES, REL_WHEEL), (h, REL_HWHEEL_HI_RES, REL_HWHEEL)):
                if not amount:
                    continue
                ev.append(_event(EV_REL, hi, amount))
                acc = self._acc[lo] + amount
                notches = int(acc / 120)
                if notches:
                    ev.append(_event(EV_REL, lo, notches))
                    acc -= notches * 120
                self._acc[lo] = acc
            if ev:
                self._write(ev)

    def button(self, code: int, down: bool) -> None:
        with self._lock:
            if down == (code in self.pressed):
                return
            self._write([_event(EV_KEY, code, 1 if down else 0)])
            (self.pressed.add if down else self.pressed.discard)(code)

    def release_all(self) -> None:
        for code in list(self.pressed):
            try:
                self.button(code, False)
            except Exception:
                self.pressed.discard(code)

    def close(self) -> None:
        self.release_all()
        fd, self.fd = self.fd, None
        if fd is not None:
            _destroy(fd)


class RecordingMouse:
    """Test double for UInputMouse: records ('move'|'scroll'|'button', a, b)."""

    def __init__(self) -> None:
        self.events: List[tuple] = []
        self.pressed: set = set()
        self.fd = 1

    def open(self) -> None:
        self.fd = 1

    def move(self, dx, dy):
        self.events.append(("move", dx, dy))

    def scroll(self, v, h):
        self.events.append(("scroll", v, h))

    def button(self, code, down):
        if down != (code in self.pressed):
            self.events.append(("button", code, down))
            (self.pressed.add if down else self.pressed.discard)(code)

    def release_all(self):
        for c in list(self.pressed):
            self.button(c, False)

    def close(self):
        self.release_all()
        self.fd = None


class RecordingKeyboard:
    """Test double: records (code, down) tuples instead of touching /dev/uinput."""

    def __init__(self) -> None:
        self.events: List[tuple] = []
        self.opened = False

    def open(self) -> None:
        self.opened = True

    def key(self, code: int, down: bool) -> None:
        self.events.append((code, down))

    def close(self) -> None:
        self.opened = False


OPEN_KEYS = ("enter", "slash", "none")


class Typer:
    """Turns a send request into key presses on a keyboard object (real or fake)."""

    def __init__(self, keyboard, sleep: Callable[[float], None] = time.sleep) -> None:
        self.kb = keyboard
        self.sleep = sleep
        self.lock = threading.Lock()

    def _tap(self, code: int, shift: bool, delay_s: float) -> None:
        if shift:
            self.kb.key(KEY_LEFTSHIFT, True)
            self.sleep(delay_s / 2)
        self.kb.key(code, True)
        self.sleep(delay_s / 2)
        self.kb.key(code, False)
        if shift:
            self.sleep(delay_s / 2)
            self.kb.key(KEY_LEFTSHIFT, False)
        self.sleep(delay_s)

    def tap_key(self, code: int, delay_ms: float = 12) -> bool:
        """One key press from the touchpad screen. False if a message is being typed right now."""
        if not self.lock.acquire(timeout=0.05):
            return False
        try:
            self.kb.open()
            self._tap(code, False, max(0.0, delay_ms) / 1000.0)
            return True
        finally:
            self.lock.release()

    def type_live(self, backspaces: int, strokes: List[Stroke], delay_ms: float = 12) -> None:
        """Live typing from the phone: delete what the phone deleted, then type what it added.
        No chat opening and no Enter; waits for a message being typed so nothing is lost."""
        delay = max(0.0, delay_ms) / 1000.0
        with self.lock:
            self.kb.open()
            try:
                for _ in range(backspaces):
                    self._tap(KEY_BACKSPACE, False, delay)
                for s in strokes:
                    self._tap(s.code, s.shift, delay)
            except Exception:
                try:
                    self.kb.key(KEY_LEFTSHIFT, False)
                except Exception:
                    pass
                raise

    def type_message(self, strokes: List[Stroke], *, open_key: str = "enter", press_enter: bool = True,
                     key_delay_ms: float = 12, open_delay_ms: float = 120) -> None:
        """open_key: 'enter' presses Enter first; 'slash' presses '/' first and expects
        strokes to already exclude that leading slash; 'none' types straight away."""
        delay = max(0.0, key_delay_ms) / 1000.0
        with self.lock:
            self.kb.open()
            try:
                if open_key == "enter":
                    self._tap(KEY_ENTER, False, delay)
                    self.sleep(open_delay_ms / 1000.0)
                elif open_key == "slash":
                    self._tap(KEY_SLASH, False, delay)
                    self.sleep(open_delay_ms / 1000.0)
                for s in strokes:
                    self._tap(s.code, s.shift, delay)
                if press_enter:
                    self.sleep(delay)
                    self._tap(KEY_ENTER, False, delay)
            except Exception:
                # Never leave shift stuck down if something goes wrong mid-message.
                try:
                    self.kb.key(KEY_LEFTSHIFT, False)
                except Exception:
                    pass
                raise
