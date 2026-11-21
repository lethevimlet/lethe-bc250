"""Just enough WebSocket (RFC 6455) for the touchpad: small text frames from the phone, a few back."""

from __future__ import annotations

import base64
import hashlib
import struct
from typing import BinaryIO, Tuple

GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
OP_TEXT, OP_BINARY, OP_CLOSE, OP_PING, OP_PONG = 0x1, 0x2, 0x8, 0x9, 0xA
MAX_PAYLOAD = 4096  # one frame carries a batch of short commands; anything bigger is not ours


class WSError(Exception):
    pass


def accept_key(key: str) -> str:
    return base64.b64encode(hashlib.sha1(key.strip().encode() + GUID).digest()).decode()


def _read(f: BinaryIO, n: int) -> bytes:
    data = f.read(n)
    if data is None or len(data) != n:
        raise WSError("connection closed")
    return data


def read_frame(f: BinaryIO) -> Tuple[int, bytes]:
    """Read one client frame. Fragmented, unmasked or oversized frames are refused."""
    b0, b1 = _read(f, 2)
    fin, opcode, masked, n = b0 & 0x80, b0 & 0x0F, b1 & 0x80, b1 & 0x7F
    if b0 & 0x70:
        raise WSError("reserved bits set")
    if not fin or opcode == 0:
        raise WSError("fragmented frames not supported")
    if not masked:
        raise WSError("client frames must be masked")
    if n == 126:
        n = struct.unpack("!H", _read(f, 2))[0]
    elif n == 127:
        raise WSError("frame too large")
    if n > MAX_PAYLOAD:
        raise WSError("frame too large")
    mask = _read(f, 4)
    payload = bytes(c ^ mask[i & 3] for i, c in enumerate(_read(f, n))) if n else b""
    return opcode, payload


def frame(opcode: int, payload: bytes = b"") -> bytes:
    """Build an unmasked server frame (payload < 64 KiB)."""
    n = len(payload)
    head = bytes([0x80 | opcode]) + (bytes([n]) if n < 126 else bytes([126]) + struct.pack("!H", n))
    return head + payload
