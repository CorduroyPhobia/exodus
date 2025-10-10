"""Preset scanning and persistence utilities."""
from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional

from . import config


@dataclass
class Preset:
    name: str
    path: Path
    sha256: str

    @classmethod
    def from_path(cls, path: Path) -> "Preset":
        return cls(name=path.stem, path=path, sha256=sha256_file(path))


def sha256_file(path: Path, chunk_size: int = 65536) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(chunk_size), b""):
            digest.update(chunk)
    return digest.hexdigest()


def list_presets(directory: Optional[Path] = None) -> List[Preset]:
    directory = directory or config.PRESETS_DIR
    presets: List[Preset] = []
    if not directory.exists():
        return presets
    for entry in sorted(directory.iterdir()):
        if entry.is_file() and entry.suffix.lower() in {".ini", ".json"}:
            presets.append(Preset.from_path(entry))
    return presets


def serialize_presets(presets: Iterable[Preset]) -> List[Dict[str, str]]:
    return [
        {
            "name": preset.name,
            "path": str(preset.path),
            "sha256": preset.sha256,
        }
        for preset in presets
    ]


def load_state() -> Dict[str, str]:
    state_file = config.STATE_DIR / "selected_preset.json"
    if not state_file.exists():
        return {}
    try:
        return json.loads(state_file.read_text())
    except json.JSONDecodeError:
        return {}


def save_state(name: str) -> None:
    config.save_selected_preset(name)
