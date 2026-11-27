# ASUS Z97-A (`Rev 1.03B`) Coreboot `v0.15` Hardware & Software Flow

**Target System:** ASUSTeK `Z97-A` `Rev 1.03B` | `i7-4790K` (`CPUID 0x306c3`) | `32 GiB` DDR3-1866 | `CT500BX500SSD1` (`SATA6G_5` / `ata5` @ `6.0 Gbps`) | `I218-V` + `I226-V` 2.5GbE | `AOC 28E850` (`1920x1080` HDMI)
**Finalized Firmware:** `coreboot -z97a-v0.15` + `EDK2 UefiPayloadPkg` + `iPXE` | **OS:** Proxmox VE 9 (`Linux 7.0.14-17-pve`)

> **Boardview `.FZ` Encryption Note:** `datasheets/ASUS Z97-A REV 1.03B - Boardview (.FZ).FZ` (not redistributed in Git) uses OpenBoardView's encrypted RC6 key (`FZKey`, 44 32-bit words). Every electrical mux, strap, and lane assignment below is proven directly from the Intel 9-Series PCH Datasheet (`Table 2-1`, `PCIEPCS`), ASUS Z97-A User Guide (`p. 6, 7, 13, 37, 41, 95-97`), `ASM1480` Datasheet, Stock ROM `IFD` (`PCHSTRP9`/`PCHSTRP18`), Stock `PcieLaneDXE` disassembly, and live hardware telemetry on `192.168.1.150`.

---

## 1. Coreboot `v0.15` Software Boot Flow

```mermaid
flowchart TD
    SPI["W25Q64FV 8 MiB SPI Flash - QE=1 Unlocked FLMSTR1=0xffff0000"]
    IFD["IFD 0x000000..0x000fff + GbE 0x001000..0x002fff + Stock ME 9.1.25.1005 0x003000..0x17ffff"]
    NVRAM["RW_MRC_CACHE 0x180000..0x18ffff + SMMSTORE 512 KiB UEFI NVRAM/CFR 0x190000..0x20ffff"]
    BB["bootblock 0x7f7000 - Microcode 0x28 - NCT6791D COM1 115200 8N1 - HWM 0x290 - DRAM_LED ON"]
    RS["romstage - Haswell Native RAM Init 32 GiB - DMI -6 dB & ASPM OFF - PCH Desktop CG"]
    PC["postcar - Tear down Cache-as-RAM & jump to ramstage"]
    RAM1["ramstage Silicon & Display - libgfxinit HD 4600 + data.vbt - IOMMU DMAR VT-d 15 groups - VGA_LED"]
    RAM2["ramstage PCIe & Storage - PCIEXP_ASPM=n CommClk+ AER - SATA AHCI 6 ports 6.0 Gbps - xHCI / I218-V / ALC892"]
    RAM3["ramstage ASUS Board - ENE EC MBE0-Z97-0121 - DIGI+ ASP1252 - Q-Fan SmartFan IV - EPU/TPU - CFR - fb_console"]
    EDK2["EDK2 UefiPayloadPkg - UEFI GOP AOC 28E850 - CFR Menu ESC - iPXE netboot - SMMSTORE Boot0005"]
    GRUB["GRUB UEFI Bootloader - /EFI/BOOT/BOOTx64.EFI -> proxmox"]
    OS["Proxmox VE 9 Linux 7.0.14-17-pve - ata5 6.0 Gbps - enp5s0 2.5GbE - nct6775 - i915 - KVM/VFIO"]

    SPI --> IFD
    SPI --> NVRAM
    SPI --> BB
    BB --> RS
    RS --> PC
    PC --> RAM1
    RAM1 --> RAM2
    RAM2 --> RAM3
    NVRAM -. "Read MRC cache + CFR/EFI vars" .-> RS
    NVRAM -. "Persist CFR settings + Boot vars" .-> EDK2
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
    PEG1["PCIEX16_1 up to x16 Gen3 / PCIEX16_2 up to x8"]

    PCH["Intel Z97 PCH - DMI 2.0 x4 - 18 Flex-I/O HSIO Lanes"]

    USB_HSIO["HSIO 1..6 - USB 3.0 Ports 1..6 xHCI + 14 USB 2.0"]
    RP3_HSIO["HSIO 7 RP3 -> Intel I218-V GbE eno0 9c:5c:8e:90:7e:f2"]
    RP4_HSIO["HSIO 8 RP4 -> ASMedia ASM1083 -> Legacy PCI1 / PCI2"]

    FLEX_MUX["HSIO 9..12 RP5..8 + ASM1480 Mux - PCHSTRP9 bits 3:2"]
    PCIE_X16_3["PCIEX16_3 max x2 RP5"]
    X1_MODE["Default Stock/Coreboot Strap = 01b -> PCIEX1_1 RP8 + PCIEX1_2 RP7 -> Intel I226-V 2.5GbE enp5s0"]
    M2_MODE["Optional Strap = 10b switch-m2-ifd.py -> M.2 Socket 3 PCIe 2.0 x2 2260/2280 SSD Only"]

    FLEX_SATAE["HSIO 13..14 Flex-I/O SATA Express - PCHSTRP18 bits 1:0 = 11b"]
    SATAE_PORT["SATA Express Black Connector - Shared SATA6G_5/6 ata5/ata6 OR PCIe RP1/RP2 x2"]
    BX500["Live Crucial CT500BX500SSD1 on SATA6G_5 ata5 at 6.0 Gbps"]
    SATA14["HSIO 15..18 Dedicated Gray SATA6G_1..4 ata1..ata4 6.0 Gbps"]

    LPC["LPC + SPI + SMBus + HDA"]
    SIO["NCT6791D SIO 0x2e - COM1 0x3f8 - HWM 0x290 - Q-Fan - EPU/TPU Switches"]
    EC["ENE EC MBE0-Z97-0121 Ports 0x62/0x66 - DIGI+ ASP1252 VRM 0x14/0x24/0x44/0x5a"]
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

    K_CORETEMP["Linux coretemp + intel_rapl_msr"]
    K_HWMON["Linux nct6775 module auto-loaded - /etc/modules-load.d/nct6775.conf"]
    SENSORS["Proxmox VE sensors / sysfs / MSR Telemetry"]

    PKG --> K_CORETEMP --> SENSORS
    SIO_SW --> OC --> PKG
    EC_VRM -. "SMBus + Page 0/1 VRM telemetry" .-> SENSORS
    SIO_HWM --> K_HWMON --> SENSORS
```

---

## 4. Stock ROM Configuration vs. Coreboot `v0.15` Live Verification (`192.168.1.150`)

### A. Stock ROM `IFD` Straps & `PcieLaneDXE` Rules (`z97-a-original-chip.bin`)

| Interface / Pool | PCH Flex-I/O / Chipset Lanes | Stock `IFD` Strap / `Setup` NVAR (`0x1a0077`) | Default Mode & Exclusivity |
| :--- | :--- | :--- | :--- |
| **`PCIEX1_1` + `PCIEX1_2` vs `M.2 Socket 3`** | `HSIO 9..12` (`RP5..RP8`) + `ASM1480` mux | `PCHSTRP9 (0x124) = 0xf0544a85` (`[3:2]=01b`, `[14]=1`), `Setup[0x852]=0x00`, `Setup[0x7ee]=0x01` | **`PCIEX1_1 (RP8)` + `PCIEX1_2 (RP7, I226-V)` active at x1**; `M.2` disabled. Setting `Setup[0x852]=0x01` or `switch-m2-ifd.py m2-x2` switches `[3:2]=10b` (`M.2` PCIe 2.0 `x2`, disables `PCIEX1_1`/`_2`). `M.2` supports **PCIe SSDs only** (no SATA M.2). |
| **`SATA Express` (`SATA6G_5/6`)** | `HSIO 13..14` (`SATA_TX/RX 4..5` / `PER/PET 1..2`) | `PCHSTRP18 (0x148) = 0x00000003` (`[1:0]=11b`), `Setup[0x2d0]=0x01` | Black mid-board `SATA Express` connector accepts **2x SATA 6.0 Gbps (`ata5`/`ata6`) OR PCIe `x2` (`RP1/RP2`)** via Flex-I/O pin ownership (`00:1c.0 + 0x410 = 0x600`). Live Crucial BX500 runs on `SATA6G_5` (`ata5`) at **`6.0 Gbps`**. |
| **`SATA6G_1..4`** | `HSIO 15..18` (`SATA_TX/RX 0..3`) | `sata_port_map = 0x3f` (AHCI) | 4 dedicated gray SATA `6.0 Gbps` ports (`ata1..ata4`). |
| **Onboard `I218-V` / `ASM1083`** | `HSIO 7` (`RP3`) & `HSIO 8` (`RP4`) | `PCHSTRP9[1:0]=01b`, `STRPFUSECFG1[18:16]=010b` | `I218-V` (`00:19.0`, MAC `9c:5c:8e:90:7e:f2`) & `ASM1083` (`00:1c.0 -> 02:00.0` `PCI1/PCI2`) always routed. |
| **PCIe ASPM Default** | Root Complex / `DMI` / `RP1..8` / `PEG` | ASUS Manual p. 95–96: **ALL `[Disabled]`** | Coreboot `v0.15` disables `CONFIG_PCIEXP_ASPM`, `PCIEXP_CLK_PM`, `PCIEXP_L1_SUB_STATE` to match stock defaults (curing the PCH Flex-I/O `ata5` Gen1 reset storm caused by `RP7` ASPM L1). |

### B. Verified `Coreboot v0.15` Live Metrics (`pve` / `192.168.1.150`)

| Subsystem | Live Verification Result (`Coreboot v0.15`) | Key Value / Evidence |
| :--- | :--- | :--- |
| **BIOS / Flash** | Booted cleanly in 33s; internal delta write (`0x211000..0x7fffff`) | `dmidecode`: `-z97a-v0.15` (`11/14/2026`); `BIOS_CNTL=0x09` unlocked; `FREG0..3` read-write |
| **SATA (`ata5`, `SATA6G_5`)** | **`6.0 Gbps` Gen3, 0 resets over 120s + 2 GiB read** | `SStatus 133`, `dd`: **`451 MB/s`** (up from `140 MB/s` Gen1), `ata5 link` delta = **`0`** |
| **PCIe `00:1c.2 -> 05:00.0` (`I226-V`)** | **`2500Mb/s` Full, zero AER errors** | `LnkSta 5GT/s x1`, `ASPM Disabled`, `CommClk+`, `DevSta CorrErr-`, `CESta/UESta` all `-` |
| **Onboard `I218-V` (`00:19.0`)** | MAC preserved in `FREG3` GbE | `eno0`: `9c:5c:8e:90:7e:f2` |
| **Display (`HD 4600` + `AOC 28E850`)** | Native `libgfxinit` + `i915` HDMI active | `card1-HDMI-A-3`: `connected enabled dpms=On`, `1920x1080`, EDID `05e3cd0c` |
| **Sensors (`nct6775` + `coretemp` + `RAPL`)** | Auto-loaded on boot via `/etc/modules-load.d/nct6775.conf` | `Vcore=896mV`, `3VSB=3.41V`, `Vbat=3.28V`, `fan2=870 RPM`, `SYSTIN +31C`, `Pkg +41C`, `PL1=88W`/`PL2=110W` |
| **ENE EC / EPU / TPU / CPU** | Verified live via ports `0x62/0x66`, SIO `0x2e`, MSRs | EC **`MBE0-Z97-0121`**, `EPU=OFF (GP72=1)`, `TPU=OFF (GP26=1, GP41=1)`, `ucode=0x28`, `TURBO=0x2a2b2c2c` |
| **Audio / IOMMU / USB / RAM** | Full parity | `ALC892` (`0x11=0x90460130`), `15` IOMMU groups + `/dev/kvm`, `14` USB2 + `6` USB3, `31Gi` (`32 GiB`) |
