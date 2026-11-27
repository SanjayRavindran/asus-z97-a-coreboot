#!/usr/bin/env bash
set -euo pipefail

# Portable reproducible build & verification script for ASUS Z97-A Coreboot + EDK2 + iPXE.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="${ROOT_DIR:-$(cd "$SCRIPT_DIR/.." && pwd)}"
COREBOOT_DIR="${COREBOOT_DIR:-$ROOT_DIR/coreboot}"
BOARD_SRC="${BOARD_SRC:-$ROOT_DIR/board/src/mainboard/asus/z97-a}"
VARIANT_NAME="${VARIANT_NAME:-with-me}"
DEFCONFIG="${DEFCONFIG:-$ROOT_DIR/configs/z97-a-8mb-${VARIANT_NAME}.defconfig}"
VBT_FILE="${VBT_FILE:-$ROOT_DIR/blobs/data.vbt}"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/outputs}"
RELEASE_TAG="${RELEASE_TAG:-v0.15}"
EDK2_CACHE_FD="${EDK2_CACHE_FD:-}"

if [ -d /opt/xgcc/bin ]; then
  export PATH="/opt/xgcc/bin:$PATH"
fi

if [ ! -d "$COREBOOT_DIR/src/mainboard/asus" ]; then
  echo "ERROR: COREBOOT_DIR='$COREBOOT_DIR' is not a valid coreboot checkout." >&2
  exit 1
fi
if [ ! -f "$DEFCONFIG" ]; then
  echo "ERROR: DEFCONFIG='$DEFCONFIG' not found." >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

echo "[1/6] Installing board port into $COREBOOT_DIR/src/mainboard/asus/z97-a"
rm -rf "$COREBOOT_DIR/src/mainboard/asus/z97-a"
cp -a "$BOARD_SRC" "$COREBOOT_DIR/src/mainboard/asus/z97-a"
cp -f "$VBT_FILE" "$COREBOOT_DIR/src/mainboard/asus/z97-a/data.vbt"

pushd "$COREBOOT_DIR" >/dev/null

echo "[2/6] Applying ASUS Z97-A chipset and libgfxinit patches"
for p in \
  "$ROOT_DIR/patches/0001-haswell-z97a-above4g-peg-dmi-gma.patch" \
  "$ROOT_DIR/patches/0002-lynxpoint-z97a-dmi-sata-pcie-aspm.patch"; do
  if git apply --check "$p" >/dev/null 2>&1; then
    git apply "$p"
    echo "Applied $(basename "$p")."
  elif git apply --reverse --check "$p" >/dev/null 2>&1; then
    echo "$(basename "$p") already applied."
  else
    echo "ERROR: Failed to apply $(basename "$p") cleanly on $COREBOOT_DIR" >&2
    exit 1
  fi
done

PATCH3="$ROOT_DIR/patches/0003-libgfxinit-z97a-port-limits.patch"
LIBGFX="$COREBOOT_DIR/3rdparty/libgfxinit"
if git -C "$LIBGFX" apply --check "$PATCH3" >/dev/null 2>&1; then
  git -C "$LIBGFX" apply "$PATCH3"
  echo "Applied $(basename "$PATCH3")."
elif git -C "$LIBGFX" apply --reverse --check "$PATCH3" >/dev/null 2>&1; then
  echo "$(basename "$PATCH3") already applied."
else
  echo "libgfxinit $(git -C "$LIBGFX" rev-parse --short HEAD) lacks CEA HDMI base; checking out libgfxinit 7ac11bc..."
  git -C "$LIBGFX" fetch --depth 1 origin 7ac11bc || git -C "$LIBGFX" fetch origin
  git -C "$LIBGFX" checkout -f 7ac11bc
  if git -C "$LIBGFX" apply --check "$PATCH3" >/dev/null 2>&1; then
    git -C "$LIBGFX" apply "$PATCH3"
    echo "Applied $(basename "$PATCH3") on libgfxinit 7ac11bc."
  else
    echo "ERROR: Failed to apply $(basename "$PATCH3") on libgfxinit" >&2
    exit 1
  fi
fi

echo "[3/6] Generating .config from $DEFCONFIG (variant=$VARIANT_NAME)"
sed -e "s|\"blobs/|\"$ROOT_DIR/blobs/|g" "$DEFCONFIG" > .config
sed -i '/^CONFIG_LOCALVERSION=/d' .config
echo "CONFIG_LOCALVERSION=\"-z97a-${RELEASE_TAG}-${VARIANT_NAME}\"" >> .config
make olddefconfig UPDATED_SUBMODULES=1

if [ -n "$EDK2_CACHE_FD" ] && [ -f "$EDK2_CACHE_FD" ] && [ "${FORCE_REBUILD_EDK2:-0}" != "1" ]; then
  mkdir -p build
  cp -f "$EDK2_CACHE_FD" build/UEFIPAYLOAD.fd
  touch build/UEFIPAYLOAD.fd
  echo "Reusing cached EDK2 payload: $EDK2_CACHE_FD"
fi

echo "[4/6] Building Coreboot + EDK2 + iPXE ($VARIANT_NAME)"
rm -f build/coreboot.rom build/coreboot.pre build/fmap.fmd build/fmap.fmap
make -j"$(nproc)" UPDATED_SUBMODULES=1

if [ -n "$EDK2_CACHE_FD" ] && [ -f build/UEFIPAYLOAD.fd ] && { [ ! -f "$EDK2_CACHE_FD" ] || [ "${FORCE_REBUILD_EDK2:-0}" = "1" ]; }; then
  mkdir -p "$(dirname "$EDK2_CACHE_FD")"
  cp -f build/UEFIPAYLOAD.fd "$EDK2_CACHE_FD"
  echo "Cached EDK2 payload -> $EDK2_CACHE_FD"
fi

echo "[5/6] Exporting complete 8 MiB ROM -> $OUT_DIR"
OUT_BIN="$OUT_DIR/asus-z97-a-coreboot-${RELEASE_TAG}-${VARIANT_NAME}.bin"
cp -f build/coreboot.rom "$OUT_BIN"

echo "[6/6] Structural & Config Validation ($(basename "$OUT_BIN"))"
IFDTOOL="$COREBOOT_DIR/build/util/ifdtool/ifdtool"
if [ ! -x "$IFDTOOL" ]; then
  if [ ! -x "$COREBOOT_DIR/util/ifdtool/ifdtool" ]; then
    make -C "$COREBOOT_DIR/util/ifdtool" -j"$(nproc)"
  fi
  IFDTOOL="$COREBOOT_DIR/util/ifdtool/ifdtool"
fi

IFD_OUT="$("$IFDTOOL" -d "$OUT_BIN")"
echo "$IFD_OUT" | grep -E "FLREG|FLMSTR|FLCOMP|JID1|PCHSTRP9|PCHSTRP18|AltMeDisable"

echo "$IFD_OUT" | grep -q "FLMSTR1:   0xffff0000"
echo "$IFD_OUT" | grep -q "FLCOMP     0x64900044"
echo "$IFD_OUT" | grep -q "JID1:  0x001740ef"
echo "$IFD_OUT" | grep -q "PCHSTRP9  : 0xf0544a85"
echo "$IFD_OUT" | grep -q "PCHSTRP18 : 0x00000003"
if [ "$VARIANT_NAME" = "without-me" ]; then
  echo "$IFD_OUT" | grep -q "AltMeDisable bit is set"
else
  echo "$IFD_OUT" | grep -q "AltMeDisable bit is not set"
fi

python3 - "$OUT_BIN" "$VARIANT_NAME" <<'PY'
import pathlib, struct, sys

rom_path, variant = sys.argv[1], sys.argv[2]
rom = pathlib.Path(rom_path).read_bytes()
assert len(rom) == 8388608, f"Expected 8388608 B ROM, got {len(rom)}"

mac = rom[0x1000:0x1006].hex(":")
print(f"FREG3 GbE MAC: {mac}")

me = rom[0x3000:0x180000]
assert me[0x10:0x14] == b"$FPT", f"Missing ME $FPT signature at 0x3010 (got {me[0x10:0x14]!r})"
assert b"FTPR" in me[:0x1000], "Missing FTPR partition entry in ME $FPT table"
print(f"ME $FPT & FTPR verified for variant '{variant}'.")
PY

CBFS_LIST="$(./build/cbfstool "$OUT_BIN" print)"
echo "$CBFS_LIST"
for entry in cpu_microcode_blob.bin fallback/romstage fallback/ramstage fallback/dsdt.aml vbt.bin fallback/payload bootblock; do
  echo "$CBFS_LIST" | grep -q "$entry" || { echo "ERROR: Missing CBFS entry '$entry'" >&2; exit 1; }
done

TMP_CFG="$(mktemp)"
./build/cbfstool "$OUT_BIN" extract -n config -f "$TMP_CFG" -U
grep -q "^# CONFIG_PCIEXP_ASPM is not set$" "$TMP_CFG"
grep -q "^# CONFIG_PCIEXP_CLK_PM is not set$" "$TMP_CFG"
grep -q "^# CONFIG_PCIEXP_L1_SUB_STATE is not set$" "$TMP_CFG"
grep -q "^CONFIG_EDK2_ENABLE_IPXE=y$" "$TMP_CFG"
rm -f "$TMP_CFG"

popd >/dev/null
echo "Verified ready-to-flash 8 MiB ROM: $OUT_BIN"
