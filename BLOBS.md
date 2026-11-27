# ASUS Z97-A Firmware, Component & Blob Inventory

This document catalogs every open-source firmware component, binary SPI region blob, CPU microcode revision, and onboard peripheral firmware version used in the ASUS `Z97-A` Coreboot build.

> **Important MAC Notice (`blobs/gbe-z97-a.bin`):** `blobs/gbe-z97-a.bin` contains an example `FREG3` GbE EEPROM with MAC address `9c:5c:8e:90:7e:f2`. If flashing another ASUS Z97-A board externally or overwriting `0x001000..0x002fff`, preserve or inject your own board's stock `FREG3` GbE region (`dd` or `ifdtool --inject-gbe`) so two machines on the same LAN do not share a MAC address.

---

## 1. Open-Source Firmware Components (`COREBOOT` CBFS)

| Component | Version / Commit | Date | Status & Notes |
|---|---|---|---|
| **Coreboot** | Live board (`v0.15`): `df45ea2c49b4`<br>CI Release (`v26.09`): tag `26.09` | `2026-11-14`<br>`2026-09` | **Latest** (CI pipeline tracks each quarterly `YY.MM` tag; next `26.12`) |
| **EDK2 `UefiPayloadPkg`** | MrChromebox `f248a68be3` (`uefipayload_2608-test`) | `2026-11-14` | **Latest** (with UEFI GOP, CFR Setup menu, NVMe DXE, SerialIo) |
| **iPXE** | `21f9cda4d` (`CONFIG_IPXE_MASTER=y`) | `2026-11-25` | **Latest** (fetched from upstream `master` on every CI build; `HTTPS` + `IMAGE_TRUST_CMD`) |
| **`libgfxinit`** | `7ac11bc` + [`0003-libgfxinit-z97a-port-limits.patch`](patches/0003-libgfxinit-z97a-port-limits.patch) | `2026-10-31` | **Latest** (automatically selected during build if Coreboot tag ships older `libgfxinit`) |
| **`libhwbase` / `vboot`** | `61f7287` / `f691f767` | `2026-07-08` / `2026-10-01` | **Latest** |

---

## 2. Binary Blobs (`blobs/`) & CPU Microcode (`cpu_microcode_blob.bin`)

### Verified Blob Catalog

| Blob File | Version / Provenance | Size (B) | SHA-256 | Notes |
|---|---|---:|---|---|
| `blobs/data.vbt` | `$VBT HASWELL` ver `100` (`BDB 189`) | `6,144` | `ada53b3d9638c206a63fbe63f6ef48b74b86da90018e34cd30f9c544b4574f36` | **Final** ASUS Z97-A `3503` `i915_vbt.bin` (identical SHA-256 to stock ROM; embedded in CBFS `vbt.bin` & ACPI IGD OpRegion for VGA, DDI-B HDMI/DVI-D, DDI-C DP/HDMI, DDI-D HDMI + Audio) |
| `blobs/gbe-z97-a.bin` | Intel `I218-V` NVM **`0.1-4`** (`word5=0x0014`) | `8,192` | `e3f2b6e5e994794b71b56060b69075de4a6f2b3fde17197842571c5a4d9635a7` | **Final** ASUS `3503` `FREG3` GbE EEPROM (`0x001000..0x002fff`, MAC `9c:5c:8e:90:7e:f2`) |
| `blobs/ifd-z97-a-stock.bin` | Stock ASUS `3503` `FREG0` IFD | `4,096` | `2fe73369341060146a6e538dd03b4c588717b065f927f286d89da6013397ed9f` | Locked stock IFD (`FLMSTR1=0x0a0b0000`, `FLCOMP=0x64900044`, `BIOS@0x180000`) |
| `blobs/ifd-z97-a-unlocked.bin` | Stock `3503` IFD (`ifdtool -u`, **`with-me`**) | `4,096` | `f0fd3c5f20f96df196aa9a10d1c6da3cedb30e279d6a476e6d47ee19bc37c72e` | Host-unlocked (`FLMSTR1/2=0xffff0000`, `PCHSTRP9=0xf0544a85`, `PCHSTRP18=0x00000003`, `AltMeDisable=0`) |
| `blobs/me-z97-a-stock-9.1.25.1005.bin` | **Intel ME `9.1.25.1005`** (**`with-me`**) | `1,560,576` | `85c8eb541b6272f2eab6200075fbf939938ae885d5cf83206293ebd752603590` | **Final ASUS `3503` ME** (`FTPR` RSA VALID; `00:16.0` MEI hidden by default via `FD2`). *(Note: Intel's final 1.5 MiB Consumer 9.1.x release is `9.1.45.3000`.)* |
| `blobs/ifd-z97-a-minimal-116k-unlocked.bin` | Shrunk IFD + `AltMeDisable=1` (**`without-me`**) | `4,096` | `e26b6c9b0f724e992cbe81aba32c51313f1b642f8f5ac7455f93d30f9ef859f8` | Minimal-ME IFD (`FLREG2=0x001f0003`, `FLREG1=0x07ff0020`, `BIOS@0x020000`, `+1.375 MiB` free BIOS flash region while keeping identical `COREBOOT@0x211000` FMAP alignment, `AltMeDisable=1`) |
| `blobs/me-z97-a-minimal-116k.bin` | Truncated `116 KiB` ME (**`without-me`**) | `118,784` | `1df709207251346c7221a72128a5b1e15cedacbd4edf7dcbcd24b73b315bdb43` | `me_cleaner.py -S -r -t` truncated `FTPR` (`ROMP` + `BUP`), RSA VALID (prevents Z97 30-min shutdown timer) |
| `blobs/ifd-z97-a-mecleaned-compat-unlocked.bin` | Optional compat IFD (`BIOS@0x180000`) | `4,096` | `5349c794720de8c41ced5fdbfbe8b4eed456e9b7fb9905ed367f5036ccb4f017` | Compat layout keeping `BIOS@0x180000` (`FLMSTR1=0xffff0000`, `PCHSTRP10=0x00400080` `AltMeDisable=1`) |
| `blobs/me-z97-a-cleaned-compat.bin` | Optional compat neutralized ME (`9.1.25.1005`) | `1,560,576` | `7e4983c1b5af5d269d967d4f0798bb41567ad81ebc30196f955e0d33a0bba896` | `me_cleaner.py -S`, non-`FTPR` modules stripped, `FTPR` RSA VALID |

### CPU Microcode (`3rdparty/intel-microcode` `bdc92ab` / `20260925`)
Stock AMI `3503` shipped `0x306c3` rev `0x24`. Coreboot bundles Intel's latest Haswell production microcode revisions inside `cpu_microcode_blob.bin`:

| CPUID | Platform Flags | Revision | Date | Size (B) | Target CPU Family |
|---|---|---|---|---:|---|
| **`0x306c3`** | `0x32` | **`0x28`** (**Latest**) | `2019-11-12` | `23,552` | Haswell / Devil's Canyon (`i7-4790K`) |
| `0x40661` | `0x32` | **`0x1c`** (**Latest**) | `2019-11-12` | `25,600` | Crystal Well (`Core i7-4xx0R/HQ`) |
| `0x40671` | `0x22` | **`0x22`** (**Latest**) | `2019-11-12` | `14,336` | Broadwell Desktop LGA1150 |

---

## 3. Separate Onboard & Add-In Hardware Firmwares (Outside 8 MiB SPI)

| Device | Firmware Version | Storage / Notes |
|---|---|---|
| **ASUS ENE Embedded Controller (`MBE0-Z97-0121`)** | **`MBE0-Z97-0121`** | Dedicated EC flash (stock ASUS `3503` final; read-only probed via `0x62/0x66`) |
| **Add-in Intel `I226-V` 2.5GbE (`enp5s0`)** | NVM **`2013:8877`** | NIC EEPROM on `PCIEX1_2` (`05:00.0`, `igc`) |
| **Crucial `CT500BX500SSD1` (`sda`)** | **`M6CR061`** | SSD controller firmware on `SATA6G_5` (`ata5`) |
