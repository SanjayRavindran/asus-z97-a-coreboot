# ASUS Z97-A Coreboot (`v0.15` / `v26.11.1`) vs. Stock AMI `3503` Parity Report

**Finalized Coreboot Image (`v26.11.1`):** `z97-a-coreboot-v0.15-8mb-stock-me-full.rom` (`8,388,608` B, SHA-256 `9865ddf777c7cfacf4961725e8292d2d149fd391c4ababf3d03182f72b3ffdc3`; `COREBOOT` delta `0x211000..0x7fffff` SHA-256 `2677aa7494c40ed5f52ebd485e20ccdb763ca72ff1ff115915a0ba48e7b57ac3`)  
**Stock Reference Image:** Stock AMI Aptio IV `3503` (`8,388,608` B, SHA-256 `a1fd7f2974dff0614ad9f7f63784db4bc9cfbb62e0779ad42e76f49589e2cab1`)  
**Live Verified Host:** ASUS `Z97-A` `Rev 1.03B`, Intel `i7-4790K`, `32 GiB` DDR3-1866, `CT500BX500SSD1` (`SATA6G_5` / `ata5`), `I218-V` + `I226-V` 2.5GbE, `AOC 28E850` HDMI (`Proxmox VE 9`, Linux `7.0.14-17-pve`)

---

## 1. SPI Flash Region & Soft-Strap Parity

| Region / Field | Stock AMI `3503` | Coreboot `v0.15` (`v26.11.1`) | Status / Notes |
| :--- | :--- | :--- | :--- |
| **Total ROM Size** | `8,388,608` B (`8 MiB`) | `8,388,608` B (`8 MiB`) | Exact match |
| **`FREG0` IFD (`0x000000..0x000fff`)** | `FLMSTR1=0x0a0b0000` (ME locked) | `FLMSTR1=0xffff0000` (ME unlocked) | **4 intentional unlock bytes only**; allows internal `flashrom` & pre-OS UEFI delta updates |
| **`PCHSTRP9` (`IFD+0x124`)** | `0xf0544a85` (`pcie-x1`) | `0xf0544a85` (`pcie-x1`) | Exact match (`PCIEX1_1` + `PCIEX1_2` / `I226-V` active; switchable to `m2-x2` via [`scripts/switch-m2-ifd.py`](../scripts/switch-m2-ifd.py)) |
| **`PCHSTRP18` (`IFD+0x148`)** | `0x00000003` (Flex-I/O) | `0x00000003` (Flex-I/O) | Exact match (`HSIO 13..14` serve `SATA Express` `SATA6G_5/6` when owned by SATA) |
| **VSCC Flash Table** | `0xc8 4017`, `0xef 4017` | `0xc8 4017`, `0xef 4017` | Exact match (**must enforce `W25Q64FV` `SR2=0x02` `QE=1`** when programming blank/external chips) |
| **`FREG3` GbE (`0x001000..0x002fff`)** | `9c:5c:8e:90:7e:f2` (`e3f2b6e5...`) | `9c:5c:8e:90:7e:f2` (`e3f2b6e5...`) | **0 byte diffs** |
| **`FREG2` Intel ME (`0x003000..0x17ffff`)** | ME `9.1.25.1005` (`85c8eb54...`) | ME `9.1.25.1005` (`85c8eb54...`) | **0 byte diffs**, `FTPR` RSA `VALID` |
| **`SMMSTORE` (`0x190000..0x20ffff`)** | N/A | `512 KiB` UEFI NVRAM + CFR | Survives `0x211000..0x7fffff` `COREBOOT` delta flashes |
| **`COREBOOT` CBFS (`0x211000..0x7fffff`)** | N/A | `6,221,824` B (`3,787,108` B free) | Contains microcode `0x28`, Coreboot stages, `vbt.bin`, `EDK2 + iPXE` (`2,009,134` B) |

---

## 2. Silicon, GPIO, Super I/O, Audio, PCIe & Display Parity

| Subsystem | Stock Reference | Coreboot `v0.15` Implementation | Verification Result (`192.168.1.150`) |
| :--- | :--- | :--- | :--- |
| **CPU Microcode (`0x306c3`)** | Stock `rev 0x24` | `rev 0x28` (`2019-11-12`) in `cpu_microcode_blob.bin` | **Upgraded** (`grep microcode /proc/cpuinfo` = `0x28`) |
| **Memory (`32 GiB` DDR3-1866)** | 4x8 GiB dual-channel | `USE_NATIVE_RAMINIT=y` (`0x50..0x53`), `PAVPC=0x84100017`, `TOLUD=0x84200001` | **Verified** (`31Gi` system RAM, `PAVPC`/`TOLUD` exact) |
| **PCH GPIOs (`GPIO0..75`)** | `inteltool` `GPIOBASE` | [`gpio.c`](../board/src/mainboard/asus/z97-a/gpio.c) (`76` pins, `GPI_INV=0x6045`) | **0 diffs across all 76 pins** |
| **Super I/O (`NCT6791D` `0x2e`)** | `superiotool` + `SIOBasicIOPei` | [`bootblock.c`](../board/src/mainboard/asus/z97-a/bootblock.c) + [`devicetree.cb`](../board/src/mainboard/asus/z97-a/devicetree.cb) | `CR 0x28=0x00` unlocks HWM `0x290` for Linux `nct6775` (`fan2=860-870 RPM`, `SYSTIN=+31°C`, voltages) |
| **`COM1` Serial Console** | `LDN 0x02` `0x03f8`, IRQ `4` | Coreboot + EDK2 + iPXE `115200 8N1` | Verified end-to-end across boot stages and `/dev/ttyS0` |
| **Realtek `ALC892` Audio** | `SBPEI` + `codec#0` | [`hda_verb.c`](../board/src/mainboard/asus/z97-a/hda_verb.c) (`0x10ec0892`, subid `0x1043860b`) | **14 / 14 pin configs match 1:1** (`NID 0x11=0x90460130`) |
| **Intel `HD 4600` & HDMI Display** | `i915_vbt.bin`, `CDCLK=0x21b` | Embedded `vbt.bin` + `libgfxinit` (`DDI-B/C/D`) + `CDCLK=0x21b` (`539` MHz) | **Live on `AOC 28E850` (`card1-HDMI-A-3`, `1920x1080`, `dpms=On`)** |
| **DMI Link & Desktop Clock Gating** | `Gen2 x4`, `-6 dB`, ASPM OFF, `PMCON1=0x0210` | Fixes in `early_pcie.c`, `early_dmi.c`, `northbridge.c`, `lpc.c` | Zero DMI correctable errors (`0x1d0=0`), `PMCON1=0x0210` |
| **PCIe ASPM & Link Stability** | Manual p. 95–96: all ASPM `[Disabled]` | `PCIEXP_ASPM=n`, `CLK_PM=n`, `L1_SUB_STATE=n` in `lynxpoint/Kconfig` | **Cured `ata5` Gen1 reset storm**; all bridges `ASPM Disabled`, zero AER errors |
| **AHCI SATA (`SATA6G_5` / `ata5`)** | `6.0 Gbps` `CT500BX500SSD1` | 6-port HSIO `DTLE=0x02` + `DAT_fffa38b4` + `00:1f.2+0x98=0x205c0220` | **`6.0 Gbps` Gen3, `451 MB/s` (`2 GiB dd`), `0` resets over soak** |
| **LPC Decodes & `GPE0_EN`** | `gen1=0x3c0291`, `gen2=0x7c0a01`, `gen4=0xff29`, `gpe0=0x40042246` | Exact match in [`devicetree.cb`](../board/src/mainboard/asus/z97-a/devicetree.cb) | `ENE EC MBE0-Z97-0121` (`0x62/0x66`) & SMBus `0x14/0x24/0x44/0x5a` accessible |

---

## 3. Intentional Improvements Over Stock AMI `3503`

1. **Unlocked SPI Flash (`FLMSTR1=0xffff0000`, `BIOS_CNTL=0x09`)**: Native internal `flashrom` updates (`0x211000..0x7fffff`) and pre-OS UEFI flashing (`tools/z97a_uefi_tool/z97a_tool.efi`) work directly without an external programmer once Coreboot is installed.
2. **Native UEFI iPXE Netboot (`NETWORK_IPXE=TRUE`)**: Embeds upstream `ipxe.efi` (`HTTPS` + `IMAGE_TRUST_CMD`) inside EDK2 `UefiPayloadPkg`, supporting both onboard `I218-V` (`0x15a1`) and add-in `I226-V` (`0x125c`).
3. **True 64-bit Above-4G MMIO + Resizable BAR (up to `32 GiB`) + SR-IOV + VT-d (`DMAR`)**: 15 IOMMU groups, `/dev/kvm`, and 64-bit `QWordMemory` (`PM64`) window active on Proxmox VE 9.
4. **Full ASUS Hardware Feature Support (`cfr.c` + `mainboard.c`)**: Q-LEDs (`DRAM`/`VGA`/`BOOT_DEVICE`), SmartFan IV Q-Fan profiles + Bank 7 `TjMax`, `ErP S4/S5`, read-only `ENE EC MBE0-Z97-0121` probe, opt-in `DIGI+ ASP1252` VRM CFR menu, `TPU` hardware switch / all-core Turbo (`MSR 0x1ad`), `EPU` hardware switch / `-20 mV` undervolt (`MSR 0x150`), LPC Port `80h` + `CMOS_POST`, and on-screen `fb_console`.

---

## 4. Final Live Verification Matrix (`Coreboot v0.15` on `192.168.1.150`)

| Device / BDF | Subsystem | Live Status (`Coreboot v0.15`) |
|---|---|---|
| `00:00.0` | Haswell Host Bridge (`8086:0c00`) | Healthy (`PAVPC=0x84100017`, `TOLUD=0x84200001`, `DMAR` 15 IOMMU groups) |
| `00:01.0` | PEG10 x16 (`8086:0c01`) | Healthy (empty slot, `ASPM Disabled`, `DevSta CorrErr-`) |
| `00:02.0` | HD 4600 iGPU (`8086:0412`) | Healthy (`AOC 28E850` on `card1-HDMI-A-3` `1920x1080` `dpms=On`, `CDCLK=0x21b`) |
| `00:03.0` | Haswell HDMI Audio (`8086:0c0c`) | Healthy (`card1`) |
| `00:14.0` | Z97 xHCI USB 3.0 (`8086:8cb1`) | Healthy (`14` USB2 + `6` USB3 ports, USB HID active) |
| `00:16.0` | Intel MEI #1 (`8086:8cba`) | Healthy (`ME 9.1.25.1005`) |
| `00:19.0` | Onboard `I218-V` (`8086:15a1`) | Healthy (`eno0`, MAC `9c:5c:8e:90:7e:f2` preserved) |
| `00:1b.0` | Realtek `ALC892` (`8086:8ca0`) | Healthy (`card0`, `1043:860b`, `NID 0x11=0x90460130`) |
| `00:1c.0 -> 02:00.0` | RP4 -> `ASM1083` (`1b21:1080`) | Healthy (`2.5 GT/s x1`, `ASPM Disabled`, `DevSta CorrErr-`) |
| `00:1c.1` | RP5 `PCIEX16_3` (`8086:8c98`) | Healthy (empty, `ASPM Disabled`, AER all `-`) |
| `00:1c.2 -> 05:00.0` | RP7 -> `I226-V` (`8086:125c`) | Healthy (**`2500Mb/s` Full**, `LnkSta 5GT/s x1`, `ASPM Disabled`, `CommClk+`, **`0` AER errors**, **`0` NIC drop/CRC errors**) |
| `00:1c.3` | RP8 `PCIEX1_1` (`8086:8c9e`) | Healthy (empty, `ASPM Disabled`, AER all `-`) |
| `00:1f.0` | Z97 LPC (`8086:8cc4`) | Healthy (`NCT6791D` HWM `nct6775`, `COM1`, `ENE EC MBE0-Z97-0121`, `EPU=OFF`, `TPU=OFF`) |
| `00:1f.2` | AHCI SATA (`8086:8c82`) | Healthy (**`ata5: SATA link up 6.0 Gbps`**, **`451 MB/s`**, **`0` resets**, `0` kernel `err`/`crit`) |
| `00:1f.3` | Z97 SMBus (`8086:8ca2`) | Healthy (`0x50..0x53` SPD, `0x24`/`0x5a` ASUS telemetry) |
