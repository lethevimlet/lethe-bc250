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
DEFAULTS = {"pause_game": True, "mute_audio": True, "wake_on_input": True, "hook_steam_sleep": True, "quiet_fans": True}
CLEAN_ENV = {"PATH": "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C.UTF-8"}
EV_KEY = 0x01
EVENT_FMT = "llHHi"  # struct input_event on 64-bit
EVENT_SIZE = struct.calcsize(EVENT_FMT)

# Fan header while asleep. The BC-250's fans hang off the Nuvoton NCT6686D; only the out-of-tree
# nct6687 driver exposes a writable pwmN. The board's own curve barely changes speed between idle
# and load, so the idle chip alone does not make the box quieter: we lower the duty ourselves.
FAN_CHIPS = ("nct6687", "nct6686", "nct6683")
FAN_SLEEP_PWM = 64  # ~25 % duty
FAN_MAX_TEMP_C = 65.0  # CPU or GPU die above this while asleep -> back to the board's curve
FAN_MIN_RPM = 300  # slower than this once settled -> stalled -> back to the board's curve
FAN_SETTLE_S = 10.0  # time the fan gets to react before the stall / no-response checks apply
FAN_CHECK_S = 3.0


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


# ---------------------------------------------------------------------- fans
def read_str(path):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return None


def read_int(path):
    try:
        return int(read_str(path))
    except (TypeError, ValueError):
        return None


def write_str(path, value):
    with open(path, "w") as f:
        f.write(str(value))


def hwmon_dirs():
    base = "/sys/class/hwmon"
    try:
        return [os.path.join(base, n) for n in sorted(os.listdir(base))]
    except OSError:
        return []


def find_fan():
    """The Super-IO channel driving the case fans: the one fanN tach that reports rpm and has a writable
    pwmN. None when the nct6687 driver is not loaded or no fan spins."""
    for h in hwmon_dirs():
        if read_str(os.path.join(h, "name")) not in FAN_CHIPS:
            continue
        for n in range(1, 9):
            rpm = read_int(os.path.join(h, f"fan{n}_input"))
            if rpm and os.access(os.path.join(h, f"pwm{n}_enable"), os.W_OK) and os.access(os.path.join(h, f"pwm{n}"), os.W_OK):
                return {"hwmon": h, "ch": n, "rpm": rpm}
    return None


def die_temps():
    """Hottest of the CPU (k10temp) and GPU (amdgpu) die sensors in C, or None if neither reads."""
    temps = []
    for h in hwmon_dirs():
        if read_str(os.path.join(h, "name")) in ("k10temp", "amdgpu"):
            t = read_int(os.path.join(h, "temp1_input"))
            if t is not None:
                temps.append(t / 1000.0)
    return max(temps) if temps else None


def fan_restore(fan):
    """Hand the header back: the mode it had, and the duty too if it was already manual (some other tool)."""
    h, ch = fan["hwmon"], fan["ch"]
    try:
        if fan.get("enable") == 1:
            write_str(os.path.join(h, f"pwm{ch}"), fan.get("pwm", 255))
        write_str(os.path.join(h, f"pwm{ch}_enable"), fan.get("enable") or 2)
        decky.logger.info("fans: pwm%d back to mode %s (%s rpm)", ch, fan.get("enable") or 2, read_int(os.path.join(h, f"fan{ch}_input")))
        return True
    except OSError as e:
        decky.logger.error("fans: restore failed: %s", e)
        return False


def fan_quiet(fan):
    """Lower the header to FAN_SLEEP_PWM, remembering in `fan` what to put back. Touches nothing and
    returns False when it cannot be done safely (no die temperature to watch, already that slow)."""
    h, ch = fan["hwmon"], fan["ch"]
    if die_temps() is None:
        decky.logger.info("fans: no die temperature sensor, leaving the board's curve")
        return False
    fan["enable"] = read_int(os.path.join(h, f"pwm{ch}_enable"))
    fan["pwm"] = read_int(os.path.join(h, f"pwm{ch}"))
    if fan["enable"] is None or fan["pwm"] is None:
        return False
    if fan["pwm"] <= FAN_SLEEP_PWM:
        decky.logger.info("fans: already at duty %s, leaving it", fan["pwm"])
        return False
    try:
        write_str(os.path.join(h, f"pwm{ch}_enable"), 1)
        write_str(os.path.join(h, f"pwm{ch}"), FAN_SLEEP_PWM)
    except OSError as e:
        decky.logger.warning("fans: lowering failed (%s), restoring", e)
        fan_restore(fan)
        return False
    decky.logger.info("fans: pwm%d duty %s -> %d (mode was %s, %s rpm)", ch, fan["pwm"], FAN_SLEEP_PWM, fan["enable"], fan["rpm"])
    return True


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
    fan_task = None

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
        fan = find_fan()
        return {
            "fan_control": fan is not None,
            "fan_rpm": fan["rpm"] if fan else None,
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
        if key == "quiet_fans" and not s[key]:
            self._fan_release()  # turned off while asleep: give the header back right away
        return {"ok": True, "output": ""}

    def _fan_release(self):
        """Stop the guard and hand the fan header back to the board, if we hold it."""
        if self.fan_task and not self.fan_task.done():
            self.fan_task.cancel()
        self.fan_task = None
        st = load_state()
        if st and st.get("fan"):
            fan_restore(st["fan"])
            st["fan"] = None
            save_state(st)

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

        if s["quiet_fans"]:
            fan = find_fan()
            if fan is None:
                decky.logger.info("fans: no controllable fan header (nct6687 driver loaded?)")
            elif fan_quiet(fan):
                st["fan"] = fan
                self.fan_task = asyncio.get_event_loop().create_task(self._fan_guard(fan))
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
        if self.fan_task and not self.fan_task.done():
            self.fan_task.cancel()  # the guard does not restore on cancel; we do it here
        self.fan_task = None
        if st.get("fan"):
            fan_restore(st["fan"])
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

    async def _fan_guard(self, fan):
        """While asleep, keep the lowered fan honest: back to the board's curve if a die gets warm, if the
        fan stalled, or if it never slowed down (the EC ignores the duty, e.g. BIOS fan mode on Full
        Speed). A failed sensor read counts as a reason too. Cancelled by wake(), which restores itself."""
        rpm_path = os.path.join(fan["hwmon"], f"fan{fan['ch']}_input")
        t0 = time.time()
        reason = None
        try:
            while reason is None:
                await asyncio.sleep(FAN_CHECK_S)
                temp, rpm = die_temps(), read_int(rpm_path)
                if temp is None or rpm is None:
                    reason = "sensor read failed"
                elif temp >= FAN_MAX_TEMP_C:
                    reason = f"die at {temp:.0f} C"
                elif time.time() - t0 >= FAN_SETTLE_S:
                    if rpm < FAN_MIN_RPM:
                        reason = f"fan stalled ({rpm} rpm)"
                    elif rpm > fan["rpm"] * 0.9:
                        reason = f"fan did not slow down ({fan['rpm']} -> {rpm} rpm; BIOS fan mode on Full Speed?)"
        except asyncio.CancelledError:
            return
        except Exception as e:  # noqa: BLE001
            reason = f"guard error: {e}"
        decky.logger.warning("fans: %s, back to the board's curve", reason)
        fan_restore(fan)
        st = load_state()
        if st is not None:
            st["fan"] = None  # nothing left for wake() to restore
            save_state(st)

    async def _watch_input(self):
        """Wake on the first key/button press on any input device (after a short grace period).

        Devices come and go while asleep: Steam's suspend flow powers off wireless controllers, and
        the pad comes back as a *new* /dev/input/eventN when its button is pressed. So the watched
        set is not fixed: rescan /dev/input about once a second and open anything new, and drop a
        node whose read fails (ENODEV once the device is unregistered; select() reports it readable
        forever otherwise, which would also spin this loop)."""
        fds = {}  # fd -> node name
        watched = set()

        def rescan():
            added = []
            for name in sorted(os.listdir("/dev/input")):
                if not name.startswith("event") or name in watched:
                    continue
                try:
                    fd = os.open(f"/dev/input/{name}", os.O_RDONLY | os.O_NONBLOCK)
                except OSError:
                    continue
                fds[fd] = name
                watched.add(name)
                added.append(name)
            return added

        def drop(fd):
            watched.discard(fds.pop(fd, None))
            try:
                os.close(fd)
            except OSError:
                pass

        rescan()
        decky.logger.info("input watcher on %d devices", len(fds))
        grace_until = time.time() + 2.0  # ignore the button release that triggered us
        last_scan = time.time()
        try:
            while True:
                r, _, _ = await asyncio.get_event_loop().run_in_executor(None, select.select, list(fds), [], [], 1.0)
                # drain everything that is readable; a press is EV_KEY with value 1
                pressed = False
                for fd in r:
                    try:
                        data = os.read(fd, EVENT_SIZE * 64)
                    except OSError:
                        decky.logger.info("input watcher: %s went away", fds.get(fd))
                        drop(fd)
                        continue
                    for i in range(0, len(data) - EVENT_SIZE + 1, EVENT_SIZE):
                        _, _, etype, _, value = struct.unpack(EVENT_FMT, data[i:i + EVENT_SIZE])
                        if etype == EV_KEY and value == 1 and time.time() > grace_until:
                            pressed = True
                if pressed:
                    decky.logger.info("input detected, waking")
                    await self.wake()
                    return
                if time.time() - last_scan >= 1.0:
                    last_scan = time.time()
                    added = rescan()
                    if added:
                        decky.logger.info("input watcher: new device(s) %s, now %d", " ".join(added), len(fds))
        except asyncio.CancelledError:
            pass
        finally:
            for fd in list(fds):
                drop(fd)

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
