---
name: serial-communication
description: >-
  Capture Coreboot, EDK2, GRUB, and Proxmox Linux serial console boot logs,
  send keystrokes (ESC for UEFI Setup, Delete for Boot Menu), and verify
  bidirectional UART traffic between ASUS Z97-A COM1 (192.168.1.150 /dev/ttyS0)
  and the x86_64 Ubuntu serial host zeb-h61 (192.168.1.90 /dev/ttyUSB0). Use
  this skill whenever flashing, booting, debugging serial output, or testing
  COM1 on the ASUS Z97-A.
---

# Serial Communication for ASUS Z97-A

Use [z97a_serial.py](./scripts/z97a_serial.py) to check serial readiness, capture boot logs, send UEFI hotkeys (`ESC` / `DEL`), and verify bidirectional communication over SSH to `zeb-h61` (`192.168.1.90`).

## Hardware & Wiring Topology

- **Target Board**: ASUS `Z97-A` (`pve`, Proxmox VE at `192.168.1.150`, SSH user `root`)
  - Super I/O: Nuvoton `NCT6791D` (`id=0xc803` at `0x2e`), `LDN 0x02` (`UART A` / `COM1`) at `0x03f8`, IRQ `4`, `/dev/ttyS0` (`PNP0501`)
  - Motherboard header: `COM1` (RS-232 level via onboard transceiver)
- **Serial Capture Host**: `zeb-h61` (`192.168.1.90`, SSH user `zero`, member of `dialout`, Ubuntu `x86_64`)
  - Adapter: QinHeng `CH340` (`1a86:7523`, `ch341-uart`) at `/dev/ttyUSB0` (`/dev/serial/by-id/usb-1a86_USB2.0-Ser_-if00-port0`)
  - *(Note: A Raspberry Pi 4B `192.168.1.45` was previously tested and discarded due to `-32` / `-EPIPE` USB control-transfer stalls on `ch341-uart`.)*
- **Serial Parameters**: `115200 8N1`, raw, no hardware/software flow control (`-crtscts`, `XON/XOFF` off).
- **Verified**: Bidirectional `115200 8N1` transfer between `192.168.1.150:/dev/ttyS0` and `192.168.1.90:/dev/ttyUSB0` passed with zero framing/parity/overrun errors.

## Usage (`z97a_serial.py`)

Run from Windows PowerShell (`python` invokes `ssh -o BatchMode=yes` using `~/.ssh/config`):

```powershell
# Check /dev/ttyUSB0 readiness and port locks on 192.168.1.90
python ".agents/skills/serial-communication/scripts/z97a_serial.py" status

# Capture live Coreboot + EDK2 + Proxmox boot logs (start BEFORE power-on/reboot)
python ".agents/skills/serial-communication/scripts/z97a_serial.py" capture --duration 90 --outfile "outputs/z97a-bootlog.txt"

# Capture while sending ESC bursts (enter EDK2 UiApp Setup) or Delete (VT220 ESC[3~)
python ".agents/skills/serial-communication/scripts/z97a_serial.py" capture --duration 60 --send-key esc --outfile "outputs/z97a-setup.txt"
python ".agents/skills/serial-communication/scripts/z97a_serial.py" capture --duration 60 --send-key del --outfile "outputs/z97a-del.txt"

# Verify two-way 115200 8N1 communication while Proxmox is booted
python ".agents/skills/serial-communication/scripts/z97a_serial.py" loopback-test
```

## Operational Notes & Pre-OS COM1 Control

- **EDK2 `SerialIo` 16-Byte FIFO Pacing Rule (CRITICAL)**: In UEFI `Shell>` and `iPXE>`, EDK2 polls the `NCT6791D` 16550 UART's 16-byte RX FIFO without hardware flow control (`-crtscts`). Sending `> 16` bytes in a single `write()` drops characters mid-command. **Always pace interactive TX at `5 ms` per byte** (`time.sleep(0.005)` with `flush()`).
- **Direct Pre-OS Diagnostics & Serial Delta Flashing**:
  - `z97a_tool.efi` v2.7 (`~72 KiB`, staged on `192.168.1.90:8080/z97a_tool.efi`) runs directly in EDK2 memory (zero SPI writes) for hardware isolation before building a consolidated firmware update:
    - `diag`: full PCI/northbridge/display/audio/USB/ME/NIC/GPIO/RCBA/SIO/SMBus/ACPI/UEFI/SATA HSIO/ABAR/PCIe RP/SPI check + `16 MiB` `BlockIO` SATA stress test.
    - `satatest2` / `satatest3` / `hdmitest4` / `v014audit`: RAM-only isolation tests (full `DMIBAR` `0x0000..0x0e40` + `RCBA` `0x0000..0x3f00` diffable dumps, W1C AER/SERR clear + idle polling, Pure-PHY-Idle, SATA Gen1/Gen2/Gen3 speed tests, Desktop CG & DMI ASPM RAM overrides, GOP/MTRR/SBI/modeset checks).
    - `serflash`: polls `0x3f8` UART LSR directly (no FIFO drops at `115200` baud) for binary-framed `Z9INFO`, `Z9CRCS`, `Z9WR4K`, `Z9VSHA`, `Z9DIAG`, and `Z9REBT` commands driven by [`scripts/z97a_serial_flash.py`](../../../scripts/z97a_serial_flash.py).
  - **~5-Minute UEFI/iPXE Idle Watchdog Pitfall**: Do NOT park the Z97-A idle at `iPXE>` for > 5 minutes between turns — after ~5–6 minutes idle it stops responding on UART and drops HDMI output. Chain RAM diagnostic subcommands automatically inside the catcher script (or immediately after parking) rather than leaving the prompt idle.
  - iPXE HTTP flashing: from EDK2 `iPXE>` over paced COM1, run `ifopen net1 && dhcp net1 && chain http://192.168.1.90:8080/z97a_flash_<tag>.efi` to delta-flash the firmware (`0x210000..0x7fffff` `FMAP + COREBOOT` to preserve `RW_MRC_CACHE` & `SMMSTORE`), verify full SHA-256, and cold-reset automatically.
- **Serial Echo Loop Prevention (`/dev/ttyUSB0`)**: Before starting a raw capture on `192.168.1.90`, configure raw `-echo -ixon -ixoff -crtscts` and flush buffers (`termios.tcflush(fd, TCIOFLUSH)`) — leaving kernel line echo enabled on `/dev/ttyUSB0` can bounce OS login banners back into `ttyS0` and flood capture files.
- Ensure no leftover background `cat`, `catch_*`, or test loop is holding `/dev/ttyUSB0` on `192.168.1.90` or `/dev/ttyS0` on `192.168.1.150` before starting a capture.
- Never restart `serial-getty@ttyS0` from inside an active `ttyS0` serial shell (use `systemctl daemon-reload` and `systemctl enable` for boot-time changes).
- In PowerShell 5.1, avoid `&&`, and avoid unescaped double quotes inside single-quoted SSH commands; pipe scripts via `@' ... '@ | ssh host "tr -d '\r' | bash -s"`.

## USB HDMI Video Capture (`192.168.1.90:/dev/video0`) — Currently Unplugged

- **Current Cabling**: The `Z97-A` HDMI output (`card1-HDMI-A-3`) is connected directly to a physical **`AOC 28E850`** monitor (the `MS2109` capture dongle is unplugged from `192.168.1.90:/dev/video0`). **Do NOT probe `/dev/video0` unless the user explicitly reconnects the capture card.**

- **Hardware**: MacroSilicon `MS2109` (`534d:2109`, `uvcvideo` at `/dev/video0`), supporting `MJPG` and `YUYV` up to `1920x1080`.
- **Important**: The Z97-A `HDMI` port is wired directly into this capture card on `192.168.1.90` (the user cannot see a physical monitor while the capture card is attached). Always grab and visually inspect frames with `read_file` to diagnose HDMI output across Coreboot `libgfxinit`, EDK2 GOP, and Linux `i915`.
- **CRITICAL `MS2109` `1.15s` V4L2 Warmup & Continuous-Drain Rule**:
  - `MS2109` powers down its HDMI RX whenever `/dev/video0` is closed and needs **~1.15s (~67 frames)** of active `VIDIOC_STREAMON` frame draining before locking onto an active `1080p` HDMI TMDS signal. Stopping after `~0.13s` (`6` frames) returns `33,308 B` (720p) or `63,163 B` (1080p) SMPTE color bars **even when the Z97-A is outputting live video**!
  - `scripts/z97a_hdmi_grab.py` (`~/z97a_hdmi_grab.py` on `.90`) enforces `--warmup` (default `1.8s`) and continuously drains V4L2 ring buffers during `--burst` intervals (`time.sleep` would overflow the 4-buffer queue).
- **No-Signal Indicator**: Standard SMPTE color bars **captured with `--warmup >= 1.8`** indicate the `MS2109` sees **no active HDMI TMDS signal** from the Z97-A.
- **Usage (`scripts/z97a_hdmi_grab.py`, synced to `~/z97a_hdmi_grab.py` on `.90`)**:
  ```powershell
  # Capture a crisp 1920x1080 MJPG frame on 192.168.1.90 with 2.0s MS2109 RX warmup
  ssh zero@192.168.1.90 "python3 ~/z97a_hdmi_grab.py --width 1920 --height 1080 --warmup 2.0 --out /home/zero/hdmi_capture.jpg"
  scp zero@192.168.1.90:/home/zero/hdmi_capture.jpg "outputs/hdmi_capture.jpg"

  # Capture a continuous-drain burst across transitions
  ssh zero@192.168.1.90 "python3 ~/z97a_hdmi_grab.py --width 1280 --height 720 --warmup 1.8 --burst 6 --interval 1.5 --prefix /home/zero/hdmi_boot"
  ```
