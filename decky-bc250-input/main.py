import asyncio
import os

# Decky injects the `decky` module and appends this plugin's py_modules/ to sys.path.
import decky

from bc250input.core import InputCore
from bc250input.netinfo import all_ips, primary_ip
from bc250input.server import InputServer
from bc250input.settings import Settings
from bc250input.uinput import UInputKeyboard, UInputMouse

SETTABLE = ("open_key", "key_delay_ms", "open_delay_ms", "port", "pointer_enabled")


class Plugin:
    core: InputCore = None
    server: InputServer = None
    keyboard: UInputKeyboard = None
    mouse: UInputMouse = None
    server_error: str = None
    uinput_error: str = None
    mouse_error: str = None

    # ---- lifecycle ---------------------------------------------------------
    async def _main(self):
        settings = Settings(os.path.join(decky.DECKY_PLUGIN_SETTINGS_DIR, "settings.json"))
        self.keyboard = UInputKeyboard()
        self.mouse = UInputMouse()
        self.core = InputCore(settings, self.keyboard, self.mouse)
        self.server = InputServer(self.core, decky.logger)
        # Create the virtual devices up front so the compositor has seen them before the first use.
        await asyncio.to_thread(self._open_keyboard)
        if settings.get("pointer_enabled"):
            await asyncio.to_thread(self._open_mouse)
        if settings.get("enabled"):
            await self._start_server()
        decky.logger.info("BC-250 Input loaded")
        # At boot the plugin can load before the port or /dev/uinput is usable: keep trying for a while
        # so the phone page is there without opening the panel.
        self._boot_retry = asyncio.create_task(self._retry_at_boot())

    async def _retry_at_boot(self, tries: int = 24, every_s: float = 5.0):
        for _ in range(tries):
            s = self.core.settings
            todo = (self.keyboard.fd is None
                    or (s.get("pointer_enabled") and self.mouse.fd is None)
                    or (s.get("enabled") and not self.server.running))
            if not todo:
                return
            await asyncio.sleep(every_s)
            if self.keyboard.fd is None:
                await asyncio.to_thread(self._open_keyboard)
            if s.get("pointer_enabled") and self.mouse.fd is None:
                await asyncio.to_thread(self._open_mouse)
            if s.get("enabled") and not self.server.running:
                await self._start_server()
                if self.server.running:
                    decky.logger.info("HTTP server up after a retry")

    async def _unload(self):
        task = getattr(self, "_boot_retry", None)
        if task:
            task.cancel()
        if self.server:
            await asyncio.to_thread(self.server.stop)
        if self.keyboard:
            await asyncio.to_thread(self.keyboard.close)
        if self.mouse:
            await asyncio.to_thread(self.mouse.close)
        decky.logger.info("BC-250 Input unloaded")

    async def _uninstall(self):
        pass

    # ---- helpers -----------------------------------------------------------
    def _open_keyboard(self):
        try:
            self.keyboard.open()
            self.uinput_error = None
        except Exception as e:
            self.uinput_error = f"{type(e).__name__}: {e}"
            decky.logger.error("uinput open failed: %s", self.uinput_error)

    def _open_mouse(self):
        try:
            self.mouse.open()
            self.mouse_error = None
        except Exception as e:
            self.mouse_error = f"{type(e).__name__}: {e}"
            decky.logger.error("uinput mouse open failed: %s", self.mouse_error)

    async def _start_server(self):
        port = self.core.settings.get("port")
        try:
            await asyncio.to_thread(self.server.start, "0.0.0.0", port)
            self.server_error = None
        except OSError as e:
            self.server_error = f"Could not listen on port {port}: {e.strerror or e}"
            decky.logger.error(self.server_error)

    # ---- frontend API ------------------------------------------------------
    async def get_status(self):
        s = self.core.settings.snapshot()
        ips = await asyncio.to_thread(all_ips)
        ip = ips[0]["ip"] if ips else primary_ip()
        port = self.server.port or s["port"]
        return {
            "enabled": s["enabled"],
            "running": self.server.running,
            "serverError": self.server_error,
            "uinputOk": self.keyboard.fd is not None,
            "uinputError": self.uinput_error,
            "pointerEnabled": s["pointer_enabled"],
            "mouseOk": self.mouse.fd is not None,
            "mouseError": self.mouse_error,
            "clients": self.core.clients(),
            "ip": ip,
            "ips": ips,
            "port": port,
            "url": f"http://{ip}:{port}/",
            "openKey": s["open_key"],
            "keyDelayMs": s["key_delay_ms"],
            "openDelayMs": s["open_delay_ms"],
            "sends": self.core.sends,
            "lastSend": self.core.last_send,
        }

    async def set_enabled(self, enabled: bool):
        self.core.settings.set("enabled", bool(enabled))
        if enabled:
            await self._start_server()
        else:
            await asyncio.to_thread(self.server.stop)
            self.server_error = None
        return await self.get_status()

    async def set_setting(self, key: str, value):
        if key not in SETTABLE:
            raise ValueError(f"unknown setting {key}")
        self.core.settings.set(key, value)
        if key == "pointer_enabled":
            # Phones reconnect by themselves and learn the new setting from the hello message.
            self.core.close_sessions()
            if value:
                await asyncio.to_thread(self._open_mouse)
            else:
                await asyncio.to_thread(self.mouse.close)
                self.mouse_error = None
        if key == "port" and self.server.running:
            await asyncio.to_thread(self.server.stop)
            await self._start_server()
        return await self.get_status()

    async def retry_uinput(self):
        await asyncio.to_thread(self._open_keyboard)
        if self.core.settings.get("pointer_enabled"):
            await asyncio.to_thread(self._open_mouse)
        return await self.get_status()
