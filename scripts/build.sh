#!/usr/bin/env bash
set -euo pipefail

# Build script for ASUS Z97-A Coreboot + EDK2 UefiPayloadPkg 8 MiB SPI ROM.
ROOT_DIR="${ROOT_DIR:-$HOME/coreboot-work/z97-a}"
COREBOOT_DIR="${COREBOOT_DIR:-$ROOT_DIR/coreboot}"
BOARD_SRC="${BOARD_SRC:-$ROOT_DIR/board/src/mainboard/asus/z97-a}"
DEFCONFIG="${DEFCONFIG:-$ROOT_DIR/configs/z97-a-8mb-stock-me.defconfig}"
VBT_FILE="${VBT_FILE:-$ROOT_DIR/blobs/data.vbt}"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/outputs}"
RELEASE_TAG="${RELEASE_TAG:-v26.11.1}"
VARIANT_NAME="${VARIANT_NAME:-8mb-stock-me}"
EDK2_CACHE_FD="${EDK2_CACHE_FD:-$ROOT_DIR/blobs/UEFIPAYLOAD-z97a-ipxe-release-compact.fd}"

if [ -d /opt/xgcc/bin ]; then
  export PATH="/opt/xgcc/bin:$PATH"
fi

if [ ! -d "$COREBOOT_DIR/src/mainboard/asus" ]; then
  echo "ERROR: COREBOOT_DIR='$COREBOOT_DIR' is not a valid coreboot checkout." >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

echo "[1/5] Installing board port into $COREBOOT_DIR/src/mainboard/asus/z97-a"
rm -rf "$COREBOOT_DIR/src/mainboard/asus/z97-a"
cp -a "$BOARD_SRC" "$COREBOOT_DIR/src/mainboard/asus/z97-a"
cp -f "$VBT_FILE" "$COREBOOT_DIR/src/mainboard/asus/z97-a/data.vbt"

pushd "$COREBOOT_DIR" >/dev/null

echo "[2/5] Generating .config from $DEFCONFIG"
sed -e "s|\"blobs/|\"$ROOT_DIR/blobs/|g" "$DEFCONFIG" > .config
make olddefconfig UPDATED_SUBMODULES=1

if [ -n "$EDK2_CACHE_FD" ] && [ -f "$EDK2_CACHE_FD" ] && [ "${FORCE_REBUILD_EDK2:-0}" != "1" ]; then
  mkdir -p build
  cp -f "$EDK2_CACHE_FD" build/UEFIPAYLOAD.fd
  touch build/UEFIPAYLOAD.fd
  echo "Reusing cached EDK2 payload: $EDK2_CACHE_FD"
fi

echo "[3/5] Building Coreboot + EDK2"
rm -f build/coreboot.rom build/coreboot.pre build/fmap.fmd build/fmap.fmap
make -j"$(nproc)" UPDATED_SUBMODULES=1

if [ -n "$EDK2_CACHE_FD" ] && [ -f build/UEFIPAYLOAD.fd ] && { [ ! -f "$EDK2_CACHE_FD" ] || [ "${FORCE_REBUILD_EDK2:-0}" = "1" ]; }; then
  mkdir -p "$(dirname "$EDK2_CACHE_FD")"
  cp -f build/UEFIPAYLOAD.fd "$EDK2_CACHE_FD"
  echo "Cached EDK2 payload -> $EDK2_CACHE_FD"
fi

echo "[4/5] Exporting full 8 MiB ROM & layout"
BASE_NAME="z97-a-coreboot-${RELEASE_TAG}-${VARIANT_NAME}"
OUT_FULL="$OUT_DIR/${BASE_NAME}-full.rom"
OUT_BIOS="$OUT_DIR/${BASE_NAME}-bios-region.bin"
LAYOUT_FILE="$OUT_DIR/cbfs-layout-${VARIANT_NAME}.txt"

cp -f build/coreboot.rom "$OUT_FULL"
python3 - "$OUT_FULL" "$OUT_BIOS" <<'PY'
import pathlib, sys
rom = pathlib.Path(sys.argv[1]).read_bytes()
assert len(rom) == 8388608, f"Expected 8388608 B ROM, got {len(rom)}"
pathlib.Path(sys.argv[2]).write_bytes(rom[0x180000:])
print(f"Full ROM: {len(rom)} B -> {sys.argv[1]}")
print(f"BIOS slice: {len(rom[0x180000:])} B -> {sys.argv[2]}")
PY

{
  echo "=== CBFS PRINT ($BASE_NAME) ==="
  ./build/cbfstool build/coreboot.rom print
  echo ""
  echo "=== FMAP LAYOUT ($BASE_NAME) ==="
  ./build/cbfstool build/coreboot.rom layout -w
} > "$LAYOUT_FILE"

echo "[5/5] Structural Validation ($BASE_NAME)"
if [ -z "${IFDTOOL:-}" ]; then
  if [ -x "$COREBOOT_DIR/build/util/ifdtool/ifdtool" ]; then
    IFDTOOL="$COREBOOT_DIR/build/util/ifdtool/ifdtool"
  else
    IFDTOOL="$HOME/coreboot-work/h61m-s1-plus/util/ifdtool/ifdtool"
  fi
fi
if [ -z "${MEC:-}" ]; then
  if [ -f "$COREBOOT_DIR/util/me_cleaner/me_cleaner.py" ]; then
    MEC="$COREBOOT_DIR/util/me_cleaner/me_cleaner.py"
  else
    MEC="$HOME/coreboot-work/h61m-s1-plus/util/me_cleaner/me_cleaner.py"
  fi
fi
"$IFDTOOL" -d "$OUT_FULL" | grep -E "File |FLREG|FLMSTR|Density|AltMeDisable|Vendor ID|Device ID"
python3 "$MEC" -c "$OUT_FULL"
python3 - "$OUT_FULL" <<'PY'
import pathlib, sys
rom = pathlib.Path(sys.argv[1]).read_bytes()
mac = rom[0x1000:0x1006].hex(":")
print("GbE MAC at 0x1000:", mac)
PY

popd >/dev/null
echo "Done: $OUT_FULL"
