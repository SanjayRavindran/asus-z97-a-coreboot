# ASUS Z97-A Stock Firmware Audit (`@rea` MCP)

Analyzed [`stock-capture/z97-a-original-chip.bin`](../stock-capture/z97-a-original-chip.bin) (`8,388,608` B, SHA-256 `a1fd7f2974dff0614ad9f7f63784db4bc9cfbb62e0779ad42e76f49589e2cab1`) using `@rea` MCP (`inspect_uefi_firmware`, `inspect_uefi_nvram`, `extract_uefi_firmware`, `open_binary`, and `decompile_all` with Ghidra 12.2) on `192.168.1.15`.

---

## 1. UEFI Flash Structure & AMI NVRAM (`inspect_uefi_firmware`, `inspect_uefi_nvram`)

- **Firmware Volumes:** 7 volumes mapped across `0x180000..0x7fffff` (`394` FFS items: `191` drivers, `106` PEIMs, `57` freeform, `17` raw, `8` combined-smm-dxe, `6` pad, `3` smm, `2` pei-core, `2` security-core, `1` dxe-core, `1` smm-core).
- **AMI NVAR Stores:** 4 NVAR stores mapped (`Store 0` `337` vars, `Store 1` `268` vars, `Store 2` `16` vars, `Store 3` `16` vars), including `Setup` (`2,159` B), `StdDefaults` (`6,307` B), `PNP0501_0_NV` / `PNP0501_1_NV` (`COM1`), `SetupHWMOneof`, and `VARSTORE_OCMR_SETTINGS_NAME`.
- **Extracted Stock Microcode (`FFS 17088572-377f-44ef-8f4e-b09fff46a070`, `82,944` B body):**
  - `0x40671` rev `0x1d` (`2018-01-21`, `12,288` B)
  - `0x306c3` rev `0x24` (`2018-01-21`, `23,552` B) — Coreboot ships newer `0x306c3` rev `0x28`
  - `0x306c2` rev `0xffff0006` (`2012-10-17`, `22,528` B)
  - `0x306c1` rev `0xffff0013` (`2012-06-14`, `24,576` B)

---

## 2. Decompiled `SIOBasicIOPei` (`17/17` Functions, `2,432` B PE32)

Source FFS: `0b4bdcff-74b2-45ad-91e1-8f6634c8a1dc` (`@0x680730`), extracted SHA-256 `d13d1fefbdd58d577625d63e696f863b7765626e8a60c5bd0e37f22836c413da`.

Key hardware findings from Ghidra pseudocode (`0xfff80be0`, `0xfff80c78`):
1. **CPU `TjMax` -> `NCT6791D` HWM Bank 7:** Reads MSR `0x1a2` (`MSR_TEMPERATURE_TARGET`), programs HWM Bank `7` (`0x295`/`0x296`, `CR 0x4e=7`) reg `0x03=0x10`, writes `(TjMax >> 16)` into Bank `7` reg `0x09`, sets Bank `7` reg `0x01=0x95`.
2. **HWM Bank 0 & Bank 4 Setup:** Bank `0` `0x5d = (val & 0xf9) | 1`, `0x5e &= 0xf0`, `0xae = 0x03`; Bank `4` `0xf8 = 0x50`, `0xfa = 0x51`.
3. **ACPI / Anti-Surge (`LDN 0x0a`, `0x0b`):**
   - `LDN 0x0a` (`ACPI`) reg `0xf1` bit 7 (`0x80`) records ASUS Anti-Surge latch; clears/arms it alongside `LDN 0x0a` `0xee |= 0x10` and `LDN 0x0b` (`HWM`) `0xe5=0x54`, `0xe6 &= ~1`, `0xf5 |= 0x10`.
   - `LDN 0x0e` (`CIR Wake-Up`, `0x2a0..0x2ab`) is only enabled if `Setup` option `local_44 != 0` (disabled in stock capture).
4. **Coreboot Impact:** `LDN 0x0a` `0xee=0x10` was already in `bootblock.c`; added `irq 0xe6 = 0x08` and `irq 0xf5 = 0x10` to `device pnp 2e.b` in `devicetree.cb` so `LDN 0x0b` matches `superiotool.txt` 1:1.

---

## 3. Decompiled `ProjectPEI` (`21/21` Functions, `8,320` B PE32)

Source FFS: `29be380a-fbc0-462b-a6c2-5c5a076cafde` (`@0x6e5f48`), extracted SHA-256 `1de1c30c6b4c62921dc2bb8edd6bcfb2029d3adc1296b9d6a856958e4b3d7e4d`.

Key hardware findings from Ghidra pseudocode (`0xfffe6491`, `0xfffe6402`, `0xfffe62b5`, `0xfffe6260`):
1. **Board Reset Pulse (`entry` @ `0xfffe6491`):**
   - Enters SIO config (`0x2e` `0x87`, `0x87`), clears bit 4 (`0x10`) in `LDN 0x08` (`GPIO1`) `0xf0`/`0xf1` (`GP14`) and clears bit 6 (`0x40`) in PCH `0x1c07` (`GPIOBASE+0x07`, `GPIO30`), delays `10000` ACPI timer ticks (`0x1800+0x08`), then restores `LDN 0x08` `0xf0 |= 0x10` and `0x1c07 |= 0x40`.
2. **Board Strap / Conditional GPIO (`0xfffe6402`):**
   - Reads `0x1c0c` (`GPIO1`) and `0x1c0e` (`GPIO17`, `GPIO21`) and sets bit 10 (`0x400`) in `0x1c30` (`GPIO_USE_SEL2`) and `0x1c34` (`GP_IO_SEL2`) -> **GPIO42 as GPIO Input**. Already captured accurately in `gpio.c` (`GPIO_USE_SEL2=0x02fd70ff`, `GP_IO_SEL2=0x1f56ff74`).
3. **SIO `GPIO0` (`0xfffe62b5`):**
   - Enables `LDN 0x08` `0x30 |= 0x02` (`GPIO0`) and configures `0xe0`/`0xe1` bit 7 (`GP07`). Coreboot's `nct6791d` driver sets `CR 0x30 = 0x82` automatically when `2e.108` (`GPIO0`) and `2e.708` (`GPIO1`) are `on` in `devicetree.cb`.
4. **PCI Master Disable (`0xfffe663e`):**
   - Loops Bus 0 devices `0..31` functions `0..7` and clears PCI Command Bus Master (`reg 0x04 &= ~4`) early in PEI before PCI enumeration.
