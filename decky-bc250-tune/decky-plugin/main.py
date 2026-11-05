"""Decky Loader backend for BC-250 Tune.

Runs as root (plugin.json flag "root") and simply drives /usr/local/bin/bc250-tune,
so every toggle in the Quick Access menu is the same as `sudo bc250-tune set KEY VALUE`.
"""
import asyncio
import json
import os

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

    # Anything the Steam shortcuts launch through a wrapper below ~/Applications, plus any
    # Battle.net Proton prefix. Homes are globbed so no username is baked in; a pgrep here
    # cannot match the loader (its own cmdline is the loader's, and pgrep excludes itself),
    # so unlike pkill over ssh this is safe.
    def _kill_patterns(self):
        import glob
        return glob.glob("/var/home/*/Applications/") + ["Program Files (x86)/Battle.net"]

    async def kill_games(self):
        """Kill every emulator / native-port / Battle.net process, foreground or background."""
        procs = {}
        for pat in self._kill_patterns():
            proc = await asyncio.create_subprocess_exec(
                "pgrep", "-f", pat,
                stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL, env=CLEAN_ENV,
            )
            out, _ = await proc.communicate()
            for pid in out.decode().split():
                procs[int(pid)] = None
        if not procs:
            return {"ok": True, "output": "nothing running"}
        for pid in procs:
            ps = await asyncio.create_subprocess_exec(
                "ps", "-o", "comm=", "-p", str(pid),
                stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL, env=CLEAN_ENV,
            )
            comm, _ = await ps.communicate()
            procs[pid] = (comm.decode().strip() or "?")[:24]
        for pid in procs:
            try:
                os.kill(pid, 15)
            except ProcessLookupError:
                pass
        await asyncio.sleep(3)
        leftovers = []
        for pid in procs:
            try:
                os.kill(pid, 9)
                leftovers.append(pid)
            except ProcessLookupError:
                pass
        decky.logger.info("kill_games: %s", ", ".join(f"{pid}={name}" for pid, name in procs.items()))
        out = ", ".join(f"{name}({pid})" for pid, name in procs.items())
        if leftovers:
            out += f" [forced: {', '.join(map(str, leftovers))}]"
        return {"ok": True, "output": out}

    async def _main(self):
        decky.logger.info("BC-250 Tune backend loaded")

    async def _unload(self):
        pass
