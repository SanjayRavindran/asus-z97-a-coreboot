# ASUS Z97-A Coreboot (`v0.17` / `v26.09-r3`) vs. Stock AMI `3503` Parity Report

**Release Variant 1 (`with-me`):** `asus-z97-a-coreboot-v26.09-r3-with-me.bin` (`8,388,608` B — Stock ME `9.1.25.1005` + `00:16.0` `MEI #1` hidden via `FD2` + unlocked IFD)  
**Release Variant 2 (`without-me`):** `asus-z97-a-coreboot-v26.09-r3-without-me.bin` (`8,388,608` B — Minimal `116 KiB` `ROMP+BUP` ME `118,784` B + `AltMeDisable=1` + `00:16.0` hidden + unlocked IFD)  
**Stock Reference Image:** Stock AMI Aptio IV `3503` (`8,388,608` B, SHA-256 `a1fd7f2974dff0614ad9f7f63784db4bc9cfbb62e0779ad42e76f49589e2cab1`)  
**Live Verified Host:** ASUS `Z97-A` `Rev 1.03B`, Intel `i7-4790K`, `32 GiB` DDR3-1866, `CT500BX500SSD1` (`SATA6G_5` / `ata5`), `I218-V` + `I226-V` 2.5GbE, `AOC 28E850` HDMI (`Proxmox VE 9`, Linux `7.0.14-17-pve`, `-z97a-v0.17-with-me`)

---

## 1. SPI Flash Region & Soft-Strap Parity

| Region / Field | Stock AMI `3503` | Coreboot `with-me` | Coreboot `without-me` (`minimal-116k`) | Status / Notes |
|---|---|---|---|---|
| **Total ROM Size** | `8,388,608` B (`8 MiB`) | `8,388,608` B (`8 MiB`) | `8,388,608` B (`8 MiB`) | Exact match |
| **`FREG0` IFD (`0x000000..0x000fff`)** | `FLMSTR1=0x0a0b0000` (ME locked) | `FLMSTR1=0xffff0000` (`FLREG1=0x07ff0180`, `FLREG2=0x017f0003`) | `FLMSTR1=0xffff0000` (`FLREG1=0x07ff0020`, `FLREG2=0x001f0003`, `AltMeDisable=1`) | Unlocked IFD; `without-me` shrinks ME to `116 KiB` |
| **`PCHSTRP9` (`IFD+0x124`)** | `0xf0544a85` (`pcie-x1`) | `0xf0544a85` (`pcie-x1`) | `0xf0544a85` (`pcie-x1`) | Switchable to `m2-x2` via [`scripts/switch-m2-ifd.py`](../scripts/switch-m2-ifd.py) |
| **`PCHSTRP18` (`IFD+0x148`)** | `0x00000003` (Flex-I/O) | `0x00000003` (Flex-I/O) | `0x00000003` (Flex-I/O) | Exact match (`HSIO 13..14` `SATA Express`) |
| **VSCC Flash Table** | `0xc8 4017`, `0xef 4017` | `0xc8 4017`, `0xef 4017` | `0xc8 4017`, `0xef 4017` | Must enforce `W25Q64FV` `SR2=0x02` (`QE=1`) on blank external chips |
| **`FREG3` GbE (`0x001000..0x002fff`)** | `9c:5c:8e:90:7e:f2` (`e3f2b6e5...`) | `9c:5c:8e:90:7e:f2` (`e3f2b6e5...`) | `9c:5c:8e:90:7e:f2` (`e3f2b6e5...`) | **0 byte diffs** (replace with your board's MAC if flashing another Z97-A) |
| **`FREG2` Intel ME** | `9.1.25.1005` (`1,560,576` B) | `9.1.25.1005` (`1,560,576` B) + `00:16.0` hidden | `118,784` B `ROMP+BUP` (`1df70920...`) + `AltMeDisable=1` + `00:16.0` hidden | `FTPR` RSA `VALID` in both (prevents Z97 30-min power-off) |
| **`SMMSTORE` (`0x190000..0x20ffff`)** | N/A | `512 KiB` UEFI NVRAM + CFR | `512 KiB` UEFI NVRAM + CFR | Aligned across both variants; survives `-i COREBOOT` deltas |
| **`COREBOOT` CBFS (`0x211000..0x7fffff`)** | N/A | `6,221,824` B | `6,221,824` B | Microcode `0x28`, Coreboot, `dsdt.aml`, `vbt.bin`, EDK2 + UEFI Shell + `iPXE` |

---

## 2. Silicon, GPIO, Super I/O, Audio, PCIe, Display & Power Parity

| Subsystem | Stock Reference | Coreboot `v0.17` Implementation | Live Verification (`192.168.1.150`) |
|---|---|---|---|
| **CPU Microcode (`0x306c3`)** | Stock `rev 0x24` | `rev 0x28` (`2019-11-12`) + `ENABLE_VMX=y` | `microcode 0x28`, `MSR 0x3a = 0x5` (`Lock=1, VMX_outside_SMX=1`), `/dev/kvm` |
| **Memory (`32 GiB` DDR3-1866)** | 4x8 GiB dual-channel | `USE_NATIVE_RAMINIT=y` (`0x50..0x53`), `PAVPC=0x84100017`, `TOLUD=0x84200001` | `31Gi` RAM, `[mem 0x87ae00000-0x7fffffffff]` 64-bit Above-4G window |
| **PCH GPIOs (`GPIO0..75`)** | `inteltool` `GPIOBASE` | [`gpio.c`](../board/src/mainboard/asus/z97-a/gpio.c) (`76` pins, `GPI_INV=0x6045`) | **0 diffs across all 76 pins** |
| **Super I/O (`NCT6791D` `0x2e`)** | `superiotool` + `SIOBasicIOPei` + `QFanDXE` | [`bootblock.c`](../board/src/mainboard/asus/z97-a/bootblock.c) + [`mainboard.c`](../board/src/mainboard/asus/z97-a/mainboard.c) (`0xae=0x03`, `4f8/4fa`, `5d/5e` thermistor, `0x66=0x11`, `0x35` crit, `0x39=65/0x3a=60`, `0x46\|=0xc0`, `0x02=0x40`, PWM mode) + [`asus-z97-a-sensors.conf`](../configs/asus-z97-a-sensors.conf) | `CR 0x28=0x00` unlocks `@0x290`; **`0` alarms** (`VCCIN 1.79V`, `+5V 5.08V`, `+12V 12.29V`, `SYSTIN +32°C`, `CPUTIN +36.5°C`, **`PECI0 +34.5°C`**, SmartFan IV **`29% -> 100%` / `509 -> 1518 RPM`**) |
| **`COM1` Serial Console** | `LDN 0x02` `0x03f8`, IRQ `4` | Coreboot + EDK2 + iPXE `115200 8N1` | Verified across `ttyS0` `0x03f8` IRQ `4` |
| **Realtek `ALC892` Audio** | `SBPEI` + `codec#0` | [`hda_verb.c`](../board/src/mainboard/asus/z97-a/hda_verb.c) (`0x10ec0892`, subid `0x1043860b`) | **14 / 14 pin configs match 1:1** (`NID 0x11=0x90460130`) |
| **Intel `HD 4600` & `data.vbt`** | `i915_vbt.bin` (`6,144` B) | Identical `blobs/data.vbt` (`ada53b3d...`) in CBFS `vbt.bin` + IGD OpRegion + 5 `libgfxinit` ports (`HDMI1`, `DP2`, `HDMI2`, `HDMI3`, `Analog`) + `CDCLK=0x21b` (`539` MHz) | `card1-HDMI-A-3` connected (`AOC 28E850`, `3840x2160`/`1920x1080`) + `00:03.0` HDMI audio |
| **DMI & Desktop Clock Gating** | `Gen2 x4`, `-6 dB`, ASPM OFF, `PMCON1=0x0210` | Fixes in `early_pcie.c`, `early_dmi.c`, `northbridge.c`, `lpc.c` | Zero DMI correctable errors, `PMCON1=0x0210` |
| **PCIe ASPM & Link Stability** | Manual p. 95–96: all ASPM `[Disabled]` | `PCIEXP_ASPM=n`, `CLK_PM=n`, `L1_SUB_STATE=n` in `lynxpoint/Kconfig` | All RPs/endpoints `ASPM Disabled`, `CommClk+`, `enp5s0` `2500Mb/s` Full |
| **AHCI SATA (`SATA6G_5` / `ata5`)** | `6.0 Gbps` `CT500BX500SSD1` | 6-port HSIO `DTLE=0x02` + `DAT_fffa38b4` + `00:1f.2+0x98=0x205c0220` | `ata5: SATA link up 6.0 Gbps`, `451 MB/s`, **`0` resets** |
| **Intel ME (`00:16.0`)** | Stock ME visible or disabled | `DISABLE_ME_PCI=y`, `lynxpoint/cfr.h` default=`1`, `me.c` `FD2` hide (`00:16.0` hidden even with `AltMeDisable=1`) | `lspci -nn -s 00:16.0` **empty** (`14` IOMMU groups) |
| **Power-ON & Wake-on-LAN** | `GEN_PMCON_3=0x3808` / `_PRW` | `POWER_STATE_ON_AFTER_FAILURE=y`, [`wol.asl`](../board/src/mainboard/asus/z97-a/acpi/wol.asl), `gpe0_en_1=0x40042a46`, `lpc.c` `WOL_EN_OVRD=1`, `smihandler.c` S5 GPE preservation | `00:1f.0 a4.w = 3808`; `/proc/acpi/wakeup`: `GLAN`, `PEGP`, `RP01..08`; `ethtool` `Wake-on: g` on `eno0` & `enp5s0` |

---

## 3. Intentional Improvements Over Stock AMI `3503`

1. **Unlocked SPI Flash (`FLMSTR1=0xffff0000`, `BIOS_CNTL=0x09`)**: Internal `flashrom` updates (`0x211000..0x7fffff`) and pre-OS UEFI delta flashing (`z97a_tool.efi`) work without removing the socketed SPI chip.
2. **Minimal / Disabled Intel ME (`without-me` & `00:16.0` Hidden)**: Removes non-`FTPR` ME partitions down to `116 KiB` `ROMP+BUP` (`AltMeDisable=1`, RSA VALID) and hides `00:16.0` (`MEI #1`) via `FD2` by default on both variants.
3. **Full Virtualization, Above-4G, ReBar & SR-IOV by Default**: `ENABLE_VMX=y`, `DMAR` VT-d (`14` IOMMU groups), `ALWAYS_ALLOW_ABOVE_4G_ALLOCATION=y` (`PM64`), Resizable BAR up to `32 GiB` (Coreboot + EDK2 `PcdPcieResizableBarSupport=TRUE`), SR-IOV + ARI, and hotplug apertures.
4. **Power-ON After AC Loss & S3/S4/S5 Wake-on-LAN**: `GEN_PMCON_3=0x3808` (`AFTERG3_EN=0`, `WOL_EN_OVRD=1`), full `_PRW`/`_DSW` ACPI wakeup (`GLAN`, `PEGP`, `RP01..08`), and S5 GPE preservation in `smihandler.c`.
5. **Native UEFI `iPXE`, `UEFI Shell` & ASUS CFR Features**: Integrated `ipxe.efi` (`HTTPS` + `IMAGE_TRUST_CMD`), `UEFI Shell`, `ESC -> Coreboot Configuration` CFR menu (superseding legacy `nvramcui`/`coreinfo`), Q-LEDs, SmartFan IV Q-Fan, `ErP S4/S5`, `ENE EC` VRM/EPU/TPU controls, and `0` kernel `err`/`crit`/`alert`/`emerg` messages.

---

## 4. Live Verification Matrix (`Coreboot v0.17` on `192.168.1.150`)

| Device / BDF | Subsystem | Live Status (`-z97a-v0.17-with-me`) |
|---|---|---|
| `00:00.0` | Haswell Host Bridge (`8086:0c00`) | Healthy (`PAVPC=0x84100017`, `TOLUD=0x84200001`, `[mem 0x87ae00000-0x7fffffffff]`, `DMAR` `14` IOMMU groups) |
| `00:01.0` | PEG10 x16 (`8086:0c01`) | Healthy (`PEGP S4 *enabled` in `/proc/acpi/wakeup`, `ASPM Disabled`) |
| `00:02.0` | HD 4600 iGPU (`8086:0412`) | Healthy (`AOC 28E850` on `card1-HDMI-A-3`, stock `vbt.bin`, `CDCLK=0x21b`) |
| `00:03.0` | Haswell HDMI Audio (`8086:0c0c`) | Healthy (`card1`) |
| `00:14.0` | Z97 xHCI USB 3.0 (`8086:8cb1`) | Healthy (`14` USB2 + `6` USB3, `XHCI S3 *enabled`) |
| `00:16.0` | Intel MEI #1 (`8086:8cba`) | **Hidden by default (`FD2` `PCH_DISABLE_MEI1`)** |
| `00:19.0` | Onboard `I218-V` (`8086:15a1`) | Healthy (`eno0`, MAC `9c:5c:8e:90:7e:f2`, `Wake-on: g`, `GLAN S4 *enabled`) |
| `00:1b.0` | Realtek `ALC892` (`8086:8ca0`) | Healthy (`card0`, `1043:860b`, `NID 0x11=0x90460130`) |
| `00:1c.0 -> 02:00.0` | RP4 -> `ASM1083` (`1b21:1080`) | Healthy (`RP01 S4 *enabled`, `2.5 GT/s x1`, `ASPM Disabled`) |
| `00:1c.1` | RP5 `PCIEX16_3` (`8086:8c98`) | Healthy (`RP02 S4 *enabled`, `ASPM Disabled`) |
| `00:1c.2 -> 05:00.0` | RP7 -> `I226-V` (`8086:125c`) | Healthy (**`2500Mb/s` Full**, `RP03 S4 *enabled`, `Wake-on: g`, `ASPM Disabled`) |
| `00:1c.3` | RP8 `PCIEX1_1` (`8086:8c9e`) | Healthy (`RP04 S4 *enabled`, `ASPM Disabled`) |
| `00:1f.0` | Z97 LPC (`8086:8cc4`) | Healthy (`a4.w = 3808` Power-ON + WoL, `nct6775`, `COM1`, `ENE EC MBE0-Z97-0121`) |
| `00:1f.2` | AHCI SATA (`8086:8c82`) | Healthy (**`ata5: SATA link up 6.0 Gbps`**, `CT500BX500SSD1`, `0` kernel errors) |
| `00:1f.3` | Z97 SMBus (`8086:8ca2`) | Healthy (`0x50..0x53` SPD, `0x24`/`0x5a` ASUS telemetry) |
