#!/usr/bin/env python3
"""
Host driver for ASUS Z97-A Pre-OS COM1 serial delta flashing & diagnostics.
Runs on 192.168.1.90 (/dev/ttyUSB0, 115200 8N1) or locally via SSH wrapper.

Commands:
  info              Query SPI lock state & current BIOS region SHA-256
  diag              Run pre-OS hardware & 16 MiB SATA stress test via serflash
  flash <8mb.rom>   Delta-flash changed 4 KiB sectors in 0x210000..0x7fffff + verify SHA-256
  reboot            Cold-reset target board via CF9=0x0E
"""

import argparse
import hashlib
import struct
import sys
import time
import zlib

import serial

BIOS_BASE = 0x210000
BIOS_END = 0x800000
BIOS_SIZE = BIOS_END - BIOS_BASE
SECTOR_SIZE = 4096
NUM_SECTORS = BIOS_SIZE // SECTOR_SIZE


def read_exact(ser: serial.Serial, n: int, timeout: float = 15.0) -> bytes:
    buf = bytearray()
    deadline = time.time() + timeout
    while len(buf) < n:
        if time.time() > deadline:
            raise TimeoutError(f"Expected {n} bytes, got {len(buf)}: {bytes(buf[:32])!r}")
        chunk = ser.read(n - len(buf))
        if chunk:
            buf.extend(chunk)
    return bytes(buf)


def enter_serflash_if_shell(ser: serial.Serial) -> None:
    """Ping serflash; if unresponsive, check if we are at UEFI Shell> / tool prompt."""
    ser.reset_input_buffer()
    ser.write(b"Z9PING")
    time.sleep(0.15)
    resp = ser.read(64)
    if b"PONG" in resp:
        return
    # Try waking Z97A_SERFLASH_READY or launching from Shell>
    ser.write(b"s")
    time.sleep(0.2)
    ser.write(b"Z9PING")
    time.sleep(0.15)
    resp = ser.read(64)
    if b"PONG" in resp:
        return
    raise RuntimeError(
        "Target did not respond PONG to Z9PING. Ensure z97a_tool.efi serflash is running on target."
    )


def cmd_info(ser: serial.Serial) -> tuple[int, int, bytes]:
    enter_serflash_if_shell(ser)
    ser.write(b"Z9INFO")
    hdr = read_exact(ser, 4, timeout=20.0)
    if hdr != b"INF0":
        raise RuntimeError(f"INFO failed: {hdr!r}")
    meta = read_exact(ser, 3, timeout=5.0)
    bc, hsfs = struct.unpack("<BH", meta)
    sha = read_exact(ser, 32, timeout=10.0)
    print(f"BIOS_CNTL=0x{bc:02x} HSFS=0x{hsfs:04x} BIOS_SHA256={sha.hex()}")
    return bc, hsfs, sha


def cmd_diag(ser: serial.Serial) -> None:
    enter_serflash_if_shell(ser)
    ser.write(b"Z9DIAG")
    buf = bytearray()
    deadline = time.time() + 60.0
    while time.time() < deadline:
        chunk = ser.read(256)
        if chunk:
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
            buf.extend(chunk)
            if b"DIAG_END" in buf:
                return
    raise TimeoutError("Timed out waiting for DIAG_END")


def cmd_flash(ser: serial.Serial, rom_path: str, auto_reboot: bool) -> None:
    with open(rom_path, "rb") as f:
        rom = f.read()
    if len(rom) != 8388608:
        raise ValueError(f"Expected 8388608 B ROM, got {len(rom)}")
    bios = rom[BIOS_BASE:BIOS_END]
    target_sha = hashlib.sha256(bios).digest()
    print(f"Target BIOS SHA-256 ({len(bios)} B): {target_sha.hex()}")

    _, _, cur_sha = cmd_info(ser)
    if cur_sha == target_sha:
        print("SPI BIOS region already matches target SHA-256!")
        if auto_reboot:
            cmd_reboot(ser)
        return

    print("Fetching 1664 sector CRC32s over COM1...")
    ser.write(b"Z9CRCS")
    hdr = read_exact(ser, 4, timeout=10.0)
    if hdr != b"CRC0":
        raise RuntimeError(f"CRCS failed: {hdr!r}")
    nsec = struct.unpack("<I", read_exact(ser, 4, timeout=5.0))[0]
    if nsec != NUM_SECTORS:
        raise RuntimeError(f"Unexpected sector count {nsec}")
    dev_crcs = struct.unpack(f"<{NUM_SECTORS}I", read_exact(ser, NUM_SECTORS * 4, timeout=30.0))

    dirty = []
    for s in range(NUM_SECTORS):
        sec = bios[s * SECTOR_SIZE : (s + 1) * SECTOR_SIZE]
        if (zlib.crc32(sec) & 0xFFFFFFFF) != dev_crcs[s]:
            dirty.append(s)

    print(f"Dirty sectors: {len(dirty)} / {NUM_SECTORS} ({len(dirty) * 4} KiB to transfer)")
    t0 = time.time()
    for idx, s in enumerate(dirty, 1):
        addr = BIOS_BASE + s * SECTOR_SIZE
        sec = bios[s * SECTOR_SIZE : (s + 1) * SECTOR_SIZE]
        crc = zlib.crc32(sec) & 0xFFFFFFFF
        pkt = b"Z9WR4K" + struct.pack("<II", addr, crc) + sec
        for attempt in range(3):
            ser.write(pkt)
            ack = read_exact(ser, 4, timeout=15.0)
            if ack == b"ACK4":
                break
            if attempt == 2:
                raise RuntimeError(f"WR4K failed @ 0x{addr:08x}: {ack!r}")
        if idx % 16 == 0 or idx == len(dirty):
            dt = max(time.time() - t0, 0.01)
            print(f"  [{idx}/{len(dirty)}] 0x{addr:08x} ({idx*4/dt:.1f} KiB/s)")

    print("Verifying full 6.5 MiB SPI readback SHA-256...")
    ser.write(b"Z9VSHA")
    hdr = read_exact(ser, 4, timeout=20.0)
    if hdr != b"SHA0":
        raise RuntimeError(f"VSHA failed: {hdr!r}")
    got_sha = read_exact(ser, 32, timeout=20.0)
    print(f"Verified SPI SHA-256: {got_sha.hex()}")
    if got_sha != target_sha:
        raise RuntimeError("FULL READBACK SHA-256 MISMATCH!")
    print("Z97A_SERIAL_DELTA_FLASH_VERIFIED_OK")

    if auto_reboot:
        cmd_reboot(ser)


def cmd_reboot(ser: serial.Serial) -> None:
    enter_serflash_if_shell(ser)
    ser.write(b"Z9REBT")
    resp = read_exact(ser, 4, timeout=5.0)
    print(f"Reboot ack: {resp!r}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("info")
    sub.add_parser("diag")
    sub.add_parser("reboot")
    fl = sub.add_parser("flash")
    fl.add_argument("rom")
    fl.add_argument("--reboot", action="store_true")
    args = ap.parse_args()

    with serial.Serial(args.port, args.baud, timeout=0.5, rtscts=False, dsrdtr=False) as ser:
        if args.cmd == "info":
            cmd_info(ser)
        elif args.cmd == "diag":
            cmd_diag(ser)
        elif args.cmd == "reboot":
            cmd_reboot(ser)
        elif args.cmd == "flash":
            cmd_flash(ser, args.rom, args.reboot)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
