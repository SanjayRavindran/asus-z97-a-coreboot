---
name: z97-a-digi-vrm-tpu-epu
description: >-
  Configure, inspect, and troubleshoot ASUS Z97-A proprietary subsystems in
  Coreboot: ENE EC (MBE0-Z97-0121), DIGI+ ASP1252 VRM controls & telemetry, ASUS
  EPU/TPU physical switches & power/turbo tuning, Q-LEDs, Q-Fan, LPC Port 80h,
  CMOS_POST, and EDK2 iPXE network boot.
---

# ASUS Z97-A Proprietary Controllers & Coreboot CFR Guide

Operational reference derived from reverse-engineering the stock AMI `3503` ROM (`docs/DIGI-VRM-TPU-EPU-ALGORITHMS.md` and `docs/STOCK-ROM-FEATURES-AUDIT.md`).

## 1. ENE EC (`MBE0-Z97-0121`) & DIGI+ `ASP1252` VRM

- **I/O Interface**: `0x62` (Data), `0x66` (Command/Status), `gen2_dec = 0x007c0a01` (`0x0a00..0x0a7f`).
  - IBF ready: `(inb(0x66) & 0x06) == 0`; OBF ready: `(inb(0x66) & 0x01) != 0`.
  - Commands: `0x80` read reg, `0x81` write reg, `0xa1` read 16-byte version (`MBE0-Z97-0121`), `0xba 0xe3 0x03` commit VRM settings.
  - Presence check: select Page `0x01` (`EC[0xff] = 0x01`), verify `EC[0xf8] == 0xec`, restore `EC[0xff] = 0x00`.
- **Page `0x00` Control Registers**:
  - `EC[0xf1]` Load-Line Calibration: `L1=0xff, L2(Auto)=0xdd, L3=0xcc, L4=0xaa, L5=0x88, L6=0x66, L7=0x44, L8=0x22, L9=0x00`.
  - `EC[0xf2]` Current Capability: `100%=0x08 .. 140%=0x0c`.
  - `EC[0xd8]` Switching Freq: `250kHz=0x22 .. 500kHz=0x77`, `Auto(300kHz)=0x33`, `Spread Spectrum=0xff`, `Active Freq Mode=0xfe` (disallowed in `Extreme` phase mode -> `0x33`).
  - `EC[0xda..0xdd]` Duty Control: `T.Probe={0x26,0x26,0x00,0x00}`, `Extreme={0x44,0x44,0x00,0x00}`.
  - `EC[0x8c..0x8f]` Phase Control: `Standard={0x22,0x22,0x11,0xe0}`, `Optimized={0x44,0x33,0x19,0xe4}`, `Extreme={0x44,0x33,0x19,0x00}`, `Manual Ultra Fast..Regular={0x02, 0x86..0xb9, 0x6a..0x9d, 0x00}`.
  - Error latch: `EC[0xd3] & 0x20` after `0xba 0xe3 0x03` commit.
- **Page `0x00` Live Telemetry**: `VCPU` (`0x5b`/`0x5c`), `ICPU` (`0x5f`/`0x60`), `PCPU` (`0x83`/`0x84`).
- **Safety Rule**: Keep `digi_vrm_apply` disabled unless intentionally tuning DIGI+ VRM; never write secondary analog DACs (`EC Page 1/2`, SMBus `0x14`) without explicit calibration.

## 2. ASUS `EPU` & `TPU` Switches and Tuning

- **EPU HW Switch**: `NCT6791D LDN 0x07 GP72` (`CR 0xe1` bit 2, active-low). Stock `EpuHwModePei` `i7-4790K` (`CPUID 0x306c3`) applies `-20 mV` Core offset; exposed cleanly in Coreboot via CFR `epu_mode` (`Disabled` / `Auto` / `Force -20 mV` via Intel `MSR 0x150` OC Mailbox, skipped if manual `oc_undervolt_apply` is active).
- **TPU 3-Way HW Switch**: `NCT6791D LDN 0x09` `GP26` (`CR 0xe1` bit 6) & `GP41` (`CR 0xf1` bit 1) -> `GP26==0` = `TPU_I` (Ratio), `GP26==1 && GP41==0` = `TPU_II` (Ratio+BCLK), else `OFF`. Synchronized all-core Turbo multiplier (`32..48`) is exposed via `cpu_turbo_ratio_all_core` (`MSR_TURBO_RATIO_LIMIT 0x1ad`, gated by `MSR_PLATFORM_INFO[28]`).

## 3. Q-LEDs, Q-Fan, LPC Port `80h` & iPXE Netboot

- **Q-LED Progression**: `DRAM_LED` (`NCT6791D LDN 0x08 GP07`) pre-RAM -> `VGA_LED` (PCH `GPIO38`) -> `BOOT_DEVICE_LED` (PCH `GPIO22`) -> all OFF at `BS_PAYLOAD_BOOT` (toggle via `qled_post_indicators`).
- **Q-Fan & PECI HWM (`NCT6791D` HWM `@0x290`, `SIOBasicIOPei` + `QFanDXE` parity in `mainboard.c`)**:
  - `CR 0x28=0x00` unlocks `@0x290` I/O; `Bank 7` (`0x03=0x10`, `0x09=TjMax`, `0x01=0x95`), `Bank 4` (`0xf8=0x50`, `0xfa=0x51`), and **`Bank 0 0xae=0x03`** (`EN_PECI0/1` — required for `PECI Agent 0` `temp7` and CPU SmartFan IV!).
  - **`Bank 0 0x5d=(val & ~0x06)|0x01`** (`EN_VBAT_MNT` + Thermistor mode for `SYSTIN`/`CPUTIN`) + `Bank 0 0x5e &= 0xf0` (disables spurious Current Mode), `Bank 0 0x39=65` / `0x3a=60` (`SYSTIN` high/hyst), `Bank 0 0x46 |= 0xc0` (clear `CASEOPEN0/1`), `Bank 0 0x04 &= ~0x01` + `Bank 6 0x00=0x00` / `0x01 &= 0xf0` (PWM duty mode).
  - SmartFan IV curves on Bank `2` (`CPU_FAN`, PECI `0x10`) and Banks `1/3/8/9` (`CHA_FAN1..4`, SYSTIN `0x01`) MUST write **`0x66=0x11`** (`FAN COUNT STEP`, reset default `0x00` freezes PWM!), `0x03=0x01`, `0x04=0x01`, `0x35=T4+5` (Critical Temp above `T4`), `0x38=0x02`, `0x3f &= 0xf8`, and `0x02=0x40`.
- **LPC Port `80h` + `CMOS_POST`**: `RCBA32(GCS)` (`0x3410`) bit 2 cleared (`RPR=0`) routes `0x80` to the `TPM` LPC header; `CONFIG_CMOS_POST=y` (`0x70`) preserves last boot POST codes. Leave `NCT6791D LDN 0x14` off.
- **iPXE UEFI Netboot & UEFI Shell**: Built natively into EDK2 (`NETWORK_IPXE=TRUE`, `B68653C7-EEA1-4435-A199-A44F59E4476C`) with HTTPS, TRUST, `COM1` support (`I218-V` `8086:15a1` + `I226-V` `8086:125c`), and interactive `UEFI Shell` (supersedes legacy `coreinfo`).

## 4. Power Management, ME & UEFI CFR Defaults (`v0.16` / `v26.09-r2`)

All runtime board toggles persist in `SMMSTORE` (`0x190000..0x20ffff`, edited via `ESC -> Device Manager -> Coreboot Configuration`, superseding legacy `nvramcui`):

- **`me_disable` (System & PCIe)**: Defaults to `1` (`Disabled` via `CONFIG(DISABLE_ME_PCI)` in `lynxpoint/cfr.h`); `lynxpoint/me.c` hides `00:16.0` (`MEI #1`) via `FD2` even when `AltMeDisable=1` (`without-me` `minimal-116k`).
- **`power_on_after_fail` (Power Management)**: Defaults to `1` (`Power on (S0)` via `CONFIG_POWER_STATE_ON_AFTER_FAILURE=y`), clearing `GEN_PMCON_3[0]` (`AFTERG3_EN=0`).
- **`wol_enable` (Power Management)**: Defaults to `1` (`Enabled`), setting `GEN_PMCON_3[13]` (`WOL_EN_OVRD=1` -> `0x3808`) unless `erp_ready=1`, preserving `PME_B0`/`PME`/`PCI_EXP` GPEs & clearing `PCIEXPWAK_DIS` on S5 in `lynxpoint/smihandler.c`, paired with `board/src/mainboard/asus/z97-a/acpi/wol.asl` (`GLAN` `0x0d`, `PEGP`/`PEG1`/`PEG2`, `RP01..08` `0x09`).
- **`erp_ready` (Power Management)**: Defaults to `0` (`Disabled` — required for WoL & standby USB).
