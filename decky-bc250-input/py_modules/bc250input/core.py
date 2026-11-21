"""BC-250 Input application logic, independent of HTTP and of Decky."""

from __future__ import annotations

import threading
import time
from typing import Any, Callable, Dict, List, Optional, Tuple

from .keymap import normalize_text, text_to_strokes
import ipaddress

from .security import RateLimiter
from .settings import MAX_ITEM_LEN, MAX_LIST, Settings
from .uinput import Typer

MAX_CHARS = 255  # WoW chat message limit
HISTORY_SIZE = 10

Result = Tuple[int, Dict[str, Any]]


def host_ok(host: Optional[str]) -> bool:
    """True for an IP address, a single-label name, or a .local / .ts.net name (with any port)."""
    if not host:
        return False
    name = host.strip().lower()
    if name.startswith("["):
        name = name[1:name.find("]")] if "]" in name else ""
    elif name.count(":") == 1:
        name = name.split(":")[0]
    try:
        ipaddress.ip_address(name)
        return True
    except ValueError:
        pass
    name = name.rstrip(".")
    return bool(name) and ("." not in name or name.endswith((".local", ".ts.net")))


class InputCore:
    def __init__(self, settings: Settings, keyboard, mouse=None, *, sleep=time.sleep, clock=time.monotonic,
                 min_interval: float = 1.0) -> None:
        self.settings = settings
        self.keyboard = keyboard
        self.mouse = mouse
        self.typer = Typer(keyboard, sleep=sleep)
        self.limiter = RateLimiter(min_interval, clock=clock)
        self.history: List[str] = []
        self.last_send: Optional[Dict[str, Any]] = None
        self.sends = 0
        self._busy = threading.Lock()
        self._state_lock = threading.Lock()
        self.seen: Dict[str, float] = {}         # ip -> last authorized request (wall clock)
        self.sessions: Dict[int, Dict[str, Any]] = {}  # live touchpad connections
        self._next_session = 1

    # ---- auth -------------------------------------------------------------
    def authorize(self, host: Optional[str], ip: str) -> Optional[Result]:
        """No pairing: anyone who can reach the port (LAN or tailnet) may use it. The Host check stops
        DNS rebinding, where a web page on some other site points its own name at the console."""
        if not host_ok(host):
            return 403, {"ok": False, "error": "Open the page by the console's address"}
        with self._state_lock:
            self.seen[ip] = time.time()
        return None

    # ---- live connections -------------------------------------------------
    def pointer_allowed(self) -> bool:
        return bool(self.settings.get("pointer_enabled")) and self.mouse is not None \
            and getattr(self.mouse, "fd", None) is not None

    def open_session(self, ip: str, closer: Callable[[], None]) -> int:
        with self._state_lock:
            sid = self._next_session
            self._next_session += 1
            self.sessions[sid] = {"ip": ip, "since": time.time(), "close": closer}
            return sid

    def close_session(self, sid: int) -> None:
        with self._state_lock:
            self.sessions.pop(sid, None)

    def close_sessions(self) -> None:
        with self._state_lock:
            closers = [s["close"] for s in self.sessions.values()]
        for c in closers:
            try:
                c()
            except Exception:
                pass

    def clients(self) -> List[Dict[str, Any]]:
        """Phones seen in the last two minutes, live touchpad connections first."""
        now = time.time()
        with self._state_lock:
            live = {s["ip"] for s in self.sessions.values()}
            for ip in [ip for ip, t in self.seen.items() if now - t > 120 and ip not in live]:
                del self.seen[ip]
            out = [{"ip": ip, "ago": round(now - t), "live": ip in live} for ip, t in self.seen.items()]
        out.sort(key=lambda c: (not c["live"], c["ago"]))
        return out

    # ---- state ------------------------------------------------------------
    def public_state(self) -> Dict[str, Any]:
        s = self.settings.snapshot()
        return {
            "ok": True,
            "chips": s["chips"],
            "phrases": s["phrases"],
            "history": list(self.history),
            "maxChars": MAX_CHARS,
            "openKey": s["open_key"],
            "pointer": self.pointer_allowed(),
            "lastSend": self.last_send,
        }

    # ---- sending ----------------------------------------------------------
    def send(self, text: Any, press_enter: Any = True) -> Result:
        if not isinstance(text, str):
            return 400, {"ok": False, "error": "text must be a string"}
        ascii_text, replaced = normalize_text(text)
        ascii_text = ascii_text.strip()
        if not ascii_text:
            return 400, {"ok": False, "error": "Nothing to send"}
        if len(ascii_text) > MAX_CHARS:
            return 413, {"ok": False, "error": f"Too long ({len(ascii_text)}/{MAX_CHARS})"}
        if not self._busy.acquire(blocking=False):
            return 409, {"ok": False, "error": "Still typing the previous message"}
        try:
            if not self.limiter.try_acquire():
                wait = self.limiter.retry_after()
                return 429, {"ok": False, "error": "Slow down (1 message per second)",
                             "retryAfter": round(wait, 2)}
            s = self.settings.snapshot()
            open_key = s["open_key"]
            body = ascii_text
            if open_key == "slash":
                if body.startswith("/"):
                    body = body[1:]
                else:
                    open_key = "enter"  # plain text: '/' would corrupt it, so open with Enter
            strokes = text_to_strokes(body)
            started = time.time()
            try:
                self.typer.type_message(strokes, open_key=open_key, press_enter=bool(press_enter),
                                        key_delay_ms=s["key_delay_ms"], open_delay_ms=s["open_delay_ms"])
            except Exception as e:  # uinput failures etc.
                self._record(ascii_text, replaced, False, f"{type(e).__name__}: {e}")
                return 503, {"ok": False, "error": f"Typing failed on the console: {e}"}
            self._record(ascii_text, replaced, True, None, time.time() - started)
            return 200, {"ok": True, "typed": ascii_text, "replaced": replaced,
                         "history": list(self.history)}
        finally:
            self._busy.release()

    def _record(self, text: str, replaced: int, ok: bool, error: Optional[str],
                duration: float = 0.0) -> None:
        with self._state_lock:
            if ok:
                self.sends += 1
                if text in self.history:
                    self.history.remove(text)
                self.history.insert(0, text)
                del self.history[HISTORY_SIZE:]
            self.last_send = {
                "time": time.time(),
                "chars": len(text),
                "preview": text[:40],
                "replaced": replaced,
                "ok": ok,
                "error": error,
                "duration": round(duration, 2),
            }

    # ---- phrases / chips --------------------------------------------------
    def edit_list(self, kind: Any, action: Any, item: Any) -> Result:
        if kind not in ("phrases", "chips"):
            return 400, {"ok": False, "error": "kind must be phrases or chips"}
        if action not in ("add", "remove"):
            return 400, {"ok": False, "error": "action must be add or remove"}
        current = self.settings.get(kind)
        if kind == "phrases":
            if not isinstance(item, str) or not item.strip():
                return 400, {"ok": False, "error": "phrase must be non-empty text"}
            item = item.strip()[:MAX_ITEM_LEN]
            if action == "add":
                if item in current:
                    current.remove(item)
                current.append(item)
            else:
                current = [p for p in current if p != item]
        else:
            if not isinstance(item, dict) or not str(item.get("prefix", "")).strip():
                return 400, {"ok": False, "error": "chip needs a prefix"}
            prefix = str(item["prefix"])
            if action == "add":
                if prefix and not prefix.endswith(" "):
                    prefix += " "
                current = [c for c in current if c["prefix"] != prefix]
                current.append({"label": str(item.get("label") or prefix.strip()), "prefix": prefix})
            else:
                current = [c for c in current if c["prefix"] != prefix]
        if action == "add" and len(current) > MAX_LIST:
            return 400, {"ok": False, "error": f"Max {MAX_LIST} items"}
        self.settings.set(kind, current)
        return 200, self.public_state()
