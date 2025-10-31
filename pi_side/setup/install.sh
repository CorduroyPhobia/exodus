#!/usr/bin/env bash
set -euo pipefail

LOG_FILE="/var/log/piai/install.log"
mkdir -p "$(dirname "$LOG_FILE")"
exec > >(tee -a "$LOG_FILE") 2>&1

echo "[piai-setup] Starting provisioning"

apt-get update
apt-get install -y python3 python3-venv python3-pip python3-dev git libusb-1.0-0-dev rsync

mkdir -p /boot/exodus/state

python3 -m venv /opt/exodus-venv
/opt/exodus-venv/bin/pip install --upgrade pip
/opt/exodus-venv/bin/pip install -r /boot/exodus/pi_side/requirements.txt

SITE_PACKAGES=$(/opt/exodus-venv/bin/python -c "import site; print(site.getsitepackages()[0])")
mkdir -p "$SITE_PACKAGES/pi_side"
rsync -a /boot/exodus/pi_side/ "$SITE_PACKAGES/pi_side/"

bash /boot/exodus/pi_side/setup/usb-gadget.sh

install -m 755 /boot/exodus/pi_side/systemd/piai.service /etc/systemd/system/piai.service
systemctl daemon-reload
systemctl enable piai.service

touch /boot/exodus/state/.setup_complete
systemctl disable piai-setup.service || true

reboot
