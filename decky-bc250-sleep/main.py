"""BC-250 Sleep — a fake sleep for a board that cannot sleep.

The BC-250 has no working S3/s2idle, so "sleep" here means:
  1. freeze the running game's whole process tree (SIGSTOP), so it stops using CPU/GPU,
  2. mute audio,
  3. put the TV/monitor to sleep through gamescope (DPMS off),
  4. wait for any controller/keyboard/mouse press and undo all of it (SIGCONT, unmute, screen on).

Runs as root (plugin.json flag "root") so it can read /dev/input and signal the game's processes.
Package power stays at the board's idle floor (~32 W SoC); the win is quick resume, a dark TV,
and a quiet box, not real power saving.
"""
import asyncio
import json
import os
import select
import signal
import struct
import subprocess
import time

import decky

STATE_DIR = "/run/bc250-sleep"
STATE_FILE = os.path.join(STATE_DIR, "state.json")
SETTINGS_FILE = os.path.join(decky.DECKY_PLUGIN_SETTINGS_DIR, "settings.json")
SLEEP_UNITS = ["sleep.target", "suspend.target", "hybrid-sleep.target", "hibernate.target", "suspend-then-hibernate.target"]
DEFAULTS = {"pause_game": True, "mute_audio": True, "wake_on_input": True, "hook_steam_sleep": True}
CLEAN_ENV = {"PATH": "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C.UTF-8"}
EV_KEY = 0x01
EVENT_FMT = "llHHi"  # struct input_event on 64-bit
EVENT_SIZE = struct.calcsize(EVENT_FMT)


def run(cmd, user_env=None, timeout=20):
    env = dict(CLEAN_ENV)
    if user_env:
        env.update(user_env)
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=env)
        return p.returncode, (p.stdout + p.stderr).strip()
    except Exception as e:  # noqa: BLE001
        return 1, str(e)


def find_session_user():
    """The user running gamescope: whoever owns /run/user/<uid>/gamescope-0."""
    for uid in os.listdir("/run/user"):
        if os.path.exists(f"/run/user/{uid}/gamescope-0"):
            import pwd
            return pwd.getpwuid(int(uid)).pw_name, int(uid)
    return None, None


def user_cmd(user, uid, cmd):
    """Run a command as the desktop user with the gamescope + pipewire environment."""
    return run(["sudo", "-u", user, "env", f"XDG_RUNTIME_DIR=/run/user/{uid}", "WAYLAND_DISPLAY=gamescope-0",
                "DISPLAY=:0", f"DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/{uid}/bus"] + cmd)


def sink_by_name(user, uid, name):
    """PipeWire node id of the sink with this node.name, or None while it is absent (e.g. HDMI audio
    disappears while the TV is asleep and comes back with a new id)."""
    rc, out = user_cmd(user, uid, ["pw-dump"])
    if rc != 0:
        return None
    try:
        for o in json.loads(out):
            p = o.get("info", {}).get("props", {})
            if p.get("media.class") == "Audio/Sink" and p.get("node.name") == name:
                return o["id"]
    except ValueError:
        pass
    return None


def default_sink_name(user, uid):
    rc, out = user_cmd(user, uid, ["wpctl", "inspect", "@DEFAULT_AUDIO_SINK@"])
    if rc != 0:
        return None
    for line in out.splitlines():
        if "node.name" in line and "=" in line:
            return line.split("=", 1)[1].strip().strip('"')
    return None


def proc_cmdline(pid):
    try:
        with open(f"/proc/{pid}/cmdline", "rb") as f:
            return f.read().replace(b"\0", b" ").decode(errors="replace").strip()
    except OSError:
        return ""


def proc_tree():
    """pid -> ppid for all processes, and children map."""
    children = {}
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        try:
            with open(f"/proc/{d}/stat") as f:
                st = f.read()
            ppid = int(st[st.rindex(")") + 2:].split()[1])
        except (OSError, ValueError):
            continue
        children.setdefault(ppid, []).append(int(d))
    return children


def proc_argv(pid):
    try:
        with open(f"/proc/{pid}/cmdline", "rb") as f:
            return [a.decode(errors="replace") for a in f.read().split(b"\0") if a]
    except OSError:
        return []


def game_roots():
    """Steam launches every game under `.../reaper SteamLaunch AppId=N -- <game>`.
    Match the argv shape exactly (argv[0] is the reaper binary, argv[1] is SteamLaunch) so that a
    shell or editor merely *containing* those words in its command line is never frozen."""
    roots = []
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        argv = proc_argv(d)
        if len(argv) >= 3 and os.path.basename(argv[0]) == "reaper" and argv[1] == "SteamLaunch" and argv[2].startswith("AppId="):
            roots.append((int(d), argv[2][len("AppId="):]))
    return roots


def descendants(pid, children):
    out, stack = [], [pid]
    while stack:
        p = stack.pop()
        out.append(p)
        stack.extend(children.get(p, []))
    return out


def signal_pids(pids, sig):
    n = 0
    for p in pids:
        try:
            os.kill(p, sig)
            n += 1
        except ProcessLookupError:
            pass
        except PermissionError:
            decky.logger.warning("no permission to signal %s", p)
    return n


def load_settings():
    s = dict(DEFAULTS)
    try:
        with open(SETTINGS_FILE) as f:
            s.update(json.load(f))
    except (OSError, ValueError):
        pass
    return s


def save_settings(s):
    os.makedirs(os.path.dirname(SETTINGS_FILE), exist_ok=True)
    with open(SETTINGS_FILE, "w") as f:
        json.dump(s, f)


def load_state():
    try:
        with open(STATE_FILE) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def save_state(st):
    os.makedirs(STATE_DIR, exist_ok=True)
    if st is None:
        try:
            os.remove(STATE_FILE)
        except OSError:
            pass
    else:
        with open(STATE_FILE, "w") as f:
            json.dump(st, f)


class Plugin:
    watcher_task = None

    # ------------------------------------------------------------------ status / settings
    async def status(self):
        st = load_state()
        roots = game_roots()
        game = None
        if roots:
            game = {"pid": roots[0][0], "appid": roots[0][1]}
        masked = False
        rc, out = run(["systemctl", "is-enabled", "suspend.target"])
        masked = out.strip() == "masked"
        return {
            "asleep": st is not None,
            "since": st.get("since") if st else None,
            "frozen": len(st.get("pids", [])) if st else 0,
            "game": game,
            "settings": load_settings(),
            "sleep_masked": masked,
            "user": find_session_user()[0],
        }

    def apply_sleep_guard(self, enabled: bool):
        """Steam's Sleep entry ends in a logind suspend, which hangs the BC-250 (s2idle never wakes).
        With the systemd sleep units masked, logind refuses the request harmlessly and only our fake
        sleep (the frontend's SuspendPC replacement) runs. Applied at load and whenever the toggle changes."""
        action = "mask" if enabled else "unmask"
        rc, out = run(["systemctl", action] + SLEEP_UNITS)
        decky.logger.info("systemctl %s sleep units rc=%s %s", action, rc, out)
        return rc, out

    async def set_setting(self, key: str, value):
        s = load_settings()
        if key not in DEFAULTS:
            return {"ok": False, "output": f"unknown setting {key}"}
        s[key] = bool(value)
        save_settings(s)
        if key == "hook_steam_sleep":
            rc, out = self.apply_sleep_guard(s[key])
            return {"ok": rc == 0, "output": out}
        return {"ok": True, "output": ""}

    # ------------------------------------------------------------------ sleep
    async def sleep(self):
        if load_state() is not None:
            return {"ok": True, "output": "already asleep"}
        s = load_settings()
        user, uid = find_session_user()
        if not user:
            return {"ok": False, "output": "no gamescope session found"}
        st = {"since": time.time(), "pids": [], "muted_by_us": False, "user": user, "uid": uid}

        if s["pause_game"]:
            children = proc_tree()
            pids = []
            for root, appid in game_roots():
                pids += descendants(root, children)
            # stop the deepest processes first so a parent cannot respawn a child mid-way
            pids = sorted(set(pids), reverse=True)
            n = signal_pids(pids, signal.SIGSTOP)
            st["pids"] = pids
            decky.logger.info("froze %d/%d game processes", n, len(pids))

        if s["mute_audio"]:
            rc, out = user_cmd(user, uid, ["wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@"])
            decky.logger.info("audio before sleep rc=%s %s", rc, out)
            if rc == 0 and "MUTED" not in out:
                rc2, out2 = user_cmd(user, uid, ["wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "1"])
                decky.logger.info("mute rc=%s %s", rc2, out2)
                st["muted_by_us"] = rc2 == 0
                st["sink_name"] = default_sink_name(user, uid)  # unmute this exact device on wake

        rc, out = user_cmd(user, uid, ["gamescopectl", "drm_sleep_external_screen", "1"])
        decky.logger.info("screen off rc=%s %s", rc, out[-120:])
        save_state(st)

        if s["wake_on_input"]:
            self.watcher_task = asyncio.get_event_loop().create_task(self._watch_input())
        return {"ok": True, "output": "asleep"}

    # ------------------------------------------------------------------ wake
    async def wake(self):
        st = load_state()
        if st is None:
            return {"ok": True, "output": "not asleep"}
        user, uid = st.get("user"), st.get("uid")
        if user:
            user_cmd(user, uid, ["gamescopectl", "drm_sleep_external_screen", "0"])
        pids = sorted(st.get("pids", []))  # parents first on the way back
        n = signal_pids(pids, signal.SIGCONT)
        if st.get("muted_by_us") and user:
            asyncio.get_event_loop().create_task(self._unmute_later(user, uid, st.get("sink_name")))
        save_state(None)
        if self.watcher_task and not self.watcher_task.done():
            self.watcher_task.cancel()
        self.watcher_task = None
        decky.logger.info("woke: screen on, thawed %d/%d, unmuted=%s", n, len(pids), st.get("muted_by_us"))
        # Tell the frontend, which clears Steam's own "suspending" black screen if Sleep came from
        # Steam's power menu (Steam waits for a resume event that a real suspend would have produced).
        try:
            await decky.emit("bc250_sleep_woke")
        except Exception as e:  # noqa: BLE001
            decky.logger.warning("emit woke failed: %s", e)
        return {"ok": True, "output": f"awake, thawed {n} processes"}

    async def _unmute_later(self, user, uid, sink_name):
        """The HDMI/DP sink vanishes while the TV is asleep and re-appears with a new node id a moment
        after wake; WirePlumber may then re-apply the saved muted state on the new node. So: wait for
        the sink to come back (by node.name), unmute it, and re-assert a few times over the next
        seconds. Runs in the background so wake() itself stays fast."""
        deadline = time.time() + 12
        reasserts = [0.0, 1.5, 3.0, 5.0]
        first_ok = None
        while time.time() < deadline:
            target = None
            if sink_name:
                nid = sink_by_name(user, uid, sink_name)
                if nid is not None:
                    target = str(nid)
            if target is None:
                target = "@DEFAULT_AUDIO_SINK@"
            rc, out = user_cmd(user, uid, ["wpctl", "set-mute", target, "0"])
            decky.logger.info("unmute %s rc=%s %s", target, rc, out[:80])
            if rc == 0:
                if first_ok is None:
                    first_ok = time.time()
                if time.time() - first_ok >= reasserts[-1]:
                    return
            await asyncio.sleep(0.5 if first_ok is None else 1.5)

    async def _watch_input(self):
        """Wake on the first key/button press on any input device (after a short grace period)."""
        fds = {}
        for name in sorted(os.listdir("/dev/input")):
            if not name.startswith("event"):
                continue
            try:
                fd = os.open(f"/dev/input/{name}", os.O_RDONLY | os.O_NONBLOCK)
                fds[fd] = name
            except OSError:
                pass
        decky.logger.info("input watcher on %d devices", len(fds))
        grace_until = time.time() + 2.0  # ignore the button release that triggered us
        try:
            while True:
                r, _, _ = await asyncio.get_event_loop().run_in_executor(None, select.select, list(fds), [], [], 1.0)
                # drain everything that is readable; a press is EV_KEY with value 1
                pressed = False
                for fd in r:
                    try:
                        data = os.read(fd, EVENT_SIZE * 64)
                    except OSError:
                        continue
                    for i in range(0, len(data) - EVENT_SIZE + 1, EVENT_SIZE):
                        _, _, etype, _, value = struct.unpack(EVENT_FMT, data[i:i + EVENT_SIZE])
                        if etype == EV_KEY and value == 1 and time.time() > grace_until:
                            pressed = True
                if pressed:
                    decky.logger.info("input detected, waking")
                    await self.wake()
                    return
        except asyncio.CancelledError:
            pass
        finally:
            for fd in fds:
                try:
                    os.close(fd)
                except OSError:
                    pass

    # ------------------------------------------------------------------ lifecycle
    async def _main(self):
        decky.logger.info("BC-250 Sleep backend loaded")
        self.apply_sleep_guard(load_settings()["hook_steam_sleep"])
        # If the loader restarted while asleep, do not leave the game frozen forever.
        if load_state() is not None:
            decky.logger.info("stale sleep state found, waking")
            await self.wake()

    async def _unload(self):
        if load_state() is not None:
            await self.wake()
