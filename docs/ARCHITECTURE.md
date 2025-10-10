# System Architecture

The redesigned solution splits responsibilities between the Raspberry Pi Zero 2W and the Windows PC while keeping the original AI execution path untouched. The diagrams and explanations below describe the main components.

## Top-Level Overview

```
+--------------------+        USB (HID + RNDIS)       +---------------------+
| Raspberry Pi Zero  | <----------------------------> | Windows 10/11 Host  |
| 2W (Pi-Side Agent) |                                  | (AI Executor)       |
+--------------------+                                  +---------------------+
| - Waveshare LCD UI |                                  | - ai.exe            |
| - Preset selection |                                  | - ONNX Runtime DLLs |
| - USB HID mouse    |                                  | - DirectML DLLs     |
| - File synchronizer|                                  | - Windows service   |
| - TCP client       |                                  | - Screen capture    |
+--------------------+                                  +---------------------+
```

1. The Pi boots and configures itself as a composite USB gadget (HID mouse + Ethernet + serial).
2. The LCD menu allows the user to select a preset stored on the Pi's SD card.
3. After connection, the Pi establishes a TCP session to the Windows service running locally.
4. The Pi streams control commands (selected preset hash, optional asset bundles) and receives mouse vectors.
5. The HID engine on the Pi injects the vectors into the USB endpoint, producing real mouse movement on the PC.
6. The Windows service launches `ai.exe` with the transferred preset and required dependencies, captures its output, and relays it to the Pi.

## Pi-Side Modules

### `pi_side/main.py`
Central orchestration loop:
- Keeps track of connection state.
- Coordinates preset manager, LCD UI, file synchronization, network protocol, and HID output.
- Provides resilience (auto-reconnect, exponential back-off, and health LED).

### `pi_side/lcd_menu.py`
- Uses the Waveshare ST7789 display + joystick.
- Implements a non-blocking UI loop allowing the user to browse presets, view status messages, and trigger re-sync.
- UI updates run on a dedicated thread to prevent blocking communications.

### `pi_side/preset_manager.py`
- Scans `/boot/exodus/presets` for `.ini` and `.json` files.
- Persists the selected preset into `/boot/exodus/state/selected_preset.json`.
- Computes SHA256 digests so the Windows side can detect when transfers are required.

### `pi_side/communication.py`
- Manages TCP socket to the Windows service.
- Implements a line-delimited JSON protocol:
  - `HELLO`: send version, preset metadata, binary hashes.
  - `SYNC_REQUEST`: Windows host requests updated assets.
  - `MOUSE`: Windows host sends `[dx, dy, buttons]` deltas.
  - `HEARTBEAT`: exchanged every 5 seconds to monitor health.
- Chunked binary transfers use a simple header (`FILE <name> <size> <sha256>`), followed by raw bytes.

### `pi_side/hid_mouse.py`
- Wraps Linux gadget FS endpoints (`/dev/hidg0`).
- Converts high-level movement vectors into HID reports (4-byte structure: buttons, x, y, wheel).
- Smooths the movement to avoid overflows.

### `pi_side/setup` scripts
- `install.sh`: invoked once by systemd to bootstrap the Pi.
- `usb-gadget.sh`: configures the composite gadget (HID + RNDIS + Serial + Mass Storage).
- `post-setup.service`: disables itself after successful provisioning.

## Windows-Side Components

### `pc_side/exodus_service.py`
- Runs as a Windows service (via `nssm.exe`).
- Opens a TCP server on `0.0.0.0:27121` restricted to the USB gadget subnet.
- On connection:
  - Validates the Pi agent version.
  - Compares expected file hashes, requesting updates if mismatched.
  - Launches `ai.exe` with `--preset <path>` (configurable) in hidden mode.
  - Streams mouse deltas to the Pi in real time.
- Uses `asyncio` to handle multiple concurrent Pi devices (future-proofing).

### `pc_side/install_service.bat`
- Installs prerequisites, copies assets, registers the service, and can be re-run safely.
- Accepts silent arguments for enterprise deployments.

### `pc_side/requirements.txt`
- `pyyaml`, `pywin32`, `psutil`, `websockets`, `pyserial`, `aiofiles`.

### `pc_side/config.yaml`
Configurable parameters:
- Paths for binaries, models, presets.
- TCP port, allowed CIDR.
- Logging location.
- Retry/back-off intervals.

## Communication Protocol

- Transport: TCP over USB Ethernet (RNDIS) or fallback to serial.
- Message encoding: JSON lines (`{"type": "HELLO", ...}`) for control; binary for large transfers.
- Security: HMAC with shared secret stored in `/boot/exodus/pi_side/config.yaml` and `%PROGRAMDATA%/ExodusAI/config.yaml`.
- Latency target: < 50 ms per round trip. Achieved by streaming inference results as soon as they are produced by `ai.exe`.

## Fault Tolerance

- **Connection loss:** The Pi detects socket drops, displays a warning, and retries every 2 seconds. HID output stops immediately.
- **Preset updates:** SHA256 mismatches trigger automatic file transfers before starting a session.
- **Windows service restart:** Pi falls back to handshake state and notifies the user on the LCD.

## Future Enhancements

- Support multiple presets simultaneously (per profile) by staging multiple processes on Windows.
- OTA updates for Pi scripts via signed tarball distribution.
- Telemetry integration with secure MQTT broker.
