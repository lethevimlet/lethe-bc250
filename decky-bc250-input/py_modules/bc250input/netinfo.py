"""Find the console's IPv4 addresses so the phone knows where to connect."""

from __future__ import annotations

import ipaddress
import re
import socket
import subprocess
from typing import List

# Container/VM bridges a phone can never reach.
_SKIP_IFACES = ("docker", "br-", "cni", "flannel", "veth", "virbr", "lxc", "podman")


def primary_ip() -> str:
    """IP of the interface holding the default route. Sends no packets (UDP connect)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))  # TEST-NET-1, never actually contacted
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def parse_ip_addr(output: str) -> List[tuple]:
    """Parse `ip -4 -o addr show` output into [(ifname, ip)]."""
    out = []
    for line in output.splitlines():
        m = re.match(r"\d+:\s+(\S+)\s+inet\s+(\d+\.\d+\.\d+\.\d+)/", line)
        if m:
            out.append((m.group(1), m.group(2)))
    return out


def all_ips() -> List[dict]:
    """[{iface, ip, kind}] for non-loopback IPv4 addresses; kind is lan|tailscale|other."""
    pairs: List[tuple] = []
    try:
        res = subprocess.run(["ip", "-4", "-o", "addr", "show"], capture_output=True, text=True, timeout=2)
        pairs = parse_ip_addr(res.stdout)
    except Exception:
        pass
    seen = set()
    result = []
    for iface, ip in pairs:
        if ip in seen or iface.startswith(_SKIP_IFACES):
            continue
        seen.add(ip)
        addr = ipaddress.ip_address(ip)
        if addr.is_loopback:
            continue
        if addr in ipaddress.ip_network("100.64.0.0/10"):
            kind = "tailscale"
        elif addr.is_private:
            kind = "lan"
        else:
            kind = "other"
        result.append({"iface": iface, "ip": ip, "kind": kind})
    p = primary_ip()
    if p not in seen and p != "127.0.0.1":
        result.insert(0, {"iface": "?", "ip": p, "kind": "lan"})
    # primary first
    result.sort(key=lambda r: 0 if r["ip"] == p else 1)
    return result
