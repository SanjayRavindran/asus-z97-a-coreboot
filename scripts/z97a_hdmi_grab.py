#!/usr/bin/env python3
"""
Reusable V4L2 USB HDMI frame grabber for MacroSilicon MS2109 (534d:2109) on 192.168.1.90:/dev/video0.
Pure Python (fcntl.ioctl + mmap, zero external packages).

Drains V4L2 frames continuously during warmup and burst intervals so MS2109
keeps its HDMI RX awake and never overflows its 4-buffer queue.
"""

import argparse
import fcntl
import mmap
import os
import select
import struct
import time

# Linux x86_64 V4L2 ioctl constants
VIDIOC_QUERYCAP = 0x80685600
VIDIOC_S_FMT = 0xC0D05605
VIDIOC_REQBUFS = 0xC0145608
VIDIOC_QUERYBUF = 0xC0585609
VIDIOC_QBUF = 0xC058560F
VIDIOC_DQBUF = 0xC0585611
VIDIOC_STREAMON = 0x40045612
VIDIOC_STREAMOFF = 0x40045613

V4L2_BUF_TYPE_VIDEO_CAPTURE = 1
V4L2_MEMORY_MMAP = 1
V4L2_FIELD_NONE = 1
V4L2_PIX_FMT_MJPG = 0x47504A4D


def grab_frames(
    dev: str,
    width: int,
    height: int,
    frames: int,
    warmup: float,
    burst: int,
    interval: float,
    out: str,
    prefix: str,
) -> None:
    fd = os.open(dev, os.O_RDWR)
    mmaps = []
    stream_on = False
    try:
        cap = bytearray(104)
        fcntl.ioctl(fd, VIDIOC_QUERYCAP, cap)
        card = cap[16:48].split(b"\0", 1)[0].decode(errors="replace")

        fmt = bytearray(208)
        struct.pack_into("<I", fmt, 0, V4L2_BUF_TYPE_VIDEO_CAPTURE)
        struct.pack_into("<IIII", fmt, 8, width, height, V4L2_PIX_FMT_MJPG, V4L2_FIELD_NONE)
        fcntl.ioctl(fd, VIDIOC_S_FMT, fmt)
        act_w, act_h, pf = struct.unpack_from("<III", fmt, 8)
        if pf != V4L2_PIX_FMT_MJPG:
            raise RuntimeError(f"Expected MJPG pixelformat, got 0x{pf:08x}")

        req = bytearray(20)
        struct.pack_into("<III", req, 0, 4, V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_MEMORY_MMAP)
        fcntl.ioctl(fd, VIDIOC_REQBUFS, req)
        count = struct.unpack_from("<I", req, 0)[0]
        if count < 1:
            raise RuntimeError("VIDIOC_REQBUFS returned 0 buffers")

        for i in range(count):
            vb = bytearray(88)
            struct.pack_into("<II", vb, 0, i, V4L2_BUF_TYPE_VIDEO_CAPTURE)
            struct.pack_into("<I", vb, 60, V4L2_MEMORY_MMAP)
            fcntl.ioctl(fd, VIDIOC_QUERYBUF, vb)
            length = struct.unpack_from("<I", vb, 72)[0]
            offset = struct.unpack_from("<I", vb, 64)[0]
            mm = mmap.mmap(fd, length, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE, offset=offset)
            mmaps.append(mm)
            fcntl.ioctl(fd, VIDIOC_QBUF, vb)

        btype = struct.pack("<I", V4L2_BUF_TYPE_VIDEO_CAPTURE)
        fcntl.ioctl(fd, VIDIOC_STREAMON, btype)
        stream_on = True

        def drain_until(min_seconds: float, max_frames: int) -> bytes:
            best = b""
            t_start = time.monotonic()
            seen_after_min = 0
            for _ in range(max_frames):
                r, _, _ = select.select([fd], [], [], 1.0)
                if not r:
                    if (time.monotonic() - t_start) >= min_seconds:
                        break
                    continue
                vb = bytearray(88)
                struct.pack_into("<I", vb, 4, V4L2_BUF_TYPE_VIDEO_CAPTURE)
                struct.pack_into("<I", vb, 60, V4L2_MEMORY_MMAP)
                fcntl.ioctl(fd, VIDIOC_DQBUF, vb)
                idx = struct.unpack_from("<I", vb, 0)[0]
                bytesused = struct.unpack_from("<I", vb, 8)[0]
                data = mmaps[idx][:bytesused]
                fcntl.ioctl(fd, VIDIOC_QBUF, vb)
                if bytesused > 2048 and data[:2] == b"\xff\xd8":
                    best = data
                elapsed = time.monotonic() - t_start
                if elapsed >= min_seconds and best:
                    seen_after_min += 1
                    if seen_after_min >= 2:
                        break
            return best

        if burst <= 1:
            max_f = max(frames, int(warmup * 90) + 20)
            img = drain_until(warmup, max_f)
            if not img:
                raise RuntimeError(f"Failed to capture valid MJPG frame from {dev} ({card})")
            os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
            with open(out, "wb") as f:
                f.write(img)
            print(f"{out}: {act_w}x{act_h} {len(img)} B hdr={img[:4].hex()} ({card})")
        else:
            os.makedirs(os.path.dirname(os.path.abspath(prefix)), exist_ok=True)
            for b in range(burst):
                dur = warmup if b == 0 else interval
                max_f = max(frames, int(max(dur, 0.2) * 90) + 20)
                img = drain_until(dur, max_f)
                path = f"{prefix}_{b:02d}.jpg"
                if img:
                    with open(path, "wb") as f:
                        f.write(img)
                    print(f"[{b + 1}/{burst}] {path}: {act_w}x{act_h} {len(img)} B hdr={img[:4].hex()}")
                else:
                    print(f"[{b + 1}/{burst}] {path}: NO VALID FRAME")
    finally:
        if stream_on:
            try:
                fcntl.ioctl(fd, VIDIOC_STREAMOFF, struct.pack("<I", V4L2_BUF_TYPE_VIDEO_CAPTURE))
            except OSError:
                pass
        for mm in mmaps:
            mm.close()
        os.close(fd)


def main() -> None:
    p = argparse.ArgumentParser(description="Capture MJPG HDMI frame(s) from V4L2 MS2109 device.")
    p.add_argument("--device", default="/dev/video0", help="V4L2 video device (default: /dev/video0)")
    p.add_argument("--width", type=int, default=1280, help="Capture width (default: 1280)")
    p.add_argument("--height", type=int, default=720, help="Capture height (default: 720)")
    p.add_argument("--frames", type=int, default=35, help="Max frames floor per capture (default: 35)")
    p.add_argument("--warmup", type=float, default=1.8, help="Warmup drain seconds before first frame (default: 1.8)")
    p.add_argument("--burst", type=int, default=1, help="Number of captures in burst mode (default: 1)")
    p.add_argument("--interval", type=float, default=1.0, help="Seconds to drain between burst captures (default: 1.0)")
    p.add_argument("--out", default="/home/zero/hdmi_capture.jpg", help="Output JPEG path for single capture")
    p.add_argument("--prefix", default="/home/zero/hdmi_burst", help="Output path prefix for --burst > 1")
    args = p.parse_args()
    grab_frames(args.device, args.width, args.height, args.frames, args.warmup, args.burst, args.interval, args.out, args.prefix)


if __name__ == "__main__":
    main()
