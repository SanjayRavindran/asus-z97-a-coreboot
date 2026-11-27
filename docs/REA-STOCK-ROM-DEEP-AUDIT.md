# ASUS Z97-A Stock `3503` ROM (`@rea`) vs. Coreboot Deep Audit

- **Target Stock ROM**: `/home/zero/coreboot-work/z97-a/inputs/z97-a-original-chip.bin` (`8,388,608` bytes, SHA-256 `a1fd7f2974dff0614ad9f7f63784db4bc9cfbb62e0779ad42e76f49589e2cab1`, Z97 PCH `8086:8cc4` rev `0x00`)
- **Live Hardware Reference**: [`stock-capture/inteltool.txt`](../stock-capture/inteltool.txt), [`stock-capture/lspci-nnvvvxxx.txt`](../stock-capture/lspci-nnvvvxxx.txt), [`stock-capture/superiotool.txt`](../stock-capture/superiotool.txt)
- **Audited Coreboot Files**: [`devicetree.cb`](../board/src/mainboard/asus/z97-a/devicetree.cb), [`romstage.c`](../board/src/mainboard/asus/z97-a/romstage.c), [`gpio.c`](../board/src/mainboard/asus/z97-a/gpio.c), [`hda_verb.c`](../board/src/mainboard/asus/z97-a/hda_verb.c), [`bootblock.c`](../board/src/mainboard/asus/z97-a/bootblock.c), [`gma-mainboard.ads`](../board/src/mainboard/asus/z97-a/gma-mainboard.ads), [`Kconfig`](../board/src/mainboard/asus/z97-a/Kconfig), [`fb_console.c`](../board/src/mainboard/asus/z97-a/fb_console.c), [`z97-a-8mb-stock-me.defconfig`](../configs/z97-a-8mb-stock-me.defconfig), [`0002-lynxpoint-z97a-dmi-sata-pcie-aspm.patch`](../patches/0002-lynxpoint-z97a-dmi-sata-pcie-aspm.patch), [`0001-haswell-z97a-above4g-peg-dmi-gma.patch`](../patches/0001-haswell-z97a-above4g-peg-dmi-gma.patch), [`0003-libgfxinit-z97a-port-limits.patch`](../patches/0003-0003-libgfxinit-z97a-port-limits.patch)

---

## 1. Executive Summary & Stock `3503` Extraction Scope

Every critical PEI, DXE, and SMM module from the physical stock ASUS Z97-A `3503` chip image was extracted (`FV@0x600000` -> `pei-full/`, `FV@0x220000` -> `dxe-raw/` and `206` decompressed `.efi` binaries in `dxe-pe32/`) and decompiled via `@rea` (`Ghidra 12.2`, `0` failed functions across all decompiled modules):

- **PEI**: `PchInitPeim` (`139`), `SBPEI` (`85`), `CRBPEI` (`16`), `NBPEI` (`52`), `SaInitPeim` (`202`), `PeiGfxDriver` (`177`), `ProjectPEI`, `SIOBasicIOPei`
- **DXE / SMM**: `PchInitDxe` (`185`), `SBDXE` (`83`), `SataController` (`37`), `SaInitDxe` (`143`), `IccPlatform` (`33`), `IccOverclockingDxe` (`16`), `HeciDxe` (`37`), `SBSMI` (`40`), `PchSmiDispatcher` (`96`), `SleepSmi` (`37`), `OnBrdDevDXE` (`16`), `CRBDXE` (`2`), `AcpiModeEnable` (`28`)
- **NVRAM Defaults (`Setup` / `StdDefaults`)**: `641` NVAR entries parsed; `Setup` (`2159` B) at `StdDefaults` (`0x180088`) and active NVRAM stores (`0x185050`, `0x1a3a0a`) are identical across all audited PCH/SA fields (`HDA=1`, `SATA=AHCI 6-port`, `xHCI=1`, `EHCI1/2=1`, `PowerLoss=1`).

### Headline Audit Conclusions

1. **Where Coreboot came from other ASUS boards (`maximus_vii_hero` / upstream `z97-a` / generic `lynxpoint`) vs. THIS physical Z97-A**:
   - **SATA Gen3 DTLE (`devicetree.cb` & `early_pch_native.c`)**: `sata_port0_gen3_dtle = 0x2` and `sata_port1_gen3_dtle = 0x2` in `devicetree.cb` came from `asus/maximus_vii_hero` (which only configures ports `0..1` late in `sata.c`). In stock Z97-A `3503`, `SBPEI/0xfff8fac2` + `PchInitPeim/0xfff9b738` (`DAT_fffa37c4` + `DAT_fffa374c`) program Gen3 RxEq **`DTLE = 0x02`** (`[29:24]=2` on `...50` and `[21:16]=2` on `...54`) in PEI **before `SATA_PCS`** across **all 6 ports** (`0xea002550, 2750, 0950, 0b50, 2150, 2350`, respecting `lane_owner` for `P4/P5`), resulting in `stock-capture/inteltool.txt` `SECRR50=0x023f3f3f`, `SECRR54=0x00020000`, `SECRR58=0xa0809000` on all 6 ports, plus `DAT_fffa38b4` (`SECRT88/8C/90`). Move `DTLE = 0x02` for all 6 ports into `early_pch_native.c` (PEI/`romstage`) and drop the `devicetree.cb` `maximus_vii_hero` DTLE properties.
   - **SATA `D31:F2` AHCI & `SIRI`/`SIRD` (`sata.c` + `0002-lynxpoint-z97a-dmi-sata-pcie-aspm.patch`)**: `PchInitDxe` (`0x180004af8`, `0x1800052cc`, `0x1800047ac`) + `PchInitPeim` (`0xfff9c037`, `0xfff9c1f6`) prove the exact Z97-A `3503` sequence (`D31:F2 +0x90`, `+0x92`, `+0x94`, `+0x98 = 0x205c0220`, `+0x9c = 0x80000420`, `SIR 0x60/0x64/0x68`, `PxCMD`, `PxDEVSLP`, `ABAR+0x00/0x0c/0x24`), and confirm `sata_hotplug_map` in `Setup` is `0` (`SALP` bit 26 / `SSS` bit 27 cleared, `EMS` bit 6 set). Coreboot `sata.c` already lands on `0x98 = 0x205c0220`.
   - **ME ChipsetInit HSIO CRC Check & Update (`PchInitPeim/0xfff9dad8` -> `PchInitDxe/0x180000bb8` + `0x18000ed4c`)**: Stock Z97-A stores a `0x124`-byte ChipsetInit blob (`DAT_fffa2d74`, CRC `0x032c599d`) and compares it to ME over HECI in PEI; Coreboot skips this (`TODO: Send GET HSIO VER and update ChipsetInit table?`). Log/verify `00:16.0+0x68` in `early_pch_native.c` (`BIOS_DEBUG`/`BIOS_WARNING` on mismatch vs `0x032c599d`).
   - **USB2 `mainboard_usb2_ports` (`romstage.c` & `early_usb_native.c`)**: Stock `SBPEI` (`0xfff8d825`, Z97 `-0x733c` / `0x8cc4` branch) uses per-port lengths **`{0x80, 0x80, 0x100, 0x100, 0x80, 0x80, 0x80, 0x80, 0x100, 0x100, 0x140, 0x140, 0x80, 0x80}`** (`1`=internal, `0`=back panel), whereas Coreboot `romstage.c` hardcoded `0x0110` for all 14 ports. Moreover, `early_usb_native.c:109` has an off-by-one IOBP address (`0xe500400f + port*0x100` instead of `PchInitPeim/0xfffa160e` **`0xe500410f + port*0x100`**), omits `0xe5004100 + port*0x100 = 0x9c01` (`U2ECRP01..14 = 0x00059c01` in `inteltool.txt`), omits xHCI MMIO `+0x8144 (& ~0xc0 | 0x100)`, and clears instead of setting `BIT(31)` in xHCI `+0x8154` (`PchInitPeim/0xfffa0b96`).
   - **USB3 (`PchInitPeim/0xfffa16cd` + `DAT_fffa4a8c`)**: `SBPEI/0xfff8d825` sets USB3 `HsioRxEq = 0` (`pcVar2[0x9e+i*4] = 0`) for all 6 ports, matching `stock-capture/inteltool.txt` (`0xe9001760 = 0`, `0xe900175c = 0x80889090`, `0xe9001768 = 0xba004a2a`, `0xe9001770 = 0x20`).
   - **HDA Verbs (`hda_verb.c` vs `SBPEI/0xfff8eeea` + `DAT_fff93f30`)**: NID `0x11` in `hda_verb.c` is `0x18561130` (carryover from another board / generic codec), whereas stock Z97-A `SBPEI` (`0xfff954c0`) programs NID `0x11 = 0x90460130` (`0x01171c30..0x01171f90`) PLUS writes codec processing/coefficient verbs `0x02050007, 0x020409c8, 0x02050007, 0x020409c8`!
   - **PCIe Root Ports (`00:1c.x`, `pcie.c`)**: `stock-capture/lspci-nnvvvxxx.txt` shows `00:1c.0`, `00:1c.3`, and `00:1c.6` ALL have **`0xe1 = 0x00`** (no dynamic clock gating `0x03`), `00:1c.0` has `0xe8 = 0x02`, `00:1c.3` has `0xe8 = 0x00, 0xec = 0x03` (`8086:244e`), and `00:1c.6` has `0xe8 = 0x10`. Coreboot `lynxpoint/pcie.c` only exempted `rp == 4` (`00:1c.3`) instead of matching all three RPs (`1c.0`, `1c.3`, `1c.6`) to stock.
   - **`SuperIO` & `GPIO` & `GPE0_EN` & `VBT`**:
     - `VBT` (`inputs/i915_vbt.bin`, `0..4493` bytes) is **0-diff byte-identical** to `ROM@0x2e4d20` (`FV@0x220000 FFS-878ac2cc...`).
     - `GPE0_EN` `0x40042246` in `devicetree.cb` (shared with `maximus_vii_hero`) IS confirmed on stock `inteltool.txt` (`pmbase+0x0028: 0x40042246`).
     - `GPIO` `USE_SEL`, `INV` (`0x00006045` = GPIO00/02/06/13/14 from `SBPEI/DAT_fff92fd8`), `RST` (`0x00000100` = GPIO8), and all 13 `GPIO OUT` levels in `gpio.c` match `inteltool.txt` 100%; `SBPEI` (`DAT_fff92fd8`) sets `32` explicit pins and leaves the rest at PCH defaults.
     - `devicetree.cb` `gpu_ddi_e_connected = 1` + `gma-mainboard.ads` (`HDMI1, DP2, HDMI2, HDMI3, Analog`) need alignment with `ROM@0x2e4d20` Child Devices (`CRT/VGA 0x0821`, `Port B HDMI 0x0400`, `Port C DP/HDMI 0x4000`, `Port D HDMI 0x2000`).

---

## 2. Public-Repo / Other-Board Carryover Audit

| Coreboot File & Setting | Current Value | Provenance / Status | Stock Z97-A `3503` Proof (`@rea` / Capture) | Action |
| :--- | :--- | :--- | :--- | :--- |
| `devicetree.cb`: `sata_port0/1_gen3_dtle` | `0x2`, `0x2` | Upstream `maximus_vii_hero` / `z97-a` (only ports 0..1, late DXE) | `SBPEI/0xfff8fac2` + `PchInitPeim/0xfff9b738` program `DTLE=0x02` (`[29:24]`/`[21:16]`) on **all 6 ports** in PEI -> `SECRR50=0x023f3f3f`, `SECRR54=0x00020000`, `SECRR58=0xa0809000` | **Move** all-6-port `DTLE=0x02` into `early_pch_native.c` before `SATA_PCS` & remove `devicetree.cb` `dtle` |
| `devicetree.cb`: `gpe0_en_1` | `0x40042246` | Same as `maximus_vii_hero` | **Verified on THIS board**: `inteltool.txt` `pmbase+0x0028 = 0x40042246` | **Keep (Stock-Confirmed)** |
| `devicetree.cb`: `gpu_ddi_e_connected` | `1` | Generic Haswell desktop / `maximus_vii_hero` | `ROM@0x2e4d20` BDB Block 2 has `CRT` (`0x0821`), `Port B` (`0x0400`), `Port C` (`0x4000`), `Port D` (`0x2000`) — `DDI_E` is unused by digital (`CRT` uses FDI/DAC) | **Verify/Keep** (`CRT` uses DAC/FDI-like `gpu_ddi_e_connected=1` in Coreboot Haswell) |
| `devicetree.cb`: USB2/3 `OC` pins (`0x14.0` / `romstage.c`) | `{0,8,1,1,2,2,3,3,4,4,5,5,6,6}`, `{0,0,1,1,2,2}` | Upstream `z97-a` / `maximus_vii_hero` | **Exact match** in `SBPEI/0xfff8d825` (`local_8==1` `0x2d..0x95`, `0x9d..0xb1`) | **Keep (Stock-Confirmed)** |
| `romstage.c` + `early_usb_native.c`: USB2 & xHCI | `0x0110` all 14, `0xe500400f`, missing `8144`/`8154` | Back-calculated from `U2ECRP` + generic `lynxpoint` bug | `SBPEI/0xfff8d825` `{0x80..0x140}`, `PchInitPeim/0xfffa160e` `0xe500410f` + `0x9c01` (`U2ECRP=0x00059c01`), `0xfffa0b96` `0x8144`/`0x8154` (`BIT(31)`) | **Replace** `romstage.c` table & fix `early_usb_native.c` |
| `hda_verb.c`: NID `0x11` | `0x18561130` | Non-Z97-A carryover | `SBPEI` `DAT_fff93f30` (`0xfff954c0`): `0x90460130` (`0x01171c30..1f90`) + missing `0x02050007, 0x020409c8` x2 | **Replace** NID `0x11` + add NID `0x20` coeff verbs |
| `hda_verb.c`: NID `0x12..0x1f`, subsys | `0x1043860b` + pins | Matches stock | Matches `SBPEI` `0xfff8eeea` (`0x860b1043`) and `0xfff954c0` `[12..63]` | **Keep (Stock-Confirmed)** |
| `gpio.c`: `USE_SEL`, `INV`, `RST`, output `LVL` | Full 76-pin map | Capture + `SBPEI/DAT_fff92fd8` | `SBPEI/DAT_fff92fd8` (`GPI_INV` 0,2,6,13,14 = `0x6045`) + `inteltool.txt` 100% match on `USE_SEL`, `INV`, `RST`, all 13 `OUT` levels (`IO_SEL` diff only on `NATIVE` pins) | **Keep**, audit `NATIVE` `IO_SEL` & `GPIO39` (`SBPEI=0x0005` vs `inteltool` `lvl=0`) |
| `patches/0002-lynxpoint-z97a-dmi-sata-pcie-aspm.patch`: `pcie.c` clock gating | Exempt only `rp == 4` | Partial fix from `00:1c.3` AER fix | `stock-capture/lspci` shows `00:1c.0`, `00:1c.3`, `00:1c.6` ALL have `0xe1 = 0x00`, `1c.0 0xe8=0x02`, `1c.3 0xe8=0x00,0xec=0x03`, `1c.6 0xe8=0x10` | **Replace** with stock `0xe1/0xe8` per-port values across `1c.0`, `1c.3`, `1c.6` |
| `patches/0002-lynxpoint-z97a-dmi-sata-pcie-aspm.patch`: `early_pch_native.c` | HSIO `SECRT88/8C` | Z97-A stock values | Matches `DAT_fffa38b4` in `PchInitPeim/0xfff9b738` + `inteltool.txt` (`SECRT88/8C` exact!) | **Keep (Stock-Confirmed)** |
| `patches/0003-0003-libgfxinit-z97a-port-limits.patch` | EDID CEA DTD + DDI `9/6/11` + `WM_LINETIME` | Custom patch for LG 4K HDMI & drops | `hw-gfx-gma-connectors-ddi-buffers.adb` `9/6/11` matches stock HDMI/DVI tuning workaround; Coreboot `fb_console.c` overlap fixed at `fb_console.c:348` (unflashed `v0.8`) | **Keep** (verified workaround against hardware display drops) |
| `patches/0001-haswell-z97a-above4g-peg-dmi-gma.patch` | `A4GB/A4GS` + `peg_bifurcation` | Custom feature | Cleanly gated behind `peg_bifurcation` / `A4GS`; zero interference with stock defaults | **Keep** |
| `inputs/i915_vbt.bin` (`blobs/data.vbt`) | `6144` B | Derived from stock ROM | Bytes `0..4492` are **0-diff identical** to `ROM@0x2e4d20` (`878ac2cc...`), zero-padded to `6144` B | **Keep (100% Stock)** |
| `bootblock.c` + `mainboard.c` SIO/EC/VRM/LED | `0x2e` `NCT6791D` + EC | Capture + `SIOBasicIOPei` / `ProjectPEI` | `superiotool.txt` exact match (`0x1a=0a, 0x1b=20, 0x1c=10, 0x24=00, 0x2a=40, 0x2f=03`, LDN `0x0a`/`0x0f`); `SIOBasicIOPei` + `SBPEI/0xfff8f7a7` confirm `LDN 0x0a 0xe4/0xe7` | **Keep (Stock-Confirmed)** |
| `configs/z97-a-8mb-stock-me.defconfig` | `USE_NATIVE_RAMINIT=y`, `IGNORE_XMP_REQUESTED_VOLTAGE=y` | Generic/user choice | `NATIVE_RAMINIT` avoids MRC blob | **Audit `IGNORE_XMP_REQUESTED_VOLTAGE`** & check missing `early_me` ChipsetInit |

---

## 3. Subsystem Deep-Dive (`@rea` vs. Coreboot + Captures)

### 3.1 SATA / HSIO / RxEq / SIRI-SIRD / AHCI (`ata2` Drop Root Causes!)
- **Observed live failure (`v0.7` Boot #5)**: `ata2` dropped with `SError: { HostInt 10B8B DevExch }` right before root mount, landing in `(initramfs)`.
- **Stock `PchInitPeim/0xfff9b738.c` (Z97 `param_5 == -0x733c`, `GetPchStepping == 0x10`)**:
  1. Executes `DAT_fffa304c` (`73` entries, includes `0xea008008 = 0x1c000000`) and `DAT_fffa2e9c` (`36` entries) — these match Coreboot `lpt_h_cx.c` (`hsio_sata_lpt_h_cx`, `hsio_sata_shared_lpt_h_cx`).
  2. Skips 8-series `DAT_fffa36d4/36bc` and instead executes `DAT_fffa371c` (`4` entries) + `DAT_fffa3704` (`2` entries): `0xea002490/2690/0890/0a90/2090/2290` `&= 0xffff0000 |= 0x3e67`.
  3. Checks `DAT_fffa37c4` (`20` rows) and `DAT_fffa374c` (`10` rows) for Gen3 RxEq (`DTLE`) overrides (`param_1[0x1ea]=1, [0x1e9]=2` per `SBPEI/0xfff8fac2`!). On Z97-A `3503`, `PchInitPeim` programs **`DTLE = 0x02`** (`[29:24]=2` on `0xea00xx50` and `[21:16]=2` on `0xea00xx54`) across **all 6 ports** (`P0..P5`, respecting `lane_owner` for `P4/P5`) in PEI **before `SATA_PCS`**, producing `stock-capture/inteltool.txt` `SECRR50=0x023f3f3f`, `SECRR54=0x00020000`, `SECRR58=0xa0809000` on ALL 6 ports! Coreboot previously copied `sata_port0_gen3_dtle = 0x2` and `sata_port1_gen3_dtle = 0x2` in `devicetree.cb` (only ports `0..1`, applied late in DXE `sata.c`), leaving ports `2..5` without `DTLE = 0x02` and doing ports `0..1` after `SATA_PCS`! Moving 6-port `DTLE = 0x02` into `early_pch_native.c` matches PEI timing & all 6 stock ports.
  4. **Unconditionally runs `DAT_fffa38b4` (`18` entries at end of `0xfff9b738.c`)**:
     - `0xea00248c/268c/088c/0a8c` `&= 0xff00ffff |= 0x00690000` (matches `inteltool.txt` `0x0c692040`)
     - `0xea00208c/228c` `&= 0xff00ffff |= 0x00800000` (matches `inteltool.txt` `0x0c802046`)
     - `0xea002488/2688/0888/0a88/2088/2288` `&= 0xffff00ff |= 0x00008000` (matches `inteltool.txt` `0x5580803a`)
     - `0xea002490..2290` `&= 0xffff0000 |= 0x00003e67` (matches `inteltool.txt` `0x2b403e67` / `0x2b3e3e67`)
     *(Note: `v0.7` on the Z97-A did NOT have this `DAT_fffa38b4` patch — `v0.8` added it, `v0.9` adds PEI 6-port `DTLE = 0x02` before `SATA_PCS`.)*
- **Stock `PchInitPeim/0xfff9c037` + `PchInitDxe/0x1800052cc` + `0x180004af8` (`D31:F2`)**:
  - `stock-capture/lspci-nnvvvxxx.txt` `00:1f.2` (`90: 60 3d 02 82 83 01 00 3d 20 02 5c 20 20 04 00 80`, little-endian):
    - `+0x90`: `0x3d60`, `+0x92` (`PCS`): `0x8202`, `+0x94`: `0x3d000183`
    - `+0x98`: **`0x205c0220`** — Coreboot `sata.c` already computes this exact value!
    - `+0x9c`: **`0x80000420`** (`0002-lynxpoint-z97a-dmi-sata-pcie-aspm.patch` already sets bits `31`, `10`, `5`)
    - `00:1f.2` `SIRI` (`+0xa0`): `PchInitDxe/0x1800052cc` writes `0x64=0x883c9001`, `0x68 &= 0xffff0000 |= 0x880a`, `0x60 |= 8|1|2` (`0x0b`). Coreboot `sata.c` writes those PLUS `0x70 = 0x3f00bf1f` (`sata.c` line 201).
  - Also check **`PchInitDxe/0x180004af8`**: loops ports `0..5`, clears `ABAR+0x144+p*0x80` bit 1 (`PxDEVSLP`), sets `ABAR+0x118+p*0x80` (`PxCMD`), and `0x1800052cc` disables unused SATA clock domains via `00:1f.2+0x94 |= 0x01000000 << port` (`0x3d000183` in `lspci` -> ports 2,3,4,5 disabled in `+0x94[29:24] = 0x3d`? Wait `0x3d = 0b111101` -> **ONLY port 1 (`ata2`, Samsung SSD!) enabled in `lspci`, bit 1 = 0!**).

### 3.2 ME ChipsetInit (`DAT_fffa2d74`) & ICC Clocks
- **`PchInitPeim/0xfff9dad8.c`**: Queries HECI `00:16.0` (`+0x4c = 0x70000001`, polls `+0x40` for `0x70000000`), reads ME's current ChipsetInit CRC from `00:16.0 + 0x68`, compares to **`DAT_fffa2d74[0] = 0x032c599d`**, and on mismatch publishes a `0x124`-byte HOB (`GUID 0xc1392859...`).
- **`PchInitDxe/0x180000bb8.c` -> `0x18000ed4c.c`**: Sends that `0x124`-byte table to ME via HECI (`0x30000`, subcmd `0x0f`) and triggers a platform reset (`*local_18[0]`).
- **`PchInitDxe/0x1800061f0.c` -> `0x180006128` -> `0x18000ee60`**: Sends HECI `0x30000` subcmd `0x03` (`ICC Set Clock Enables`) based on populated PCIe RPs (`0x1c.0..7`) + SATA (`00:1f.2`), and `0x18000efb8` locks ICC (`0x30000`, subcmd `0x0a`, `0x17f01`).
- **`IccPlatform/0x180000628.c`** + **`SBPEI/0xfff8c475.c`**: Writes `RCBA+0x38b0` (`0xfed1f8b0` `PMSYNC` / ICC profile) `&= 0xffff8003 |= 0x403c` (PEI) / `0x4028` (DXE).
- **Coreboot gap**: `lynxpoint/early_pch_native.c` has `/** TODO: Send GET HSIO VER and update ChipsetInit table? Is it needed? **/` and never checks `00:16.0+0x68` or ICC clock enables. Wait: Stock ME on Z97-A `3503` boots with ChipsetInit `0x032c599d` already programmed in ME NVM, **OR** we can verify `00:16.0+0x68` live over serial / implement the read-only CRC check (`0x032c599d`) in `early_me.c`!

### 3.3 USB2 (14 ports) & USB3 (6 ports) & xHCI (`00:14.0`)
- **`SBPEI/0xfff8d825.c` (Z97 `-0x733c` branch)**:
  - **USB2 OC pins (`U2OCM`)**: `{0, 8(SKIP), 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6}` -> matches `romstage.c`.
  - **USB2 Port Lengths & Back/Internal Locations**: `{0x80(1), 0x80(1), 0x100(0), 0x100(0), 0x80(1), 0x80(1), 0x80(1), 0x80(1), 0x100(0), 0x100(0), 0x140(0), 0x140(0), 0x80(1), 0x80(1)}` (`1` = back/internal flag in `SBPEI`). Note `PchInitPeim/0xfffa160e` writes `0xe5007f04 = 0x4481`, `0xe5004100 + 0x100*i = (reg & 0xffff00ff) | 0x9c01` (`DAT_fffa498c`), and `0xe5007f14 = 0x180000`, matching `inteltool.txt` `U2ECRP01..14 = 0x00059c01`! In Coreboot, `romstage.c` should use the real `SBPEI` lengths (`0x80`/`0x100`/`0x140`) and we must check whether `early_usb_native.c` already lands on `0x00059c01` or needs `DAT_fffa498c` (`0x9c01`).
  - **USB3 OC pins**: `{0, 0, 1, 1, 2, 2}` -> matches `romstage.c`. `SBPEI` sets USB3 `HsioRxEq = 0` (`DAT_fffa4a8c` NOT applied), matching `inteltool.txt`.
  - **xHCI MMIO (`PchInitPeim/0xfffa0b96` & `PchInitDxe/0x18000c168`)**: Programs `00:14.0` `+0x44 |= 0xc401`, `+0x46 |= 0x0f`, `+0x50 = ...0x7886e9f`, and xHCI MMIO `+0x8008..0x819c`, plus `USB2PR` (`+0xd0`), `USB3PR` (`+0xd8`), `USB2PRM` (`+0xd4`), `USB3PRM` (`+0xdc`), matching `PchInitDxe/0x180005ce4`.

### 3.4 PCH GPIOs (`SBPEI/DAT_fff92fd8` vs `inteltool.txt` vs `gpio.c`)
- **`SBPEI/DAT_fff92fd8`** explicitly configures **32 GPIOs**:
  - `GPIO00, 02, 06, 13, 14 = 0x000b` (`GPIO IN`, **`GPI_INV = 1`** -> `0x00006045` exact match!)
  - `GPIO01, 05, 07, 16, 17, 18, 20, 21, 22, 23, 25, 30, 31, 38, 44, 45, 46, 48, 49, 50, 54, 57, 68, 69, 70, 71 = 0x0003` (`GPIO IN`, note later code / hardware straps change `GPIO48` to `OUT=0`, `GPIO16` to `NATIVE`!)
  - `GPIO39 = 0x0005` (`GPIO OUT`, bit2=`1`? Wait: `inteltool.txt` had `GP_LVL2` bit7 = `0` — late DXE or `0xfff8e4d2` bit mapping sets `lvl`!).
- **Coreboot `gpio.c` vs `stock-capture/inteltool.txt` diff**:
  - `USE_SEL[0..2]`: **0 diffs** (`0xfbfee1ff`, `0x02fd70ff`, `0x000001f0`).
  - `INV[0..2]`: **0 diffs** (`0x00006045`, `0`, `0`).
  - `RST[0..2]`: **0 diffs** (`0x00000100`, `0`, `0`).
  - **All 13 `GPIO OUT` pins (`8, 15, 24, 28, 29, 32, 33, 35, 39, 48, 51, 53, 55`) levels**: **0 diffs**!
  - `IO_SEL` differed ONLY on `GPIO_MODE_NATIVE` pins (`9,10,11,16,26,40..43,47,49,56,58..60,73,75`), which Coreboot `common/gpio.c` writes harmlessly or ignores when native-muxed.

### 3.5 HDA Audio `ALC892` (`SBPEI/0xfff8eeea` + `0xfff954c0` vs `hda_verb.c`)
- **`SBPEI` (`DAT_fff93f30` -> `0xfff954c0`, `68` dwords)** vs [`hda_verb.c`](../board/src/mainboard/asus/z97-a/hda_verb.c):
  - Codec `0x10ec0892`, Subsystem ID `0x1043860b` (note `0xfff8eeea` applies `0x860b1043` cleanly over its `0x85341043` template).
  - Pin widgets `0x12..0x1f` match **100%**.
  - **DISCREPANCY #1**: **NID `0x11`** in `SBPEI` (`0xfff954c0[8..11]`) is `0x01171c30, 0x01171d01, 0x01171e46, 0x01171f90` (**`0x90460130`**, internal S/PDIF / HDMI-out pin config), whereas `hda_verb.c` has `0x18561130` (carryover from another board!).
  - **DISCREPANCY #2**: `SBPEI` (`0xfff954c0[64..67]`) appends ALC892 processing-coefficient verbs **`0x02050007, 0x020409c8`** (written twice) which `hda_verb.c` omits!

### 3.6 PCIe Root Ports (`00:1c.0..7`, `ASM1083`, `I226-V`)
- **`stock-capture/lspci-nnvvvxxx.txt`** proves the exact post-DXE state of all active RPs:
  - `00:1c.0` (`RP1`, `8086:8c90`): `0xe1 = 0x00`, `0xe8 = 0x02`, `0xec = 0x00`, `LnkCap 5GT/s x2`
  - `00:1c.3` (`RP4`, `8086:244e` Subtractive -> `ASM1083`): `0xe1 = 0x00`, `0xe8 = 0x00`, `0xec = 0x03`, `DevCtl AER off`
  - `00:1c.6` (`RP7`, `8086:8c9c` -> `I226-V 8086:125c`): `0xe1 = 0x00`, `0xe8 = 0x10`, `0xec = 0x00`, `LnkSta 5GT/s x1`
- **`PchInitPeim/0xfff9e494`** initially sets `0xe1 |= 3`, `0xe8 |= 3`, `0x324 |= 0x20` only for active unlocked RPs, then `Setup`/DXE (`PchInitDxe/0x180008130` + PCIe CLKREQ/dynamic clock gating setup) leaves **`0xe1 = 0x00` across ALL active RPs (`1c.0, 1c.3, 1c.6`)**! Coreboot's `lynxpoint/pcie.c` currently enables `0xe1 = 0x3f`/`0x03` and `0xe8 = 0x01` on `1c.0` and `1c.6` (only skipping `rp == 4`).

### 3.7 Super I/O (`NCT6791D`)
- **`bootblock.c`** + **`devicetree.cb`**: Matches `stock-capture/superiotool.txt` global config (`0x1a=0a, 0x1b=20, 0x1c=10, 0x24=00, 0x2a=40, 0x2f=03`), LDN `0x0a` ACPI (`0xe6=0a, 0xe7=11, 0xec=80, 0xed=01, 0xee=10, 0xf2=5d, 0xfc=80`), and LDN `0x0f` (`0xe0=7e, 0xe2=6f, 0xe3=bf, 0xe4=fa, 0xe6=7f`).
- Confirmed via `SBPEI/0xfff8f7a7` (`LDN 0x0a 0xe4/0xe7`) & `SIOBasicIOPei` (`LDN 0x00/0x04/0x07/0x0a`). *No other-board drift found here.*

### 3.8 iGPU / Display / VBT / SA / VT-d (`NBPEI`, `PeiGfxDriver`, `SaInitPeim`, `SaInitDxe`)
- **VBT (`ROM@0x2e4d20`, `FFS-878ac2cc...`)**: `4493` B, `BDB v189`, **0-diff byte-identical** to `inputs/i915_vbt.bin`. Block 1 `dp_ssc_en = 0`. *(Note: stock ROM also contains a second unreferenced `4608` B VBT `FFS-5a84ef47` with `CRT` zeroed out, whereas `stock-capture/i915_vbt.bin` matches `FFS-878ac2cc`.)*
- **Ports in `ROM@0x2e4d20` BDB Block 2**:
  - `Child[0]`: `0x78c6` (CRT / VGA `handle=0x0008`, `crt_ddc=0x02`)
  - `Child[1]`: `0x60d2` (Port B HDMI/DVI-D `handle=0x0004`, `ddc=0x01`)
  - `Child[2]`: `0x60d6` (Port C DP/HDMI `handle=0x0040`, `ddc=0x08`, `aux=0x20`)
  - `Child[3]`: `0x60d2` (Port D HDMI `handle=0x0020`, `ddc=0x09`)
- **Already matching stock `3503`**: `gma-mainboard.ads`, `libgfxinit` HDMI DDI level shifters (`9/6/11`), `gpu_dp_b/c/d_hotplug = 4`, `gpu_ddi_e_connected = 1`, `GGC = 0x0211` (`64` MiB DVMT + `2` MiB GTT), and all 19 GT PM registers + 4 PM locks in `SaInitDxe/0x180001224`.
- **Aligned in `v0.10` from `SaInitDxe` & `stock-capture/i915_opregion.bin`**:
  - **ACPI `_DOD` / `DIDL` (`devicetree.cb`)**: Added `register "gfx"` (`use_spread_spectrum_clock = 0`, `ndid = 5`, `did = { 0x0100, 0x0300, 0x0301, 0x0302, 0x0303 }`) matching `stock-capture/i915_opregion.bin` (`PCON=0x0`, `DIDL`) so `gma_generate_ssdt()` emits `\_SB.PCI0.GFX0._DOD`.
  - **Dynamic RP1 Graphics Frequency (`gma.c` `0xa008`)**: Read `RP1` ratio `(mchbar_read32(0x5998) >> 8) & 0xff` (`SaInitDxe/0x180002830:84` + `0x180001224:71-75`, `0x07` on `4790K` vs hardcoded `0x08`).
  - **CPU DP AUX 2X Clock Dividers (`gma.c` `0x64010`/`0x64810`)**: Program `dpdiv` (`270` / `0x10e` at `540 MHz` CDCLK, `225` at `450 MHz`, `169` at `337.5 MHz`) on Haswell too (`SaInitDxe/0x180000ea8:68-83`).
  - **`PAVPC` WOPCM + Lock (`northbridge.c` `00:00.0+0x58`)**: Program `((TOLUD_MB - 1) << 20) | 0x17` (`0xbf100017`, matching `SaInitDxe/0x180000c84` & `stock-capture/lspci-nnvvvxxx.txt`) or `1 << 2` if IGD disabled.
- **VT-d / DMAR**: `SaInitDxe/0x180005e1c` checks `00:00.0 + 0xe4` bit 23 (`VT-d` capability) and builds `DMAR`; matches Coreboot VT-d support when enabled.
- **HDMI display overlap (`v0.7`)**: Confirmed in [`fb_console.c`](../board/src/mainboard/asus/z97-a/fb_console.c) — `v0.7` left the Coreboot framebuffer console rendered in memory before jumping to EDK2 without clearing it (`v0.8`+ clears it at `fb_console.c:348`).

### 3.9 PM / RCBA / PIRQ / `GPE0_EN` (`SBPEI`, `PchInitDxe`, `SBSMI`, `PchSmiDispatcher`, `SleepSmi`)
- **`PchInitDxe/0x18000368c` (`RCBA 0x3314..0x3a9c`, `2304..2340`, `2b14..2b2c`)**: Coreboot `lynxpoint/lpc.c` programs almost the exact same Intel LPT-H sequence as `0x18000368c` (`0x3314..0x33d0`, `0x3a80=0x01040000`, `0x3a84=0x01041001`, `0x3a88=1`), **except** `lpc.c` wrote `0x2b14 = 0x1e0a0317` (stock `inteltool.txt` = `0x1e0a0314`) and `0x33d4 = 0xc80bc000` (stock `inteltool.txt` = `0xc00bc000`). Fixed in `v0.9`!
- **PIRQ / Device IRQ Routing (`SBPEI/DAT_fff9414c`)**:
  - `RCBA 0x3140..0x3160` in `DAT_fff9414c`: `0x3140=0x0235`, `0x3144=0x2037` (`0x32102037`), `0x3148=0x3216`, `0x314c=0x3250`, `0x3150=0x7654`, `0x315c=0x3210`, `0x3160=0x3215` — **100% match** with `stock-capture/inteltool.txt`. Programmed in `lpc.c` (`pch_pirq_init()`) in `v0.11` so `D31IR..D20IR` and generated `PCI0._PRT` match stock rather than leaving `0x3210` defaults.
- **PCIe RPs (`1c.0..1c.7`) & `EHCI #2` (`1a.0`) (`v0.11`)**:
  - Per `IFD 0x124 = 0xf0544a85`, `9-series-chipset-pch-datasheet.pdf` Table 2-1, and live probe: `1c.7` (`RP8`) is `PCIEX1_1` (was `off`, now enabled), `1c.6` (`RP7`) is `PCIEX1_2` (`I226-V`), `1c.4` (`RP5`) is `PCIEX16_3` (`x2`), `1c.3` (`RP4`) is `ASM1083` (`x1`), `1c.0` (`RP1`) is `M.2`/`SATA Express` Flex-I/O `x2`. `1a.0` (`EHCI #2`) disabled (`off`) to match stock `FD=0x03b62001`.
- **`GPE0_EN` (`pmbase+0x28`)**: `stock-capture/inteltool.txt` shows `0x40042246` — **100% match** with `devicetree.cb`.

---

## 4. Serial TX-at-Boot Coreboot Analysis (`COM1` `0x3f8`)

Can we **TX into COM1 at boot** from `.90:/dev/ttyUSB0` to perform live Coreboot/platform analysis? **Yes, across 3 layers:**

1. **EDK2 Payload & UEFI Shell (`CONFIG_EDK2_SERIAL_SUPPORT=y` + `CONFIG_EDK2_HAVE_EFI_SHELL=y`)** *(Available right now / zero C hacks)*:
   - TX `ESC` / `Delete` / Arrow keys on `.90:/dev/ttyUSB0` during the EDK2 splash/banner enters the **UEFI Boot Manager / CFR Setup / UEFI Shell** (`Shell>`).
   - Inside UEFI Shell over COM1 you can interactively run `pci`, `mm` (read/write PCI/MMIO/IO regs like `0xfed1f418`, `0xe00fa090`, `0x3f8`), `mem`, `dh -b`, `devtree`, `connect -r` — inspecting or testing register fixes **before Linux even boots**!
2. **Linux `(initramfs)` / Proxmox (`console=ttyS0,115200n8`)** *(Currently live on `v0.7` in BusyBox)*:
   - Interactive root shell over COM1 to inspect `dmesg`, `lspci -vvvxxx`, `ahci`, `iomem`, or pass `libata.force=1.5G` / kernel parameters at **GRUB serial TX** (`terminal_input serial`).
3. **Coreboot Ramstage / Romstage (`CONFIG_GDB_STUB=y` or custom serial prompt hook)**:
   - Coreboot's `src/console/console.c:89` (`CONFIG_GDB_STUB`) supports serial GDB debugging in 32-bit stages (`# FIXME Not implemented in long mode`, which fits `x86_32` Coreboot pre-payload stages), OR adding a 1-second `tstc()/getchar()` debug key check at `BS_POST_DEVICE` / `BS_PAYLOAD_LOAD` lets us drop into a minimal Coreboot register-peek/poke or pause cleanly over COM1.
   - *(Note: `.agents/skills/serial-communication` already documents non-blocking `ESC`/`Delete`/shell TX from `192.168.1.90`.)*

---

## 5. Stock-Pure Coreboot Update Blueprint (`v0.9`)

Applied to make Coreboot 100% faithful to the `@rea`-verified Z97-A `3503` stock ROM:
1. **SATA & HSIO (`early_pch_native.c`, `sata.c`, `devicetree.cb`)**:
   - Remove `sata_port0_gen3_dtle = 0x2` / `sata_port1_gen3_dtle = 0x2` carryover from `maximus_vii_hero` in `devicetree.cb` and program **`DTLE = 0x02`** (`SECRR50[29:24]=2`, `SECRR54[21:16]=2`) across **all 6 ports** (`0xea002550, 2750, 0950, 0b50, 2150, 2350`, respecting `lane_owner`) inside `early_pch_native.c` before `SATA_PCS`.
   - Keep `DAT_fffa38b4` (`SECRT88/8C`), `00:1f.2+0x98 = 0x205c0220`, `0x9c = 0x80000420`, `0x90/0x92/0x94`, `SIRI/SIRD`, and `ABAR` `PxCMD`/`PxDEVSLP`.
   - Read & log `00:16.0+0x68` ChipsetInit CRC vs stock `0x032c599d` in `early_pch_native.c`.
2. **USB2 & xHCI (`romstage.c`, `early_usb_native.c`)**:
   - Update `mainboard_usb2_ports` lengths/locations to exact `SBPEI/0xfff8d825` values (`0x80` internal, `0x100`/`0x140` back panel), fix `early_usb_native.c` `0xe500400f` -> `0xe500410f`, apply `DAT_fffa498c` (`0x9c01` -> `U2ECRP01..14 = 0x00059c01`), set xHCI `+0x8144` (`& ~0xc0 | 0x100`), and set `BIT(31)` on xHCI `+0x8154`.
3. **HDA Verbs (`hda_verb.c`)**:
   - Replace NID `0x11` (`0x18561130`) with stock `SBPEI` **`0x90460130`** and append `0x02050007, 0x020409c8` x2.
4. **PCIe Root Ports (`pcie.c` / `0002-lynxpoint-z97a-dmi-sata-pcie-aspm.patch`)**:
   - Match stock `lspci` across ALL active RPs (`00:1c.0`, `00:1c.3`, `00:1c.6`): `0xe1 = 0x00`, `00:1c.0 0xe8 = 0x02`, `00:1c.3 0xe8 = 0x00, 0xec = 0x03`, `00:1c.6 0xe8 = 0x10`, plus `0x324 |= (1 << 5)`.
5. **Display / iGPU (`devicetree.cb`, `gma.c`, `northbridge.c`) (`v0.10`)**:
   - Add `register "gfx"` (`use_spread_spectrum_clock=0`, `ndid=5`, `did={0x0100, 0x0300, 0x0301, 0x0302, 0x0303}`) matching `i915_opregion.bin` `DIDL`.
   - Program dynamic `0xa008` RP1 frequency (`MCHBAR+0x5998[15:8]`) & Haswell DP AUX clock dividers (`0x64010`/`0x64810`) in `gma.c`, and lock `PAVPC` (`00:00.0+0x58 = ((TOLUD_MB - 1) << 20) | 0x17`) in `northbridge.c` (`0001-haswell-z97a-above4g-peg-dmi-gma.patch`).
6. **RCBA Misc PM, PIRQ & PCIe/EHCI Topology (`lpc.c`, `devicetree.cb`) (`v0.9` / `v0.11`)**:
   - Set `RCBA 0x2b14 = 0x1e0a0314` and `0x33d4 = 0xc00bc000` (matching `inteltool.txt` and `PchInitDxe/0x18000368c`), plus `RCBA16(D31IR..D20IR)` (`0x3140..0x3160`) in `lpc.c` (`pch_pirq_init()`).
   - Enable `1c.7` (`RP8` `PCIEX1_1`), fix `1c.0`/`1c.4` (`PCIEX16_3 x2`) SMBIOS descriptors, and disable `1a.0` (`EHCI #2`, stock `FD=0x03b62001`) in `devicetree.cb`.
   - Keep `fb_console.c:348` screen clear before EDK2 handoff + keep `EDK2_SERIAL_SUPPORT=y` + `EDK2_HAVE_EFI_SHELL=y` for interactive TX-at-boot diagnostics.
7. **Gen3 SATA `10B8B` / `PAVPC` Lock Order / `GETHSIOVER` Fixes (`early_pch_native.c`, `sata.c`, `northbridge.c`) (`v0.12`)**:
   - **`early_pch_native.c` (`PchInitPeim/0xfff9c1f6` + `DAT_fffa37c4`/`DAT_fffa374c`)**: Set AHCI mode on `00:1f.2+0x90` (`& 0x3f | 0x60`) BEFORE `0x94=0x183`, HSIO/DTLE, and `SATA_PCS`; widen `z97_dtle_base` mask from 4-bit `SATA_DTLE_MASK` (`0xF`) to stock 6-bit `0x3f` (`0xc0ffffff` on `...50`, `0xffc0ffff` on `...54`); and add Port 1/common `pch_iobp_read()` telemetry (`0xea002688/8c/90/2750/54/8008/8100/8104`).
   - **`early_pch_native.c` (`PchInitPeim/0xfff9dad8`)**: Replace the raw `00:16.0+0x68` read (which returned `0x00000000` in `v0.11`) with the stock HECI `GETHSIOVER` sequence (`+0x4c = 0x70000001`, poll `+0x40` for `0x70000000`, read `+0x68`, write `+0x4c = 0x70000000` to close).
   - **`sata.c` (`PchInitDxe/0x180007b78.c:174`)**: Clear bit 0 of `ABAR+0xa4` (matching `stock-capture/inteltool.txt` `0x0a4 = 0x000c00fe`) and log `00:1f.2` + `ABAR` + `RCBA 0x38b0` telemetry.
   - **`northbridge.c` (`SaInitDxe/0x180000c84`)**: Write `PAVPC` (`00:00.0+0x58`) at the top of `northbridge_final()` BEFORE locking `GGC`/`DPR`/`BDSM`/`BGSM`/`TSEG`/`TOLUD` and `MC_LOCK` (`0x8f`).


---

## 6. Live Bring-Up Status (`v0.14` Flashed) & RAM-Only Audit Runbook

### 6.1 Current Host & Hardware State (`Coreboot v0.14`)
- **Target Board (`192.168.1.150`, ASUS `Z97-A Rev 1.03B`)**:
  - Spare `W25Q64FV` installed in DIP-8 socket (`QE=1`, unlocked `IFD` `FLMSTR1/2=0xffff0000`, stock ME `9.1.25.1005`, `I218-V` MAC `9c:5c:8e:90:7e:f2`). Original ASUS `3503` chip remains untouched for instant physical recovery.
  - Currently running **`Coreboot v0.14`** ([z97-a-coreboot-v0.14-8mb-stock-me-full.rom](../outputs/z97-a-coreboot-v0.14-8mb-stock-me-full.rom), SHA-256 `91809bfafd489a2f4a47c547da3d1704f708211579dd4a5c176e3851e9010901`), flashed hands-free via `iPXE>` HTTP using delta flasher `0x210000..0x7fffff` ([z97a_flash_v014.efi](../outputs/z97a_flash_v014.efi), SHA-256 `9769971d864566997818951f832e40a7e9dcc60f3ea0a8b5292c2bc76f14ea09`, `Z97A_FLASH_VERIFIED_OK`), preserving `IFD`/`GbE`/`ME`/`QE=1`/`RW_MRC_CACHE`/`SMMSTORE` (`Boot0005 proxmox`).
  - **Rule on SPI Wear**: Never flash speculative test images. Run all register and hardware isolation tests in RAM via `z97a_tool.efi` v2.7 over `iPXE>` HTTP (`72,192` B), then consolidate fixes into ONE SPI delta update.
- **Serial / HTTP / HDMI Host (`192.168.1.90`, `zeb-h61`, SSH user `zero`)**:
  - `COM1` UART (`115200 8N1`): `CH340` at `/dev/ttyUSB0`, currently armed with `catch_and_v014audit.py` to auto-catch `UiApp -> Boot Menu -> iPXE>` and run `v014audit` on next power-on.
  - HTTP Netboot Server: `python3 -m http.server 8080 --directory /home/zero/z97a-netboot --bind 0.0.0.0`.
  - **USB HDMI Capture Card**: MacroSilicon `MS2109` (`534d:2109`) at `/dev/video0`, driven by [`scripts/z97a_hdmi_grab.py`](../scripts/z97a_hdmi_grab.py) (`--warmup 1.8` default + continuous V4L2 buffer drain).

### 6.2 Resolved in `v0.13` -> `v0.14`
1. **`PAVPC` (`0x84100017`) & `ME` Normal Boot (`v0.13`)**:
   - Programmed `PAVPC` in `native_raminit/memory_map.c` before `mc_lockdown()`, removed runtime `GETHSIOVER` interference, and preserved `RW_MRC_CACHE`/`SMMSTORE` via `0x210000..0x7fffff` delta flashing.
2. **HDMI 1080p Proven Working (`MS2109` Wakeup Root Cause + `CDCLK_FREQ`)**:
   - Diagnosed via `hdmitest3` + `hdmitest4`: EDK2 GOP (`0xe0000000`, `1920x1080`) never dropped HDMI; `MS2109` powers down its HDMI RX when `/dev/video0` closes and needs **~1.15s (~67 frames)** of active V4L2 streaming before locking. Fixed `z97a_hdmi_grab.py` (`--warmup` + continuous drain) and enabled Haswell `CDCLK_FREQ=0x21b` in `gma.c`.
   - Visually verified `1920x1080` in [hdmi_v013_live_rgbw_1080p.jpg](../outputs/hdmi_v013_live_rgbw_1080p.jpg), [hdmi_v014_uiapp_1080p.jpg](../outputs/hdmi_v014_uiapp_1080p.jpg), and [hdmi_v014_ipxe_1080p.jpg](../outputs/hdmi_v014_ipxe_1080p.jpg).
   - **UEFI Pitfall Recorded**: Never dereference unmapped stolen RAM (`BDSM` `0x80200000`) from UEFI tools (caused triple-fault reset in early `hdmitest4`); always use mapped `GMADR` (`0xe0000000`).
3. **Six Upstream Haswell / LynxPoint DMI & PCH Clock-Gating Bugs Fixed in `v0.14`**:
   - Live `satatest3` + `hdmitest4` dumps exposed continuous `DMIBAR CESTS (0x1d0)=0x000011c1` and `00:1c.2 AER_110=0x3181` (live clearing Desktop CG & DMI ASPM in `satatest3` cured `00:1c.2 AER_110` to `0x00000000`). Audited against `SaInitPeim/0xfffb8134.c`, `SaInitPeim/0xfffb6422.c`, `SaInitDxe/0x180004484.c`, `SaInitDxe/0x1800053a8.c`, `PchInitDxe/0x18000368c.c`, `PchInitDxe/0x18000455c.c`, and `inteltool.txt`:
     1. `early_pcie.c`: fixed `0x910 + i -> 0x80c + i` (`[20:18]`) and `0xc28` shift `18 -> 0` (`0x13`).
     2. `early_dmi.c`: preserved `DMILCAP[17:12]` (`mask 0xf`), SA RC `0x71c=0x13` / `0x720=0x02000000`, `VC0=0x8000007b` (`VC1` off) on `BOARD_ASUS_Z97_A`.
     3. `early_pch_native.c`: guarded `V1STS` polling on `VC_ACTIVE`.
     4. `northbridge.c`: set `-6.0 dB` DMI de-emphasis (`DMILCTL2[6]=0`) and kept SA DMI ASPM OFF on Z97-A.
     5. `lpc.c`: kept PCH DMI ASPM OFF (`RCBA 0x21a8[1:0]=0`) and skipped Mobile `enable_clock_gating()` on Z97-A (`PMCON1=0x0210` verified).
     6. `gma.c`: programmed `CDCLK_FREQ(0x46200)=0x21b` (`539`).

### 6.3 Remaining Blocker & Armed `v0.14` Audit
- **SATA Port 1 (`CT500BX500SSD1`)**: In [`outputs/v014_diag_clean.txt`](../outputs/v014_diag_clean.txt), chunks `0..127` passed (`PxSERR=0`), then chunk `128/256` failed with `status=0x12 PxSSTS=00000133 PxSERR=040d0802` (`Z97A_SATA_STRESS_FAIL`), and `00:1c.2` still showed `ce_aer=0x1101` before W1C clear. Note that after ~6 minutes idle at `iPXE>`, EDK2/iPXE became unresponsive on UART/HDMI.
- **Armed RAM Diagnostic (`v014audit`, ZERO SPI writes)**:
  - `catch_and_v014audit.py` on `192.168.1.90` waits for next power-on, runs full non-zero `DMIBAR` (`0x0000..0x0e40`) & `RCBA` (`0x0000..0x3f00`) dumps, full `00:1f.2`/`00:1c.2` 256B PCI cfg, W1C AER/SERR clear + `2.5s` active-idle + `2.5s` pure-PHY-idle (`ST=0, FRE=0`), SATA Gen1/Gen2/Gen3 `3s` idle + `4 MiB` isolation, and `00:1c.2` ASPM-OFF `2s` isolation.
  - Once `v014audit` output is diffed against `stock-capture/inteltool.txt` (plus physical SATA cable/port check if Gen1–3 all drop at PHY idle), either Proxmox boots cleanly or the single remaining diff goes into one final SPI update.
