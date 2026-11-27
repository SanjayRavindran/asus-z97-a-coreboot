# ASUS Z97-A DIGI+ VRM (`ASP1252`), ENE EC (`MBE0-Z97-0121`), TPU, EPU & LPC Port `80h` Reference

**Source Image:** [`z97-a-original-chip.bin`](../stock-capture/z97-a-original-chip.bin) (`8,388,608` B, SHA-256 `a1fd7f2974dff0614ad9f7f63784db4bc9cfbb62e0779ad42e76f49589e2cab1`, AMI Aptio IV `3503`)  
**Analyzed Modules:** `EnePEI`, `EneDXE`, `OCMR_PEI`, `EpuHwModePei`, `EpuHwModeDxe`, `OC_Tuner`, `OC_Wizard`, `PEbiosinterface`

---

## 1. ENE Embedded Controller (`MBE0-Z97-0121`) Protocol

ASUS routes DIGI+ VRM configuration, telemetry, and TPU state tracking through the proprietary **ENE Embedded Controller** (`MBE0-Z97-0121`) over LPC I/O ports `0x62` (Data) and `0x66` (Command/Status), alongside PCH decode `gen2_dec = 0x007c0a01` (`0x0a00..0x0a7f`).

### A. Low-Level Port `0x62` / `0x66` Handshake (`EnePEI` `0xfff7f4cf..0xfff7f655`)
* **Input Buffer Empty (IBF=0) Wait:** Poll `inb(0x66)` up to `0x4000` times until `(status & 0x06) == 0`.
* **Output Buffer Full (OBF=1) Wait:** Poll `inb(0x66)` up to `0x4000` times until `(status & 0x01) != 0`.
* **EC Commands (`outb(cmd, 0x66)`):**
  * **`0x80` (EC RAM Read):** wait IBF -> write `0x80` to `0x66` -> wait IBF -> write `reg` to `0x62` -> wait IBF -> wait OBF -> read `inb(0x62)`.
  * **`0x81` (EC RAM Write):** wait IBF -> write `0x81` to `0x66` -> wait IBF -> write `reg` to `0x62` -> wait IBF -> write `val` to `0x62` -> wait IBF.
  * **`0xa1` (EC Version String):** wait IBF -> write `0xa1` to `0x66` -> wait IBF -> loop up to 16 times (`wait OBF -> inb(0x62)` until `0x00`) -> returns `"MBE0-Z97-0121"`.
  * **`0xba` (DIGI+ VRM Commit / Latch):** wait IBF -> write `0xba` to `0x66` -> wait IBF -> write `0xe3` to `0x62` -> wait IBF -> write `0x03` to `0x62` -> wait IBF (`OCMR_PEI` `0xfff66037..0xfff66055`).
* **Page Selector (`EC[0xff]`):** Selects EC register page (`0x00`, `0x01`, `0x02`).
* **Readiness Signature Check (`EnePEI` `0xfff7f879`):** Set `EC[0xff] = 0x01`, read `EC[0xf8]`; valid hardware returns **`0xec`**. `EnePEI` init then restores `EC[0xff] = 0x00` and sets Page 0 `0xde = 0x04`, `0xc0 = 0x00`.

### B. EC Bank 0 SMBus Backdoor (`EnePEI` `0xfff7f681..0xfff7f878`)
In **Page `0x00`**, `EC[0x00..0x24]` implements an ACPI EC SMBus 2.0 Master Bridge:
* `EC[0x00]`: Command / trigger (`0x06`=Write Byte, `0x07`=Read Byte, `0x08`=Write Word, `0x09`=Read Word, `0x0a`=Write Block, `0x0b`=Read Block).
* `EC[0x01]`: Status (poll `bit 7` `0x80` set for completion; `(status & 0x1f) == 0` for OK).
* `EC[0x02]`: 8-bit SMBus Slave Address.
* `EC[0x03]`: Command / Register Offset.
* `EC[0x04..0x23]`: Data Buffer (up to 32 bytes).
* `EC[0x24]`: Block byte count.

---

## 2. ASUS DIGI+ `ASP1252` VRM Complete Map & Algorithms

Extracted from **`OCMR_PEI`** (`0xfff65e00..0xfff66240`, lookup tables `0xfff6ab20..0xfff6ab64`) and **`PEbiosinterface`** (`0x180008dcc..0x1800095d3`). All primary DIGI+ VRM controls live on **ENE EC Page `0x00`**:

### A. DIGI+ VRM Control Registers (ENE EC Page `0x00`)

| Feature | EC Reg(s) | Exact Register Encodings |
| :--- | :--- | :--- |
| **CPU Load-Line Calibration (LLC)** | `0xf1` | Table `0xfff6ab20`: `Level 1 = 0xff`, `Level 2 (Auto default) = 0xdd`, `Level 3 = 0xcc`, `Level 4 = 0xaa`, `Level 5 = 0x88`, `Level 6 = 0x66`, `Level 7 = 0x44`, `Level 8 = 0x22`, `Level 9 = 0x00` |
| **CPU Current Capability** | `0xf2` | Table `0xfff6ab2c`: `100% (Auto default) = 0x08`, `110% = 0x09`, `120% = 0x0a`, `130% = 0x0b`, `140% = 0x0c` |
| **CPU VRM Switching Frequency** | `0xd8` | Table `0xfff6ab34`: `250 kHz = 0x22`, `300 kHz (Auto default) = 0x33`, `350 kHz = 0x44`, `400 kHz = 0x55`, `450 kHz = 0x66`, `500 kHz = 0x77`. Manual formula (`0xfff65ec2`): `idx = ((freq_khz + freq_khz/10) - 250) / 50`. Auto + **Spread Spectrum** = `0xff`. Auto + **Active Frequency Mode** = `0xfe` (falls back to `0x33` if Phase = `Extreme`). |
| **CPU Power Duty Control** | `0xda..0xdd` | Table `0xfff6ab3c`: `T.Probe (Thermal, default) = {0x26, 0x26, 0x00, 0x00}`, `Extreme (Current) = {0x44, 0x44, 0x00, 0x00}` |
| **CPU Power Phase Control** | `0x8c..0x8f` | Table `0xfff6ab44` (presets) & `0xfff6ab54` (manual):<br>- `Standard (Auto default)`: `{0x22, 0x22, 0x11, 0xe0}`<br>- `Optimized`: `{0x44, 0x33, 0x19, 0xe4}`<br>- `Extreme`: `{0x44, 0x33, 0x19, 0x00}`<br>- `Manual Ultra Fast`: `{0x02, 0x86, 0x6a, 0x00}`<br>- `Manual Fast`: `{0x02, 0x97, 0x7b, 0x00}`<br>- `Manual Medium`: `{0x02, 0xa8, 0x8c, 0x00}`<br>- `Manual Regular`: `{0x02, 0xb9, 0x9d, 0x00}` |

### B. Commit & Error Check Sequence (`0xfff66037..0xfff6609b`)
1. Select `EC[0xff] = 0x00`.
2. Write `0xf1`, `0xf2`, `0xd8`, `0xda..0xdd`, `0x8c..0x8f`.
3. Issue EC commit: command `0xba`, data byte `0xe3`, data byte `0x03`.
4. Read `EC[0xd3]`; bit 5 (`0x20`) indicates a VRM configuration/handshake fault (stock AMI outputs POST `0x77` and halts).

### C. Live Read-Only VRM Telemetry (`PEbiosinterface` `0x180008dcc..0x180008f60`, Page `0x00`)
* **`VCPU Value`:** `EC[0x5b]` / `EC[0x5c]` (`VCPU`, raw SVID/ADC bytes)
* **`ICPU Value`:** `EC[0x5f]` / `EC[0x60]` (`ICPU`, raw VRM current bytes)
* **`PCPU Value`:** `EC[0x83]` / `EC[0x84]` (`PCPU`, raw VRM power bytes)

### D. Secondary Analog Rail DACs & `OCMR_PEI` PPI (`631fc7e6-6b40-4698-9314-14aec537c90b`)
Documented for completeness (**Coreboot leaves all analog voltage DACs at hardware defaults** unless user explicitly adjusts CPU via Intel OC Mailbox):
* **`PPI[+0x00]` (`0xfff64ea0`):** EC Page `0x02`, regs `0x90`/`0x91`, `raw = ((mv_arg - 1) * 32 + 17) / 34`, default `0xa9`.
* **`PPI[+0x04]` (`0xfff64f8c`):** EC Page `0x02`, regs `0x92`/`0x93`, same formula, default `0xa9`.
* **`PPI[+0x08]` (`0xfff65078`):** EC Page `0x01`, regs `0x3e`/`0x3f`, `(arg * 32 + 17) / 34`, default `0xa9`, polls `Page 0 EC[0xd0] & 0x10 == 0` + toggles `NCT6791D LDN 0x09 CR 0x30/0xf0/0xf1`.
* **`PPI[+0x0c]` (`0xfff65298`):** EPU VRM trim: `Page 1 EC[0xc0]=0x01, EC[0xc1]=0x0e`, `Page 0 EC[0xe8]=0x01, EC[0xe9]=val, EC[0xea]=val` (or clears), then checks `EC[0xd3]`.
* **`PPI[+0x10]` (`0xfff663bd`):** PCH SMBus byte write to **`0x28` (7-bit `0x14`)**, reg `0x03` (`arg==0 -> 0x02`, `1..29 -> max(0x01, 0x1e - arg - 2)`, `>=30 -> arg - 0x1c`).
* **`PPI[+0x14]` (`0xfff655b9`):** `Page 1 EC[0x4c] = arg + 0x5f`, `EC[0x4d] = 0` (default `arg=0x19`), polls `EC[0xd0] & 0x10 == 0`.
* **`PPI[+0x18]` (`0xfff656cf`):** `Page 1 EC[0x4a] = arg + 0x37`, `EC[0x4b] = 0` (default `arg=0x1d`), polls `EC[0xd0] & 0x10 == 0`.
* **`PPI[+0x1c]` (`0xfff657e3`):** `Page 1 EC[0x48] = arg + 0x2f`, `EC[0x49] = 0` (default `arg=0x0d`), polls `EC[0xd0] & 0x10 == 0`.
* **`PPI[+0x20]` (`0xfff6632b`):** Configures Intel CPU Platform Policy SVID negative voltage offset (`-mV`).

---

## 3. ASUS `EPU` Controller & Algorithm (`EpuHwModePei`, `EpuHwModeDxe`)

1. **Physical EPU Slide Switch (`0xffd7b1b8`):**
   * Wired to **`NCT6791D LDN 0x07 GP72`**: clear `CR 0x2f` bit 2 (`~0x04`), set `CR 0x30 |= 0x02`, read `CR 0xe1` bit 2 -> `enabled = ((~(val >> 2)) & 1)`.
2. **85-Entry Per-CPU Lookup Table (`0xffd7bfb0`, 20 bytes/entry):**
   * Entry format: `{ uint32 cpuid; char model[5]; char base_ghz[5]; uint8 flag14; uint8 param15; uint8 param16; uint8 pad[3]; }`.
   * Exact **`i7-4790K`** entry (`[83]` at `0xffd7c62c`): `CPUID = 0x000306c3`, `"4790"`, `"4.00"`, `flag14 = 0`, `param15 = 0x02`, `param16 = 0x04`.
3. **Exact `i7-4790K` Power-Saving Actions (`0xffd7b377..0xffd7b4aa`):**
   * Look up VRM efficiency trim `0xffd7c668[param15=2] = 0xa9`, calls `OCMR_PEI PPI[+0x0c](0xa9 - 0x4e = 0x5b)`.
   * Look up Core undervolt `0xffd7c66c[param16=4] = 0x14` (**`-20 mV`**), calls `OCMR_PEI PPI[+0x20](20)` (applied in Coreboot via Intel OC Mailbox `MSR 0x150` `OC_PLANE_CORE` when `epu_mode` is active and manual `oc_undervolt_apply` is off).
4. **DXE Guard (`EpuHwModeDxe` `0x1800004df..0x180000544`):**
   * Reads `0x1247` via DXE protocol `9851740c-22e0-440d-9090-ef2d71c251c9`; only asserts EPU DXE state when `0x1247 == 0` (no active TPU watchdog/OC lock).

---

## 4. ASUS `TPU` Controller & Auto-OC Algorithm (`OC_Tuner`, `OC_Wizard`, `OCMR_PEI`)

1. **Physical 3-Position TPU Slide Switch (`OC_Tuner` `0x18000044c..0x18000048b`):**
   * Reads **`NCT6791D LDN 0x09` `CR 0xe1` bit 6 (`GP26`)** and **`CR 0xf1` bit 1 (`GP41`)**:
     * `GP26 == 0` -> **`TPU_I` (`1`)**: Ratio-only Auto-OC (100 MHz BCLK)
     * `GP26 == 1` & `GP41 == 0` -> **`TPU_II` (`2`)**: Ratio + BCLK Auto-OC via Intel ICC
     * `GP26 == 1` & `GP41 == 1` -> **`OFF` (`0`)**: Stock behavior (verified on board)
2. **State & Watchdog Protocol Registers (`OC_Tuner` + `OCMR_PEI`):**
   * Protocol `9851740c-22e0-440d-9090-ef2d71c251c9` manages `0x1247` (OC watchdog / retry flag), `0x125b`, `0x125c`, `0x125d`, `0x125f` (TPU HW enable), `0x1270`, `0x4241`, `0x8288` (stored Turbo Ratio).
   * `OC_Tuner` writes **Intel `MSR_TURBO_RATIO_LIMIT (0x1ad)`** to synchronize core turbo multipliers, and programs Intel BCLK/clock straps via `IccOverClocking`.
3. **`OCMR_PEI` Intel OC Mailbox (`MSR 0x150`) & Auto-Vcore Curve (`0xfff668a0`):**
   * Uses `MSR 0x150` mailbox commands `0x80000002`, `0x80000003`, `0x80000004`, `0x80000007`, `0x80000010`, `0x80000012` for Core/Cache/Ring/Uncore ratio & SVID voltage/offset limits.
   * Built-in `4790K` auto-OC curve at `0xfff668a0` scales Core Voltage (`1000 mV` at `3590 MHz` up to `1250 mV` / `+275 mV` offset at `4490+ MHz`) when ASUS Auto-Rule OC is active.
   * **Coreboot Safe Exposure:** Exposes **`cpu_turbo_ratio_all_core`** in `cfr.c` (`0` = stock CPU default; `32..48` writes all-core `MSR_TURBO_RATIO_LIMIT (0x1ad)` if `MSR_PLATFORM_INFO` bit 28 `Programmable Turbo Ratio Limit` is set). BCLK / auto-overvolting remain disabled unless configured explicitly via `oc_mailbox`.

---

## 5. LPC / Port `80h` Routing & `CMOS_POST`

* **Hardware Routing (`RCBA + 0x3410` `GCS` bit 2 `RPR`):**
  * Stock AMI `3503` had `GCS = 0x00000c64` (bit 2 = `1` -> Port `80h` routed to **PCI**).
  * Coreboot `src/southbridge/intel/lynxpoint/bootblock.c` explicitly clears bit 2 (`RCBA32(GCS) &= ~0x4`), routing all `outb(val, 0x80)` writes onto the **LPC bus** (visible on the Z97-A `TPM` 14-1 pin LPC header).
* **Coreboot Configuration (`Kconfig` + `z97-a-8mb-stock-me.defconfig`):**
  * `select DEFAULT_POST_ON_LPC` -> `CONFIG_POST_DEVICE_LPC=y`
  * `CONFIG_POST_IO=y`, `CONFIG_POST_IO_PORT=0x80`, `CONFIG_CONSOLE_POST=y`
  * `CONFIG_CMOS_POST=y`, `CONFIG_CMOS_POST_OFFSET=0x70` (stores alternating Bank 0 / Bank 1 POST codes in upper CMOS so `cmos_post_previous_boot()` reports the last POST code of the previous boot on serial/screen diagnostics).
* **`NCT6791D LDN 0x14` (`Port 80 UART`):** Left **off** in `devicetree.cb` (matches stock `CR 0x2b = 0x00`, avoiding any Super I/O pin-mux conflicts while LPC Port `80h` handles hardware POST card output).
