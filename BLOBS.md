# ASUS Z97-A Firmware Blobs & Provenance

The chipset/board blobs committed in [`blobs/`](./blobs/) (`IFD`, Intel `ME`, `GbE`, and `data.vbt`) were extracted from a stock ASUS `Z97-A` `3503` SPI image (`8,388,608` B) and live Linux `i915` VBT capture (`6,144` B).

> **Important MAC Notice (`blobs/gbe-z97-a.bin`):** `blobs/gbe-z97-a.bin` contains an example `FREG3` GbE EEPROM with MAC address `9c:5c:8e:90:7e:f2`. If flashing another ASUS Z97-A board externally or overwriting `0x001000..0x002fff`, preserve or inject your own board's stock `FREG3` GbE region (`dd` or `ifdtool --inject-gbe`) so two machines on the same LAN do not share a MAC address.

## Verified Blob Catalog

| Blob File | Size (B) | SHA-256 | Source & Notes |
|---|---:|---|---|
| `blobs/data.vbt` | `6,144` | `ada53b3d9638c206a63fbe63f6ef48b74b86da90018e34cd30f9c544b4574f36` | Live `i915_vbt` (`$VBT HASWELL`, HD 4600 `8086:0412`: CRT/VGA, HDMI-B DVI-D, DP-C, DP-D). |
| `blobs/gbe-z97-a.bin` | `8,192` | `e3f2b6e5e994794b71b56060b69075de4a6f2b3fde17197842571c5a4d9635a7` | Live `FREG3` GbE EEPROM (`0x001000..0x002fff`). Preserves onboard Intel `I218-V` MAC `9c:5c:8e:90:7e:f2`. |
| `blobs/ifd-z97-a-stock.bin` | `4,096` | `2fe73369341060146a6e538dd03b4c588717b065f927f286d89da6013397ed9f` | Unmodified stock `FREG0` IFD (`FLMSTR1=0x0a0b0000`, Host ME read/write locked, `BIOS@0x180000`). |
| `blobs/ifd-z97-a-unlocked.bin` | `4,096` | `f0fd3c5f20f96df196aa9a10d1c6da3cedb30e279d6a476e6d47ee19bc37c72e` | `ifdtool -u` Host-unlocked IFD (`FLMSTR1=0xffff0000`, `FLMSTR2=0xffff0000`, `FLMSTR3=0x08080118`, `BIOS@0x180000`, `AltMeDisable=0`). Used by `v0.1` `stock-me`. |
| `blobs/me-z97-a-stock-9.1.25.1005.bin` | `1,560,576` | `85c8eb541b6272f2eab6200075fbf939938ae885d5cf83206293ebd752603590` | Live initialized Intel ME `9.1.25.1005` (`0x003000..0x17ffff`), `FTPR` RSA VALID. Used by `v0.1` `stock-me`. |
| `blobs/ifd-z97-a-mecleaned-compat-unlocked.bin` | `4,096` | `5349c794720de8c41ced5fdbfbe8b4eed456e9b7fb9905ed367f5036ccb4f017` | Host-unlocked IFD (`FLMSTR1=0xffff0000`) with `AltMeDisable=1` (`PCHSTRP10`) and stock `BIOS@0x180000` layout. |
| `blobs/me-z97-a-cleaned-compat.bin` | `1,560,576` | `7e4983c1b5af5d269d967d4f0798bb41567ad81ebc30196f955e0d33a0bba896` | `me_cleaner.py -S` (`AltMeDisable=1`), non-`FTPR` partitions removed, `FTPR` RSA signature VALID. |
| `blobs/ifd-z97-a-minimal-116k-unlocked.bin` | `4,096` | `e26b6c9b0f724e992cbe81aba32c51313f1b642f8f5ac7455f93d30f9ef859f8` | Shrunk & Host-unlocked IFD (`FLMSTR1=0xffff0000`, `AltMeDisable=1`, `FLREG2=0x001f0003` ME `0x3000..0x1ffff`, `FLREG1=0x07ff0020` `BIOS@0x020000`). |
| `blobs/me-z97-a-minimal-116k.bin` | `118,784` | `1df709207251346c7221a72128a5b1e15cedacbd4edf7dcbcd24b73b315bdb43` | `me_cleaner.py -r -t` truncated ME (`0x1d000` = `116 KiB`), keeps essential `ROMP` + `BUP` in relocated `FTPR`, RSA signature VALID. |

## CPU Microcode Note

- Stock AMI `3503` includes microcode for `0x40671` (rev `0x1d`), `0x306c3` (rev `0x24`), `0x306c2` (rev `0xffff0006`), and `0x306c1` (rev `0xffff0013`).
- Coreboot's `3rdparty/intel-microcode` supplies newer microcode in `cpu_microcode_blob.bin`: **`0x306c3` rev `0x28`** (installed `i7-4790K`), `0x40661` rev `0x1c`, and `0x40671` rev `0x22`.
