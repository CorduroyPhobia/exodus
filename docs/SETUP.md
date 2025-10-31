# End-to-End Setup Guide

This guide describes how to transform the existing Windows-only application into a split Pi/PC system that delivers the same functionality while remaining "plug-and-play" for end users. The guide is divided into three phases: preparing the Raspberry Pi image, provisioning the Windows host, and verifying the combined workflow.

## 1. Prerequisites

### Hardware
- Raspberry Pi Zero 2W with 16GB (or larger) microSD card.
- USB Dongle Expansion Board capable of HID gadget mode.
- Waveshare 1.2" IPS LCD Display HAT with joystick/buttons.
- Windows 10/11 PC with USB-A port.

### Software & Files
- Raspberry Pi Imager (or Balena Etcher) on a separate PC.
- Raspberry Pi OS Lite image (64-bit recommended).
- The compiled Windows artifacts (`ai.exe`, DLLs, models, presets, `config.ini`, etc.) from the original project.
- This repository cloned locally.

## 2. Build the Pi Image

1. **Flash the base image**
   - Launch Raspberry Pi Imager and select Raspberry Pi OS Lite (64-bit).
   - Choose the target SD card.
   - Use the *Advanced Options* dialog to set hostname (e.g. `pi-ai`), enable SSH (for emergency access), and configure Wi-Fi credentials if desired.

2. **Pre-load repository contents**
   - After flashing, mount the `boot` partition of the SD card on your PC.
   - Copy the entire `pi_side/`, `pc_side/`, `models/`, `presets/`, and any additional assets (DLLs, executables) into a new top-level directory named `exodus`.
   - Ensure the directory structure on the card is:
     ```
     /boot/exodus/
         models/
         presets/
         windows_binaries/
             ai.exe
             onnxruntime.dll
             ...
         pi_side/
         pc_side/
         docs/
     ```
   - Eject the SD card safely.
   - (Optional but recommended) Create a `firstrun.sh` script in the `boot` partition with the following content to auto-enable the provisioning service:
     ```bash
     #!/bin/bash
     cp /boot/exodus/pi_side/systemd/piai-setup.service /etc/systemd/system/
     systemctl enable piai-setup.service
     systemctl start piai-setup.service
     rm /boot/firstrun.sh
     ```
     Mark the script as executable (`chmod +x firstrun.sh`). Raspberry Pi OS executes this script once on the first boot, ensuring the provisioning workflow runs without manual SSH steps.

3. **First boot automation**
   - Insert the card into the Pi Zero 2W and power it via USB (not yet connected to the Windows PC).
   - On boot, the `pi_side/setup/install.sh` script (enabled by `systemd` unit `piai-setup.service`) will:
     - Expand filesystem and update package lists.
     - Install required APT packages (`python3`, `python3-pip`, `python3-venv`, `git`, `st7789`, `python3-serial`, `python3-rpi.gpio`, etc.).
     - Create a Python virtual environment under `/opt/exodus-venv` and install pip dependencies listed in `pi_side/requirements.txt`.
     - Configure USB gadget mode using `pi_side/setup/usb-gadget.sh`.
     - Deploy the operational systemd service `piai.service`.
   - Progress and potential errors are mirrored onto the LCD screen for convenience.

> **Note:** The setup service runs only once. Subsequent boots skip the provisioning logic and go directly to the operational service.

## 3. Configure the Windows Host

1. **Initial connection**
   - Connect the Pi to the PC via the USB dongle board. The Pi enumerates as both a serial device and a USB Ethernet gadget.
   - The Pi automatically pushes `pc_side/installer` to the host via the emulated mass storage interface (or shared folder exposed over USB Ethernet).

2. **Run the installer**
   - Execute `pc_side/install_service.bat`. This script:
     - Installs Python (if not present) silently via the embedded installer or prompts the user.
     - Installs the Python dependencies (`requirements.txt`).
     - Copies the Windows binaries/DLLs into `%PROGRAMDATA%\ExodusAI\bin`.
     - Registers `exodus_service.py` as a Windows service using [NSSM](https://nssm.cc/) (bundled in `pc_side/tools`).
     - Creates firewall rules allowing only the USB gadget subnet.

3. **Service behaviour**
   - The Windows service listens for connections from the Pi on TCP port `27121` (configurable in `pc_side/config.yaml`).
   - Upon receiving a session request, it validates the preset file hash, syncs assets if necessary, launches `ai.exe`, and streams inference outputs back to the Pi.

## 4. Operational Workflow

1. On boot, the Pi loads the persisted preset selection (from `pi_side/state/selected_preset.json`).
2. The LCD displays the current preset and connection status. Joystick/button input opens the preset menu sourced from `/boot/exodus/presets`.
3. When the Pi is connected to the PC:
   - The USB gadget enumerates as a composite device: HID mouse + RNDIS Ethernet + serial console.
   - The Pi establishes a TCP socket to the Windows service and sends a `HELLO` handshake containing version information, preset metadata, and (if required) zipped binary payloads.
   - The Windows host runs the AI pipeline (`ai.exe`) in the background and streams mouse vector data back.
   - The Pi converts the vectors into HID reports and injects them into the USB gadget endpoint.
4. Disconnecting the Pi cleanly tears down the session; reconnecting restarts the handshake automatically.

## 5. Troubleshooting

- **LCD shows "PC NOT DETECTED"**: Ensure the USB cable supports data, the USB gadget service is active (`sudo systemctl status piai.service`), and Windows installed the RNDIS driver.
- **Windows service not running**: Check Event Viewer → Windows Logs → Application for entries from `ExodusAIService`.
- **High latency**: Verify the Pi and Windows host negotiated the `LAN 480Mbps` configuration. Lower the `FRAME_INTERVAL_MS` constant in `pi_side/main.py` if CPU usage allows.

## 6. Development Notes

- All configuration files live under `/boot/exodus`. To update presets or models, modify that directory on the SD card or via SCP.
- To replace the Windows binaries, drop updated files into `/boot/exodus/windows_binaries` and reboot the Pi; the sync logic detects new hashes and re-transfers.
- Logging:
  - Pi-side logs: `/var/log/piai/`.
  - PC-side logs: `%PROGRAMDATA%\ExodusAI\logs`.

Refer to `docs/ARCHITECTURE.md` for deep-dive design diagrams and communication protocol details.
