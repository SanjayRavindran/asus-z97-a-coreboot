---
name: bios-image-inspection
description: >-
  Perform read-only inspection, IFD/ME/GbE/BIOS region auditing, and evidence
  extraction from ASUS Z97-A AMI Aptio BIOS dumps, capsules, boardview (.FZ),
  and captured OS hardware tables (UEFITool, ifdtool, me_cleaner, UBU/MMTool,
  REA MCP). Use this skill when analyzing Z97-A firmware images, building a
  composite stock recovery ROM, or validating Coreboot Gates A-E.
---

# Safe BIOS Image Inspection & Evidence-Gated Porting (ASUS Z97-A)

Follow strict read-only provenance and verification gates before treating any vendor firmware dump or board clue as implementation truth.

## Non-Negotiable Safeguards

1. **Preserve all vendor/stock dumps unchanged**: Work strictly on copies (`/tmp/z97a-inspect/` on `192.168.1.15`); keep `~/coreboot-work/z97-a/inputs/*` `chmod 444`. Never run `me_cleaner.py` without `-O <output>` on an input file (it edits in-place by default).
2. **Never confuse partial/locked internal reads with full-chip backups**:
   - On stock ASUS Z97-A (`FLMSTR1=0x0a0b0000`), Host CPU read/write access to `FREG2` (Intel ME `0x003000–0x17ffff`) is **disabled**.
   - Internal `flashrom -p internal --ifd -i fd -i gbe -i bios -r` produces an 8 MiB file whose `FREG2` (`1,560,576` B) is **all `0x00` bytes** (`me_cleaner -c`: `The ME/TXE region is corrupted or missing`). Never flash that raw internal dump onto a blank/spare SPI chip as-is.
3. **Never use vendor UEFI binaries as Coreboot code**: Extracted DXE/PEI modules, DSDT, or VBT tables are hardware-clue evidence only. Use Coreboot's native `haswell` / `lynxpoint` / `nct6791d` drivers.
4. **MMTool A4/A5 safety (`UBU_v1.79.17\mmtool_a4.exe`, `mmtool_a5.exe`)**: Use read-only inspection/extraction on analysis copies only; cross-check any extracted module/offset against `UEFITool` / `ifdtool` or `@rea` MCP on `192.168.1.15` before citing it.

## Provenance Table (Verified 2026-11-26)

| Artifact | Size (B) | SHA-256 | Notes |
|---|---:|---|---|
| `stock-capture/z97-a-original-chip.bin` | `8,388,608` | `a1fd7f2974dff0614ad9f7f63784db4bc9cfbb62e0779ad42e76f49589e2cab1` | **True physical OEM Backup #1** (two identical CH341A reads of original socketed `W25Q64`). Live IFD (`2fe73369...`), live GbE (`e3f2b6e5...`), live initialized ME `9.1.25.1005` (`85c8eb54...`, `FTPR` RSA VALID), live BIOS/NVRAM (`494cacd3...`). |
| `stock-capture/z97-a-ifd-gbe-bios-read1.bin` | `8,388,608` | `ec3b21ae464ae980836bef457d4fc28018001cc02923d1028d72dfc195fa3e08` | Two identical internal `flashrom` reads (`--ifd -i fd -i gbe -i bios`). Has live IFD (`2fe73369...`), live GbE MAC `9c:5c:8e:90:7e:f2` (`e3f2b6e5...`), live BIOS/NVRAM (`494cacd3...`), and zeroed locked ME (`49b92ecd...`). |
| `Z97-A-ASUS-3503/Z97-A-ASUS-3503.CAP` | `8,390,656` | `2c78aeaaf9fc0494115e2559925bd6425996e354d8c78621537b6d161d08f017` | Official ASUS 3503 capsule (2048-byte CAP header + 8 MiB SPI image). |
| Stripped `Z97-A-ASUS-3503` (`dd bs=2048 skip=1`) | `8,388,608` | `cb626728fd13a8ef5ff8f843f21388594287b3e11398bfd04039302bfb504a0c` | Stock IFD (`2fe73369...`), valid ME `9.1.25.1005` (`0daade80...`, `FTPR` RSA VALID), placeholder GbE MAC `88:88:88:88:87:88` (`88ae0236...`), stock BIOS (`83f5567f...`). |
| `Z97-A-ASUS-2801/Z97-A-ASUS-2801.CAP` | `8,390,656` | `7445ad6c5eefab0daa2168837d5fad2431de0db015dee06e707d9ae4e31a2d55` | Official ASUS 2801 capsule (stripped SHA-256 `dd254369a8a6ee4be85b8e51c4eba1e7f45e56669cc106ea00d13e55d940195c`, same IFD/ME `9.1.25.1005`). |
| `mod_Z97A_ReBar.ROM` | `8,388,608` | `df80ced1187cdbafdec6db6dfcc80d77cd9c4ea9801a3600e75a5e9687d88e03` | Unlocked IFD (`FLMSTR1=0xffff0000`, `f5caa07b...`), valid ME `9.1.40.1000` (`92e378ff...`, `FTPR` RSA VALID), placeholder GbE MAC `88:88:88:88:87:88`. |
| `stock-capture/i915_vbt.bin` (`blobs/data.vbt`) | `6,144` | `ada53b3d9638c206a63fbe63f6ef48b74b86da90018e34cd30f9c544b4574f36` | Captured live from `/sys/kernel/debug/dri/*/i915_vbt` (Intel HD 4600 `8086:0412`). BDB `189`: `CRT_GMBUS=2` (VGA `PCH_DAC`), `DDI-B` DVI-D/HDMI, `DDI-C` DP+HDMI (`AUX 0x20`), `DDI-D` HDMI, `SSC=0`. |
| `blobs/ifd-z97-a-minimal-116k-unlocked.bin` | `4,096` | `e26b6c9b0f724e992cbe81aba32c51313f1b642f8f5ac7455f93d30f9ef859f8` | `without-me` minimal-ME IFD (`FLREG2=0x001f0003`, `FLREG1=0x07ff0020`, `AltMeDisable=1`, keeps identical `COREBOOT@0x211000` FMAP alignment). |
| `blobs/me-z97-a-minimal-116k.bin` | `118,784` | `1df709207251346c7221a72128a5b1e15cedacbd4edf7dcbcd24b73b315bdb43` | `me_cleaner.py -S -r -t` truncated `116 KiB` ME (`ROMP+BUP` only, `FTPR` RSA VALID — avoids 30-min PCH timer). |

## Evidence-Gated Coreboot Porting (Gates A–E)

- **Gate A (Baseline & Recovery)**:
  - Board: ASUSTeK `Z97-A` `Rev 1.xx`, CPU `i7-4790K` (`CPUID 0x306c3`), PCH `Z97` (`8086:8cc4`), Super I/O `NCT6791D` (`0xc803` at `0x2e`), Audio `ALC892` (`0x10ec0892`, subsystem `0x1043860b`), Onboard LAN `I218-V` (`00:19.0`, `8086:15a1`, MAC `9c:5c:8e:90:7e:f2`), Add-in LAN `I226-V` (`05:00.0`, `8086:125c`).
  - Flash layout: 8 MiB (`FREG0` IFD `0x0..0xfff`, `FREG3` GbE `0x1000..0x2fff`, `FREG2` ME `0x3000..0x17ffff`, `FREG1` BIOS `0x180000..0x7fffff`); VSCC supports GigaDevice (`0xc8 4017`) and Winbond W25Q64 (`0xef 4017`).
  - Recovery: External CH341A (`1a86:5512`) on `192.168.1.90` + spare 8 MiB DIP-8 SPI chip (socketed ROM on Z97-A).
- **Gate B (Upstream Silicon Support)**: Inspect `src/mainboard/asus/` (`maximus_vii_*`, `z87-k`, etc.) and `src/mainboard/asrock/z97*` in Coreboot on `192.168.1.15` for `haswell` / `lynxpoint` / `nct6791d` patterns.
- **Gate C (Measured Board Facts)**: Cross-reference `stock-capture/inteltool.txt` (PCH GPIOs `GPIOBASE=0x1c00`), `stock-capture/superiotool.txt` (`NCT6791D` LDNs `0x01..0x16`), `stock-capture/acpi/` (`DSDT`/`SSDT` ASL), `stock-capture/hda/`, and `ASUS Z97-A REV 1.03B - Boardview (.FZ).FZ` (`openboardview.exe`).
- **Gate D (Minimal Native Port)**: Implement smallest native board target without guessing GPIOs, straps, or verb tables.
- **Gate E (Structural & Boot Validation)**: Verify `ifdtool -d`, `me_cleaner.py -c`, `cbfstool print`, and capture full `COM1` serial bootlog on `192.168.1.90`.

## Stock Capture & Inspection References

- Full stock hardware dump directory: [`stock-capture/`](../../../stock-capture/)
- Region/IFD/ME comparison report: [`stock-capture/rom-ifd-analysis.txt`](../../../stock-capture/rom-ifd-analysis.txt)
- Reference project lessons: [`../h61m-s1-plus-coreboot/`](../../../../h61m-s1-plus-coreboot/)
