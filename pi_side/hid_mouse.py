"""USB HID mouse gadget driver."""
from __future__ import annotations

import os
import threading
import time
from pathlib import Path
from typing import Iterable, Tuple

HID_DEVICE = Path("/dev/hidg0")
REPORT_LEN = 4  # buttons, x, y, wheel
MAX_DELTA = 127


class HidMouse:
    """Simple HID report writer for the USB gadget endpoint."""

    def __init__(self, device: Path = HID_DEVICE) -> None:
        self.device = device
        self._lock = threading.Lock()

    def send(self, dx: int, dy: int, buttons: int = 0, wheel: int = 0) -> None:
        dx = max(-MAX_DELTA, min(MAX_DELTA, dx))
        dy = max(-MAX_DELTA, min(MAX_DELTA, dy))
        wheel = max(-MAX_DELTA, min(MAX_DELTA, wheel))
        report = bytes(
            [
                buttons & 0xFF,
                dx & 0xFF,
                dy & 0xFF,
                wheel & 0xFF,
            ]
        )
        with self._lock:
            with self.device.open("wb", buffering=0) as handle:
                handle.write(report)

    def smooth_path(self, points: Iterable[Tuple[int, int, int]]) -> None:
        for dx, dy, buttons in points:
            steps = max(abs(dx), abs(dy))
            if steps <= MAX_DELTA:
                self.send(dx, dy, buttons)
                continue
            sx = dx / steps
            sy = dy / steps
            for _ in range(int(steps)):
                self.send(int(round(sx)), int(round(sy)), buttons)
                time.sleep(0.002)


def ensure_permissions(device: Path = HID_DEVICE) -> None:
    if not device.exists():
        raise FileNotFoundError(f"HID device {device} does not exist. Ensure usb-gadget is configured.")
    if not os.access(device, os.W_OK):
        raise PermissionError(f"Insufficient permissions to write to {device}. Use udev rules or run as root.")
