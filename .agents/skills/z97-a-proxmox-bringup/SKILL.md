---
name: z97-a-proxmox-bringup
description: >-
  Verify and configure Proxmox VE 9 / Debian Linux on the ASUS Z97-A host
  (192.168.1.150) before and after Coreboot migration, covering COM1 115200 8N1
  serial console, Intel I218-V + I226-V NICs, VT-d/IOMMU, HD 4600 IGD, ALC892
  audio, and NCT6791D sensors. Use when validating Linux hardware state on
  192.168.1.150.
---

# ASUS Z97-A Proxmox Bring-Up & Verification

Baseline captured on stock AMI BIOS `3503` (`pve`, kernel `7.0.14-17-pve`, `192.168.1.150`) in [`stock-capture/`](../../../stock-capture/).

## 1. Confirmed Hardware Map

- **CPU & Chipset**: Intel Core `i7-4790K` (`CPUID 0x306c3`), Host Bridge `8086:0c00`, Z97 LPC `8086:8cc4` (`1043:8534`), `i915` IGD `8086:0412` (`i915_vbt.bin` `6,144` B).
- **Super I/O (`0x2e`)**: Nuvoton `NCT6791D` (`id=0xc803`)
  - `COM1` (`LDN 0x02`): `0x03f8`, IRQ `4`, `/dev/ttyS0` (`PNP0501`)
  - `KBC` (`LDN 0x05`): `0x60`/`0x64`, IRQ `1`/`12`
  - `HWM` (`LDN 0x0b`): `0x0290`
  - `LPT1` (`LDN 0x01`): disabled (`CR 0x30 = 0x00`) in stock AMI
- **Networking**:
  - `enp5s0` (`05:00.0`, Intel `I226-V` `8086:125c` rev `04`, MAC `02:76:c6:00:f7:7d`) -> active Proxmox bridge `vmbr0` (`192.168.1.150/24`)
  - `eno0` (`00:19.0`, Intel `I218-V` `8086:15a1`, MAC `9c:5c:8e:90:7e:f2`) -> onboard GbE (must preserve MAC in SPI `FREG3` GbE region!)
- **Audio**:
  - `card0`: Realtek `ALC892` (`0x10ec0892`, subsystem `0x1043860b`)
  - `card1`: Intel Haswell HDMI (`0x80862807`, subsystem `0x80860101`)
- **Storage & Boot**:
  - `sda`: `CT500BX500SSD1` (`465.8 GiB` SATA, `sda2` `1 GiB` vfat `/boot/efi`, `sda3` LVM `pve`)
  - EFI bootloaders verified on `sda2`: `\EFI\BOOT\BOOTx64.EFI` + `\EFI\BOOT\fbx64.efi` (`BOOTX64.CSV` -> `shimx64.efi,proxmox`) and `\EFI\proxmox\grub.cfg` (UUID `c525225c-6406-4f69-842f-355cc4175e94`, `pve-root` `/boot/grub/grub.cfg`). Stock AMI previously booted in Legacy CSM (`LEGACY_BIOS`); Coreboot + EDK2 boots natively in pure **UEFI** via `\EFI\BOOT\BOOTx64.EFI` fallback without needing pre-populated NVRAM variables.
  - Stock cmdline: `intel_iommu=on iommu=pt pcie_acs_override=downstream,multifunction`

## 2. Serial Console Configuration (GRUB + systemd)

Coreboot, EDK2, iPXE, GRUB, and Linux should all standardize on `COM1` (`0x3f8`, `/dev/ttyS0`) at `115200 8N1`:

- GRUB cmdline additions (when ready to enable Linux boot logging over `COM1`):
  `console=tty0 console=ttyS0,115200n8`
- `serial-getty@ttyS0`: configure `115200` baud in `/etc/systemd/system/serial-getty@ttyS0.service.d/10-baud-115200.conf` and enable with `systemctl daemon-reload && systemctl enable serial-getty@ttyS0.service` (do not restart while connected over `ttyS0`).

## 3. Post-Flash Verification Checklist

Run over SSH (`192.168.1.150`) or `COM1` after booting Coreboot:

1. `dmidecode -t bios -t baseboard` & `cat /sys/firmware/efi/fw_platform_size` -> verify `Vendor: coreboot`, `64`-bit UEFI, and 8 MiB ROM.
2. **Coreboot Serial Log (`COM1`)**: confirm `RCBA GCS` shows `LPC` Port `80h` routing, `CMOS_POST`, `ENE EC version: MBE0-Z97-0121`, `ASP1252` telemetry (`VCPU`/`ICPU`/`PCPU`), `EPU`/`TPU` HW switches, Q-LED transitions, `libgfxinit` linear FB -> UEFI GOP, and EDK2 `iPXE Network Boot` entry for `I218-V`/`I226-V`.
3. `ip -br link` & `ethtool` -> confirm `enp5s0`/`vmbr0` up at `2500Mb/s`, `eno0` retains MAC `9c:5c:8e:90:7e:f2`, and both show `Wake-on: g`.
4. **`v0.16` Virtualization, ME, Above-4G, Power-ON & WoL Checks**:
   - `lspci -nn -s 00:16.0` -> **empty** (`MEI #1` hidden by default via `FD2`).
   - `setpci -s 00:1f.0 a4.w` -> **`3808`** (`AFTERG3_EN=0` Power ON after AC loss + `WOL_EN_OVRD=1` WoL enabled).
   - `cat /proc/acpi/wakeup` -> exposes **`GLAN` (`S4 *enabled`)**, **`PEGP` (`S4 *enabled`)**, **`RP01..RP08` (`RP03 *enabled`)**, and **`XHCI` (`S3 *enabled`)**.
   - `dmesg | grep "root bus resource"` -> includes `[mem 0x87ae00000-0x7fffffffff window]` (true 64-bit Above-4G aperture).
   - `ls /dev/kvm`, `find /sys/kernel/iommu_groups -mindepth 1 -maxdepth 1 -type d | wc -l` (`14` groups), `MSR 0x3a = 0x5` (`Lock=1, VMX_outside_SMX=1`), `dmesg -l err,crit,alert,emerg` -> empty (`0` errors).
5. `lspci -vv` & `dmesg | grep -iE 'dmar|iommu|i915|snd_hda|e1000e|igc|bar|ata5|ahci|serror|10b8b|aer|dmi'` -> verify `DMAR`/VT-d active, Above-4G / Resizable BAR windows, clean `i915` init (`vbt.bin` SHA-256 `ada53b3d...` parity, `AOC 28E850` on `HDMI-A-3`), `ata5: SATA link up 6.0 Gbps` with zero resets / `SError`, `ALC892` (`NID 0x11=0x90460130`) + `00:03.0` HDMI audio, and clean PCIe bridges (`ASM1083` at `00:1c.0` -> `02:00.0`, `I226-V` at `00:1c.2` -> `05:00.0` `ASPM Disabled`, `CommClk+`).
5. `setpci -s 00:00.0 58.l` -> verify `PAVPC = 0x84100017` (`TOLUD - 1 MiB` | lock/enable bits matching stock `3503`).
6. **DMI, Desktop Clock Gating & RCBA Parity**: verify live `DMIBAR` (`0088=00420000`, `0098=00000002`, `01d0=00000000`), `00:1f.0` `a0=0210/0610`, and `RCBA` (`21a8=40420000`, `2234=0`, `341c=0`, `38c0=8`).
7. **HDMI Output Verification**: Check `/sys/class/drm/card*-HDMI-*/{status,enabled,modes,edid}` for live connected monitor (`AOC 28E850` on `card1-HDMI-A-3`). Only use `192.168.1.90:/dev/video0` (`z97a_hdmi_grab.py`) when the USB HDMI capture card is explicitly connected instead of a physical monitor.
8. `grep . /sys/bus/pnp/devices/*/id` & `cat /proc/tty/driver/serial | head -n 3` -> confirm `PNP0501` and `ttyS0` at `0x03f8` IRQ `4`.
9. `sensors` -> verify `coretemp-isa-0000`, `intel-rapl:0`, and `nct6791-isa-0290` (persisted via `/etc/modules-load.d/nct6775.conf`, unlocked via `CR 0x28=0x00`) temperatures, voltages, and `fan2`/`PWM` telemetry.
