# ASUS Z97-A (`Rev 1.03B`) Coreboot `v0.16` (`v26.09-r2`) Hardware & Software Flow

**Target System:** ASUSTeK `Z97-A` `Rev 1.03B` | `i7-4790K` (`CPUID 0x306c3`) | `32 GiB` DDR3-1866 | `CT500BX500SSD1` (`SATA6G_5` / `ata5` @ `6.0 Gbps`) | `I218-V` + `I226-V` 2.5GbE | `AOC 28E850` (`1920x1080` HDMI)
**Firmware:** `coreboot -z97a-v0.16` / CI Release `v26.09-r2` + `EDK2 UefiPayloadPkg` + `iPXE` + `UEFI Shell` | **OS:** Proxmox VE 9 (`Linux 7.0.14-17-pve`)

> **Full Hardware-to-Software Index:** For complete per-subsystem Mermaid diagrams (SPI layout, WoL/AC Power-ON, `data.vbt` display path, Virtualization/ReBar/SR-IOV) and the Master Hardware-to-Software Mapping Table, see [`HARDWARE-TO-SOFTWARE-MAP.md`](HARDWARE-TO-SOFTWARE-MAP.md).
> **Boardview `.FZ` Note:** `datasheets/ASUS Z97-A REV 1.03B - Boardview (.FZ).FZ` (not redistributed in Git) uses OpenBoardView's encrypted RC6 key (`FZKey`). Every electrical mux, strap, and lane assignment below is proven directly from the Intel 9-Series PCH Datasheet (`Table 2-1`, `PCIEPCS`), ASUS Z97-A User Guide (`p. 6, 7, 13, 37, 41, 95-97`), `ASM1480` Datasheet, Stock ROM `IFD` (`PCHSTRP9`/`PCHSTRP18`), Stock `PcieLaneDXE` disassembly, and live hardware telemetry on `192.168.1.150`.

---

## 1. Coreboot `v0.16` / `v26.09-r2` Software Boot Flow

```mermaid
flowchart TD
    SPI["W25Q64FV 8 MiB SPI Flash - QE=1 Unlocked FLMSTR1=0xffff0000"]
    IFD["IFD + GbE + ME: with-me Stock 9.1.25.1005 OR without-me 116 KiB ROMP+BUP AltMeDisable=1"]
    NVRAM["RW_MRC_CACHE 0x180000..0x18ffff + SMMSTORE 512 KiB UEFI NVRAM/CFR 0x190000..0x20ffff"]
    BB["bootblock 0x7f7000 - Microcode 0x28 - NCT6791D COM1 115200 8N1 - HWM 0x290 - DRAM_LED ON"]
    RS["romstage - Haswell Native RAM Init 32 GiB - DMI -6 dB & ASPM OFF - PCH Desktop CG"]
    PC["postcar - Tear down Cache-as-RAM & jump to ramstage"]
    RAM1["ramstage Silicon & Virt - libgfxinit + data.vbt - PM64 Above-4G - DMAR VT-d 14 groups - VMX MSR 0x3a=0x5"]
    RAM2["ramstage PCH & Storage - PCIEXP_ASPM=n ReBar 32G SR-IOV - SATA 6.0 Gbps - MEI #1 hidden FD2 - GEN_PMCON_3=0x3808"]
    RAM3["ramstage ASUS & ACPI - ENE EC MBE0-Z97-0121 - DIGI+ VRM - Q-Fan - EPU/TPU - wol.asl _PRW - fb_console"]
    EDK2["EDK2 UefiPayloadPkg - UEFI GOP - CFR Menu ESC - ReBar TRUE - UEFI Shell - iPXE netboot"]
    GRUB["GRUB UEFI Bootloader - /EFI/BOOT/BOOTx64.EFI -> proxmox"]
    OS["Proxmox VE 9 Linux 7.0.14-17-pve - ata5 6.0G - enp5s0 2.5GbE - WoL GLAN/RP01..08 - nct6775 - KVM/VFIO"]

    SPI --> IFD
    SPI --> NVRAM
    SPI --> BB
    BB --> RS
    RS --> PC
    PC --> RAM1
    RAM1 --> RAM2
    RAM2 --> RAM3
    NVRAM -. "Read MRC cache + CFR/EFI vars" .-> RS
    NVRAM -. "Persist CFR defaults + Boot vars" .-> EDK2
    RAM3 --> EDK2
    EDK2 --> GRUB
    GRUB --> OS
```

---

## 2. Z97-A Hardware & Intel Flex-I/O Interconnect (`2x PCIe x1`, `M.2`, `SATA 1..4`, `SATA Express 5/6`)

```mermaid
flowchart LR
    CPU["Intel Core i7-4790K LGA1150"]
    IMC["Dual-Ch DDR3-1866 32 GiB - SMBus SPD 0x50..0x53"]
    DDI["HD 4600 iGPU DDI-B/C/D -> ASM1442K -> HDMI AOC 28E850 / DVI / DP / VGA"]
    PEG_MUX["ASM1480 CPU PEG Switches - CFG6:5 Straps"]
    PEG1["PCIEX16_1 up to x16 Gen3 / PCIEX16_2 up to x8 - PEGP/PEG1/PEG2 WoL _PRW"]

    PCH["Intel Z97 PCH - DMI 2.0 x4 - 18 Flex-I/O HSIO Lanes"]

    USB_HSIO["HSIO 1..6 - USB 3.0 Ports 1..6 xHCI + 14 USB 2.0"]
    RP3_HSIO["HSIO 7 RP3 -> Intel I218-V GbE eno0 9c:5c:8e:90:7e:f2 + GLAN WoL S4"]
    RP4_HSIO["HSIO 8 RP4 -> ASMedia ASM1083 -> Legacy PCI1 / PCI2"]

    FLEX_MUX["HSIO 9..12 RP5..8 + ASM1480 Mux - PCHSTRP9 bits 3:2"]
    PCIE_X16_3["PCIEX16_3 max x2 RP5"]
    X1_MODE["Default Strap = 01b -> PCIEX1_1 RP8 + PCIEX1_2 RP7 -> Intel I226-V 2.5GbE enp5s0 + RP03 WoL"]
    M2_MODE["Optional Strap = 10b switch-m2-ifd.py -> M.2 Socket 3 PCIe 2.0 x2 2260/2280 SSD Only"]

    FLEX_SATAE["HSIO 13..14 Flex-I/O SATA Express - PCHSTRP18 bits 1:0 = 11b"]
    SATAE_PORT["SATA Express Black Connector - Shared SATA6G_5/6 ata5/ata6 OR PCIe RP1/RP2 x2"]
    BX500["Live Crucial CT500BX500SSD1 on SATA6G_5 ata5 at 6.0 Gbps"]
    SATA14["HSIO 15..18 Dedicated Gray SATA6G_1..4 ata1..ata4 6.0 Gbps"]

    LPC["LPC + SPI + SMBus + HDA"]
    SIO["NCT6791D SIO 0x2e - COM1 0x3f8 - HWM 0x290 - Q-Fan - EPU/TPU Switches"]
    EC["ENE EC MBE0-Z97-0121 Ports 0x62/0x66 - DIGI+ ASP1252 VRM"]
    AUD["Realtek ALC892 HDA + Winbond W25Q64FV SPI + TPM"]

    CPU --- IMC
    CPU --- DDI
    CPU --- PEG_MUX
    PEG_MUX --> PEG1
    CPU <-- "DMI 2.0 x4 -6 dB ASPM OFF" --> PCH

    PCH --> USB_HSIO
    PCH --> RP3_HSIO
    PCH --> RP4_HSIO
    PCH --> FLEX_MUX
    FLEX_MUX --> PCIE_X16_3
    FLEX_MUX --> X1_MODE
    FLEX_MUX -. "Mutually Exclusive PCB Mux" .- M2_MODE

    PCH --> FLEX_SATAE
    FLEX_SATAE --> SATAE_PORT
    SATAE_PORT --> BX500
    PCH --> SATA14

    PCH --- LPC
    LPC --- SIO
    LPC --- EC
    LPC --- AUD
```

---

## 3. OS Sensors, Telemetry, EPU/TPU & Power/Thermal Control Flow

```mermaid
flowchart LR
    PKG["CPU i7-4790K DTS + RAPL MSRs - PL1=88W PL2=110W Microcode 0x28"]
    OC["Coreboot mainboard.c - MSR 0x1ad Turbo 42..44x + OC Mailbox 0x150"]
    SIO_SW["NCT6791D Physical Switches - EPU GP72 LDN7 0xe1 / TPU GP26 GP41 LDN9"]
    SIO_HWM["NCT6791D HWM 0x290 - 5 PWM SmartFan IV Q-Fan + Voltages + Thermistors"]
    EC_VRM["ENE EC MBE0-Z97-0121 0x62/0x66 <-> DIGI+ ASP1252 VRM"]
    PM_WOL["PCH GEN_PMCON_3=0x3808 (Power ON + WOL_EN_OVRD) + acpi/wol.asl"]

    K_CORETEMP["Linux coretemp + intel_rapl_msr"]
    K_HWMON["Linux nct6775 module auto-loaded - /etc/modules-load.d/nct6775.conf"]
    SENSORS["Proxmox VE sensors / sysfs / acpi wakeup / MSR Telemetry"]

    PKG --> K_CORETEMP --> SENSORS
    SIO_SW --> OC --> PKG
    EC_VRM -. "SMBus + Page 0/1 VRM telemetry" .-> SENSORS
    SIO_HWM --> K_HWMON --> SENSORS
    PM_WOL --> SENSORS
```

---

## 4. Stock ROM Configuration vs. Coreboot `v0.16` Live Verification (`192.168.1.150`)

### A. Stock ROM `IFD` Straps & `PcieLaneDXE` Rules (`z97-a-original-chip.bin`)

| Interface / Pool | PCH Flex-I/O / Chipset Lanes | Stock `IFD` Strap / `Setup` NVAR (`0x1a0077`) | Default Mode & Exclusivity |
| :--- | :--- | :--- | :--- |
| **`PCIEX1_1` + `PCIEX1_2` vs `M.2 Socket 3`** | `HSIO 9..12` (`RP5..RP8`) + `ASM1480` mux | `PCHSTRP9 (0x124) = 0xf0544a85` (`[3:2]=01b`, `[14]=1`), `Setup[0x852]=0x00`, `Setup[0x7ee]=0x01` | **`PCIEX1_1 (RP8)` + `PCIEX1_2 (RP7, I226-V)` active at x1**; `M.2` disabled. Setting `Setup[0x852]=0x01` or `switch-m2-ifd.py m2-x2` switches `[3:2]=10b` (`M.2` PCIe 2.0 `x2`, disables `PCIEX1_1`/`_2`). `M.2` supports **PCIe SSDs only** (no SATA M.2). |
| **`SATA Express` (`SATA6G_5/6`)** | `HSIO 13..14` (`SATA_TX/RX 4..5` / `PER/PET 1..2`) | `PCHSTRP18 (0x148) = 0x00000003` (`[1:0]=11b`), `Setup[0x2d0]=0x01` | Black mid-board `SATA Express` connector accepts **2x SATA 6.0 Gbps (`ata5`/`ata6`) OR PCIe `x2` (`RP1/RP2`)** via Flex-I/O pin ownership (`00:1c.0 + 0x410 = 0x600`). Live Crucial BX500 runs on `SATA6G_5` (`ata5`) at **`6.0 Gbps`**. |
| **`SATA6G_1..4`** | `HSIO 15..18` (`SATA_TX/RX 0..3`) | `sata_port_map = 0x3f` (AHCI) | 4 dedicated gray SATA `6.0 Gbps` ports (`ata1..ata4`). |
| **Onboard `I218-V` / `ASM1083`** | `HSIO 7` (`RP3`) & `HSIO 8` (`RP4`) | `PCHSTRP9[1:0]=01b`, `STRPFUSECFG1[18:16]=010b` | `I218-V` (`00:19.0`, MAC `9c:5c:8e:90:7e:f2`) & `ASM1083` (`00:1c.0 -> 02:00.0` `PCI1/PCI2`) always routed. |
| **PCIe ASPM Default** | Root Complex / `DMI` / `RP1..8` / `PEG` | ASUS Manual p. 95–96: **ALL `[Disabled]`** | Coreboot disables `CONFIG_PCIEXP_ASPM`, `PCIEXP_CLK_PM`, `PCIEXP_L1_SUB_STATE` to match stock defaults (curing the PCH Flex-I/O `ata5` Gen1 reset storm caused by `RP7` ASPM L1). |

### B. Verified `Coreboot v0.16` Live Metrics (`pve` / `192.168.1.150`)

| Subsystem | Live Verification Result (`-z97a-v0.16-with-me`) | Key Value / Evidence |
| :--- | :--- | :--- |
| **BIOS / Flash** | Booted cleanly; internal delta write (`COREBOOT` + `SMMSTORE`) | `/sys/class/dmi/id/bios_version`: `-z97a-v0.16-with-me`; `BIOS_CNTL=0x09` unlocked; `FREG0..3` read-write |
| **Intel ME (`00:16.0`)** | **Hidden by default via `FD2`** (`minimal-116k` `without-me` also built) | `lspci -nn -s 00:16.0` returns empty; `without-me` uses `116 KiB` `ROMP+BUP` (`AltMeDisable=1`, RSA VALID) |
| **Power & Wake-on-LAN** | **Power ON after AC loss + S3/S4/S5 WoL enabled** | `GEN_PMCON_3 (00:1f.0 a4.w) = 0x3808`; `/proc/acpi/wakeup`: `GLAN`, `PEGP`, `RP01..08`, `XHCI`; `ethtool` `Wake-on: g` on `eno0` & `enp5s0` |
| **Above-4G / Virt / ReBar** | **True 64-bit MMIO, VT-x, VT-d, ReBar & SR-IOV active** | `pci_bus 0000:00: root bus resource [mem 0x87ae00000-0x7fffffffff]`; `MSR 0x3a = 0x5`; `14` IOMMU groups + `/dev/kvm` |
| **SATA (`ata5`, `SATA6G_5`)** | **`6.0 Gbps` Gen3, 0 resets** | `SStatus 133`, `dd`: **`451 MB/s`**, `0` kernel `err`/`crit` |
| **PCIe `00:1c.2 -> 05:00.0` (`I226-V`)** | **`2500Mb/s` Full**, `LnkSta 5GT/s x1`, `ASPM Disabled`, `CommClk+` | `enp5s0` UP; `eno0` (`00:19.0`) MAC `9c:5c:8e:90:7e:f2` preserved |
| **Display (`HD 4600` + `data.vbt`)** | Stock `i915_vbt.bin` (`ada53b3d...`) + native `libgfxinit` | `card1-HDMI-A-3`: `connected` (`AOC 28E850`, `1920x1080` / `3840x2160` EDID); `00:03.0` HDMI Audio + `ALC892` (`0x11=0x90460130`) |
| **Sensors (`nct6775` + `coretemp` + `RAPL`)** | Auto-loaded on boot via `/etc/modules-load.d/nct6775.conf` | `Vcore=888mV`, `+3.3V=3.38V`, `3VSB=3.42V`, `Vbat=3.28V`, `fan2=873 RPM`, `SYSTIN +32°C`, `Pkg +44°C`, `PL1=88W`/`PL2=110W` |
