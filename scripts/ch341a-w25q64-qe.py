#!/usr/bin/env python3
"""CH341A (1a86:5512) Winbond W25Q64FV Status Register / Quad Enable (QE=1) helper.

Uses ctypes + libusb-1.0 directly over CH341A bulk endpoints (0x02 OUT / 0x82 IN)
with bit-reversed SPI bytes, mirroring flashrom ch341a_spi.c.
"""

import argparse
import ctypes
import ctypes.util
import sys
import time

CH341A_VID = 0x1A86
CH341A_PID = 0x5512
EP_OUT = 0x02
EP_IN = 0x82

CMD_SPI_STREAM = 0xA8
CMD_I2C_STREAM = 0xAA
CMD_UIO_STREAM = 0xAB
UIO_STM_OUT = 0x80
UIO_STM_DIR = 0x40
UIO_STM_END = 0x20


def bitrev8(b: int) -> int:
    return int(f"{b:08b}"[::-1], 2)


class Ch341aSpi:
    def __init__(self) -> None:
        lib_path = ctypes.util.find_library("usb-1.0") or "libusb-1.0.so.0"
        self.lib = ctypes.CDLL(lib_path)
        self.lib.libusb_open_device_with_vid_pid.restype = ctypes.c_void_p
        self.ctx = ctypes.c_void_p()
        rc = self.lib.libusb_init(ctypes.byref(self.ctx))
        if rc != 0:
            raise RuntimeError(f"libusb_init failed: {rc}")
        self.handle = self.lib.libusb_open_device_with_vid_pid(self.ctx, CH341A_VID, CH341A_PID)
        if not self.handle:
            raise RuntimeError("CH341A (1a86:5512) not found or permission denied")
        self.handle = ctypes.c_void_p(self.handle)
        self.lib.libusb_set_auto_detach_kernel_driver(self.handle, 1)
        rc = self.lib.libusb_claim_interface(self.handle, 0)
        if rc != 0:
            raise RuntimeError(f"libusb_claim_interface failed: {rc}")
        self._enable_pins(True)

    def close(self) -> None:
        if getattr(self, "handle", None):
            try:
                self._enable_pins(False)
            except Exception:
                pass
            self.lib.libusb_release_interface(self.handle, 0)
            self.lib.libusb_close(self.handle)
            self.handle = None
        if getattr(self, "ctx", None):
            self.lib.libusb_exit(self.ctx)
            self.ctx = None

    def _bulk(self, ep: int, data: bytes, rx_len: int = 0) -> bytes:
        transferred = ctypes.c_int(0)
        if ep == EP_OUT:
            buf = (ctypes.c_ubyte * len(data))(*data)
            rc = self.lib.libusb_bulk_transfer(
                self.handle, ep, buf, len(data), ctypes.byref(transferred), 2000
            )
            if rc != 0 or transferred.value != len(data):
                raise RuntimeError(f"USB OUT failed rc={rc} sent={transferred.value}/{len(data)}")
            return b""
        buf = (ctypes.c_ubyte * rx_len)()
        rc = self.lib.libusb_bulk_transfer(
            self.handle, ep, buf, rx_len, ctypes.byref(transferred), 2000
        )
        if rc != 0:
            raise RuntimeError(f"USB IN failed rc={rc}")
        return bytes(buf[: transferred.value])

    def _enable_pins(self, enable: bool) -> None:
        if enable:
            self._bulk(EP_OUT, bytes([CMD_I2C_STREAM, 0x61, 0x00]))
        dir_byte = UIO_STM_DIR | (0x3F if enable else 0x00)
        pkt = bytes([CMD_UIO_STREAM, UIO_STM_OUT | 0x37, dir_byte, UIO_STM_END])
        self._bulk(EP_OUT, pkt)

    def _cs(self, asserted: bool) -> None:
        val = 0x36 if asserted else 0x37
        self._bulk(EP_OUT, bytes([CMD_UIO_STREAM, UIO_STM_OUT | val, UIO_STM_END]))

    def xfer(self, tx: bytes, rx_len: int = 0) -> bytes:
        total = tx + (b"\xff" * rx_len)
        if len(total) > 26:
            raise ValueError("Packet too long for single CH341A SPI frame")
        self._cs(True)
        try:
            rev_tx = bytes(bitrev8(b) for b in total)
            self._bulk(EP_OUT, bytes([CMD_SPI_STREAM]) + rev_tx)
            raw_rx = self._bulk(EP_IN, b"", len(total))
            if len(raw_rx) != len(total):
                raise RuntimeError(f"Short SPI RX {len(raw_rx)}/{len(total)}")
            rx = bytes(bitrev8(b) for b in raw_rx)
        finally:
            self._cs(False)
        return rx[len(tx) :] if rx_len else b""


def read_state(spi: Ch341aSpi) -> tuple[bytes, int, int, int]:
    rdid = spi.xfer(b"\x9f", 3)
    sr1 = spi.xfer(b"\x05", 1)[0]
    sr2 = spi.xfer(b"\x35", 1)[0]
    sr3 = spi.xfer(b"\x15", 1)[0]
    return rdid, sr1, sr2, sr3


def wait_wip_clear(spi: Ch341aSpi, timeout_s: float = 2.0) -> None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        sr1 = spi.xfer(b"\x05", 1)[0]
        if (sr1 & 0x01) == 0:
            return
        time.sleep(0.01)
    raise TimeoutError("Timed out waiting for WIP bit to clear")


def main() -> int:
    ap = argparse.ArgumentParser(description="Read/set Winbond W25Q64FV QE=1 via CH341A")
    ap.add_argument("--set-qe", action="store_true", help="Enforce non-volatile SR1=0x00, SR2=0x02 (QE=1)")
    args = ap.parse_args()

    spi = Ch341aSpi()
    try:
        rdid, sr1, sr2, sr3 = read_state(spi)
        print(f"RDID={rdid.hex(' ')} SR1=0x{sr1:02x} SR2=0x{sr2:02x} (QE={(sr2 >> 1) & 1}) SR3=0x{sr3:02x}")
        if rdid != b"\xef\x40\x17":
            raise RuntimeError(f"Expected Winbond W25Q64FV RDID ef 40 17, got {rdid.hex(' ')}")

        if args.set_qe and (sr1 != 0x00 or sr2 != 0x02):
            print("Programming non-volatile SR1=0x00, SR2=0x02 (QE=1) via WRSR (0x01)...")
            spi.xfer(b"\x06")
            spi.xfer(b"\x01\x00\x02")
            wait_wip_clear(spi)
            rdid, sr1, sr2, sr3 = read_state(spi)
            if sr2 != 0x02:
                print("WRSR did not latch SR2=0x02; retrying via WRSR2 (0x31)...")
                spi.xfer(b"\x06")
                spi.xfer(b"\x31\x02")
                wait_wip_clear(spi)
                rdid, sr1, sr2, sr3 = read_state(spi)
            print(f"AFTER: RDID={rdid.hex(' ')} SR1=0x{sr1:02x} SR2=0x{sr2:02x} (QE={(sr2 >> 1) & 1}) SR3=0x{sr3:02x}")
            if sr1 != 0x00 or sr2 != 0x02:
                raise RuntimeError(f"Failed to verify SR1=0x00 SR2=0x02 (got 0x{sr1:02x} / 0x{sr2:02x})")
        elif args.set_qe:
            print("QE=1 already active (SR1=0x00, SR2=0x02).")
        return 0
    finally:
        spi.close()


if __name__ == "__main__":
    raise SystemExit(main())
