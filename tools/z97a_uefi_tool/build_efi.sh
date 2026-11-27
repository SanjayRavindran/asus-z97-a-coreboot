#!/usr/bin/env bash
set -euo pipefail

# Build freestanding PE32+ UEFI apps for ASUS Z97-A:
#   1) z97a_tool.efi (diag + COM1 delta serflash server)
#   2) z97a_flash_<tag>.efi (optional embedded 6.5 MiB BIOS one-shot flasher)
#
# Usage: ./build_efi.sh [8mb_coreboot_rom] [tag]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${OUT_DIR:-$SCRIPT_DIR/out}"
mkdir -p "$OUT_DIR"

CFLAGS=(
  --target=x86_64-unknown-windows
  -ffreestanding
  -fno-stack-protector
  -fno-builtin
  -fno-exceptions
  -mno-red-zone
  -mstack-probe-size=1048576
  -Os
  -Wall
  -Wextra
  -Werror
)

echo "[1/2] Building z97a_tool.efi"
clang "${CFLAGS[@]}" -c "$SCRIPT_DIR/z97a_tool.c" -o "$OUT_DIR/z97a_tool.obj"
lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib \
  /out:"$OUT_DIR/z97a_tool.efi" "$OUT_DIR/z97a_tool.obj"
ls -lh "$OUT_DIR/z97a_tool.efi"

if [ $# -ge 1 ]; then
  ROM_PATH="$1"
  TAG="${2:-v012}"
  echo "[2/2] Building z97a_flash_${TAG}.efi from $ROM_PATH"
  python3 - "$ROM_PATH" "$OUT_DIR/bios_blob.bin" "$OUT_DIR/bios_blob.S" <<'PY'
import hashlib, pathlib, sys
rom = pathlib.Path(sys.argv[1]).read_bytes()
assert len(rom) == 8388608, f"Expected 8388608 B ROM, got {len(rom)}"
bios = rom[0x210000:0x800000]
assert len(bios) == 6225920
sha = hashlib.sha256(bios).digest()
pathlib.Path(sys.argv[2]).write_bytes(bios)
hex_bytes = ", ".join(f"0x{b:02x}" for b in sha)
asm = f"""\t.section .rdata,"dr"
\t.p2align 4
\t.globl z97a_bios_bin
\t.globl z97a_bios_end
\t.globl z97a_bios_sha256
z97a_bios_bin:
\t.incbin "{sys.argv[2]}"
z97a_bios_end:
z97a_bios_sha256:
\t.byte {hex_bytes}
"""
pathlib.Path(sys.argv[3]).write_text(asm)
print("Embedded BIOS SHA-256:", sha.hex())
PY
  clang "${CFLAGS[@]}" -DEMBED_BIOS_IMAGE -c "$SCRIPT_DIR/z97a_tool.c" -o "$OUT_DIR/z97a_flash.obj"
  clang --target=x86_64-unknown-windows -c "$OUT_DIR/bios_blob.S" -o "$OUT_DIR/bios_blob.obj"
  lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib \
    /out:"$OUT_DIR/z97a_flash_${TAG}.efi" "$OUT_DIR/z97a_flash.obj" "$OUT_DIR/bios_blob.obj"
  ls -lh "$OUT_DIR/z97a_flash_${TAG}.efi"
fi
