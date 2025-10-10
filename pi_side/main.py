"""Entry point for the Pi-side automation agent."""
from __future__ import annotations

import asyncio
import logging
import signal
import sys
from typing import Optional

from . import communication, config, hid_mouse, preset_manager
from .lcd_menu import LcdMenu

config.ensure_directories()

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    handlers=[
        logging.FileHandler(config.LOG_DIR / "piai.log"),
        logging.StreamHandler(sys.stdout),
    ],
)
LOGGER = logging.getLogger("piai")


class PiAgent:
    def __init__(self) -> None:
        self.settings = config.load_config()
        self.selected_preset: Optional[preset_manager.Preset] = None
        self.lcd = LcdMenu([], self._on_preset_selected, self.settings.get("joystick_pins", {}))
        self.hid = hid_mouse.HidMouse()

    async def run(self) -> None:
        hid_mouse.ensure_permissions()
        presets = preset_manager.list_presets()
        self.lcd.update_presets([p.name for p in presets])
        saved = config.load_selected_preset()
        self.selected_preset = next((p for p in presets if p.name == saved), presets[0] if presets else None)
        if self.selected_preset:
            self.lcd.show_message(f"Preset: {self.selected_preset.name}", ttl=5)
        self.lcd.start()

        backoffs = self.settings.get("reconnect_backoff", [2])
        backoff_index = 0
        while True:
            try:
                if not self.selected_preset:
                    await asyncio.sleep(1)
                    continue
                session = communication.SessionInfo(
                    preset=self.selected_preset,
                    version=self.settings["version"],
                    secret=self.settings["handshake_secret"],
                )
                client = communication.CommunicationClient(
                    self.settings["service_host"],
                    self.settings["service_port"],
                )
                try:
                    async for message in communication.run_handshake(session, client):
                        dx = int(message["dx"])
                        dy = int(message["dy"])
                        buttons = int(message.get("buttons", 0))
                        self.hid.send(dx, dy, buttons)
                finally:
                    await client.close()
                backoff_index = 0
            except (ConnectionError, OSError) as exc:
                LOGGER.warning("Connection lost: %s", exc)
                self.lcd.show_message("PC NOT DETECTED", ttl=2)
                delay = backoffs[min(backoff_index, len(backoffs) - 1)]
                backoff_index = min(backoff_index + 1, len(backoffs) - 1)
                await asyncio.sleep(delay)
            except Exception as exc:  # pragma: no cover
                LOGGER.exception("Unhandled error: %s", exc)
                await asyncio.sleep(5)

    def _on_preset_selected(self, name: str) -> None:
        presets = preset_manager.list_presets()
        match = next((preset for preset in presets if preset.name == name), None)
        if not match:
            self.lcd.show_message("Preset missing", ttl=2)
            return
        self.selected_preset = match
        preset_manager.save_state(name)
        self.lcd.show_message(f"Selected {name}", ttl=2)


def main() -> int:
    agent = PiAgent()
    loop = asyncio.get_event_loop()

    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, loop.stop)

    try:
        loop.run_until_complete(agent.run())
    finally:
        agent.lcd.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
