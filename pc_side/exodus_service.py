"""Windows service wrapper that bridges the Pi agent and ai.exe."""
from __future__ import annotations

import asyncio
import json
import logging
import os
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import win32event
import win32service
import win32serviceutil
import yaml

CONFIG_PATH = Path(__file__).resolve().parent / "config.yaml"

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(name)s: %(message)s")
LOGGER = logging.getLogger("exodus_pc")


@dataclass
class Settings:
    version: str
    listen_host: str
    listen_port: int
    allowed_cidr: str
    program_data: Path
    binary_dir: Path
    preset_dir: Path
    model_dir: Path
    log_dir: Path
    handshake_secret: str
    ai_executable: Path
    heartbeat_timeout: int

    @classmethod
    def load(cls) -> "Settings":
        data = yaml.safe_load(CONFIG_PATH.read_text())
        return cls(
            version=data["version"],
            listen_host=data["listen_host"],
            listen_port=int(data["listen_port"]),
            allowed_cidr=data["allowed_cidr"],
            program_data=Path(data["program_data"]),
            binary_dir=Path(data["binary_dir"]),
            preset_dir=Path(data["preset_dir"]),
            model_dir=Path(data["model_dir"]),
            log_dir=Path(data["log_dir"]),
            handshake_secret=data["handshake_secret"],
            ai_executable=Path(data["ai_executable"]),
            heartbeat_timeout=int(data["heartbeat_timeout"]),
        )


async def handle_client(reader: asyncio.StreamReader, writer: asyncio.StreamWriter, settings: Settings) -> None:
    peer = writer.get_extra_info("peername")
    LOGGER.info("Connection from %s", peer)
    preset_path: Optional[Path] = None
    process: Optional[subprocess.Popen] = None
    try:
        while True:
            line = await reader.readline()
            if not line:
                break
            message = json.loads(line)
            if message.get("type") == "HELLO":
                if message.get("secret") != settings.handshake_secret:
                    LOGGER.warning("Rejected client with invalid secret")
                    break
                if message.get("version") != settings.version:
                    LOGGER.warning("Version mismatch: expected %s, received %s", settings.version, message.get("version"))
                    break
                preset_name = message["preset"]["name"]
                preset_path = settings.preset_dir / f"{preset_name}.ini"
                if not preset_path.exists():
                    LOGGER.warning("Preset missing: %s", preset_path)
                    break
                process = launch_ai(settings, preset_path)
                writer.write(json.dumps({"type": "ACK", "status": "READY"}).encode("utf-8") + b"\n")
                await writer.drain()
                await stream_ai_output(process, writer)
                break
            elif message.get("type") == "HEARTBEAT":
                continue
    finally:
        if process:
            terminate_process(process)
        writer.close()
        await writer.wait_closed()


def launch_ai(settings: Settings, preset_path: Path) -> subprocess.Popen:
    cmd = [str(settings.ai_executable), "--preset", str(preset_path)]
    LOGGER.info("Launching %s", cmd)
    return subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


async def stream_ai_output(process: subprocess.Popen, writer: asyncio.StreamWriter) -> None:
    if not process.stdout:
        return
    while True:
        line = await asyncio.get_event_loop().run_in_executor(None, process.stdout.readline)
        if not line:
            break
        try:
            dx, dy, buttons = map(int, line.strip().split(","))
        except ValueError:
            continue
        payload = json.dumps({"type": "MOUSE", "dx": dx, "dy": dy, "buttons": buttons}) + "\n"
        try:
            writer.write(payload.encode("utf-8"))
            await writer.drain()
        except ConnectionResetError:
            break


def terminate_process(process: subprocess.Popen) -> None:
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()


async def run_server(settings: Settings) -> None:
    server = await asyncio.start_server(
        lambda r, w: handle_client(r, w, settings), settings.listen_host, settings.listen_port
    )
    async with server:
        await server.serve_forever()


class ExodusService(win32serviceutil.ServiceFramework):
    _svc_name_ = "ExodusAIService"
    _svc_display_name_ = "Exodus AI Automation Service"
    _svc_description_ = "Handles communication with Raspberry Pi agent and executes ai.exe"

    def __init__(self, args):
        super().__init__(args)
        self.hWaitStop = win32event.CreateEvent(None, 0, 0, None)
        self.loop: Optional[asyncio.AbstractEventLoop] = None

    def SvcStop(self) -> None:  # pragma: no cover - service entry point
        self.ReportServiceStatus(win32service.SERVICE_STOP_PENDING)
        win32event.SetEvent(self.hWaitStop)
        if self.loop:
            self.loop.call_soon_threadsafe(self.loop.stop)

    def SvcDoRun(self) -> None:  # pragma: no cover - service entry point
        settings = Settings.load()
        os.makedirs(settings.log_dir, exist_ok=True)
        self.loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self.loop)
        try:
            self.loop.run_until_complete(run_server(settings))
        finally:
            self.loop.close()


if __name__ == "__main__":
    win32serviceutil.HandleCommandLine(ExodusService)
