"""Configuration helpers for the Pi-side agent.

The module discovers the shared `/boot/exodus` directory that stores
configuration, presets, and binaries. Functions in this file are designed
for unit testing by allowing overrides via environment variables.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any, Dict

BASE_DIR = Path(os.environ.get("EXODUS_ROOT", "/boot/exodus"))
STATE_DIR = BASE_DIR / "state"
MODELS_DIR = BASE_DIR / "models"
PRESETS_DIR = BASE_DIR / "presets"
WINDOWS_BIN_DIR = BASE_DIR / "windows_binaries"
LOG_DIR = Path("/var/log/piai")

CONFIG_FILE = BASE_DIR / "pi_side" / "config.json"
DEFAULT_CONFIG: Dict[str, Any] = {
    "version": "1.0.0",
    "service_host": "192.168.137.1",  # Default USB gadget gateway
    "service_port": 27121,
    "handshake_secret": "change-me",
    "heartbeat_interval": 5.0,
    "reconnect_backoff": [2, 4, 8, 16],
    "joystick_pins": {
        "up": "D5",
        "down": "D6",
        "left": "D16",
        "right": "D26",
        "select": "D13",
    },
}


def ensure_directories() -> None:
    """Create required directories if they do not already exist."""
    for directory in (STATE_DIR, LOG_DIR):
        directory.mkdir(parents=True, exist_ok=True)


def load_config() -> Dict[str, Any]:
    """Load configuration from disk with fallbacks to defaults."""
    ensure_directories()
    if CONFIG_FILE.exists():
        try:
            data = json.loads(CONFIG_FILE.read_text())
            merged = {**DEFAULT_CONFIG, **data}
            return merged
        except json.JSONDecodeError:
            # Fall back to defaults if corruption occurs
            pass
    return DEFAULT_CONFIG.copy()


def save_selected_preset(name: str) -> None:
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    (STATE_DIR / "selected_preset.json").write_text(json.dumps({"name": name}))


def load_selected_preset() -> str | None:
    target = STATE_DIR / "selected_preset.json"
    if not target.exists():
        return None
    try:
        data = json.loads(target.read_text())
    except json.JSONDecodeError:
        return None
    return data.get("name")
