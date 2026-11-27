#!/usr/bin/env python3
"""Serial console helper for ASUS Z97-A via zeb-h61 (192.168.1.90 /dev/ttyUSB0).

Uses stdlib only and delegates remote serial I/O over BatchMode SSH configured in
~/.ssh/config.
"""

import argparse
import pathlib
import subprocess
import sys
import threading
import time

DEFAULT_PI_OR_SERIAL_HOST = "192.168.1.90"
DEFAULT_SERIAL_DEV = "/dev/ttyUSB0"
DEFAULT_TARGET_HOST = "192.168.1.150"
DEFAULT_TARGET_DEV = "/dev/ttyS0"

KEY_BYTES = {
    "esc": b"\x1b",
    "del": b"\x1b[3~",
    "enter": b"\r",
}


def ssh_run(host: str, remote_cmd: str, stdin_text: str | None = None, check: bool = True) -> subprocess.CompletedProcess:
    cmd = ["ssh", "-o", "BatchMode=yes", host, remote_cmd]
    return subprocess.run(
        cmd,
        input=stdin_text.encode("utf-8") if stdin_text is not None else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=check,
    )


def cmd_status(args: argparse.Namespace) -> int:
    script = f"""
id
ls -la {args.dev} /dev/serial/by-id/ 2>&1 || true
fuser -v {args.dev} 2>&1 || true
stty -F {args.dev} 115200 cs8 -cstopb -parenb raw -echo && echo "STTY_OK=1"
""".strip()
    proc = ssh_run(args.serial_host, "tr -d '\\r' | bash -s", stdin_text=script, check=False)
    sys.stdout.write(proc.stdout.decode("utf-8", errors="replace"))
    if proc.stderr:
        sys.stderr.write(proc.stderr.decode("utf-8", errors="replace"))
    return proc.returncode


def cmd_capture(args: argparse.Namespace) -> int:
    send_hex = KEY_BYTES[args.send_key].hex() if args.send_key else ""
    remote_py = f"""
import os, select, subprocess, sys, termios, time

dev = {args.dev!r}
duration = {float(args.duration)!r}
send_bytes = bytes.fromhex({send_hex!r})

subprocess.run(['stty', '-F', dev, '115200', 'cs8', '-cstopb', '-parenb', 'raw', '-echo'], check=True)
fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
termios.tcflush(fd, termios.TCIOFLUSH)

start = time.monotonic()
last_send = 0.0
try:
    while time.monotonic() - start < duration:
        if send_bytes and (time.monotonic() - last_send) >= 0.4:
            try:
                os.write(fd, send_bytes)
            except OSError:
                pass
            last_send = time.monotonic()
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try:
                chunk = os.read(fd, 4096)
                if chunk:
                    sys.stdout.buffer.write(chunk)
                    sys.stdout.buffer.flush()
            except BlockingIOError:
                pass
finally:
    os.close(fd)
""".lstrip()

    cmd = ["ssh", "-o", "BatchMode=yes", args.serial_host, "python3 -"]
    out_file = pathlib.Path(args.outfile) if args.outfile else None
    if out_file:
        out_file.parent.mkdir(parents=True, exist_ok=True)

    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=sys.stderr,
    )
    assert proc.stdin is not None and proc.stdout is not None
    proc.stdin.write(remote_py.encode("utf-8"))
    proc.stdin.close()

    collected = bytearray()
    try:
        while True:
            data = proc.stdout.read(256)
            if not data:
                break
            collected.extend(data)
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
    except KeyboardInterrupt:
        proc.terminate()
    rc = proc.wait()

    if out_file:
        out_file.write_bytes(bytes(collected))
        print(f"\n[saved {len(collected)} bytes to {out_file}]")
    return rc


def _read_once_worker(host: str, dev: str, timeout_s: float, result: dict) -> None:
    remote_py = f"""
import os, select, subprocess, termios, time
dev = {dev!r}
subprocess.run(['stty', '-F', dev, '115200', 'cs8', '-cstopb', '-parenb', 'raw', '-echo'], check=True)
fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
termios.tcflush(fd, termios.TCIOFLUSH)
buf = bytearray()
deadline = time.monotonic() + {timeout_s!r}
while time.monotonic() < deadline:
    r, _, _ = select.select([fd], [], [], 0.2)
    if r:
        try:
            chunk = os.read(fd, 4096)
            if chunk:
                buf.extend(chunk)
                if b'\\n' in buf:
                    break
        except BlockingIOError:
            pass
os.close(fd)
print(repr(bytes(buf)))
""".lstrip()
    proc = ssh_run(host, "python3 -", stdin_text=remote_py, check=False)
    result["rc"] = proc.returncode
    result["out"] = proc.stdout.decode("utf-8", errors="replace").strip()
    result["err"] = proc.stderr.decode("utf-8", errors="replace").strip()


def _write_once(host: str, dev: str, payload: str) -> tuple[int, str]:
    remote_py = f"""
import os, subprocess
dev = {dev!r}
msg = {payload!r}.encode('ascii') + b'\\r\\n'
subprocess.run(['stty', '-F', dev, '115200', 'cs8', '-cstopb', '-parenb', 'raw', '-echo'], check=True)
fd = os.open(dev, os.O_RDWR | os.O_NOCTTY)
os.write(fd, msg)
os.close(fd)
print(repr(msg))
""".lstrip()
    proc = ssh_run(host, "python3 -", stdin_text=remote_py, check=False)
    out = proc.stdout.decode("utf-8", errors="replace").strip()
    err = proc.stderr.decode("utf-8", errors="replace").strip()
    return proc.returncode, out or err


def cmd_loopback(args: argparse.Namespace) -> int:
    ok = True
    for label, tx_host, tx_dev, rx_host, rx_dev, token in [
        ("Z97-A -> zeb-h61", args.target_host, args.target_dev, args.serial_host, args.dev, "Z97A_TO_ZEB_OK_115200"),
        ("zeb-h61 -> Z97-A", args.serial_host, args.dev, args.target_host, args.target_dev, "ZEB_TO_Z97A_OK_115200"),
    ]:
        res: dict = {}
        t = threading.Thread(target=_read_once_worker, args=(rx_host, rx_dev, 4.0, res))
        t.start()
        time.sleep(1.0)
        tx_rc, tx_msg = _write_once(tx_host, tx_dev, token)
        t.join()
        rx_msg = res.get("out", "")
        passed = (tx_rc == 0) and (res.get("rc") == 0) and (token in rx_msg)
        ok = ok and passed
        print(f"[{label}] {'PASS' if passed else 'FAIL'} | tx={tx_msg} | rx={rx_msg}")
        if res.get("err"):
            print(f"  stderr: {res['err']}")
    return 0 if ok else 1


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="ASUS Z97-A COM1 serial helper via zeb-h61")
    p.add_argument("--serial-host", default=DEFAULT_PI_OR_SERIAL_HOST, help="SSH host with USB-serial adapter")
    p.add_argument("--dev", default=DEFAULT_SERIAL_DEV, help="Serial device on serial-host")
    p.add_argument("--target-host", default=DEFAULT_TARGET_HOST, help="SSH host for Z97-A Proxmox")
    p.add_argument("--target-dev", default=DEFAULT_TARGET_DEV, help="COM1 device on Z97-A")

    sub = p.add_subparsers(dest="command", required=True)
    sub.add_parser("status", help="Check /dev/ttyUSB0 permissions, locks, and 115200 stty")

    cap = sub.add_parser("capture", help="Capture serial output from Z97-A COM1")
    cap.add_argument("--duration", type=float, default=60.0, help="Capture duration in seconds")
    cap.add_argument("--outfile", help="Optional local path to save raw capture bytes")
    cap.add_argument("--send-key", choices=sorted(KEY_BYTES), help="Periodically send key during capture")

    sub.add_parser("loopback-test", help="Verify bidirectional 115200 8N1 transfer")
    return p


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.command == "status":
        return cmd_status(args)
    if args.command == "capture":
        return cmd_capture(args)
    if args.command == "loopback-test":
        return cmd_loopback(args)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
