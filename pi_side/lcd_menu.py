"""Waveshare 1.2\" IPS LCD UI and input handling."""
from __future__ import annotations

import queue
import threading
import time
from dataclasses import dataclass
from typing import Any, Callable, List, Optional

from PIL import Image, ImageDraw, ImageFont

try:  # Hardware-specific imports are optional during development
    import board
    import digitalio
    from adafruit_rgb_display import st7789
except ImportError:  # pragma: no cover - optional dependency on the Pi
    board = None
    digitalio = None
    st7789 = None


@dataclass
class UiMessage:
    text: str
    ttl: float = 3.0


class LcdMenu:
    """Background UI thread for preset selection and status updates."""

    def __init__(self, presets: List[str], on_select: Callable[[str], None], joystick_pins: Optional[dict[str, str]] = None) -> None:
        self.presets = presets
        self.on_select = on_select
        self.current_index = 0
        self._messages: "queue.Queue[UiMessage]" = queue.Queue()
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self._display = None
        self._font = ImageFont.load_default()
        self._inputs: dict[str, Any] = {}
        self._last_input = 0.0
        self._debounce = 0.2
        self._pin_config = joystick_pins or {}

    # Hardware initialization is kept lazy so unit tests can instantiate the class without Pi hardware present.
    def _init_display(self) -> None:
        if st7789 is None:
            raise RuntimeError("ST7789 library not available. Install on Raspberry Pi.")
        spi = board.SPI()
        tft_cs = digitalio.DigitalInOut(board.CE0)
        tft_dc = digitalio.DigitalInOut(board.D25)
        tft_reset = digitalio.DigitalInOut(board.D24)
        self._display = st7789.ST7789(
            spi,
            height=240,
            width=240,
            cs=tft_cs,
            dc=tft_dc,
            rst=tft_reset,
            rotation=90,
        )
        self._init_inputs()

    def _init_inputs(self) -> None:
        if digitalio is None:
            return
        mapping: dict[str, digitalio.DigitalInOut] = {}
        for name, pin_name in self._pin_config.items():
            pin = getattr(board, pin_name, None)
            if pin is None:
                continue
            dio = digitalio.DigitalInOut(pin)
            dio.direction = digitalio.Direction.INPUT
            dio.pull = digitalio.Pull.UP
            mapping[name] = dio
        self._inputs = mapping

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=1.0)
        for pin in self._inputs.values():
            try:
                pin.deinit()
            except AttributeError:
                pass
        self._inputs.clear()

    def update_presets(self, presets: List[str]) -> None:
        self.presets = presets
        if presets:
            self.current_index = min(self.current_index, len(presets) - 1)
        else:
            self.current_index = 0

    def show_message(self, text: str, ttl: float = 3.0) -> None:
        self._messages.put(UiMessage(text=text, ttl=ttl))

    def _draw_screen(self, message: Optional[UiMessage]) -> Image.Image:
        image = Image.new("RGB", (240, 240), color=(0, 0, 0))
        draw = ImageDraw.Draw(image)
        if not self.presets:
            draw.text((10, 10), "No presets", font=self._font, fill=(255, 0, 0))
        else:
            for offset, preset in enumerate(self.presets[self.current_index:self.current_index + 5]):
                y = 30 + offset * 40
                prefix = ">" if offset == 0 else " "
                draw.text((10, y), f"{prefix} {preset}", font=self._font, fill=(0, 255, 0))
        if message:
            draw.rectangle((0, 200, 240, 240), fill=(32, 32, 32))
            draw.text((10, 210), message.text[:18], font=self._font, fill=(255, 255, 0))
        return image

    def _handle_input(self) -> None:
        if not self._inputs:
            return
        now = time.monotonic()
        if now - self._last_input < self._debounce:
            return

        moved = False
        if (pin := self._inputs.get("up")) and not pin.value:
            self.current_index = max(0, self.current_index - 1)
            moved = True
        elif (pin := self._inputs.get("down")) and not pin.value:
            if self.presets:
                self.current_index = min(len(self.presets) - 1, self.current_index + 1)
            moved = True
        elif (pin := self._inputs.get("left")) and not pin.value:
            self.current_index = max(0, self.current_index - 5)
            moved = True
        elif (pin := self._inputs.get("right")) and not pin.value:
            if self.presets:
                self.current_index = min(len(self.presets) - 1, self.current_index + 5)
            moved = True

        if (pin := self._inputs.get("select")) and not pin.value:
            if self.presets:
                self.on_select(self.presets[self.current_index])
                self.show_message(f"Preset {self.presets[self.current_index]}", ttl=2)
            moved = True

        if moved:
            self._last_input = now

    def _run(self) -> None:
        if self._display is None:
            try:
                self._init_display()
            except RuntimeError:
                # On development machines, just skip rendering.
                return
        active_message: Optional[UiMessage] = None
        message_deadline = 0.0
        while not self._stop.is_set():
            now = time.monotonic()
            try:
                while True:
                    active_message = self._messages.get_nowait()
                    message_deadline = now + active_message.ttl
            except queue.Empty:
                if active_message and now > message_deadline:
                    active_message = None
            image = self._draw_screen(active_message)
            self._display.image(image)
            self._handle_input()
            time.sleep(0.1)
