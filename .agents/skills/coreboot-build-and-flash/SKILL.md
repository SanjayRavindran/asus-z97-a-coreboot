---
name: coreboot-build-and-flash
description: >-
  Build, assemble, validate, and safely flash Coreboot or stock recovery SPI
  images for the ASUS Z97-A using build host 192.168.1.15, external CH341A on
  192.168.1.90, and internal flashrom on 192.168.1.150. Use this skill when
  creating recovery SPI backups, unlocking IFD/ME regions, building Coreboot +
  EDK2 for Z97-A, or flashing SPI chips externally/internally.
---

# ASUS Z97-A Coreboot Build & Flash Workflow

## Hosts & Tools

- **Build & Inspection Host**: `192.168.1.15` (`ubuntu-gpu-docker`, SSH user `zero`, Ubuntu `x86_64`)
  - Immutable inputs: `/home/zero/coreboot-work/z97-a/inputs/` (`chmod 444`)
  - Reference tree & utilities: `/home/zero/coreboot-work/h61m-s1-plus/util/{ifdtool,cbfstool,me_cleaner,inteltool,superiotool}`
- **External SPI Programmer, Serial, Netboot & HDMI Host**: `192.168.1.90` (`zeb-h61`, SSH user `zero`, Ubuntu `x86_64`)
  - Hardware: QinHeng `CH341` in EPP/MEM/I2C/SPI mode (`USB ID 1a86:5512`), `CH340` UART (`/dev/ttyUSB0`), MacroSilicon `MS2109` USB HDMI capture (`534d:2109`, `/dev/video0`)
  - Software: `flashrom` (`ch341a_spi` programmer) + `/etc/udev/rules.d/99-ch341a.rules`, HTTP netboot server (`~/z97a-netboot/` on `:8080`), `~/z97a_hdmi_grab.py`, `~/z97a_serial_flash.py`
- **Target Board**: `192.168.1.150` (`pve`, ASUS `Z97-A`, SSH user `root`)
  - Socketed 8 MiB SPI ROM (`FLMSTR1=0x0a0b0000` on stock AMI `3503`)

## Critical Composite Stock-Backup Rule (Internal Dump Lacks ME!)

Because stock `Z97-A` `FLMSTR1=0x0a0b0000` locks Host reads of `FREG2` (`0x003000–0x17ffff`), an internal `flashrom` read (`z97-a-ifd-gbe-bios-read1.bin`) contains **all `0x00` bytes in `FREG2`**.

- **NEVER flash `z97-a-ifd-gbe-bios-read1.bin` directly** onto a spare SPI chip.
- Before flashing a spare 8 MiB SPI chip externally on `192.168.1.90`:
  1. **Always back up whatever is currently on the spare chip first** (`flashrom -p ch341a_spi -r spare-before-read1.bin` and verify a second read).
  2. **Either** read the motherboard's original socketed chip directly in the CH341A (to get the true byte-for-byte full-chip backup including the exact stock `FREG2` ME), **or** build a verified composite stock recovery image from:
     - `0x000000..0x000fff` (`4 KiB` IFD): from `z97-a-ifd-gbe-bios-read1.bin` (`2fe73369...`)
     - `0x001000..0x002fff` (`8 KiB` GbE): from `z97-a-ifd-gbe-bios-read1.bin` (`e3f2b6e5...`, preserves onboard `I218-V` MAC `9c:5c:8e:90:7e:f2`)
     - `0x003000..0x17ffff` (`1,560,576 B` ME): from stripped `Z97-A-ASUS-3503.CAP` (`0daade80...`, valid ME `9.1.25.1005` — or confirm live `mei0` version first)
     - `0x180000..0x7fffff` (`6,815,744 B` BIOS): from `z97-a-ifd-gbe-bios-read1.bin` (`494cacd3...`, preserves Proxmox UEFI boot variables)
  3. Validate the assembled 8,388,608-byte image with `ifdtool -d` and `me_cleaner.py -c` (`Checking the FTPR RSA signature... VALID`) before flashing.

## Coreboot + EDK2 + iPXE Build (`v0.16` / Release Cadence `v26.09-r2`)

1. Run `RELEASE_TAG=v26.09-r2 ./scripts/build_release_roms.sh` (or `VARIANT_NAME=with-me ./scripts/build.sh`) on `192.168.1.15` (and automated CI runs via `.github/workflows/coreboot-release.yml` tracking quarterly `YY.MM` Coreboot tags):
   - Uses `configs/z97-a-8mb-with-me.defconfig` (stock ME `9.1.25.1005` + `FLREG1=0x07ff0180` + `FLREG2=0x017f0003`) and `configs/z97-a-8mb-without-me.defconfig` (`blobs/me-z97-a-minimal-116k.bin` `118,784` B `ROMP+BUP` + `blobs/ifd-z97-a-minimal-116k-unlocked.bin` `FLREG1=0x07ff0020` `FLREG2=0x001f0003` `AltMeDisable=1`), rewriting `blobs/` paths dynamically to `$ROOT_DIR/blobs/`.
   - Key defaults enabled across both variants: `CONFIG_ALWAYS_ALLOW_ABOVE_4G_ALLOCATION=y`, `CONFIG_ENABLE_VMX=y`, `CONFIG_PCIEXP_SUPPORT_RESIZABLE_BARS=y` (`35` bits / `32 GiB`, EDK2 `PcdPcieResizableBarSupport=TRUE`), `CONFIG_PCIEXP_HOTPLUG=y` + `PCIEXP_HOTPLUG_PREFETCH_MEM_ABOVE_4G=y` + SR-IOV/ARI, `CONFIG_POWER_STATE_ON_AFTER_FAILURE=y` (`AFTERG3_EN=0`), `CONFIG_DISABLE_ME_PCI=y` (`00:16.0` hidden via `FD2`), `CONFIG_PCIEXP_ASPM=n`/`CLK_PM=n`/`L1_SUB_STATE=n`, `CONFIG_EDK2_ENABLE_IPXE=y` (`HTTPS` + `IMAGE_TRUST_CMD`), `CONFIG_EDK2_HAVE_EFI_SHELL=y`, `CONFIG_EDK2_SERIAL_SUPPORT=y`, `CONFIG_POST_DEVICE_LPC=y`, and `CONFIG_CMOS_POST=y` (`0x70`). *(Note: `CONFIG_IPXE_SERIAL_CONSOLE` is disabled so EDK2 `SerialIo` cleanly provides `COM1`. Legacy `nvramcui`/`coreinfo` ELF payloads are omitted because `NVRAMCUI_SECONDARY_PAYLOAD` selects `USE_OPTION_TABLE`, which conflicts with `SMMSTORE` `USE_UEFI_VARIABLE_STORE`, and `mrchromebox/edk2` uses `ESC -> Coreboot Configuration` + `UEFI Shell` instead.)*
   - Apply all 3 patches in `patches/` (`0001-haswell-z97a-above4g-peg-dmi-gma.patch`, `0002-lynxpoint-z97a-dmi-sata-pcie-aspm.patch`, `0003-libgfxinit-z97a-port-limits.patch`) to upstream Coreboot (`26.09` or `df45ea2c49b4`) + `3rdparty/libgfxinit` (`7ac11bc`, fetched automatically by `scripts/build.sh` when needed) before building.
   - Switch between `pcie-x1` (default, keeps `PCIEX1_2` / `I226-V` at `05:00.0`) and `m2-x2` (`M.2 Socket 3` PCIe SSD) using `python3 scripts/switch-m2-ifd.py`.
   - **Coreboot `v0.14`–`v0.16` Board & Chipset Patches** (`patches/0001` & `patches/0002`):
     - `lynxpoint/Kconfig` (`v0.15`): `PCIEXP_ASPM=n`, `PCIEXP_CLK_PM=n`, `PCIEXP_L1_SUB_STATE=n` for `BOARD_ASUS_Z97_A` (cures `ata5` resets and restores `6.0 Gbps` `451 MB/s` + zero `I226-V` AER errors).
     - `lynxpoint/{cfr.h,me.c}` (`v0.16`): set `me_disable` CFR default to `CONFIG(DISABLE_ME_PCI)` and fall through in `intel_me_finalize()` on `BOARD_ASUS_Z97_A` even when `AltMeDisable=1` so `00:16.0` is hidden via `FD2` in both `with-me` and `without-me`.
     - `lynxpoint/{lpc.c,smihandler.c}` + `board/.../acpi/wol.asl` (`v0.16`): set `GEN_PMCON_3` bit 13 (`WOL_EN_OVRD`) & bit 0 (`AFTERG3_EN=0` -> `0x3808`), preserve `PME_B0`/`PME`/`PCI_EXP` GPEs & clear `PCIEXPWAK_DIS` on S5 entry, and expose `_PRW`/`_DSW` on `GLAN` (`0x0d`), `PEGP`/`PEG1`/`PEG2`, and `RP01..RP08` (`0x09`).
     - `early_pcie.c`, `early_dmi.c`, `early_pch_native.c`, `northbridge.c`, `gma.c`: `PM64` Above-4G window (`<4G` clamp on `IGD`), `DMAR`, `-6.0 dB` DMI de-emphasis, Desktop PCH CG (`PMCON1=0x0210`), 6-port SATA `DTLE=0x02`, and `CDCLK=0x21b` (`539` MHz).

## Flashing Rules & SPI Flash Endurance

1. **SPI Flash Endurance Rule (RAM-First Testing — CRITICAL)**:
   - **NEVER flash speculative test builds to SPI.** Prove register and hardware fixes in RAM first using `z97a_tool.efi` over `iPXE>` HTTP (`diag`, `satatest2`, `satatest3`, `hdmitest4`, `v014audit`), then consolidate verified fixes into ONE delta flash (`0x211000..0x7fffff`).
2. **Always start `COM1` serial capture first** on `192.168.1.90` before powering on/rebooting after any flash (see `serial-communication`).
3. **Preferred: Zero-Chip-Swap Direct Pre-OS / Internal Flashing (`192.168.1.150`)**:
   - Once an unlocked Coreboot image (`FLMSTR1=0xffff0000`, `BLE=0`, `SMM_BWP=0`, `PR0..PR4=0`) is on the chip, **do NOT unplug the SPI chip** for Coreboot BIOS updates. Even with `FLOCKDN=1` and without booting any OS, Intel PCH SPI Hardware Sequencing (`SPIBAR = 0xfed1f800`) works directly from UEFI via [`tools/z97a_uefi_tool/z97a_tool.c`](../../../tools/z97a_uefi_tool/z97a_tool.c), or from Linux via `flashrom -p internal -l layout.txt -i coreboot -w <rom>` (`0x00211000:0x007fffff coreboot`).
   - **FMAP Layout & Preservation Invariant (`v0.13`+)**:
     - Both `with-me` (`BIOS@0x180000`) and `without-me` (`minimal-116k`, `FLREG1` `0x020000..0x7fffff`) share identical FMAP offsets (`RW_MRC_CACHE` `0x180000..0x18ffff`, `SMMSTORE` `0x190000..0x20ffff`, `FMAP` `0x210000..0x210fff`, `COREBOOT` `0x211000..0x7fffff`).
     - Restrict routine delta-flashing to `COREBOOT` (`0x211000..0x7fffff`) so `IFD`, `GbE`, `ME`, `QE=1`, `RW_MRC_CACHE`, and `SMMSTORE` survive intact. **Upgrading CFR defaults note:** because `SMMSTORE` caches previously initialized CFR values (`me_disable`, `power_on_after_fail`, `wol_enable`), either update them inside `ESC -> Coreboot Configuration` or also write `-i SMMSTORE` (`0x190000..0x20ffff`, blank `0xff` in release builds; EDK2 auto-boots via `\EFI\BOOT\BOOTx64.EFI`) to reset CFR & NVRAM to the new release's defaults.
     - **NEVER flash `IFD` or `ME` internally (`flashrom -p internal`) while Intel ME is active.** Switching between `with-me` and `without-me` (`minimal-116k`) MUST be done externally with CH341A (`bash scripts/flash-ch341a.sh <bin>` on `192.168.1.90`).
   - **Build the Pre-OS EFI tools on `192.168.1.15`** (no Makefile; always use `build_efi.sh`):
     - `cd /home/zero/coreboot-work/z97-a/tools/z97a_uefi_tool && bash ./build_efi.sh ../outputs/<rom>.rom <tag>` produces:
       - `z97a_tool.efi` (`~72 KiB` v2.7): pre-OS hardware & Gen3 SATA stress tester (`diag`), RAM isolation/audit subcommands (`satatest2`, `satatest3`, `hdmitest4`, `v014audit`), and raw COM1 delta flash server (`serflash`).
       - `z97a_flash_<tag>.efi`: self-contained delta flasher (`6,232,576 B`) with embedded `0x210000..0x7fffff` Coreboot slice & SHA-256 that updates only changed `4 KiB` sectors, verifies readback SHA-256, and cold-resets into the new firmware.
   - **EDK2 `BDSM` Unmapped Memory Pitfall**: Never dereference IGD stolen RAM (`BDSM`, e.g. `0x80200000`) directly from UEFI tools — EDK2 page tables do not map `E820_RESERVED` memory and dereferencing it causes a CPU triple-fault reset. Access the framebuffer only via mapped `GMADR` (`0xe0000000`).
   - **Flash over iPXE HTTP (fast, ~5s)**:
     - Serve `~/z97a-netboot/` on `192.168.1.90:8080` (`python3 -m http.server 8080`).
     - On Z97-A, `net0` (`I218-V`) has no cable; `net1` (`I226-V`, MAC `02:76:c6:00:f7:7d`) receives DHCP automatically. In EDK2/iPXE over paced COM1: `ifopen net1 && dhcp net1 && chain http://192.168.1.90:8080/z97a_flash_<tag>.efi`.
   - **Flash/Diagnose over COM1 Serial (`scripts/z97a_serial_flash.py`)**:
     - Run `z97a_tool.efi serflash` (from `FS0:\EFI\tools\z97a_tool.efi serflash` in UEFI `Shell>` or via `chain http://192.168.1.90:8080/z97a_tool.efi serflash`), then on `192.168.1.90`:
       - `python3 ~/z97a_serial_flash.py info` / `diag` / `flash <8mb.rom> --reboot` / `reboot`.
   - **Pre-OS Hardware & SATA Gen3 Diagnostic (`z97a_tool.efi diag` / `v014audit`)**:
     - Verifies `PAVPC`, `TOLUD`, `IGD RP1/AUX/CDCLK`, `D31IR..D20IR`, SATA HSIO IOBP (`0xea002488..8104`), `00:1f.2`, `ABAR` (`CAP`, `PI`, `CAP2`, `0xa4`, `PxSSTS`, `PxSERR`), PCIe RPs, SPI SHA-256, full `DMIBAR`/`RCBA` dumps, and `16 MiB` UEFI `BlockIO` SATA stress tests.
4. **External SPI flash (`192.168.1.90`, CH341A)**:
   - Needed only when changing `IFD`/`GbE`/`ME` (`0x000000..0x17ffff`), recovering a non-POSTing chip, or provisioning a locked/blank chip (`FLASH_LOCKED`).
   - **Recommended clean Winbond `W25Q64FV` sequence**:
     1. Read/backup current chip if needed, then blank (`flashrom -p ch341a_spi -c "W25Q64BV/W25Q64CV/W25Q64FV" -E` or `-w` which erases/writes/verifies).
     2. Write image and let `flashrom` complete full `-v` verification (`flashrom -p ch341a_spi -c "W25Q64BV/W25Q64CV/W25Q64FV" -w <rom> -v <rom>`).
     3. **Winbond `W25Q64` Quad Enable (`QE = 1`) requirement (mandatory on Z97-A!):** The original ASUS Z97-A `W25Q64FV` runs with non-volatile **`SR1=0x00, SR2=0x02` (`QE=1`)**. Spare chips or full-chip erases can leave `SR2=0x00` (`QE=0`), causing a silent non-POST even when flash contents verify 100%. Always enforce non-volatile `SR1=0x00, SR2=0x02` (`WREN 0x06` -> `WRSR 0x01 0x00 0x02`, wait `WIP=0`) and read back `RDSR1 (0x05)` + `RDSR2 (0x35)` to confirm `0x00` / `0x02` before unplugging the chip!
   - Use a **spare 8 MiB DIP-8 SPI chip** for Coreboot bring-up so the original ASUS AMI chip stays untouched as instant physical recovery.
5. **Internal SPI flash (`192.168.1.150`, Proxmox)**:
   - On stock locked `FLMSTR1=0x0a0b0000` (`BIOS_CNTL=0x0b` `BLE=1`), `FREG0`/`FREG2` are locked and `BIOS_CNTL` prevents full-chip writes; once an unlocked Coreboot IFD is running, `flashrom -p internal -w` can also update from Linux if booted.
6. **Build & Structural Validation Checklist (`192.168.1.15`)**:
   - Exact image size `8,388,608` bytes (`8 MiB`), `BIOS` region starts on a 64 KiB boundary (`0x180000`).
   - `ifdtool -d` confirms unlocked `FLMSTR1=0xffff0000` and expected `PCHSTRP9`/`PCHSTRP18`.
   - `me_cleaner.py -c` confirms **`FTPR` RSA signature VALID** (ME `9.1.25.1005` on `with-me` and on the `118,784 B` `0x3000..0x1ffff` ME slice of `without-me`).
   - `cbfstool <rom> print` confirms microcode (`0x306c3` rev `0x28`), `romstage`, `ramstage`, `dsdt.aml`, `vbt.bin` (`data.vbt`), `payload` (EDK2 + UEFI Shell + iPXE), and `.config` contains `ALWAYS_ALLOW_ABOVE_4G_ALLOCATION=y`, `ENABLE_VMX=y`, `POWER_STATE_ON_AFTER_FAILURE=y`, `DISABLE_ME_PCI=y`, `PCIEXP_SUPPORT_RESIZABLE_BARS=y`, `PCIEXP_HOTPLUG=y`, and `!PCIEXP_ASPM`.
