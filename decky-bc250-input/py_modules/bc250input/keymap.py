"""Text -> Linux input keycode conversion for a US QWERTY layout.

Only printable ASCII (0x20-0x7E) is typed. Common "smart" characters that phone
keyboards insert (curly quotes, en/em dashes, ellipsis, non-breaking spaces) are
mapped to ASCII first; accents are stripped where possible; anything left over
becomes '?'.
"""

from __future__ import annotations

from typing import List, NamedTuple, Tuple

try:  # unicodedata is a C extension; tolerate its absence in a frozen runtime.
    import unicodedata as _ud
except Exception:  # pragma: no cover
    _ud = None

# Linux input-event-codes.h
KEY_ESC = 1
KEY_1, KEY_2, KEY_3, KEY_4, KEY_5 = 2, 3, 4, 5, 6
KEY_6, KEY_7, KEY_8, KEY_9, KEY_0 = 7, 8, 9, 10, 11
KEY_MINUS = 12
KEY_EQUAL = 13
KEY_BACKSPACE = 14
KEY_TAB = 15
KEY_LEFTBRACE = 26
KEY_RIGHTBRACE = 27
KEY_ENTER = 28
KEY_SEMICOLON = 39
KEY_APOSTROPHE = 40
KEY_GRAVE = 41
KEY_LEFTSHIFT = 42
KEY_BACKSLASH = 43
KEY_COMMA = 51
KEY_DOT = 52
KEY_SLASH = 53
KEY_SPACE = 57
KEY_HOME, KEY_UP, KEY_PAGEUP, KEY_LEFT, KEY_RIGHT = 102, 103, 104, 105, 106
KEY_END, KEY_DOWN, KEY_PAGEDOWN, KEY_DELETE = 107, 108, 109, 111

# Keys the touchpad screen can press by name (no text, no modifiers).
NAMED_KEYS = {
    "esc": KEY_ESC, "tab": KEY_TAB, "enter": KEY_ENTER, "backspace": KEY_BACKSPACE, "space": KEY_SPACE,
    "up": KEY_UP, "down": KEY_DOWN, "left": KEY_LEFT, "right": KEY_RIGHT,
    "home": KEY_HOME, "end": KEY_END, "pageup": KEY_PAGEUP, "pagedown": KEY_PAGEDOWN, "delete": KEY_DELETE,
}

_LETTERS = {
    "q": 16, "w": 17, "e": 18, "r": 19, "t": 20, "y": 21, "u": 22, "i": 23, "o": 24, "p": 25,
    "a": 30, "s": 31, "d": 32, "f": 33, "g": 34, "h": 35, "j": 36, "k": 37, "l": 38,
    "z": 44, "x": 45, "c": 46, "v": 47, "b": 48, "n": 49, "m": 50,
}

_UNSHIFTED = {
    "1": KEY_1, "2": KEY_2, "3": KEY_3, "4": KEY_4, "5": KEY_5,
    "6": KEY_6, "7": KEY_7, "8": KEY_8, "9": KEY_9, "0": KEY_0,
    "-": KEY_MINUS, "=": KEY_EQUAL, "[": KEY_LEFTBRACE, "]": KEY_RIGHTBRACE,
    ";": KEY_SEMICOLON, "'": KEY_APOSTROPHE, "`": KEY_GRAVE, "\\": KEY_BACKSLASH,
    ",": KEY_COMMA, ".": KEY_DOT, "/": KEY_SLASH, " ": KEY_SPACE,
}

_SHIFTED = {
    "!": KEY_1, "@": KEY_2, "#": KEY_3, "$": KEY_4, "%": KEY_5,
    "^": KEY_6, "&": KEY_7, "*": KEY_8, "(": KEY_9, ")": KEY_0,
    "_": KEY_MINUS, "+": KEY_EQUAL, "{": KEY_LEFTBRACE, "}": KEY_RIGHTBRACE,
    ":": KEY_SEMICOLON, '"': KEY_APOSTROPHE, "~": KEY_GRAVE, "|": KEY_BACKSLASH,
    "<": KEY_COMMA, ">": KEY_DOT, "?": KEY_SLASH,
}


class Stroke(NamedTuple):
    code: int
    shift: bool


# char -> Stroke for every printable ASCII character
KEYMAP = {}
for _c, _k in _LETTERS.items():
    KEYMAP[_c] = Stroke(_k, False)
    KEYMAP[_c.upper()] = Stroke(_k, True)
for _c, _k in _UNSHIFTED.items():
    KEYMAP[_c] = Stroke(_k, False)
for _c, _k in _SHIFTED.items():
    KEYMAP[_c] = Stroke(_k, True)

# Every keycode the virtual keyboard must advertise.
ALL_KEYCODES = sorted({s.code for s in KEYMAP.values()} | {KEY_LEFTSHIFT} | set(NAMED_KEYS.values()))

# Characters phone keyboards / autocorrect commonly produce.
_SMART = {
    "‘": "'", "’": "'", "‚": "'", "‛": "'", "′": "'",
    "“": '"', "”": '"', "„": '"', "‟": '"', "″": '"',
    "–": "-", "—": "-", "―": "-", "−": "-", "‐": "-", "‑": "-",
    "…": "...",
    " ": " ", " ": " ", " ": " ", " ": " ", " ": " ",
    "​": "", "‌": "", "‍": "", "﻿": "",
    "\n": " ", "\r": " ", "\t": " ",
}


def normalize_text(text: str) -> Tuple[str, int]:
    """Return (ascii_text, replaced_count). replaced_count counts chars turned into '?'."""
    out: List[str] = []
    replaced = 0
    for ch in text:
        if ch in KEYMAP:
            out.append(ch)
            continue
        if ch in _SMART:
            out.append(_SMART[ch])
            continue
        if _ud is not None:
            base = "".join(c for c in _ud.normalize("NFKD", ch) if not _ud.combining(c))
            if base and all(c in KEYMAP for c in base):
                out.append(base)
                continue
        out.append("?")
        replaced += 1
    return "".join(out), replaced


def text_to_strokes(text: str) -> List[Stroke]:
    """Convert already-normalized ASCII text to strokes. Raises ValueError on unmapped chars."""
    strokes = []
    for ch in text:
        s = KEYMAP.get(ch)
        if s is None:
            raise ValueError(f"unmapped character {ch!r}")
        strokes.append(s)
    return strokes
