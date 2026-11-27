# ASUS Z97-A — Coreboot + EDK2 UEFI + iPXE Port

![Release](https://img.shields.io/badge/Release-v26.11.1-2ea44f?style=flat-square)
![Coreboot](https://img.shields.io/badge/Coreboot-df45ea2c49b4-0066cc?style=flat-square)
![Payload](https://img.shields.io/badge/Payload-EDK2%20UefiPayloadPkg%20%2B%20iPXE-5c2d91?style=flat-square)
![Flash](https://img.shields.io/badge/SPI%20Flash-W25Q64FV%20(8M)-d97706?style=flat-square)
![Hardware](https://img.shields.io/badge/Hardware-Verified%20on%20ASUS%20Z97--A-success?style=flat-square)
![License](https://img.shields.io/badge/License-GPL--2.0%20%2F%20BSD--2--Clause--Patent-blue?style=flat-square)

**Quick Navigation:**
[**Releases**](../../releases) ·
[**HW & SW Flowcharts**](docs/COREBOOT-HW-SW-FLOW.md) ·
[**Parity & Live Report (`v0.15`)**](docs/COREBOOT-VS-STOCK-PARITY-REPORT.md) ·
[**Hardware, PCIe & Flex-I/O Map**](docs/Z97-A-HARDWARE-AND-PCIE.md) ·
[**Stock ROM (`3503`) Audit**](docs/STOCK-ROM-FEATURES-AUDIT.md) ·
[**DIGI+ VRM, TPU & EPU**](docs/DIGI-VRM-TPU-EPU-ALGORITHMS.md) ·
[**Blobs & Licenses**](BLOBS.md) ·
[**UEFI Diagnostic & Flash Tool**](tools/z97a_uefi_tool/) ·
[**Agent Skills**](.agents/skills/)

Open-source **Coreboot + EDK2 `UefiPayloadPkg` + iPXE** motherboard port (tracking upstream Coreboot stable releases via [`.github/workflows/coreboot-release.yml`](.github/workflows/coreboot-release.yml)), Haswell & Lynx Point Z97 stock-parity chipset patches ([`patches/`](patches/)), two-variant release builder ([`scripts/build_release_roms.sh`](scripts/build_release_roms.sh): `with-me` & `without-me`), pre-OS UEFI diagnostic & SPI delta-flasher (`z97a_tool.efi`, [`tools/z97a_uefi_tool/`](tools/z97a_uefi_tool/)), `M.2` vs `PCIEX1_1`/`PCIEX1_2` IFD strap switcher ([`scripts/switch-m2-ifd.py`](scripts/switch-m2-ifd.py)), `NCT6791D` `lm-sensors` profile ([`configs/asus-z97-a-sensors.conf`](configs/asus-z97-a-sensors.conf)), committed IFD/ME/GbE/VBT blobs ([`blobs/`](blobs/)), hardware/software flowcharts ([`docs/COREBOOT-HW-SW-FLOW.md`](docs/COREBOOT-HW-SW-FLOW.md)), stock `3503` reverse-engineering documentation ([`docs/`](docs/)), and reusable AI agent workflows ([`.agents/skills/`](.agents/skills/)) for the **ASUSTeK `Z97-A`** (`Rev 1.03B`, LGA1150 Haswell / Devil's Canyon, Intel `Z97` PCH `8086:8cc4`, Nuvoton `NCT6791D` Super I/O, `ENE MBE0-Z97-0121` EC, `DIGI+ ASP1252` VRM, Realtek `ALC892`, Intel `I218-V` GbE, Winbond `W25Q64FV` 8 MiB DIP-8 SPI flash).

---

## Highlights (`v26.11.1` / `Coreboot v0.15`)

- **Full Stock Silicon & Board Parity**:
  - **76 / 76 PCH GPIOs**, **14 / 14 `ALC892` audio pin verbs** (`NID 0x11=0x90460130`), **`PAVPC=0x84100017` / `TOLUD=0x84200001`**, **DMI `-6.0 dB` Gen2 x4**, **Desktop PCH Clock Gating (`PMCON1=0x0210`)**, and **6-port HSIO SATA Gen3 `DTLE=0x02`** matched 1:1 against stock AMI `3503` (`PEI`/`DXE`) and live hardware telemetry.
  - **PCIe ASPM OFF Default (`lynxpoint/Kconfig`)**: Matches ASUS Z97-A factory BIOS defaults (`ASPM Disabled` across PEG, DMI, and PCH root ports) to prevent PCH Flex-I/O `SATA Express` (`ata5`) COMRESET link-drop storms; live Crucial `CT500BX500SSD1` negotiates **`6.0 Gbps` Gen3** (`451 MB/s` read) with **0 resets** and **0 PCIe AER errors**.
- **Display, Networking, Virtualization & Sensors**:
  - Native **`libgfxinit`** HD 4600 (`00:02.0`, `CDCLK=539 MHz`, stock `data.vbt`) + **EDK2 UEFI GOP** + on-screen verbose [`fb_console.c`](board/src/mainboard/asus/z97-a/fb_console.c), verified on a `1920x1080` HDMI monitor (`AOC 28E850`).
  - Integrated **`iPXE Network Boot`** (`HTTPS` + `IMAGE_TRUST_CMD`) inside EDK2 supporting both onboard **`I218-V`** (`eno0`) and add-in **`I226-V` `2.5GbE`** (`enp5s0`, zero NIC error/CRC counters).
  - **True 64-bit Above-4G MMIO (`PM64` `QWordMemory`)**, **Resizable BAR up to 32 GiB**, **SR-IOV**, and **VT-d (`DMAR`, 15 IOMMU groups, `/dev/kvm`)**.
  - **Proxmox VE 9 (`Linux 7.0.14-17-pve`) Sensors & Hardware Controls**: `nct6775` (`NCT6791D @ 0x0290` unlocked via `CR 0x28=0x00`), `coretemp`, `intel-rapl`, `Q-LEDs` (`DRAM -> VGA -> BOOT_DEVICE`), SmartFan IV Q-Fan curves, `ErP S4/S5`, read-only `ENE EC MBE0-Z97-0121`, opt-in `DIGI+ ASP1252` CFR menu, `TPU` (`MSR 0x1ad`), and `EPU` (`-20 mV` OC Mailbox).
  - **Haswell `microcode 0x28`** (newer than stock `0x24`) and **unlocked SPI (`FLMSTR1=0xffff0000`, `BIOS_CNTL=0x09`)** allowing future `COREBOOT`-only internal `flashrom` or pre-OS UEFI delta updates without removing the socketed SPI chip.

---

## Verified Hardware Telemetry (`192.168.1.150` Live Proxmox VE 9)

| Subsystem | Live Verification (`-z97a-v0.15`, kernel `7.0.14-17-pve`) |
|---|---|
| **CPU / Microcode / Memory** | `Intel i7-4790K @ 4.00GHz` (`4.2–4.4 GHz` Turbo `0x2a2b2c2c`), microcode `0x28`, `32 GiB` DDR3-1866 (`4x 8 GiB` dual-channel) |
| **SATA Storage (`ata5`, `SATA6G_5`)** | `CT500BX500SSD1`: **`6.0 Gbps` (`SStatus 133`)**, `dd`: **`451 MB/s`**, **0 resets**, SMART `Reallocated = 0`, `UDMA_CRC` no new errors |
| **PCIe & Networking** | `00:1c.2 -> 05:00.0` (`I226-V`, `enp5s0`): **`2500Mb/s` Full**, `LnkSta 5GT/s x1`, `ASPM Disabled`, `CommClk+`, **0 AER / NIC errors**; `00:19.0` (`I218-V`): MAC preserved; `00:1c.0 -> 02:00.0` (`ASM1083` PCI bridge): `2.5GT/s x1` |
| **Display & Audio** | `card1-HDMI-A-3` connected (`AOC 28E850`, `1920x1080`, `dpms=On`); `Realtek ALC892` (`1043:860b`, `NID 0x11=0x90460130`) + Haswell HDMI audio |
| **Sensors, RAPL & EC** | `nct6791-isa-0290` (`Vcore=888–896 mV`, `+3.3V=3.38V`, `3VSB=3.41V`, `Vbat=3.28V`, `fan2=860–870 RPM`, `SYSTIN=+31°C`); `coretemp` `Pkg +41–42°C`; `intel-rapl` `PL1=88W`/`PL2=110W`; `ENE EC: MBE0-Z97-0121` |
| **Serial, USB, IOMMU & SPI** | `ttyS0` `0x03f8` IRQ `4` (`115200 8N1`); `14` USB2 + `6` USB3 (`xhci_hcd`); `15` IOMMU groups + `/dev/kvm`; `dmesg -l err,crit,alert,emerg`: **0 errors** |

---

## Building from Source

1. Check out upstream Coreboot (`df45ea2c49b4`) with submodules and apply the 3 patches in [`patches/`](patches/):
   ```bash
   git clone https://review.coreboot.org/coreboot.git
   cd coreboot && git checkout df45ea2c49b4 && git submodule update --init --recursive
   git apply ../patches/0001-haswell-z97a-above4g-peg-dmi-gma.patch
   git apply ../patches/0002-lynxpoint-z97a-dmi-sata-pcie-aspm.patch
   git -C 3rdparty/libgfxinit apply ../../../patches/0003-libgfxinit-z97a-port-limits.patch
   ```
2. Build both complete `8 MiB` `.bin` images (`with-me` and `without-me`, reusing one EDK2+iPXE build):
   ```bash
   RELEASE_TAG=v26.09 ./scripts/build_release_roms.sh
   ```
   Or build a single variant with `VARIANT_NAME=with-me ./scripts/build.sh` (`with-me` or `without-me`). Automated builds also run on every push and on Coreboot's quarterly stable release cadence (`YY.03`/`06`/`09`/`12`) via [`.github/workflows/coreboot-release.yml`](.github/workflows/coreboot-release.yml), publishing ONLY the two verified 8 MiB `.bin` files.

> **Optional `M.2` vs `PCIEX1_1`/`PCIEX1_2` Selection:** Because ASUS Z97-A `PCIEX1_1` (`RP8`), `PCIEX1_2` (`RP7`), and `M.2 Socket 3` (`x2`) share PCH `HSIO 9..12` through an `ASM1480` mux controlled by `PCHSTRP9`, use [`scripts/switch-m2-ifd.py`](scripts/switch-m2-ifd.py) (`pcie-x1` default vs `m2-x2`) before flashing IFD if you move from add-in `x1` cards to an `M.2` PCIe SSD.

---

## Safe Flashing & Recovery

1. **Always keep your original ASUS SPI chip (or a full external backup) untouched** for instant physical recovery.
2. **External SPI Programmer (`CH341A` + `W25Q64FV` QE=1 Requirement):**
   - When flashing a blank or external Winbond `W25Q64FV` DIP-8 chip, **always replace `blobs/gbe-z97-a.bin` (`0x001000..0x002fff`) with your own board's `FREG3` GbE region** so you keep your motherboard's unique LAN MAC address.
   - ASUS Z97-A requires **non-volatile Quad Enable (`SR1=0x00`, `SR2=0x02`, `QE=1`)** on `W25Q64FV` or the board will not POST even if flash verification passes. Because `QE=1` lives in a silicon status register outside the `.bin` image, flash blank chips with [`scripts/flash-ch341a.sh`](scripts/flash-ch341a.sh) (`flashrom -p ch341a_spi -w <file>.bin -v <file>.bin` + `python3 scripts/ch341a-w25q64-qe.py --set-qe`) in one step.
3. **Updating Once Coreboot is Running (Internal `flashrom` or UEFI `z97a_tool.efi`):**
   - Coreboot's FMAP places `RW_MRC_CACHE` at `0x180000..0x18ffff`, `SMMSTORE` at `0x190000..0x20ffff`, `FMAP` at `0x210000..0x210fff`, and `COREBOOT` at `0x211000..0x7fffff`.
   - Write **only** `0x00211000:0x007fffff coreboot` (`flashrom -p internal -c "W25Q64BV/W25Q64CV/W25Q64FV" -l layout.txt -i coreboot -w <full.rom> --noverify-all`) or use [`tools/z97a_uefi_tool/z97a_tool.c`](tools/z97a_uefi_tool/z97a_tool.c) so `IFD`, `GbE`, `ME`, `QE=1`, `RW_MRC_CACHE`, and `SMMSTORE` remain intact.

---

## Important Platform Notes

- **Minor Kernel Log Warning (`SystemIO 0x480-0x4cf conflicts with \_SB.PCI0.GPLV`)**: Harmless upstream Haswell/LynxPoint ACPI GPIO OpRegion overlap notice in Linux `dmesg`.
- **Intel ME ICC BCLK**: Runs at Intel ME's stock down-spread default (~`99.77 MHz`); standard `chrony`/`systemd-timesyncd` keeps wall-clock drift negligible.
- **DIGI+ VRM / EPU / TPU Safety Policy**: `digi_vrm_apply`, `epu_mode`, and `cpu_turbo_ratio_all_core` default to **Disabled / Stock (`0`)** in [`cfr.c`](board/src/mainboard/asus/z97-a/cfr.c) so the board boots safely on stock SVID/EC defaults until opted in via UEFI Setup (`ESC`).
- **Linux Sensor Persistence**: Copy [`configs/asus-z97-a-sensors.conf`](configs/asus-z97-a-sensors.conf) to `/etc/sensors.d/asus-z97-a.conf` and add `nct6775` to `/etc/modules-load.d/nct6775.conf`.
- **LLM / AI-Assisted Engineering Disclaimer**: Portions of the board port, chipset patches, reverse-engineering notes, UEFI diagnostic tooling, skill workflows, and documentation were authored or refined with Large Language Model (LLM) assistance under human supervision, and cross-verified against the Intel 9-Series PCH Datasheet, ASUS Z97-A User Guide, stock `3503` SPI disassembly, and live physical hardware telemetry.

---

## Documentation & Skills Index

| File | Contents |
|---|---|
| [**`docs/COREBOOT-HW-SW-FLOW.md`**](docs/COREBOOT-HW-SW-FLOW.md) | 3 Mermaid architecture flowcharts (Software Boot Flow, Z97-A Hardware & Flex-I/O `HSIO 1..18` Interconnect, OS Sensor/EPU/TPU Flow) + verified `v0.15` metrics |
| [**`docs/COREBOOT-VS-STOCK-PARITY-REPORT.md`**](docs/COREBOOT-VS-STOCK-PARITY-REPORT.md) | Complete SPI, silicon, PCIe, SATA, audio, display, and `192.168.1.150` live hardware parity report |
| [**`docs/Z97-A-HARDWARE-AND-PCIE.md`**](docs/Z97-A-HARDWARE-AND-PCIE.md) | Onboard IC inventory, Flex-I/O `M.2`/`PCIEX1_1`/`PCIEX1_2`/`SATA Express` muxing, Haswell `CFG[6:5]` PEG bifurcation, Above-4G / ReBar / SR-IOV / IOMMU |
| [**`docs/STOCK-ROM-FEATURES-AUDIT.md`**](docs/STOCK-ROM-FEATURES-AUDIT.md) | Reverse-engineered `ENE EC` (`MBE0-Z97-0121`), `PcieLaneDXE`, `Q-LEDs`, `EPU`/`TPU`/`DirectKey`, `Q-Fan` banks, `ErP S4/S5`, and `DIGI+ ASP1252` |
| [**`docs/DIGI-VRM-TPU-EPU-ALGORITHMS.md`**](docs/DIGI-VRM-TPU-EPU-ALGORITHMS.md) | Decoded `ASP1252` EC Page 0/1/2 register map, telemetry decoding, `EPU` `-20 mV`, `TPU` 3-way switch, and LPC Port `80h` |
| [**`docs/REA-STOCK-ROM-DEEP-AUDIT.md`**](docs/REA-STOCK-ROM-DEEP-AUDIT.md) & [**`docs/REA-FIRMWARE-AUDIT.md`**](docs/REA-FIRMWARE-AUDIT.md) | Full stock `3503` PEI/DXE/SMM disassembly audit vs Coreboot drivers |
| [**`.agents/skills/`**](.agents/skills/) | Reusable agent skills (`coreboot-build-and-flash`, `bios-image-inspection`, `serial-communication`, `z97-a-digi-vrm-tpu-epu`, `z97-a-proxmox-bringup`) |
