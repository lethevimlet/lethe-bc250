"""Tiny stdlib HTTP server serving the phone page and its API (no pairing: LAN / tailnet only)."""

from __future__ import annotations

import json
import logging
import os
import socket
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Optional
from urllib.parse import urlparse

from . import ws
from .core import InputCore
from .remote import RemoteSession

MAX_BODY = 8 * 1024
WEB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "web")
# Not a secret: a page from another site can only send a custom header after a CORS preflight, which
# this server never answers, so requiring it keeps other sites open on the phone from typing.
API_HEADER = "X-BC250-Input"
WS_IDLE_S = 40  # the page sends a keep-alive every 10 s

SECURITY_HEADERS = {
    "Cache-Control": "no-store",
    "X-Content-Type-Options": "nosniff",
    "X-Frame-Options": "DENY",
    "Referrer-Policy": "no-referrer",
    "Content-Security-Policy": "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
                               "connect-src 'self' ws: wss:; img-src 'self' data:; base-uri 'none'; form-action 'none'",
}


def make_handler(core: InputCore, log: logging.Logger, page_path: str):
    class Handler(BaseHTTPRequestHandler):
        server_version = "BC250Input/1"
        sys_version = ""
        protocol_version = "HTTP/1.1"

        def log_message(self, fmt, *args):  # route to the plugin log
            log.debug("http %s %s", self.client_address[0], fmt % args)

        def _send(self, code: int, body: bytes, ctype: str) -> None:
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            for k, v in SECURITY_HEADERS.items():
                self.send_header(k, v)
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(body)

        def _json(self, code: int, obj) -> None:
            self._send(code, json.dumps(obj).encode("utf-8"), "application/json; charset=utf-8")

        def _auth(self, need_header: bool = True) -> bool:
            if need_header and self.headers.get(API_HEADER) != "1":
                self._json(403, {"ok": False, "error": "Missing request header"})
                return False
            err = core.authorize(self.headers.get("Host"), self.client_address[0])
            if err:
                self._json(*err)
                return False
            return True

        def _body(self) -> Optional[dict]:
            try:
                n = int(self.headers.get("Content-Length", "0"))
            except ValueError:
                n = -1
            if n < 0 or n > MAX_BODY:
                self._json(413, {"ok": False, "error": "Request too large"})
                self.close_connection = True
                return None
            raw = self.rfile.read(n) if n else b""
            try:
                data = json.loads(raw.decode("utf-8") or "{}")
            except (ValueError, UnicodeDecodeError):
                self._json(400, {"ok": False, "error": "Invalid JSON"})
                return None
            if not isinstance(data, dict):
                self._json(400, {"ok": False, "error": "Expected a JSON object"})
                return None
            return data

        def do_GET(self):
            path = urlparse(self.path).path
            if path in ("/", "/index.html"):
                try:
                    with open(page_path, "rb") as f:
                        page = f.read()
                except OSError:
                    self._json(500, {"ok": False, "error": "page missing"})
                    return
                self._send(200, page, "text/html; charset=utf-8")
            elif path == "/healthz":
                self._json(200, {"ok": True})
            elif path == "/api/state":
                if self._auth():
                    self._json(200, core.public_state())
            elif path == "/api/ws":
                self._websocket()
            else:
                self._json(404, {"ok": False, "error": "Not found"})

        do_HEAD = do_GET

        def _websocket(self) -> None:
            h = self.headers
            key = h.get("Sec-WebSocket-Key")
            if (h.get("Upgrade", "").lower() != "websocket" or "upgrade" not in h.get("Connection", "").lower()
                    or h.get("Sec-WebSocket-Version") != "13" or not key or self.command != "GET"):
                self._json(400, {"ok": False, "error": "WebSocket upgrade expected"})
                return
            # Browsers cannot put a custom header on a WebSocket; the Origin check keeps pages from
            # other sites out instead.
            origin = h.get("Origin")
            if origin and urlparse(origin).netloc != h.get("Host"):
                self._json(403, {"ok": False, "error": "Wrong origin"})
                return
            if not self._auth(need_header=False):
                return
            self.send_response(101)
            self.send_header("Upgrade", "websocket")
            self.send_header("Connection", "Upgrade")
            self.send_header("Sec-WebSocket-Accept", ws.accept_key(key))
            self.end_headers()
            self.close_connection = True
            ip = self.client_address[0]
            conn = self.connection

            def closer():
                try:
                    conn.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass

            sess = RemoteSession(core)
            sid = core.open_session(ip, closer)
            log.info("touchpad connected from %s (mouse %s)", ip, "on" if sess.pointer else "off")
            conn.settimeout(WS_IDLE_S)
            try:
                self.wfile.write(ws.frame(ws.OP_TEXT, json.dumps({"hello": 1, "pointer": sess.pointer}).encode()))
                while True:
                    op, payload = ws.read_frame(self.rfile)
                    if op == ws.OP_TEXT:
                        sess.handle(payload.decode("utf-8", "replace"))
                    elif op == ws.OP_PING:
                        self.wfile.write(ws.frame(ws.OP_PONG, payload[:125]))
                    elif op == ws.OP_CLOSE:
                        self.wfile.write(ws.frame(ws.OP_CLOSE, payload[:2]))
                        break
            except (OSError, ws.WSError):
                pass
            except Exception as e:  # a uinput write failing, for instance
                log.error("touchpad session from %s failed: %s", ip, e)
            finally:
                sess.close()
                core.close_session(sid)
                log.info("touchpad disconnected from %s%s", ip,
                         f" ({sess.dropped} commands dropped)" if sess.dropped else "")

        def do_POST(self):
            path = urlparse(self.path).path
            if path not in ("/api/send", "/api/list"):
                self._json(404, {"ok": False, "error": "Not found"})
                return
            # Read the body first so keep-alive stays in sync, then authenticate.
            data = self._body()
            if data is None:
                return
            if not self._auth():
                return
            if path == "/api/send":
                code, obj = core.send(data.get("text"), data.get("enter", True))
                if code == 200:
                    log.info("typed %d chars (replaced %d)", len(obj["typed"]), obj["replaced"])
                elif code == 503:
                    log.error("send failed: %s", obj.get("error"))
                self._json(code, obj)
            else:
                self._json(*core.edit_list(data.get("kind"), data.get("action"), data.get("item")))

    return Handler


class _HTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    log: logging.Logger = logging.getLogger("bc250input")

    def handle_error(self, request, client_address):  # a phone dropping off Wi-Fi is not a traceback
        import sys
        self.log.debug("connection from %s ended: %s", client_address[0], sys.exc_info()[1])


class InputServer:
    def __init__(self, core: InputCore, log: Optional[logging.Logger] = None,
                 page_path: str = os.path.join(WEB_DIR, "index.html")) -> None:
        self.core = core
        self.log = log or logging.getLogger("bc250input")
        self.page_path = page_path
        self.httpd: Optional[ThreadingHTTPServer] = None
        self.thread: Optional[threading.Thread] = None

    @property
    def running(self) -> bool:
        return self.httpd is not None

    @property
    def port(self) -> Optional[int]:
        return self.httpd.server_address[1] if self.httpd else None

    def start(self, host: str = "0.0.0.0", port: int = 8251) -> None:
        if self.httpd:
            return
        httpd = _HTTPServer((host, port), make_handler(self.core, self.log, self.page_path))
        httpd.log = self.log
        self.httpd = httpd
        self.thread = threading.Thread(target=httpd.serve_forever, name="bc250input-http", daemon=True)
        self.thread.start()
        self.log.info("BC-250 Input HTTP server on %s:%d", host, self.port)

    def stop(self) -> None:
        httpd, self.httpd = self.httpd, None
        if not httpd:
            return
        self.core.close_sessions()
        httpd.shutdown()
        httpd.server_close()
        if self.thread:
            self.thread.join(timeout=2)
        self.thread = None
        self.log.info("BC-250 Input HTTP server stopped")
