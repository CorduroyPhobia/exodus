#!/usr/bin/env bash
set -euo pipefail

GADGET_DIR="/sys/kernel/config/usb_gadget/exodus"

if [ ! -d /sys/kernel/config/usb_gadget ]; then
  modprobe libcomposite
fi

mkdir -p "$GADGET_DIR"
cd "$GADGET_DIR"

echo 0x1d6b > idVendor
echo 0x0104 > idProduct
mkdir -p strings/0x409

echo "1337" > strings/0x409/serialnumber
echo "Exodus" > strings/0x409/manufacturer
echo "Exodus AI Interface" > strings/0x409/product

mkdir -p configs/c.1/strings/0x409
echo "Exodus Config" > configs/c.1/strings/0x409/configuration
echo 120 > configs/c.1/MaxPower

mkdir -p functions/hid.usb0
cat <<EOF2 > functions/hid.usb0/report_desc
\x05\x01\x09\x02\xA1\x01\x09\x01\xA1\x00\x05\x09\x19\x01\x29\x03\x15\x00\x25\x01\x95\x03\x75\x01\x81\x02\x95\x01\x75\x05\x81\x03\x05\x01\x09\x30\x09\x31\x09\x38\x15\x81\x25\x7F\x75\x08\x95\x03\x81\x06\xC0\xC0
EOF2
echo 8 > functions/hid.usb0/report_length
ln -sf functions/hid.usb0 configs/c.1/

mkdir -p functions/acm.usb0
ln -sf functions/acm.usb0 configs/c.1/

mkdir -p functions/rndis.usb0
ln -sf functions/rndis.usb0 configs/c.1/

if [ -d /sys/class/udc ]; then
  ls /sys/class/udc > UDC
fi
