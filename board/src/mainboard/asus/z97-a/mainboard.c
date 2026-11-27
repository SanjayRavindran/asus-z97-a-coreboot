/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/io.h>
#include <bootstate.h>
#include <console/console.h>
#include <cpu/intel/haswell/haswell.h>
#include <cpu/x86/msr.h>
#include <delay.h>
#include <drivers/intel/oc_mailbox/oc_mailbox.h>
#include <option.h>
#include <post.h>
#include <southbridge/intel/lynxpoint/pch.h>
#include <stdbool.h>
#include <stdint.h>

#define SIO_INDEX		0x2e
#define SIO_DATA		0x2f
#define NCT6791D_HWM_BASE	0x0290
#define HWM_INDEX		(NCT6791D_HWM_BASE + 5)
#define HWM_DATA		(NCT6791D_HWM_BASE + 6)

#define ENE_EC_DATA		0x62
#define ENE_EC_CMD_STS		0x66
#define ENE_EC_IBF_MASK		0x06
#define ENE_EC_OBF_MASK		0x01
#define ENE_EC_CMD_READ		0x80
#define ENE_EC_CMD_WRITE	0x81
#define ENE_EC_CMD_VERSION	0xa1
#define ENE_EC_CMD_VRM_COMMIT	0xba
#define ENE_EC_REG_PAGE		0xff
#define ENE_EC_PAGE1_SIG_REG	0xf8
#define ENE_EC_SIGNATURE	0xec
#define ENE_EC_POLL_LIMIT	4096

#define OC_MBOX_CMD_VF_READ	0x10
#define OC_MBOX_CMD_VF_WRITE	0x11
#define OC_MBOX_PLANE_CORE	0
#define EPU_UNDERVOLT_MV	20
#define PLATFORM_INFO_PRG_TURBO	(1U << 28)

enum qfan_profile {
	QFAN_STANDARD = 0,
	QFAN_SILENT = 1,
	QFAN_TURBO = 2,
	QFAN_FULL_SPEED = 3,
	QFAN_KEEP_DEFAULT = 4,
};

struct qfan_curve {
	u8 temp[4];
	u8 pwm[4];
};

static const struct qfan_curve cpu_curves[] = {
	[QFAN_STANDARD] = { .temp = { 30, 50, 70, 80 }, .pwm = {  64, 115, 191, 255 } },
	[QFAN_SILENT]   = { .temp = { 40, 60, 75, 85 }, .pwm = {  51,  97, 166, 255 } },
	[QFAN_TURBO]    = { .temp = { 30, 50, 65, 75 }, .pwm = { 115, 166, 217, 255 } },
};

static const struct qfan_curve chassis_curves[] = {
	[QFAN_STANDARD] = { .temp = { 30, 40, 55, 65 }, .pwm = {  77, 115, 179, 255 } },
	[QFAN_SILENT]   = { .temp = { 35, 45, 60, 70 }, .pwm = {  64,  97, 153, 255 } },
	[QFAN_TURBO]    = { .temp = { 30, 40, 50, 60 }, .pwm = { 128, 179, 217, 255 } },
};

static inline void sio_enter(void)
{
	outb(0x87, SIO_INDEX);
	outb(0x87, SIO_INDEX);
}

static inline void sio_exit(void)
{
	outb(0xaa, SIO_INDEX);
}

static inline u8 sio_read(u8 ldn, u8 reg)
{
	outb(0x07, SIO_INDEX);
	outb(ldn, SIO_DATA);
	outb(reg, SIO_INDEX);
	return inb(SIO_DATA);
}

static inline void sio_write(u8 ldn, u8 reg, u8 val)
{
	outb(0x07, SIO_INDEX);
	outb(ldn, SIO_DATA);
	outb(reg, SIO_INDEX);
	outb(val, SIO_DATA);
}

static inline u8 hwm_read(u8 bank, u8 reg)
{
	outb(0x4e, HWM_INDEX);
	outb(bank, HWM_DATA);
	outb(reg, HWM_INDEX);
	return inb(HWM_DATA);
}

static inline void hwm_write(u8 bank, u8 reg, u8 val)
{
	outb(0x4e, HWM_INDEX);
	outb(bank, HWM_DATA);
	outb(reg, HWM_INDEX);
	outb(val, HWM_DATA);
}

/*
 * Q-LED helpers matching stock ProjectPEI / ProjectDXE:
 *  - DRAM_LED:        NCT6791D LDN 0x08 GP07 (active-low output -> Hi-Z input OFF)
 *  - VGA_LED:         PCH GPIO38 (GP_USE_SEL2 +0x30 / GP_IO_SEL2 +0x34 / GP_LVL2 +0x38 bit 6, active-high -> Hi-Z OFF)
 *  - BOOT_DEVICE_LED: PCH GPIO22 (GP_USE_SEL +0x00 / GP_IO_SEL +0x04 / GP_LVL +0x0c bit 22, active-high -> Hi-Z OFF)
 */
static void set_dram_led(bool on)
{
	u8 cr;

	sio_enter();
	cr = sio_read(0x08, 0xe0);
	sio_write(0x08, 0xe0, on ? (cr & ~0x80) : (cr | 0x80));
	cr = sio_read(0x08, 0xe1);
	sio_write(0x08, 0xe1, on ? (cr & ~0x80) : (cr | 0x80));
	sio_exit();
}

static void set_pch_qled(u16 use_off, u16 sel_off, u16 lvl_off, u32 mask, bool on)
{
	u16 gbase = get_gpiobase();
	u32 reg;

	if (on) {
		reg = inl(gbase + use_off);
		outl(reg | mask, gbase + use_off);
		reg = inl(gbase + sel_off);
		outl(reg & ~mask, gbase + sel_off);
		reg = inl(gbase + lvl_off);
		outl(reg | mask, gbase + lvl_off);
	} else {
		reg = inl(gbase + lvl_off);
		outl(reg & ~mask, gbase + lvl_off);
		reg = inl(gbase + sel_off);
		outl(reg | mask, gbase + sel_off);
	}
}

static inline void set_vga_led(bool on)
{
	set_pch_qled(0x30, 0x34, 0x38, 1U << 6, on);
}

static inline void set_boot_led(bool on)
{
	set_pch_qled(0x00, 0x04, 0x0c, 1U << 22, on);
}

static bool ene_ec_wait_ibf(void)
{
	for (unsigned int i = 0; i < ENE_EC_POLL_LIMIT; i++) {
		if ((inb(ENE_EC_CMD_STS) & ENE_EC_IBF_MASK) == 0)
			return true;
		udelay(10);
	}
	return false;
}

static bool ene_ec_wait_obf(void)
{
	for (unsigned int i = 0; i < ENE_EC_POLL_LIMIT; i++) {
		if (inb(ENE_EC_CMD_STS) & ENE_EC_OBF_MASK)
			return true;
		udelay(10);
	}
	return false;
}

static bool ene_ec_send_cmd(u8 cmd)
{
	if (!ene_ec_wait_ibf())
		return false;
	outb(cmd, ENE_EC_CMD_STS);
	return ene_ec_wait_ibf();
}

static bool ene_ec_send_data(u8 val)
{
	if (!ene_ec_wait_ibf())
		return false;
	outb(val, ENE_EC_DATA);
	return ene_ec_wait_ibf();
}

static bool ene_ec_read_reg(u8 reg, u8 *val)
{
	if (!ene_ec_send_cmd(ENE_EC_CMD_READ))
		return false;
	if (!ene_ec_send_data(reg))
		return false;
	if (!ene_ec_wait_obf())
		return false;
	*val = inb(ENE_EC_DATA);
	return true;
}

static bool ene_ec_write_reg(u8 reg, u8 val)
{
	if (!ene_ec_send_cmd(ENE_EC_CMD_WRITE))
		return false;
	if (!ene_ec_send_data(reg))
		return false;
	return ene_ec_send_data(val);
}

static bool ene_ec_check_sig(void)
{
	u8 sig = 0;

	if (!ene_ec_write_reg(ENE_EC_REG_PAGE, 0x01))
		return false;
	if (!ene_ec_read_reg(ENE_EC_PAGE1_SIG_REG, &sig))
		return false;
	if (!ene_ec_write_reg(ENE_EC_REG_PAGE, 0x00))
		return false;
	return sig == ENE_EC_SIGNATURE;
}

static bool ene_ec_commit_vrm(u8 *status_out)
{
	if (!ene_ec_send_cmd(ENE_EC_CMD_VRM_COMMIT))
		return false;
	if (!ene_ec_send_data(0xe3))
		return false;
	if (!ene_ec_send_data(0x03))
		return false;
	return ene_ec_read_reg(0xd3, status_out);
}

static bool ene_ec_read_version(char *buf, size_t len)
{
	size_t idx = 0;

	if (!len)
		return false;
	buf[0] = '\0';

	if (!ene_ec_send_cmd(ENE_EC_CMD_VERSION))
		return false;

	for (size_t i = 0; i < 16; i++) {
		u8 c;

		if (!ene_ec_wait_obf())
			break;
		c = inb(ENE_EC_DATA);
		if (c == 0)
			break;
		if (idx + 1 < len)
			buf[idx++] = (c >= 0x20 && c <= 0x7e) ? (char)c : '?';
	}
	buf[idx] = '\0';
	return idx > 0;
}

static bool read_epu_hw_switch(void)
{
	u8 cr30, cr2f, val;

	sio_enter();
	cr2f = sio_read(0x07, 0x2f);
	sio_write(0x07, 0x2f, cr2f & ~0x04);
	cr30 = sio_read(0x07, 0x30);
	sio_write(0x07, 0x30, cr30 | 0x02);
	val = sio_read(0x07, 0xe1);
	sio_write(0x07, 0x30, cr30);
	sio_exit();

	return ((~(val >> 2)) & 0x01) != 0;
}

/*
 * Physical 3-position TPU slide switch (OC_Tuner 0x18000044c..0x18000048b):
 *   NCT6791D LDN 0x09 GP26 (0xe1 bit 6) & GP41 (0xf1 bit 1)
 *   Returns: 0 = OFF, 1 = TPU_I (Ratio), 2 = TPU_II (Ratio + BCLK)
 */
static unsigned int read_tpu_hw_switch(void)
{
	u8 gp2, gp4;

	sio_enter();
	gp2 = sio_read(0x09, 0xe1);
	gp4 = sio_read(0x09, 0xf1);
	sio_exit();

	if ((gp2 & 0x40) == 0)
		return 1;
	if ((gp4 & 0x02) == 0)
		return 2;
	return 0;
}

static const char *tpu_switch_name(unsigned int state)
{
	static const char *const names[] = {
		"OFF", "TPU_I (Ratio)", "TPU_II (Ratio+BCLK)"
	};

	return (state < ARRAY_SIZE(names)) ? names[state] : "Unknown";
}

static void handle_digi_vrm_asp1252(void)
{
	static const u8 llc_tbl[] = {
		0xdd, 0xff, 0xdd, 0xcc, 0xaa, 0x88, 0x66, 0x44, 0x22, 0x00
	};
	static const u8 currcap_tbl[] = {
		0x08, 0x09, 0x0a, 0x0b, 0x0c
	};
	static const u8 swfreq_tbl[] = {
		0x33, 0xff, 0xfe, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77
	};
	static const u8 duty_tbl[][4] = {
		{ 0x26, 0x26, 0x00, 0x00 }, /* T.Probe Thermal */
		{ 0x44, 0x44, 0x00, 0x00 }, /* Extreme Current */
	};
	static const u8 phase_tbl[][4] = {
		{ 0x22, 0x22, 0x11, 0xe0 }, /* Standard */
		{ 0x44, 0x33, 0x19, 0xe4 }, /* Optimized */
		{ 0x44, 0x33, 0x19, 0x00 }, /* Extreme */
		{ 0x02, 0x86, 0x6a, 0x00 }, /* Manual Ultra Fast */
		{ 0x02, 0x97, 0x7b, 0x00 }, /* Manual Fast */
		{ 0x02, 0xa8, 0x8c, 0x00 }, /* Manual Medium */
		{ 0x02, 0xb9, 0x9d, 0x00 }, /* Manual Regular */
	};
	u8 llc = 0, cur = 0, swf = 0, d0 = 0, d1 = 0, p0 = 0, p1 = 0, p2 = 0, p3 = 0;
	u8 v0 = 0, v1 = 0, i0 = 0, i1 = 0, w0 = 0, w1 = 0;
	bool ok = true;

	if (!ene_ec_check_sig()) {
		printk(BIOS_WARNING, "Z97-A: ENE EC signature (0xec) mismatch; skipping DIGI+ VRM\n");
		return;
	}

	/* Mirror stock EnePEI (0xfff7f8b0) Page 0 initialization before VRM access */
	ene_ec_write_reg(0xde, 0x04);
	ene_ec_write_reg(0xc0, 0x00);

	if (get_uint_option("digi_vrm_apply", 0)) {
		unsigned int llc_opt = get_uint_option("digi_vrm_llc", 0);
		unsigned int cur_opt = get_uint_option("digi_vrm_current_cap", 0);
		unsigned int swf_opt = get_uint_option("digi_vrm_sw_freq", 0);
		unsigned int phs_opt = get_uint_option("digi_vrm_phase_ctrl", 0);
		unsigned int dty_opt = get_uint_option("digi_vrm_duty_ctrl", 0);
		u8 swf_val, sts = 0;

		if (llc_opt >= ARRAY_SIZE(llc_tbl))
			llc_opt = 0;
		if (cur_opt >= ARRAY_SIZE(currcap_tbl))
			cur_opt = 0;
		if (swf_opt >= ARRAY_SIZE(swfreq_tbl))
			swf_opt = 0;
		if (phs_opt >= ARRAY_SIZE(phase_tbl))
			phs_opt = 0;
		if (dty_opt >= ARRAY_SIZE(duty_tbl))
			dty_opt = 0;

		/* Spread Spectrum (0xff) / AFM (0xfe) are disallowed in Extreme phase mode (OCMR_PEI) */
		swf_val = swfreq_tbl[swf_opt];
		if (phs_opt == 2 && (swf_val == 0xff || swf_val == 0xfe))
			swf_val = 0x33;

		ok &= ene_ec_write_reg(0xf1, llc_tbl[llc_opt]);
		ok &= ene_ec_write_reg(0xf2, currcap_tbl[cur_opt]);
		ok &= ene_ec_write_reg(0xd8, swf_val);
		for (size_t i = 0; i < 4; i++) {
			ok &= ene_ec_write_reg(0xda + i, duty_tbl[dty_opt][i]);
			ok &= ene_ec_write_reg(0x8c + i, phase_tbl[phs_opt][i]);
		}
		if (ok && ene_ec_commit_vrm(&sts)) {
			if (sts & 0x20)
				printk(BIOS_WARNING, "Z97-A: DIGI+ ASP1252 VRM commit flagged EC[0xd3]=0x%02x\n", sts);
			else
				printk(BIOS_INFO, "Z97-A: DIGI+ ASP1252 VRM programmed & committed (EC[0xd3]=0x%02x)\n", sts);
		} else {
			printk(BIOS_WARNING, "Z97-A: DIGI+ ASP1252 VRM write/commit timed out\n");
		}
	}

	ok = ene_ec_read_reg(0xf1, &llc) &&
	     ene_ec_read_reg(0xf2, &cur) &&
	     ene_ec_read_reg(0xd8, &swf) &&
	     ene_ec_read_reg(0xda, &d0) && ene_ec_read_reg(0xdb, &d1) &&
	     ene_ec_read_reg(0x8c, &p0) && ene_ec_read_reg(0x8d, &p1) &&
	     ene_ec_read_reg(0x8e, &p2) && ene_ec_read_reg(0x8f, &p3) &&
	     ene_ec_read_reg(0x5b, &v0) && ene_ec_read_reg(0x5c, &v1) &&
	     ene_ec_read_reg(0x5f, &i0) && ene_ec_read_reg(0x60, &i1) &&
	     ene_ec_read_reg(0x83, &w0) && ene_ec_read_reg(0x84, &w1);
	if (ok) {
		printk(BIOS_INFO,
		       "Z97-A: ASP1252 VRM LLC=0x%02x CurrCap=0x%02x SwFreq=0x%02x Duty=%02x.%02x Phase=%02x.%02x.%02x.%02x\n",
		       llc, cur, swf, d0, d1, p0, p1, p2, p3);
		printk(BIOS_INFO,
		       "Z97-A: ASP1252 raw telemetry VCPU=%02x%02x ICPU=%02x%02x PCPU=%02x%02x\n",
		       v0, v1, i0, i1, w0, w1);
	}
}

static void apply_tpu_all_core_turbo(void)
{
	unsigned int ratio = get_uint_option("cpu_turbo_ratio_all_core", 0);
	msr_t pinfo, old_turbo, new_turbo;

	if (ratio < 32 || ratio > 48)
		return;

	pinfo = rdmsr(MSR_PLATFORM_INFO);
	if (!(pinfo.lo & PLATFORM_INFO_PRG_TURBO)) {
		printk(BIOS_WARNING, "Z97-A: Programmable Turbo Ratio not supported on this CPU\n");
		return;
	}

	old_turbo = rdmsr(MSR_TURBO_RATIO_LIMIT);
	new_turbo.hi = old_turbo.hi;
	new_turbo.lo = (ratio & 0xffU) * 0x01010101U;
	wrmsr(MSR_TURBO_RATIO_LIMIT, new_turbo);

	printk(BIOS_INFO, "Z97-A: TPU All-Core Turbo Ratio 0x%08x -> 0x%08x (%ux)\n",
	       old_turbo.lo, new_turbo.lo, ratio);
}

static void apply_epu_power_saving(bool epu_hw_on)
{
	unsigned int epu_opt = get_uint_option("epu_mode", 0);

	if (epu_opt == 0 || (epu_opt == 1 && !epu_hw_on))
		return;

#if CONFIG(INTEL_OC_MAILBOX_ENABLE_UNDERVOLTING)
	if (get_uint_option("oc_undervolt_apply", 0)) {
		printk(BIOS_INFO, "Z97-A: EPU undervolt skipped (manual oc_undervolt_apply is active)\n");
		return;
	}

	const union oc_mailbox_interface rd_cmd = {
		.cmd_code = OC_MBOX_CMD_VF_READ,
		.param1 = OC_MBOX_PLANE_CORE,
	};
	const union oc_mailbox_interface wr_cmd = {
		.cmd_code = OC_MBOX_CMD_VF_WRITE,
		.param1 = OC_MBOX_PLANE_CORE,
	};
	const int32_t offset = -(EPU_UNDERVOLT_MV * 1024) / 1000;
	u32 data = oc_mailbox_read(rd_cmd);

	data = (data & 0x001fffffU) | (((u32)offset & 0x7ffU) << 21);
	if (oc_mailbox_write(wr_cmd, data) == 0)
		printk(BIOS_INFO, "Z97-A: EPU active (-%u mV Core OC mailbox offset applied)\n",
		       EPU_UNDERVOLT_MV);
	else
		printk(BIOS_WARNING, "Z97-A: EPU OC mailbox write failed\n");
#else
	printk(BIOS_WARNING, "Z97-A: EPU requested but OC mailbox undervolting disabled\n");
#endif
}

static void nct6791d_apply_fan_profile(u8 bank, u8 temp_src, unsigned int profile, bool is_cpu)
{
	const struct qfan_curve *curve;
	u8 val;

	if (profile >= QFAN_KEEP_DEFAULT)
		return;

	val = hwm_read(bank, 0x00);
	hwm_write(bank, 0x00, (val & 0xe0) | (temp_src & 0x1f));

	if (profile == QFAN_FULL_SPEED) {
		val = hwm_read(bank, 0x02);
		hwm_write(bank, 0x02, val & 0x0f);
		hwm_write(bank, 0x09, 0xff);
		return;
	}

	curve = is_cpu ? &cpu_curves[profile] : &chassis_curves[profile];
	for (unsigned int i = 0; i < 4; i++) {
		hwm_write(bank, 0x21 + i, curve->temp[i]);
		hwm_write(bank, 0x27 + i, curve->pwm[i]);
	}

	val = hwm_read(bank, 0x02);
	hwm_write(bank, 0x02, (val & 0x0f) | 0x40);
}

static const char *qfan_profile_name(unsigned int profile)
{
	static const char *const names[] = {
		"Standard", "Silent", "Turbo", "Full Speed", "Keep Default"
	};

	return (profile < ARRAY_SIZE(names)) ? names[profile] : "Unknown";
}

static void z97a_dev_init_entry(void *arg)
{
	/* Memory init completed cleanly; clear DRAM_LED and illuminate VGA_LED during PCIe/GMA init */
	set_dram_led(false);
	if (get_uint_option("qled_post_indicators", 1))
		set_vga_led(true);
}

static void z97a_post_device(void *arg)
{
	static const u8 chassis_banks[] = { 0x01, 0x03, 0x08, 0x09 };
	unsigned int cpu_prof = get_uint_option("qfan_cpu_profile", QFAN_STANDARD);
	unsigned int cha_prof = get_uint_option("qfan_chassis_profile", QFAN_STANDARD);
	unsigned int erp_en = get_uint_option("erp_ready", 0);
	unsigned int qled_en = get_uint_option("qled_post_indicators", 1);
	u8 tjmax = (rdmsr(MSR_TEMPERATURE_TARGET).lo >> 16) & 0xff;
	char ec_ver[20];
	bool epu_on;
	u8 cr;

	if (!tjmax)
		tjmax = 100;

	/* VGA/libgfxinit finished; clear VGA_LED and illuminate BOOT_DEVICE_LED until payload boot */
	set_vga_led(false);
	set_boot_led(qled_en != 0);

	/* Configure ErP Ready Deep S4/S5 (NCT6791D LDN 0x16) matching ASUS_EUPSxSMI */
	sio_enter();
	cr = sio_read(0x16, 0x30);
	if (erp_en) {
		sio_write(0x16, 0x30, cr | 0x01);
		sio_write(0x16, 0xe2, 0x0a);
	} else {
		sio_write(0x16, 0x30, cr & ~0x01);
	}
	sio_exit();

	/*
	 * Initialize NCT6791D HWM Bank 7 TjMax matching SIOBasicIOPei (0xfff80c78),
	 * apply Q-Fan SmartFan IV curves, then select Bank 0 for OS nct6775.
	 */
	hwm_write(0x07, 0x03, 0x10);
	hwm_write(0x07, 0x09, tjmax);
	hwm_write(0x07, 0x01, 0x95);

	nct6791d_apply_fan_profile(0x02, 0x10, cpu_prof, true);
	for (size_t i = 0; i < ARRAY_SIZE(chassis_banks); i++)
		nct6791d_apply_fan_profile(chassis_banks[i], 0x01, cha_prof, false);

	u8 vid = hwm_read(0x00, 0x4f);
	unsigned int tpu_sw = read_tpu_hw_switch();
	u32 gcs = RCBA32(GCS);
	epu_on = read_epu_hw_switch();

	printk(BIOS_INFO,
	       "Z97-A: NCT6791D HWM @0x%x unlocked (ID=0x%02x, TjMax=%uC, Q-Fan CPU=%s/CHA=%s, ErP=%s)\n",
	       NCT6791D_HWM_BASE, vid, tjmax,
	       qfan_profile_name(cpu_prof), qfan_profile_name(cha_prof),
	       erp_en ? "On" : "Off");

	printk(BIOS_INFO, "Z97-A: Port 80h routing: %s (RCBA GCS=0x%08x)\n",
	       (gcs & 0x4) ? "PCI" : "LPC", gcs);
	if (CONFIG(CMOS_POST)) {
		u8 prev_code = 0;
		u32 prev_extra = 0;
		int prev_err = cmos_post_previous_boot(&prev_code, &prev_extra);

		printk(BIOS_INFO, "Z97-A: CMOS_POST previous boot: code=0x%02x extra=0x%08x (%s)\n",
		       prev_code, prev_extra, prev_err ? "abnormal" : "ok");
	}

	if (ene_ec_read_version(ec_ver, sizeof(ec_ver))) {
		printk(BIOS_INFO, "Z97-A: ENE EC version: %s\n", ec_ver);
		handle_digi_vrm_asp1252();
	} else {
		printk(BIOS_INFO, "Z97-A: ENE EC version: not responding\n");
	}

	printk(BIOS_INFO, "Z97-A: HW switches: EPU=%s, TPU=%s\n",
	       epu_on ? "ON" : "OFF", tpu_switch_name(tpu_sw));
	apply_tpu_all_core_turbo();
	apply_epu_power_saving(epu_on);
	printk(BIOS_INFO, "Z97-A: libgfxinit linear FB -> EDK2 UEFI GOP ready\n");
}

static void z97a_payload_boot(void *arg)
{
	/* Ensure all POST Q-LEDs are off before jumping to EDK2 / Proxmox */
	set_dram_led(false);
	set_vga_led(false);
	set_boot_led(false);
}

BOOT_STATE_INIT_ENTRY(BS_DEV_INIT, BS_ON_ENTRY, z97a_dev_init_entry, NULL);
BOOT_STATE_INIT_ENTRY(BS_POST_DEVICE, BS_ON_EXIT, z97a_post_device, NULL);
BOOT_STATE_INIT_ENTRY(BS_PAYLOAD_BOOT, BS_ON_ENTRY, z97a_payload_boot, NULL);
