#!/bin/bash
set -e

BOARD_DIR=$(dirname "$0")
DTS="${BOARD_DIR}/rc522-overlay.dts"
DTBO="${BINARIES_DIR}/rpi-firmware/overlays/rc522-overlay.dtbo"

dtc -@ -I dts -O dtb -o "${DTBO}" "${DTS}"