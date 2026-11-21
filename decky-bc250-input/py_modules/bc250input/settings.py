"""Persistent JSON settings with validation."""

from __future__ import annotations

import copy
import json
import os
import threading
from typing import Any, Dict

MAX_LIST = 30
MAX_ITEM_LEN = 255
MAX_LABEL_LEN = 16

DEFAULTS: Dict[str, Any] = {
    "enabled": True,
    "port": 8251,
    "open_key": "enter",       # enter | slash | none
    "key_delay_ms": 12,
    "open_delay_ms": 120,
    "pointer_enabled": True,    # the phone's touchpad may move the mouse and click
    # WoW Classic-era defaults. prefix is inserted at the start of the textarea.
    "chips": [
        {"label": "Say", "prefix": "/s "},
        {"label": "Party", "prefix": "/p "},
        {"label": "Guild", "prefix": "/g "},
        {"label": "Raid", "prefix": "/raid "},
        {"label": "Whisper", "prefix": "/w "},
        {"label": "Reply", "prefix": "/r "},
        {"label": "Trade", "prefix": "/2 "},
        {"label": "General", "prefix": "/1 "},
        {"label": "Yell", "prefix": "/y "},
    ],
    "phrases": [],
}

LIMITS = {
    "key_delay_ms": (2, 100),
    "open_delay_ms": (0, 1000),
    "port": (1024, 65535),
}


def _clamp_int(value: Any, lo: int, hi: int) -> int:
    v = int(value)
    return max(lo, min(hi, v))


def validate(key: str, value: Any) -> Any:
    if key in ("enabled", "pointer_enabled"):
        return bool(value)
    if key in LIMITS:
        lo, hi = LIMITS[key]
        return _clamp_int(value, lo, hi)
    if key == "open_key":
        if value not in ("enter", "slash", "none"):
            raise ValueError("open_key must be enter, slash or none")
        return value
    if key == "phrases":
        if not isinstance(value, list):
            raise ValueError("phrases must be a list")
        out = []
        for p in value[:MAX_LIST]:
            p = str(p).strip()[:MAX_ITEM_LEN]
            if p and p not in out:
                out.append(p)
        return out
    if key == "chips":
        if not isinstance(value, list):
            raise ValueError("chips must be a list")
        out = []
        for c in value[:MAX_LIST]:
            if not isinstance(c, dict):
                continue
            prefix = str(c.get("prefix", ""))[:40]
            label = str(c.get("label", "")).strip()[:MAX_LABEL_LEN] or prefix.strip()
            if prefix.strip():
                out.append({"label": label, "prefix": prefix})
        return out
    raise KeyError(key)


class Settings:
    def __init__(self, path: str) -> None:
        self.path = path
        self._lock = threading.Lock()
        self.data: Dict[str, Any] = copy.deepcopy(DEFAULTS)
        self.load()

    def load(self) -> None:
        try:
            with open(self.path, "r", encoding="utf-8") as f:
                raw = json.load(f)
        except (OSError, ValueError):
            return
        if not isinstance(raw, dict):
            return
        for k, v in raw.items():
            if k in DEFAULTS:
                try:
                    self.data[k] = validate(k, v)
                except (ValueError, TypeError, KeyError):
                    pass

    def save(self) -> None:
        d = os.path.dirname(self.path)
        if d:
            os.makedirs(d, exist_ok=True)
        tmp = self.path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self.data, f, indent=2)
        os.replace(tmp, self.path)

    def get(self, key: str) -> Any:
        return copy.deepcopy(self.data[key])

    def set(self, key: str, value: Any) -> Any:
        if key not in DEFAULTS:
            raise KeyError(key)
        v = validate(key, value)
        with self._lock:
            self.data[key] = v
            self.save()
        return v

    def snapshot(self) -> Dict[str, Any]:
        return copy.deepcopy(self.data)
