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

## Coreboot + EDK2 + iPXE Build (`v26.11.1` / `Coreboot v0.15` Finalized Baseline)

1. Run `RELEASE_TAG=v26.11.1 ./scripts/build.sh` (or `make -j$(nproc)` in `coreboot/`) on `192.168.1.15`:
   - Uses `configs/z97-a-8mb-stock-me.defconfig` (rewriting `blobs/` paths dynamically to `$ROOT_DIR/blobs/`) with `CONFIG_LOCALVERSION="-z97a-v0.15"`, `CONFIG_PCIEXP_ASPM=n`, `CONFIG_PCIEXP_CLK_PM=n`, `CONFIG_PCIEXP_L1_SUB_STATE=n` (via `lynxpoint/Kconfig` `!BOARD_ASUS_Z97_A`, matching ASUS manual p. 95–96 defaults and preventing PCH Flex-I/O `ata5` Gen1 COMRESET storms), `CONFIG_EDK2_ENABLE_IPXE=y`, `CONFIG_BUILD_IPXE=y`, `CONFIG_IPXE_BUILD_EFI=y`, `CONFIG_IPXE_HAS_HTTPS=y`, `CONFIG_EDK2_SERIAL_SUPPORT=y`, `CONFIG_POST_DEVICE_LPC=y`, and `CONFIG_CMOS_POST=y` (`0x70`). *(Note: `CONFIG_IPXE_SERIAL_CONSOLE` is disabled so EDK2 `SerialIo` cleanly provides `COM1`.)*
   - Apply all 3 patches in `patches/` (`0001-haswell-z97a-above4g-peg-dmi-gma.patch`, `0002-lynxpoint-z97a-dmi-sata-pcie-aspm.patch`, `0003-libgfxinit-z97a-port-limits.patch`) to upstream Coreboot (`df45ea2c49b4`) + `3rdparty/libgfxinit` before building.
   - Switch between `pcie-x1` (default, keeps `PCIEX1_2` / `I226-V` at `05:00.0`) and `m2-x2` (`M.2 Socket 3` PCIe SSD) using `python3 scripts/switch-m2-ifd.py`.
   - **Coreboot `v0.14`–`v0.15` Stock-Parity Patches** (proven against stock `3503` PEI/DXE modules + `inteltool.txt` + live Proxmox VE 9):
     - `lynxpoint/Kconfig` (`v0.15`): `PCIEXP_ASPM=n`, `PCIEXP_CLK_PM=n`, `PCIEXP_L1_SUB_STATE=n` for `BOARD_ASUS_Z97_A` (cures `ata5` resets and restores `6.0 Gbps` `451 MB/s` + zero `I226-V` AER errors).
     - `early_pcie.c`: `0x80c + i` (`[20:18]`, not `0x910 + i`) and `0xc28` shift `0` (`0x13`, not shift `18`).
     - `early_dmi.c` + `early_pch_native.c`: preserve `DMILCAP[17:12]` (`mask 0xf`), SA RC `0x71c=0x13` / `0x720=0x02000000`, `VC0=0x8000007b` / `VC1` off (`V1STS`/`DMIVC1RSTS` guarded) on `BOARD_ASUS_Z97_A`.
     - `northbridge.c` + `lpc.c`: `-6.0 dB` DMI de-emphasis (`DMILCTL2[6]=0`), SA & PCH DMI ASPM OFF (`DMILCTL[1:0]=0`, `RCBA 0x21a8[1:0]=0`), skip Mobile `enable_clock_gating()` on Desktop `BOARD_ASUS_Z97_A`.
     - `gma.c`: program `CDCLK_FREQ(0x46200) = 0x21b` (`539` MHz) on `BOARD_ASUS_Z97_A`.

## Flashing Rules & SPI Flash Endurance

1. **SPI Flash Endurance Rule (RAM-First Testing — CRITICAL)**:
   - **NEVER flash speculative test builds to SPI.** Prove register and hardware fixes in RAM first using `z97a_tool.efi` over `iPXE>` HTTP (`diag`, `satatest2`, `satatest3`, `hdmitest4`, `v014audit`), then consolidate verified fixes into ONE delta flash (`0x211000..0x7fffff`).
2. **Always start `COM1` serial capture first** on `192.168.1.90` before powering on/rebooting after any flash (see `serial-communication`).
3. **Preferred: Zero-Chip-Swap Direct Pre-OS / Internal Flashing (`192.168.1.150`)**:
   - Once an unlocked Coreboot image (`FLMSTR1=0xffff0000`, `BLE=0`, `SMM_BWP=0`, `PR0..PR4=0`) is on the chip, **do NOT unplug the SPI chip** for Coreboot BIOS updates. Even with `FLOCKDN=1` and without booting any OS, Intel PCH SPI Hardware Sequencing (`SPIBAR = 0xfed1f800`) works directly from UEFI via [`tools/z97a_uefi_tool/z97a_tool.c`](../../../tools/z97a_uefi_tool/z97a_tool.c), or from Linux via `flashrom -p internal -l layout.txt -i coreboot -w <rom>` (`0x00211000:0x007fffff coreboot`).
   - **FMAP Layout & Preservation Invariant (`v0.13`+)**:
     - Coreboot FMAP places `RW_MRC_CACHE` at `0x180000..0x18ffff` (`64 KiB`), `SMMSTORE` at `0x190000..0x20ffff` (`512 KiB`), `FMAP` at `0x210000..0x210fff` (`4 KiB`), and `COREBOOT` CBFS at `0x211000..0x7fffff` (`6,221,824 B`).
     - Restrict delta-flashing to `FMAP + COREBOOT` (`0x210000..0x7fffff`, `1,520` sectors = `6,225,920 B`) so `IFD`, `GbE` (`MAC 9c:5c:8e:90:7e:f2`), stock Intel `ME`, Winbond `QE=1`, `RW_MRC_CACHE`, and `SMMSTORE` (`Boot0005 proxmox` EFI NVRAM) all survive every flash intact. Every flash verifies full readback `SHA-256` before cold-resetting (`CF9 = 0x0e`). *(Note: `v0.12` flashed `0x180000..0x7fffff` `6.5 MiB` once, which wiped `SMMSTORE`.)*
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
   - `me_cleaner.py -c` confirms **`FTPR` RSA signature VALID** (ME `9.1.25.1005`).
   - `cbfstool build/coreboot.rom print` confirms microcode (`0x306c3` rev `0x28`), `romstage`, `ramstage`, `dsdt.aml`, `vbt.bin`, `payload` (with iPXE), `SMMSTORE`, and `RW_MRC_CACHE`.
