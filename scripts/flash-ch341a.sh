#!/usr/bin/env bash
set -euo pipefail

# Flash a complete 8 MiB ASUS Z97-A Coreboot .bin image onto a W25Q64FV chip
# using an external CH341A SPI programmer and enforce Winbond QE=1 (SR2=0x02).

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 <asus-z97-a-coreboot-*.bin>" >&2
  exit 1
fi

ROM="$1"
if [ ! -f "$ROM" ]; then
  echo "ERROR: File not found: $ROM" >&2
  exit 1
fi

SZ="$(wc -c < "$ROM")"
if [ "$SZ" -ne 8388608 ]; then
  echo "ERROR: Expected exact 8,388,608-byte 8 MiB ROM, got $SZ bytes." >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "[1/2] Flashing and verifying $ROM via ch341a_spi..."
flashrom -p ch341a_spi -c "W25Q64BV/W25Q64CV/W25Q64FV" -w "$ROM" -v "$ROM"

echo "[2/2] Enforcing Winbond W25Q64FV non-volatile SR1=0x00, SR2=0x02 (QE=1)..."
python3 "$SCRIPT_DIR/ch341a-w25q64-qe.py" --set-qe

echo "Done. Chip is flashed, verified, and QE=1 enabled for ASUS Z97-A."
