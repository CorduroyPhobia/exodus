"""Async communication client for talking to the Windows service."""
from __future__ import annotations

import asyncio
import json
import logging
from dataclasses import dataclass
from typing import Any, AsyncIterator, Dict, Optional

from . import preset_manager

LOGGER = logging.getLogger(__name__)


@dataclass
class SessionInfo:
    preset: preset_manager.Preset
    version: str
    secret: str

    def to_dict(self) -> Dict[str, Any]:
        return {
            "type": "HELLO",
            "version": self.version,
            "secret": self.secret,
            "preset": {
                "name": self.preset.name,
                "sha256": self.preset.sha256,
            },
        }


class CommunicationClient:
    def __init__(self, host: str, port: int, loop: Optional[asyncio.AbstractEventLoop] = None) -> None:
        self.host = host
        self.port = port
        self._loop = loop or asyncio.get_event_loop()
        self._writer: Optional[asyncio.StreamWriter] = None
        self._reader: Optional[asyncio.StreamReader] = None

    async def connect(self) -> None:
        LOGGER.info("Connecting to %s:%s", self.host, self.port)
        self._reader, self._writer = await asyncio.open_connection(self.host, self.port)

    async def close(self) -> None:
        if self._writer:
            self._writer.close()
            await self._writer.wait_closed()
        self._writer = None
        self._reader = None

    async def send_json(self, payload: Dict[str, Any]) -> None:
        if not self._writer:
            raise RuntimeError("Not connected")
        data = json.dumps(payload).encode("utf-8") + b"\n"
        self._writer.write(data)
        await self._writer.drain()

    async def stream_mouse(self) -> AsyncIterator[Dict[str, Any]]:
        if not self._reader:
            raise RuntimeError("Not connected")
        while True:
            line = await self._reader.readline()
            if not line:
                break
            try:
                payload = json.loads(line)
            except json.JSONDecodeError:
                LOGGER.warning("Malformed message: %s", line)
                continue
            if payload.get("type") == "MOUSE":
                yield payload

    async def request_sync(self, files: Dict[str, str]) -> None:
        await self.send_json({"type": "SYNC", "files": files})


async def run_handshake(session: SessionInfo, client: CommunicationClient) -> AsyncIterator[Dict[str, Any]]:
    await client.connect()
    await client.send_json(session.to_dict())
    async for payload in client.stream_mouse():
        yield payload
