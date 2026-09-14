"""Decky Loader backend for BC-250 Tune.

Runs as root (plugin.json flag "root") and simply drives /usr/local/bin/bc250-tune,
so every toggle in the Quick Access menu is the same as `sudo bc250-tune set KEY VALUE`.
"""
import asyncio
import json

import decky

BIN = "/usr/local/bin/bc250-tune"
# Decky Loader is a PyInstaller bundle: its LD_LIBRARY_PATH would make systemctl/umr in the child
# load the bundled (older) OpenSSL and fail. Give the script a clean system environment.
CLEAN_ENV = {"PATH": "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C.UTF-8", "HOME": "/root"}


async def run(*args: str, timeout: float = 120):
    try:
        proc = await asyncio.create_subprocess_exec(
            BIN, *args, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT, env=CLEAN_ENV
        )
        out, _ = await asyncio.wait_for(proc.communicate(), timeout)
        return proc.returncode, out.decode(errors="replace")
    except FileNotFoundError:
        return 127, f"{BIN} not found"
    except asyncio.TimeoutError:
        return 124, "timeout"


class Plugin:
    async def status(self):
        rc, out = await run("status", "--json", timeout=30)
        if rc != 0:
            decky.logger.warning("status failed rc=%s: %s", rc, out)
        for line in reversed(out.splitlines()):
            line = line.strip()
            if line.startswith("{"):
                try:
                    return json.loads(line)
                except json.JSONDecodeError:
                    pass
        return None

    async def set_option(self, key: str, value: str):
        rc, out = await run("set", str(key), str(value))
        decky.logger.info("set %s=%s rc=%s\n%s", key, value, rc, out)
        return {"ok": rc == 0, "output": out}

    async def reboot(self):
        decky.logger.info("warm reboot requested")
        rc, out = await run("reboot", timeout=60)
        decky.logger.info("reboot rc=%s\n%s", rc, out)
        return {"ok": rc == 0, "output": out}

    async def restart_session(self):
        decky.logger.info("gaming session restart requested")
        rc, out = await run("restart-session", timeout=60)
        decky.logger.info("restart-session rc=%s\n%s", rc, out)
        return {"ok": rc == 0, "output": out}

    async def _main(self):
        decky.logger.info("BC-250 Tune backend loaded")

    async def _unload(self):
        pass
