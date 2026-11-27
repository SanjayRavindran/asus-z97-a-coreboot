# ASUS Z97-A Hardware, Chipset & PCIe Architecture Reference

**Board:** ASUSTeK `Z97-A` `Rev 1.03B` (`Rev 1.xx` DMI) | **Target Host:** `pve` (`192.168.1.150`)
**CPU Installed:** Intel Core `i7-4790K` (`CPUID 0x306c3`, Devil's Canyon / Haswell)
**RAM Installed:** `32 GiB` (`4x 8 GiB` Kingston DDR3-1866 1.5V, dual-channel `0x50..0x53`)
**Add-In Card:** Intel `I226-V` 2.5GbE (`05:00.0`, `8086:125c`, installed in `PCIEX1_2`)

---

## 1. Onboard Chip / IC Inventory & Prioritized Datasheet List

### Priority 1 — High Value for Coreboot & Hardware Tuning

| Chip / Reference | Board Role | Verified Evidence (Z97-A Capture) | Why Datasheet Helps |
| :--- | :--- | :--- | :--- |
| **Nuvoton `NCT6791D`** | Super I/O, Hardware Monitor (`0x0290`), Fan Xpert (`CPU_FAN`, `OPT_FAN`, `CHA_FAN1..4`), Front Panel LED, `COM1`, PS/2, GPIO `0..8` | `superiotool.txt`: `id=0xc803` at `0x2e`; `LDN 0x0b` `io 0x60=0x0290`, `0xf7=0x87`, `0xf8=0x47`; `CR 0x28` bit 4 locks `0x295`/`0x296` | Fan curve registers, SmartFan IV banks, GPIO muxes, LED S0/S3 blink modes, voltage/temp scaling |
| **ASMedia `ASM1480`** | PCIe 3.0/2.0 4-lane 2:1 high-speed differential mux/demux switches (x4 bank for `PCIEX16_1`/`_2` + chipset lane muxing) | Z97-A PCB BOM / stock `M.2` vs `PCIEX1_1`/`_2` switching | Pinout & `SEL` polarity for identifying which PCH GPIO toggles `M.2` vs `PCIEX1_1`/`_2` and CPU `x16` vs `x8/x8` switches |
| **Intel 4th Gen Core Processor (LGA1150) EDS Vol. 1 & 2** | CPU, IMC, HD 4600 iGPU (`8086:0412`), Mini-HD Audio (`03.0`), Host Bridge (`8086:0c00`), PEG `01.0/.1/.2`, VT-d (`dmar0`/`dmar1`) | `/proc/cpuinfo`: `address sizes: 39 bits physical, 48 bits virtual`; `CAPID0_A` VT-d enabled; `lspci` `00:01.0` `LnkCap 8GT/s x16` | `CFG[6:5]` strap electrical rules (`11=x16`, `10=x8/x8`, `00=x8/x4/x4`), `DEVEN` (`0x54`), `TOUUD`, OC Mailbox MSR `0x150` |
| **Intel 9 Series Chipset Family (Z97) PCH EDS** | PCH (`8086:8cc4`), xHCI (`8086:8cb1`), EHCI (`8ca6`/`8cad`), AHCI (`8c82`), SMBus (`8ca2`), HDA (`8ca0`), MEI (`8cba`), `RP01..08` | `lspci -nnvvvxxx.txt`, `inteltool.txt`: `RCBA`, `STRPFUSECFG1=0x41fa6801`, `STRPFUSECFG2=0x07002800`, 76 GPIOs | `STRPFUSECFG` lane merging (`RPC`), IOBP USB2/SATA/PCIe tuning, `SATALED#`, `GPE0_EN`, `gen1..4_dec` |

### Priority 2 — Reference Datasheets (Already Functional in Coreboot)

| Chip / Reference | Board Role | Verified Evidence (Z97-A Capture) | Notes |
| :--- | :--- | :--- | :--- |
| **Winbond `W25Q64FV`** | 8 MiB DIP-8 socketed SPI NOR Flash | `flashrom`: `0xef 0x4017`; requires non-volatile `SR2 = 0x02 (QE=1)` | Confirmed critical: `QE=0` prevents Z97-A boot |
| **Intel `I218-V`** | Onboard Gigabit Ethernet PHY + MAC | `00:19.0`, `8086:15a1`, subsys `1043:85c4`, MAC `9c:5c:8e:90:7e:f2` | Mapped to PCH RP3 (`STRPFUSECFG1`), `gbe-z97-a.bin` (`8 KiB`) |
| **Realtek `ALC892`** | 8-ch HD Audio Codec | `codec#0` (`0x10ec0892`), subsys `0x1043860b`, on HDA `00:1b.0` | All 14 pin widget verbs captured in `hda_verb.c` |
| **ASMedia `ASM1083`** | PCIe 2.0 x1-to-32-bit PCI Bridge (`PCI1`, `PCI2`) | `03:00.0`, `1b21:1080`, subsys `1043:8489`, behind PCH RP4 (`00:1c.3`) | Standard PCI bridge; works natively |
| **ASMedia `ASM1442K`** | High-speed HDMI/DVI-D TMDS Level Shifter | Z97-A PCB BOM / `data.vbt` (`DDI-B` DVI-D, `DDI-C` DP/HDMI, `DDI-D` DVI/HDMI) | Driven automatically via Intel VBT + `libgfxinit` |

### Priority 3 — Proprietary ASUS ASICs, Memory & Add-In Hardware

| Device | Board Role | Verified Evidence (Z97-A Capture) | Notes |
| :--- | :--- | :--- | :--- |
| **ASUS `DIGI+ ASP1252`** | Digital PWM CPU VRM Controller (8-phase virtual / 4-phase doubled w/ NXP LF-PAK MOSFETs) | Z97-A VRM subsystem (`asus.com` spec / PCB review) | Controlled automatically over CPU SVID; no open datasheet needed |
| **ASUS `TPU` & `EPU` Microcontrollers** | Hardware auto-OC (`TPU` switch) & power management (`EPU` switch) | Stock `gen2_dec=0x007c0a01` (`0x0a00..0x0a7f` active live bytes) + `i2c-0` SMBus `0x14`, `0x44`, `0x5a` | Proprietary ASUS EC/ASICs; SMBus addresses `0x14`/`0x44`/`0x5a` observed live on `i2c-0` |
| **4x Kingston DDR3-1866 8 GiB** | System Memory (32 GiB total, 1.5V) | SMBus `0x50..0x53`; `dmidecode -t 17`: 2x `DDR3 1866 8G`, 2x `HX318C10F/8` | Initialized via `USE_NATIVE_RAMINIT` (dual-channel `spd_addresses`) |
| **Intel `I226-V`** | Add-in PCIe 2.5GbE NIC (2500BASE-T) | `05:00.0`, `8086:125c`, on PCH RP7 (`00:1c.6`), `PCIEX1_2` | Supports LTR, L1 Substates, MSI-X (5 vectors), PTM |

> **Boardview Note:** The ASUS `Z97-A REV 1.03B` `.FZ` boardview uses OpenBoardView's 44-word RC6 key (`FZKey`). All PCH/M.2/`PCIEX1_1`/`PCIEX1_2`/`SATA Express` muxing rules below were proven directly via `PcieLaneDXE` disassembly, `PCHSTRP9`/`PCHSTRP18`, Intel 9-Series PCH Datasheet Table 2-1, and the ASUS Z97-A User Guide.

---

## 2. PCIe Port Wiring, Muxing & Bifurcation Analysis

### A. PCH Z97 PCIe 2.0 Root Ports (`00:1c.0` .. `00:1c.7`) & M.2 / `PCIEX1` Muxing

Stock strap dumps from `lspci-nnvvvxxx.txt`:
* `00:1c.0` `0xfc` (`STRPFUSECFG1`) = `0x41fa6801` -> RPC = `00b` (`4x1` nominal), GbE mapped to **RP3 (`00:1c.2`)** (`((0x41fa6801 >> 16) & 7) + 1 = 3`).
* `00:1c.4`/`00:1c.6` `0xfc` (`STRPFUSECFG2`) = `0x07002800` -> RPC = `00b` (`1+1+1+1` links across `RP5..RP8`, `5.0 GT/s` `x1` / `x2` capability).

Logical PCH slot & storage sharing on the Z97-A:

| Interface Pool | Shared Resources | Default Mode (Stock Captured GPIOs) | Behavior in Coreboot |
| :--- | :--- | :--- | :--- |
| **Pool 1: `M.2 Socket 3` vs `PCIEX1_1` & `PCIEX1_2`** | `M.2` (PCIe 2.0 `x2`, 10 Gb/s) shares chipset PCIe lanes via onboard `ASM1480` muxes with `PCIEX1_1` + `PCIEX1_2` | **`PCIEX1_1` + `PCIEX1_2` active** (`I226-V` active on `00:1c.6` RP7; `M.2` disabled) | Preserved by `gpio.c` / `bootblock.c` so `I226-V` keeps working (`PCIEX1_1`, `PCIEX1_2` usable). Mutually exclusive with PCIe M.2 at the PCB mux level. |
| **Pool 2: `M.2 Socket 3` vs `SATA Express` / `SATA6G_5/6`** | `SATA Express` shares chipset PCIe/SATA Flex-I/O lanes with `M.2` (PCIe mode) and `SATA5`/`SATA6` | `sata_port_map = 0x3f` (6 SATA ports enabled in AHCI) | Enabling all PCH RPs (`1c.0..1c.7 on`) lets Coreboot enumerate whatever PCH ports are physically routed by board straps & GPIOs (`lynxpoint/pcie.c` auto-hides `RP3` GbE). |
| **Legacy PCI Bridge (`00:1c.3` RP4)** | `03:00.0` `ASMedia ASM1083` (`1b21:1080`) -> `PCI1` & `PCI2` slots | Always routed (`x1`) | Enabled in `devicetree.cb` (`device pci 1c.3 on`). |

> If you switch from `PCIEX1_2` (`I226-V`) to an `M.2` NVMe SSD later, inspect the `ASM1480` `SEL` net next to the M.2 socket in `openboardview.exe` — we can wire that single PCH/SIO GPIO into the UEFI Setup (`cfr.c`) so switching requires no rebuild.

### B. CPU Haswell PCIe 3.0 PEG Bifurcation (`00:01.0`, `00:01.1`, `00:01.2`)

1. **Silicon Capability (i7-4790K / LGA1150 Haswell):**
   * Haswell desktop processors provide **16 PCIe 3.0 lanes** divided across **3 PCIe root ports**: `00:01.0` (`PEG10`), `00:01.1` (`PEG11`), and `00:01.2` (`PEG12`).
   * **Why `x4/x4/x4/x4` is NOT supported natively:** Haswell silicon only implements **3 root ports**, never 4. The supported hardware configurations selected by CPU pins **`CFG[6:5]`** (sampled at reset deassertion) are:
     * `CFG[6:5] = 11`: **`x16`** (`PEG10` x16; `PEG11`/`PEG12` disabled)
     * `CFG[6:5] = 10`: **`x8 / x8`** (`PEG10` x8 + `PEG11` x8; `PEG12` disabled)
     * `CFG[6:5] = 00`: **`x8 / x4 / x4`** (`PEG10` x8 + `PEG11` x4 + `PEG12` x4)
     * `CFG[6:5] = 01`: Reserved
   * A true `x4/x4/x4/x4` quad-bifurcation requires either a **PLX/Broadcom/ASMedia PCIe switch riser card** or a HEDT/server/newer CPU platform with 4+ root ports per x16 group.
2. **ASUS Z97-A PCB Wiring for `x8/x4/x4`:**
   * On the Z97-A, a bank of `ASM1480` switches routes lanes `8..15` between `PCIEX16_1` (`16` lanes) and `PCIEX16_2` (`8` wired lanes) based on `PCIEX16_2` `PRSNT#` / `CFG5`.
   * Because **every LGA1150 CPU hard-latches `CFG[6:5]` at reset** (no software register overrides `CFG[6:5]`), getting **`x8 / x4 / x4`** in `PCIEX16_1` (`x8` + bifurcated riser or split) or `PCIEX16_2` (`x4` + `x4` on an unswitched/riser card) requires **both**:
     1. Physical **`CFG6 = 0` and `CFG5 = 0`** strap at reset (pulling socket/CPU `CFG6` pad to `GND`, plus `CFG5 = 0`), **and**
     2. Firmware leaving **`00:01.0`, `00:01.1`, and `00:01.2`** enabled (`DEVEN_D1F0EN | DEVEN_D1F1EN | DEVEN_D1F2EN`).
   * **What our Coreboot build now includes:** `00:01.2` is enabled in `devicetree.cb`, and a **`PCIe x16 Slot Bifurcation`** UEFI Setup option (`peg_bifurcation`) is added to [`cfr.c`](../board/src/mainboard/asus/z97-a/cfr.c) + [`0001-haswell-z97a-above4g-peg-dmi-gma.patch`](../patches/0001-haswell-z97a-above4g-peg-dmi-gma.patch) (`Auto`, `Force 1x16`, `2x8`, `1x8+2x4`) so `PEG10`/`PEG11`/`PEG12` all train and appear cleanly when `CFG[6:5]=00` is strapped.

---

## 3. Enabled Performance, Virtualization & Stability Features

| Feature | Implementation in Z97-A Coreboot Build (`v0.16` / `v26.09-r2`) | Verification on Linux / Proxmox |
| :--- | :--- | :--- |
| **True 64-bit Above-4G MMIO (`QWordMemory`)** | Patched `src/northbridge/intel/haswell/northbridge.c` + `acpi/hostbridge.asl` (`PM64` window from `TOUUD` to `1 << 39` = `512 GiB` CPU PA) + `CONFIG_ALWAYS_ALLOW_ABOVE_4G_ALLOCATION=y` + `CONFIG_PCIEXP_HOTPLUG_PREFETCH_MEM_ABOVE_4G=y` | `dmesg \| grep "root bus resource"` shows `[mem 0x87ae00000-0x7fffffffff window]` |
| **Resizable BAR (ReBar up to 32 GiB)** | `CONFIG_PCIEXP_SUPPORT_RESIZABLE_BARS=y`, `CONFIG_PCIEXP_DEFAULT_MAX_RESIZABLE_BAR_BITS=35` (`32 GiB` max BAR), EDK2 `PcdPcieResizableBarSupport=TRUE` (wired via `payloads/external/edk2/Makefile`) | `lspci -vv` shows `Physical Resizable BAR` resized cleanly above `4 GiB` |
| **SR-IOV + ARI + Hotplug Aperture** | Coreboot `pciexp_dev_read_resources()` automatically scans `PCIE_EXT_CAP_SRIOV_ID` (`TOTAL_VFS` * VF BAR, aligned above `4G` if `>= 16 MiB`) + `CONFIG_PCIEXP_HOTPLUG=y` + EDK2 `PcdSrIovSupport=TRUE` & `PcdAriSupport=TRUE` | `cbmem -c \| grep -i SR-IOV` & `/sys/bus/pci/devices/*/sriov_totalvfs` on SR-IOV NICs/cards |
| **VT-x (`VMX`) & VT-d IOMMU** | `CONFIG_ENABLE_VMX=y` (`MSR 0x3a=0x5` locked); `northbridge.c` + `acpi.c` emit `DMAR` DRHD (`GFXVTBAR 0xfed90000` + `VTVC0BAR 0xfed91000`, `INCLUDE_PCI_ALL`, IOAPIC, HPET MSI) + RMRR | `dmesg \| grep -E "DMAR\|IOMMU"`, `/dev/kvm`, `14` IOMMU groups |
| **PCIe Link Optimization & AER** | `PCIEXP_ASPM=n`, `PCIEXP_CLK_PM=n`, `PCIEXP_L1_SUB_STATE=n` (matching ASUS Manual p. 95–96 defaults; cures `ata5` Flex-I/O COMRESET storm), `PCIEXP_COMMON_CLOCK=y`, `PCIEXP_AER=y`, `PCIEXP_LANE_ERR_STAT_CLEAR=y`, `early_pcie.c` Haswell Gen3 AFE/CTLE recipe | `lspci -vv` shows `ASPM Disabled`, `CommClk+`, `LnkSta 5GT/s x1`, zero `ata5` resets at `6.0 Gbps` |
| **Restore AC Power ON & Wake-on-LAN (`S3/S4/S5`)** | `CONFIG_POWER_STATE_ON_AFTER_FAILURE=y` (`MAINBOARD_POWER_FAILURE_STATE=1`), `wol_enable=1` in [`cfr.c`](../board/src/mainboard/asus/z97-a/cfr.c), [`acpi/wol.asl`](../board/src/mainboard/asus/z97-a/acpi/wol.asl), `gpe0_en_1=0x40042a46`, `0002` patch (`lpc.c` `WOL_EN_OVRD`, `smihandler.c` S5 GPE preservation) | `setpci -s 00:1f.0 a4.w` == `3808`; `/proc/acpi/wakeup` exposes `GLAN`, `PEGP`, `RP01..08`; `ethtool` `Wake-on: g` on `eno0` & `enp5s0` |
| **Minimal / Hidden ME & Security** | `with-me` (`9.1.25.1005`) vs `without-me` (`minimal-116k` `me_cleaner -S -r -t` `ROMP+BUP`, `AltMeDisable=1`, RSA VALID); `DISABLE_ME_PCI=y` + `0002` patch (`cfr.h` default=`1`, `me.c` `FD2` hide even with `AltMeDisable=1`); unlocked `FLMSTR1=0xffff0000` | `lspci -nn -s 00:16.0` is empty (`MEI #1` hidden by default); `flashrom` internal reads/writes unlocked |
| **CPU Power / Undervolt / OC Mailbox & CFR** | `DRIVERS_OPTION_CFR=y` + `SMMSTORE=y` (512 KiB UEFI NVRAM/CFR vars) + `DRIVERS_INTEL_OC_MAILBOX=y` + `INTEL_OC_MAILBOX_ENABLE_UNDERVOLTING=y` + `EDK2_HAVE_EFI_SHELL=y` | Press `ESC` at EDK2 splash -> Coreboot Configuration (`CFR`) persists in `SMMSTORE`; UEFI Shell (`Boot0003`) & `iPXE` (`Boot0004`) built in (supersede `nvramcui`/`coreinfo`) |


---

## 4. Q-LEDs (POST State LEDs), First-Boot UEFI GOP & On-Screen Verbose Logs

### A. ASUS Q-LEDs (`CPU_LED`, `DRAM_LED`, `VGA_LED`, `BOOT_DEVICE_LED`)

Disassembly and `@rea` decompilation of stock PEI modules (`ProjectPEI`, `Memory_OK_PEI`, `AsusPostErrPei`) revealed:

| LED | Hardware Mechanism Found | Coreboot Status |
| :--- | :--- | :--- |
| **`DRAM_LED` (MemOK!)** | `NCT6791D` `LDN 0x08` (`GP07`, `CR 0xe0`/`0xe1` bit 7 active-low; `LDN 0x0b CR 0xfb` bit 7 blink) | **Implemented:** Lit ON in `bootblock.c` before RAM init; turned OFF in `mainboard.c` (`BS_POST_DEVICE`) after RAM init & device init pass |
| **`VGA_LED`, `BOOT_DEVICE_LED`** | Driven via PCH `GPIO38` (`VGA_LED`, `GP_LVL2` bit 6) & `GPIO22` (`BOOT_DEVICE_LED`) (`bootblock.c`/`mainboard.c`) | **Implemented:** Lit sequentially (`DRAM_LED -> VGA_LED -> BOOT_DEVICE_LED`) and extinguished at `BS_PAYLOAD_BOOT` (toggleable via `qled_post_indicators` in CFR) |

### B. First-Boot UEFI GOP & On-Screen Verbose Coreboot Console

| Stage | Driver / Mechanism Loaded at Boot | Behavior on First Boot |
| :--- | :--- | :--- |
| **Coreboot `ramstage`** | `libgfxinit` + stock [`data.vbt`](../blobs/data.vbt) (`MAINBOARD_USE_LIBGFXINIT=y`, `GENERIC_LINEAR_FRAMEBUFFER=y`, `INTEL_GMA_ADD_VBT=y`) | Initializes Haswell HD 4600 (`00:02.0`) directly across VGA, DVI-D (`DDI-B`), DP/HDMI (`DDI-C`), and DVI/HDMI (`DDI-D`) into a native 32-bit linear framebuffer before payload handoff |
| **Coreboot `fb_console.c`** | Renders `CBMEM_ID_CONSOLE` (`256 KiB` ring, loglevel `8` verbose + `TIMESTAMPS_ON_CONSOLE=y` + `CONSOLE_POST=y`) at `BS_PAYLOAD_LOAD` | Displays full color-coded verbose Coreboot boot log on-screen for **5 seconds** (`fb_debug_pause`, configurable `0s`/`3s`/`5s`/`10s` in UEFI Setup CFR) |
| **EDK2 `UefiPayloadPkg`** | `GraphicsOutputDxe.dll` + `GraphicsConsoleDxe.dll` + `CfrSetupMenuDxe.dll` + `PciBusDxe.dll` (`EDK2_LOAD_OPTION_ROMS=y`) | Attaches **UEFI GOP (`GraphicsOutput`)** directly to Coreboot's `libgfxinit` framebuffer on first boot (plus loads Option ROMs/GOP if a discrete GPU is used), and enables `ESC` -> Setup/CFR |

> **Full Priority 3 & Stock ROM Reverse-Engineering Reference:** See [`STOCK-ROM-FEATURES-AUDIT.md`](STOCK-ROM-FEATURES-AUDIT.md) for complete decoded details from the 259 modules in `z97-a-original-chip.bin` (ENE EC `MBE0-Z97-0121` protocol & SMBus bridge, IFD `PCHSTRP9` `0x124`/`PCHSTRP18` `0x148` M.2 vs `PCIEX1_1`/`PCIEX1_2` switching, `VGA_LED` PCH `GPIO38`, `BOOT_DEVICE_LED` PCH `GPIO22`, EPU/TPU/DirectKey pins, `QFanDXE` HWM Banks `1/2/3/8/9`, and `ASUS_EUPSxSMI` ErP S4/S5).

---

## 5. Finalized `Coreboot v0.15` Live Verification (`192.168.1.150`)

Live verification on Proxmox VE 9 (`-z97a-v0.15`, kernel `7.0.14-17-pve`, direct `AOC 28E850` HDMI monitor, and [`COREBOOT-HW-SW-FLOW.md`](COREBOOT-HW-SW-FLOW.md)) confirmed:

- **Intel Z97 Flex-I/O `HSIO 1..18` & SATA/M.2/`PCIEX1` Wiring** (`9-series-chipset-pch-datasheet.pdf` Table 2-1, `Asus Z97-A_UG_V2_WEB.pdf` p. 6–7, 37, 41, 95–97, `PCHSTRP9 = 0xf0544a85`, `PCHSTRP18 = 0x00000003`):
  - `HSIO 1..6`: 6x USB 3.0 ports.
  - `HSIO 7` (`RP3`): onboard `I218-V` (`00:19.0`, `eno0`, MAC `9c:5c:8e:90:7e:f2`).
  - `HSIO 8` (`RP4` -> coalesced `00:1c.0`): `ASMedia ASM1083` (`02:00.0`, `PCI1`/`PCI2`, `2.5 GT/s x1`).
  - `HSIO 9..12` (`RP5..RP8`): `PCIEX16_3 x2` (`00:1c.1`) + `ASM1480` mux between `PCIEX1_1`/`PCIEX1_2` (`00:1c.3` / `00:1c.2 -> 05:00.0 I226-V`, `PCHSTRP9[3:2]=01b`) and `M.2 Socket 3` PCIe 2.0 `x2` (`10b`, PCIe SSD only).
  - `HSIO 13..14` (`SATA Express` black connector): Flex-I/O shared `SATA6G_5/6` (`ata5/ata6`) vs `RP1/RP2 x2` (`00:1c.0 + 0x410 = 0x600`, `PCHSTRP18[1:0]=11b`).
  - `HSIO 15..18`: 4 dedicated gray `SATA6G_1..4` (`ata1..ata4`).
- **Root Cause & Fix for `ata5` (`SATA6G_5` / `CT500BX500SSD1`) Resets**:
  - Disabling `RP7` (`00:1c.2 -> 05:00.0`) ASPM L1 (`50.b: 0x42 -> 0x40`) cured `ata5` resets completely in live testing (`0` resets over `120s`), whereas disabling LTR alone (`00:1c.2+0xe4=0`) with ASPM L1 ON still dropped links (`2` resets in `60s`).
  - Coreboot `v0.15` disables `CONFIG_PCIEXP_ASPM`, `CONFIG_PCIEXP_CLK_PM`, and `CONFIG_PCIEXP_L1_SUB_STATE` for `BOARD_ASUS_Z97_A` (matching ASUS factory BIOS defaults on manual p. 95–96).
  - **Live `Coreboot v0.15` Result**: `ata5: SATA link up 6.0 Gbps (SStatus 133 SControl 300)`, `dd if=/dev/sda` = **`451 MB/s`** (up from `140 MB/s` Gen1), **`0` resets over 120s**, `00:1c.2` & `05:00.0` show `ASPM Disabled`, `CommClk+`, `LnkSta 5GT/s x1`, `DevSta CorrErr-`, `CESta/UESta` all `-`, and `enp5s0` up at **`2500Mb/s` Full**.
- **Sensors, Display, Audio, EPU/TPU & IOMMU**:
  - `nct6775` (`nct6791-isa-0290`, persisted via `/etc/modules-load.d/nct6775.conf`), `coretemp-isa-0000`, and `intel-rapl:0` (`PL1=88W`, `PL2=110W`) all live; `card1-HDMI-A-3` active on `AOC 28E850` (`1920x1080`); `ALC892` (`0x11=0x90460130`), `15` IOMMU groups + `/dev/kvm`, `ENE EC MBE0-Z97-0121`, `EPU=OFF`, `TPU=OFF`, `microcode 0x28`, `32 GiB` RAM, and unlocked `flashrom` (`BIOS_CNTL=0x09`, `FREG0..3` read-write).
