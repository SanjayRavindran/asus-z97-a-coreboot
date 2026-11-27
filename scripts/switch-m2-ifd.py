#!/usr/bin/env python3
"""
Switch ASUS Z97-A Intel Flash Descriptor (IFD) PCH Soft Straps between:
  - pcie-x1 : PCIEX1_1 + PCIEX1_2 active (stock default: PCHSTRP9 0x124 bits[3:2]=01b)
  - m2-x2   : M.2 Socket 3 PCIe x2 active (PCHSTRP9 0x124 bits[3:2]=10b; disables PCIEX1_1/2)

Matches the exact IFD strap rewrite logic extracted from stock PcieLaneDXE.
Supports either a 4 KiB IFD slice or a full 8 MiB SPI image (both have IFD at offset 0x0000..0x0fff).
"""

import argparse
import pathlib
import struct
import sys

IFD_SIG_OFFSET = 0x10
IFD_SIG_MAGIC = 0x0FF0A55A
PCHSTRP9_OFF = 0x124
PCHSTRP18_OFF = 0x148


def decode_mode(data: bytes) -> tuple[int, int, str]:
    if len(data) < 0x1000:
        raise ValueError(f"Image too small ({len(data)} bytes); expected >= 4096 bytes")
    sig = struct.unpack_from("<I", data, IFD_SIG_OFFSET)[0]
    if sig != IFD_SIG_MAGIC:
        raise ValueError(f"Invalid IFD signature 0x{sig:08x} at 0x10 (expected 0x{IFD_SIG_MAGIC:08x})")
    strp9 = struct.unpack_from("<I", data, PCHSTRP9_OFF)[0]
    strp18 = struct.unpack_from("<I", data, PCHSTRP18_OFF)[0]
    pciepcs2 = (strp9 >> 2) & 0x3
    if pciepcs2 == 0x1:
        mode = "pcie-x1 (PCIEX1_1 + PCIEX1_2 enabled)"
    elif pciepcs2 == 0x2:
        mode = "m2-x2 (M.2 Socket 3 PCIe x2 enabled; PCIEX1_1/2 disabled)"
    else:
        mode = f"other (PCIEPCS2=0x{pciepcs2:x})"
    return strp9, strp18, mode


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect or switch ASUS Z97-A IFD M.2 vs PCIEX1_1/2 mode")
    parser.add_argument("--input", "-i", required=True, help="Input 4 KiB IFD slice or 8 MiB ROM")
    parser.add_argument("--output", "-o", help="Output image path (required unless --inspect)")
    parser.add_argument("--mode", "-m", choices=["pcie-x1", "m2-x2"], help="Target PCIe/M.2 mux mode")
    parser.add_argument("--inspect", action="store_true", help="Inspect current IFD strap mode only")
    args = parser.parse_args()

    in_path = pathlib.Path(args.input).resolve()
    data = bytearray(in_path.read_bytes())
    strp9, strp18, mode_desc = decode_mode(data)
    print(f"Input: {in_path} ({len(data)} B)")
    print(f"  PCHSTRP9  (IFD+0x{PCHSTRP9_OFF:03x}) = 0x{strp9:08x}")
    print(f"  PCHSTRP18 (IFD+0x{PCHSTRP18_OFF:03x}) = 0x{strp18:08x}")
    print(f"  Detected mode              = {mode_desc}")

    if args.inspect:
        return 0

    if not args.mode or not args.output:
        parser.error("--mode and --output are required unless --inspect is specified")

    out_path = pathlib.Path(args.output).resolve()
    if out_path == in_path:
        parser.error("Refusing to overwrite input file in-place; specify a different --output path")

    new_strp9 = (strp9 & ~0x0C) | (0x04 if args.mode == "pcie-x1" else 0x08)
    struct.pack_into("<I", data, PCHSTRP9_OFF, new_strp9)
    out_path.write_bytes(data)

    verify9, verify18, verify_desc = decode_mode(data)
    print(f"Output: {out_path} ({len(data)} B)")
    print(f"  PCHSTRP9  (IFD+0x{PCHSTRP9_OFF:03x}) = 0x{verify9:08x}")
    print(f"  PCHSTRP18 (IFD+0x{PCHSTRP18_OFF:03x}) = 0x{verify18:08x}")
    print(f"  New mode                   = {verify_desc}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
