# ASUS Z97-A Stock ROM (`3503`) Reverse-Engineering & Priority 3 Feature Audit

**Source Image:** [`z97-a-original-chip.bin`](../stock-capture/z97-a-original-chip.bin) (`8,388,608` B, SHA-256 `a1fd7f2974dff0614ad9f7f63784db4bc9cfbb62e0779ad42e76f49589e2cab1`, AMI Aptio IV `3503`)  
**Scope:** Complete extraction of all **Priority 3** proprietary ASUS hardware interfaces (`ENE MBE0-Z97-0121` EC, `M.2` vs `PCIEX1_1`/`PCIEX1_2` mux, `Q-LEDs`, `Q-Fan` SmartFan IV curves, `EPU`/`TPU`/`DirectKey` switches, `ErP S4/S5`, and `DIGI+ ASP1252` SMBus topology) from 259 PEI/DXE/SMM modules inside `FV@0x220000`.

---

## 1. ENE EC (`MBE0-Z97-0121`) Protocol (`EnePEI`, `EneDXE`, `EneUpdDXE`)

ASUS uses a proprietary **ENE Embedded Controller** running firmware **`MBE0-Z97-0121`** connected via the LPC bus.

### A. I/O Ports & Low-Level Handshake
* **Command / Status Port:** `0x66`
* **Data Port:** `0x62`
* **Input Buffer Full (IBF) Wait:** Poll `inb(0x66)` until `(al & 0x06) == 0` (`EneDXE` `0x180000fdc`).
* **Output Buffer Full (OBF) Wait:** Poll `inb(0x66)` until `(al & 0x01) != 0` (`EneDXE` `0x18000101c`).
* **LPC Mailbox Aperture:** `gen2_dec = 0x007c0a01` (`0x0a00..0x0a7f`) preserved in [`devicetree.cb`](../board/src/mainboard/asus/z97-a/devicetree.cb).

### B. EC Commands (`outb(cmd, 0x66)`)
| Command | Direction | Arguments / Payload | Purpose |
| :--- | :--- | :--- | :--- |
| **`0x80`** | Read | Write `addr` to `0x62`, wait OBF, read `data` from `0x62` | **EC RAM Read** (`EneDXE` `0x1800010f4`) |
| **`0x81`** | Write | Write `addr` to `0x62`, write `data` to `0x62` | **EC RAM Write** (`EneDXE` `0x18000114c`) |
| **`0xa1`** | Read Block | Wait OBF & read up to `16` bytes until `0x00` from `0x62` | **Read EC Firmware Version** (`MBE0-Z97-0121`) (`EneDXE` `0x180001188`) |

### C. EC RAM Bank Map & ACPI SMBus HC 2.0 Master Bridge (`EneDXE` `0x1800011e0`, `0x180001210`)
* **Bank Select (`EC[0xff]`):**
  * `EC[0xff] = 0x01`, read `EC[0xf8] == 0xec`: EC presence & readiness signature (`EneDXE` `0x1800011e0`).
  * `EC[0xff] = 0x00`: **Bank 0** (SMBus master bridge, Q-Fan virtual temp, ACPI/SCI flags).
* **ACPI SMBus Host Controller 2.0 in EC Bank 0 (`EneDXE` `0x180001210`):**
  * `EC[0x00]`: Protocol / Trigger (`0x06`=Write Byte, `0x07`=Read Byte, `0x08`=Write Word, `0x09`=Read Word, `0x0a`=Write Block, `0x0b`=Read Block)
  * `EC[0x01]`: Status (`bit 7` `0x80` = Done; `status & 0x1f == 0` = OK)
  * `EC[0x02]`: 8-bit SMBus Slave Address
  * `EC[0x03]`: Command / Register Offset
  * `EC[0x04..0x23]`: Data Buffer (up to 32 bytes)
  * `EC[0x24]`: Block Count
* **SCI / USB / Q-Fan EC RAM Offsets:**
  * `EC[0xd2] |= 0x10` + PCH GPE `0x1e` (`0x182a`/`0x1838`/`GPI_ROUT` bit `0x0e`): Enables EC ACPI SCI (`EneDXE` `0x180000c98`).
  * `EC[0xd3]` bit `7`: Controls ErP / USB standby power state (`EneDXE` `0x180000d20`).
  * `EC[0x3d..0x3f]`: Q-Fan virtual thermal sensor sources (`QFanDXE`).
  * `EC[0x40..0x41]`, `EC[0x43..0x44]`, `EC[0x47]`, `EC[0x88]` (bits `0..3` for `CHA_FAN1..4`), `EC[0x89] = 0xe0`: Q-Fan / EC fan sync (`QFanDXE`).
  * 16-bit EC/protocol register commands `0x1247`, `0x125b`, `0x125c`, `0x125d`, `0x125f`, `0x1270`: Used by `EpuHwModeDxe`, `AsusPostErrDxe`, `OCMR_PEI`/`DXE`, `OC_Tuner`.

> **Coreboot Implementation (`mainboard.c`):** Coreboot performs a strictly **read-only** `0xa1` EC version probe (`ene_ec_read_version()`) at `BS_POST_DEVICE` with a bounded poll timeout and logs `MBE0-Z97-0121` without risking any blind writes to proprietary EC RAM.

---

## 2. `M.2 Socket 3` vs `PCIEX1_1` / `PCIEX1_2` & `SATA Express` Mux (`PcieLaneDXE`)

Disassembly of `PcieLaneDXE` (`0x1800004ac..0x180000981`) proved how ASUS switches `M.2`, `PCIEX1_1`/`PCIEX1_2`, and `SATA Express`:

* **No discrete GPIO switches the PCIe lanes at runtime** — the lanes are reconfigured by updating **Intel Z97 IFD (Flash Descriptor) PCH Soft Straps** (`PcieLaneDXE` reads `0x86f`-byte `Setup` NVAR into `[rsp+0x40]`, reading `Setup[0x852]` (`[rsp+0x892]`), `Setup[0x7ee]` (`[rsp+0x82e]`), and `Setup[0x2d0]` (`[rsp+0x310]`)):
  * **`IFD + 0x124` (`PCHSTRP9`, stock value `0xf0544a85`, `Setup[0x852]=0x00`, `Setup[0x7ee]=0x01`):**
    * Bits `[3:2]` (`PCIEPCS2` = `01b` -> `0xf0544a85`): **`PCIEX1_1` + `PCIEX1_2` enabled** (`RP5..RP8` configured as `1x2 + 2x1`; **`I226-V` at `00:1c.6` active**).
    * Bits `[3:2]` (`PCIEPCS2` = `10b` -> `0xf0544a89`, when `Setup[0x852]=0x01`): **`M.2 Socket 3` (PCIe 2.0 `x2`) enabled** (`RP5+RP6` merged to `x2`; `PCIEX1_1` & `PCIEX1_2` disabled; supports PCIe SSDs only).
    * Bit `14` (`0x4000`): controlled by `Setup[0x7ee]`.
  * **`IFD + 0x148` (`PCHSTRP18`, stock value `0x00000003`, `Setup[0x2d0]=0x01`):**
    * Bits `[1:0] = 11b` (`0x3`): Shared Flex-I/O lanes routed per `SATAXPCIE` pin ownership (`HSIO 13..14` serve `SATA6G_5/6` inside `SATA Express` vs `RP1/RP2 x2`).
    * Bits `[1:0] = 00b` (`0x0`): Forced SATA / Flex-I/O mode when `Setup[0x2d0] != 1`.
* **`NCT6791D LDN 0x09 GPIO46` (`CR 0xf0`/`0xf1` bit `6`):** Wired to PCH **`HDA_SDO`** (Flash Descriptor Security Override) so the locked stock BIOS could temporarily unlock IFD, rewrite `0x124`/`0x148`, and issue a `0xcf9` full reset (`0x0e`).
* **Coreboot Advantage & Helper Tool:** Because Coreboot uses an unlocked IFD (`FLMSTR1 = 0xffff0000`), [`scripts/switch-m2-ifd.py`](../scripts/switch-m2-ifd.py) allows switching between `pcie-x1` (default, keeps `I226-V` active) and `m2-x2` cleanly on a ROM or live IFD slice without needing `HDA_SDO` pin tricks.

---

## 3. Complete Q-LED POST Indicators (`ProjectPEI` + `ProjectDXE`)

| Q-LED | Hardware Signal | Active State (ON) | Inactive State (OFF) | Coreboot Lifecycle (`bootblock.c` / `mainboard.c`) |
| :--- | :--- | :--- | :--- | :--- |
| **`CPU_LED`** | Pre-CPU hardware fault latch | CPU init fault (HW) | Normal CPU startup | Automatic hardware behavior |
| **`DRAM_LED`** | **`NCT6791D LDN 0x08 GP07`** (`CR 0xe0`/`0xe1` bit 7) | Output `0` (`CR 0xe0 &= ~0x80`, `CR 0xe1 &= ~0x80`) | Input / Hi-Z (`CR 0xe0 \|= 0x80`, `CR 0xe1 \|= 0x80`) | Lit in `bootblock.c` pre-RAM; cleared at `BS_DEV_INIT` entry |
| **`VGA_LED`** | **Intel Z97 PCH `GPIO38`** (`GP_IO_SEL2` + `GP_LVL2` bit 6 = `0x40`) | Output `1` (`GP_IO_SEL2 &= ~0x40`, `GP_LVL2 \|= 0x40`) | Input / Hi-Z (`GP_IO_SEL2 \|= 0x40`) | Lit at `BS_DEV_INIT` entry; cleared at `BS_POST_DEVICE` exit (`libgfxinit` ready) |
| **`BOOT_DEVICE_LED`** | **Intel Z97 PCH `GPIO22`** (`GP_IO_SEL`+2 / `GP_LVL`+2 bit 6 = `0x40`) | Output `1` (`GP_IO_SEL[22] = 0`, `GP_LVL[22] = 1`) | Input / Hi-Z (`GP_IO_SEL[22] = 1`) | Lit at `BS_POST_DEVICE` exit; cleared at `BS_PAYLOAD_BOOT` entry |

> All Q-LEDs can also be toggled via the UEFI Setup CFR option **`Q-LED POST Indicators`** (`qled_post_indicators`, default Enabled).

---

## 4. Physical Switches & DirectKey (`EpuHwModePei`/`Dxe`, `ProjectDXE`, `ASUSDirectKeyPEI`/`DXE`)

| Switch / Button | Hardware Pin / Register | Polarity / Decoding | Notes |
| :--- | :--- | :--- | :--- |
| **EPU Hardware Switch** | `NCT6791D LDN 0x07 GP72` (`CR 0x2f &= ~0x04`, `CR 0x30 \|= 0x02`, `CR 0xe1` bit 2) | Active-LOW (`(~(CR 0xe1 >> 2)) & 1`) | Read & logged cleanly by Coreboot at `BS_POST_DEVICE` |
| **TPU / EZ_XMP Switches** | PCH `GPIO1` (`GP_LVL` bit 1), `GPIO17` (`GP_LVL+2` bit 1), `GPIO21` (`GP_LVL+2` bit 5) | Two-stage TPU strap (`ProjectDXE` `0x1800007fc`) | Preserved as inputs in [`gpio.c`](../board/src/mainboard/asus/z97-a/gpio.c) |
| **DirectKey Header** | `NCT6791D LDN 0x0a` (ACPI) `CR 0x42` bit 4 (`0x10`) | Latched flag cleared via `CR 0x46 \|= 0x80` | Hardware UEFI-Setup wake key trigger |

---

## 5. `NCT6791D` Q-Fan SmartFan IV Header Mapping (`QFanDXE`, `HWM_SetupDXE`, `SIOBasicIOPei`)

### A. Bank & Thermal Source Assignments
| Motherboard Header | `NCT6791D` HWM Bank | Temp Source (`Reg 0x00[4:0]`) | Mode (`Reg 0x02[7:4]`) | Curve Registers (`T1..T4` / `PWM1..PWM4`) |
| :--- | :--- | :--- | :--- | :--- |
| **`CPU_FAN` / `CPU_OPT`** | **Bank `0x02`** | `0x10` (PECI Agent 0) | `0x00` Manual / `0x40` SmartFan IV | `0x21..0x24` / `0x27..0x2a` (Manual `0x09`) |
| **`CHA_FAN1`** | **Bank `0x01`** | `0x01` (SYSTIN / MB) | `0x00` Manual / `0x40` SmartFan IV | `0x21..0x24` / `0x27..0x2a` (Manual `0x09`) |
| **`CHA_FAN2`** | **Bank `0x03`** | `0x01` (SYSTIN / MB) | `0x00` Manual / `0x40` SmartFan IV | `0x21..0x24` / `0x27..0x2a` (Manual `0x09`) |
| **`CHA_FAN3`** | **Bank `0x08`** | `0x01` (SYSTIN / MB) | `0x00` Manual / `0x40` SmartFan IV | `0x21..0x24` / `0x27..0x2a` (Manual `0x09`) |
| **`CHA_FAN4`** | **Bank `0x09`** | `0x01` (SYSTIN / MB) | `0x00` Manual / `0x40` SmartFan IV | `0x21..0x24` / `0x27..0x2a` (Manual `0x09`) |

* **PECI TjMax Calibration (`SIOBasicIOPei`):** Bank `0x07` `Reg 0x03 = 0x10`, `Reg 0x09 = (MSR_TEMPERATURE_TARGET >> 16) & 0xff`, `Reg 0x01 = 0x95`.
* **Coreboot UEFI Setup CFR Options (`cfr.c` + `mainboard.c`):**
  * `qfan_cpu_profile`: `Standard` (30–80 °C -> 25–100%), `Silent` (40–85 °C -> 20–100%), `Turbo` (30–75 °C -> 45–100%), `Full Speed` (100% PWM), `Keep Hardware Default`.
  * `qfan_chassis_profile`: `Standard` (30–65 °C -> 30–100%), `Silent` (35–70 °C -> 25–100%), `Turbo` (30–60 °C -> 50–100%), `Full Speed` (100% PWM), `Keep Hardware Default`.

---

## 6. ErP Ready S4/S5 Deep Sleep (`ASUS_EUPSxSMI`)

* **Hardware Control:** `NCT6791D LDN 0x16` (`DS5` / Deep Sleep):
  * Enable ErP: `CR 0x30 |= 0x01`, `CR 0xe2 = 0x0a`.
  * Disable ErP (default, preserves WoL & USB standby): `CR 0x30 &= ~0x01`.
* **Coreboot CFR Option (`cfr.c`):** `erp_ready` (`0 = Disabled`, `1 = Enabled`).

---

## 7. DIGI+ VRM `ASP1252` & SMBus Topology (`OCMR_PEI`, `OCMR_DXE`, `OC_Tuner`)

* **PCH SMBus (`00:1f.3`, `i2c-0`) Slaves Observed:**
  * `0x14`, `0x44`, `0x5a`: Proprietary ASUS VRM (`DIGI+ ASP1252`) & TPU/EPU telemetry/control endpoints (`OCMR_PEI`/`DXE`, `OC_Tuner`).
  * `0x50..0x53`: 4x 8 GiB Kingston DDR3-1866 DIMM SPDs.
* **Safety Policy & Full Decoded Spec:** Analog VRM DAC offsets (`0x14` / `EC Page 1/2`) are intentionally left at their safe hardware/SVID power-on defaults unless explicitly configured by the user. Full binary-extracted register maps and algorithms for **`ENE MBE0-Z97-0121`**, **`DIGI+ ASP1252`** (`EC[0xf1/0xf2/0xd8/0xda..0xdd/0x8c..0x8f]`, telemetry `0x5b/0x5f/0x83`), **`ASUS EPU`** (`i7-4790K` `-20 mV`), **`ASUS TPU`** (`GP26/GP41` 3-way switch, `MSR 0x1ad`, `MSR 0x150`), and **LPC Port `80h`** are documented in [`DIGI-VRM-TPU-EPU-ALGORITHMS.md`](./DIGI-VRM-TPU-EPU-ALGORITHMS.md) and exposed cleanly in [`cfr.c`](../board/src/mainboard/asus/z97-a/cfr.c) and [`mainboard.c`](../board/src/mainboard/asus/z97-a/mainboard.c).
