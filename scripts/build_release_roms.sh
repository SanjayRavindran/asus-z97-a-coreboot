#!/usr/bin/env bash
set -euo pipefail

# Build ONLY the 2 complete, ready-to-flash 8 MiB ASUS Z97-A ROM binaries:
#   1. asus-z97-a-coreboot-<RELEASE_TAG>-with-me.bin    (Stock ME 9.1.25.1005 + unlocked IFD)
#   2. asus-z97-a-coreboot-<RELEASE_TAG>-without-me.bin (Neutralized ME + AltMeDisable=1 + unlocked IFD)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="${ROOT_DIR:-$(cd "$SCRIPT_DIR/.." && pwd)}"
COREBOOT_DIR="${COREBOOT_DIR:-$ROOT_DIR/coreboot}"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/outputs}"
RELEASE_TAG="${RELEASE_TAG:-v0.15}"

mkdir -p "$OUT_DIR"
rm -f "$OUT_DIR"/*.bin

EDK2_CACHE_FD="$(mktemp /tmp/z97a-uefipayload.XXXXXX.fd)"
trap 'rm -f "$EDK2_CACHE_FD"' EXIT
rm -f "$EDK2_CACHE_FD"

for VARIANT in with-me without-me; do
  echo ""
  echo "=================================================================="
  echo " Building ASUS Z97-A variant: $VARIANT ($RELEASE_TAG)"
  echo "=================================================================="
  ROOT_DIR="$ROOT_DIR" \
  COREBOOT_DIR="$COREBOOT_DIR" \
  OUT_DIR="$OUT_DIR" \
  RELEASE_TAG="$RELEASE_TAG" \
  VARIANT_NAME="$VARIANT" \
  DEFCONFIG="$ROOT_DIR/configs/z97-a-8mb-${VARIANT}.defconfig" \
  EDK2_CACHE_FD="$EDK2_CACHE_FD" \
    bash "$ROOT_DIR/scripts/build.sh"
done

BIN_COUNT="$(find "$OUT_DIR" -maxdepth 1 -type f -name '*.bin' | wc -l)"
if [ "$BIN_COUNT" -ne 2 ]; then
  echo "ERROR: Expected exactly 2 .bin files in $OUT_DIR, found $BIN_COUNT" >&2
  exit 1
fi

echo ""
echo "=== BOTH RELEASE ROM BINARIES BUILT & VERIFIED ==="
(cd "$OUT_DIR" && ls -lh *.bin && sha256sum *.bin)
