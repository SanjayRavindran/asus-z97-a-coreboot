/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <cpu/intel/haswell/cfr.h>
#include <drivers/intel/oc_mailbox/cfr.h>
#include <drivers/option/cfr_frontend.h>
#include <southbridge/intel/lynxpoint/cfr.h>

static const struct sm_object peg_bifurcation = SM_DECLARE_ENUM({
	.opt_name	= "peg_bifurcation",
	.ui_name	= "PCIe x16 Slot Bifurcation",
	.ui_helptext	= "Control CPU PEG root ports 00:01.0/.1/.2.\n"
			  "2x8 requires CPU CFG[5]=0 strap; 1x8+2x4 requires CFG[6:5]=00.\n"
			  "Haswell silicon has 3 PEG ports (x4/x4/x4/x4 is not supported).",
	.default_value	= 0,
	.values		= (const struct sm_enum_value[]) {
				{ "Auto (HW CFG[6:5] Strap)",		0 },
				{ "Force 1x16 (Disable PEG11/12)",	1 },
				{ "2x8 (Req. CFG[5]=0 strap)",		2 },
				{ "1x8+2x4 (Req. CFG[6:5]=00 strap)",	3 },
				SM_ENUM_VALUE_END },
});

static const struct sm_object tdp_pl1_override = SM_DECLARE_NUMBER({
	.opt_name	= "tdp_pl1_override",
	.ui_name	= "CPU PL1 Power Limit (W, 0=Default)",
	.ui_helptext	= "Long-duration package power limit in Watts (0 uses CPU TDP).",
	.default_value	= 0,
	.min		= 0,
	.max		= 200,
	.step		= 1,
	.display_flags	= 0,
});

static const struct sm_object tdp_pl2_override = SM_DECLARE_NUMBER({
	.opt_name	= "tdp_pl2_override",
	.ui_name	= "CPU PL2 Power Limit (W, 0=Default)",
	.ui_helptext	= "Short-duration Turbo power limit in Watts (0 uses 1.25x TDP).",
	.default_value	= 0,
	.min		= 0,
	.max		= 250,
	.step		= 1,
	.display_flags	= 0,
});

static const struct sm_object cpu_turbo_ratio_all_core = SM_DECLARE_NUMBER({
	.opt_name	= "cpu_turbo_ratio_all_core",
	.ui_name	= "TPU All-Core Turbo Ratio (0=Stock)",
	.ui_helptext	= "0 keeps CPU stock Turbo ratios.\n"
			  "32-48 applies TPU-style synchronized all-core Turbo multiplier\n"
			  "via MSR_TURBO_RATIO_LIMIT (0x1ad) on unlocked K-series CPUs.",
	.default_value	= 0,
	.min		= 0,
	.max		= 48,
	.step		= 1,
	.display_flags	= 0,
});

static const struct sm_object digi_vrm_apply = SM_DECLARE_BOOL({
	.opt_name	= "digi_vrm_apply",
	.ui_name	= "Program DIGI+ VRM (ASP1252 via ENE EC)",
	.ui_helptext	= "Write ASP1252 VRM controls (LLC, Current Cap, SwFreq, Phase, Duty)\n"
			  "to ENE EC Page 0 and commit with 0xba 0xe3 0x03.\n"
			  "Disabled leaves hardware SVID/EC power-on defaults.",
	.default_value	= false,
});

static const struct sm_object digi_vrm_llc = SM_DECLARE_ENUM({
	.opt_name	= "digi_vrm_llc",
	.ui_name	= "CPU Load-line Calibration (LLC)",
	.ui_helptext	= "ASP1252 EC[0xf1] Load-Line Calibration level.",
	.default_value	= 0,
	.values		= (const struct sm_enum_value[]) {
				{ "Auto (Level 2 / 0xdd)",	0 },
				{ "Level 1 (0xff)",		1 },
				{ "Level 2 (0xdd)",		2 },
				{ "Level 3 (0xcc)",		3 },
				{ "Level 4 (0xaa)",		4 },
				{ "Level 5 (0x88)",		5 },
				{ "Level 6 (0x66)",		6 },
				{ "Level 7 (0x44)",		7 },
				{ "Level 8 (0x22)",		8 },
				{ "Level 9 (0x00)",		9 },
				SM_ENUM_VALUE_END },
}, WITH_DEP_VALUES(&digi_vrm_apply, true));

static const struct sm_object digi_vrm_current_cap = SM_DECLARE_ENUM({
	.opt_name	= "digi_vrm_current_cap",
	.ui_name	= "CPU Current Capability",
	.ui_helptext	= "ASP1252 EC[0xf2] over-current headroom limit.",
	.default_value	= 0,
	.values		= (const struct sm_enum_value[]) {
				{ "100% (Default)",	0 },
				{ "110%",		1 },
				{ "120%",		2 },
				{ "130%",		3 },
				{ "140%",		4 },
				SM_ENUM_VALUE_END },
}, WITH_DEP_VALUES(&digi_vrm_apply, true));

static const struct sm_object digi_vrm_sw_freq = SM_DECLARE_ENUM({
	.opt_name	= "digi_vrm_sw_freq",
	.ui_name	= "CPU VRM Switching Frequency",
	.ui_helptext	= "ASP1252 EC[0xd8] PWM switching frequency / Spread / AFM.",
	.default_value	= 0,
	.values		= (const struct sm_enum_value[]) {
				{ "Auto (300 kHz / 0x33)",	0 },
				{ "Spread Spectrum (0xff)",	1 },
				{ "Active Frequency Mode (0xfe)", 2 },
				{ "Manual 250 kHz (0x22)",	3 },
				{ "Manual 300 kHz (0x33)",	4 },
				{ "Manual 350 kHz (0x44)",	5 },
				{ "Manual 400 kHz (0x55)",	6 },
				{ "Manual 450 kHz (0x66)",	7 },
				{ "Manual 500 kHz (0x77)",	8 },
				SM_ENUM_VALUE_END },
}, WITH_DEP_VALUES(&digi_vrm_apply, true));

static const struct sm_object digi_vrm_phase_ctrl = SM_DECLARE_ENUM({
	.opt_name	= "digi_vrm_phase_ctrl",
	.ui_name	= "CPU Power Phase Control",
	.ui_helptext	= "ASP1252 EC[0x8c..0x8f] dynamic phase shedding / speed.",
	.default_value	= 0,
	.values		= (const struct sm_enum_value[]) {
				{ "Standard (Default)",		0 },
				{ "Optimized",			1 },
				{ "Extreme (All Phases)",	2 },
				{ "Manual Ultra Fast",		3 },
				{ "Manual Fast",		4 },
				{ "Manual Medium",		5 },
				{ "Manual Regular",		6 },
				SM_ENUM_VALUE_END },
}, WITH_DEP_VALUES(&digi_vrm_apply, true));

static const struct sm_object digi_vrm_duty_ctrl = SM_DECLARE_ENUM({
	.opt_name	= "digi_vrm_duty_ctrl",
	.ui_name	= "CPU Power Duty Control",
	.ui_helptext	= "ASP1252 EC[0xda..0xdd] thermal vs. current balancing.",
	.default_value	= 0,
	.values		= (const struct sm_enum_value[]) {
				{ "T.Probe (Thermal Balance)",	0 },
				{ "Extreme (Current Balance)",	1 },
				SM_ENUM_VALUE_END },
}, WITH_DEP_VALUES(&digi_vrm_apply, true));

static const struct sm_object epu_mode = SM_DECLARE_ENUM({
	.opt_name	= "epu_mode",
	.ui_name	= "ASUS EPU Power Saving Mode",
	.ui_helptext	= "Applies stock EpuHwModePei i7-4790K -20 mV Core undervolt via OC Mailbox\n"
			  "(skipped if manual OC mailbox undervolt is enabled).",
	.default_value	= 0,
	.values		= (const struct sm_enum_value[]) {
				{ "Disabled",			0 },
				{ "Auto (Follow EPU HW Switch)", 1 },
				{ "Force Enabled (-20 mV Core)", 2 },
				SM_ENUM_VALUE_END },
});

static const struct sm_object fb_debug_pause = SM_DECLARE_ENUM({
	.opt_name	= "fb_debug_pause",
	.ui_name	= "On-Screen Coreboot Log Pause",
	.ui_helptext	= "Duration to show verbose Coreboot/POST log on UEFI GOP display before EDK2 boot.",
	.default_value	= 1,
	.values		= (const struct sm_enum_value[]) {
				{ "0s (Immediate)",	0 },
				{ "2s (Default)",	1 },
				{ "5s",			2 },
				{ "10s",		3 },
				SM_ENUM_VALUE_END },
});

static const struct sm_object qled_post_indicators = SM_DECLARE_ENUM({
	.opt_name	= "qled_post_indicators",
	.ui_name	= "Q-LED POST Indicators",
	.ui_helptext	= "Light onboard ASUS Q-LEDs (DRAM -> VGA -> BOOT_DEVICE) across Coreboot boot milestones.",
	.default_value	= 1,
	.values		= (const struct sm_enum_value[]) {
				{ "Enabled (DRAM -> VGA -> BOOT)",	1 },
				{ "Disabled (All Off)",			0 },
				SM_ENUM_VALUE_END },
});

static const struct sm_object qfan_cpu_profile = SM_DECLARE_ENUM({
	.opt_name	= "qfan_cpu_profile",
	.ui_name	= "CPU Fan Profile (Q-Fan)",
	.ui_helptext	= "NCT6791D SmartFan IV curve for CPU_FAN / CPU_OPT (Bank 2, PECI temp source).",
	.default_value	= 0,
	.values		= (const struct sm_enum_value[]) {
				{ "Standard (30-80C -> 25-100%)",	0 },
				{ "Silent (40-85C -> 20-100%)",		1 },
				{ "Turbo (30-75C -> 45-100%)",		2 },
				{ "Full Speed (100% PWM)",		3 },
				{ "Keep Hardware Default",		4 },
				SM_ENUM_VALUE_END },
});

static const struct sm_object qfan_chassis_profile = SM_DECLARE_ENUM({
	.opt_name	= "qfan_chassis_profile",
	.ui_name	= "Chassis Fan Profile (Q-Fan)",
	.ui_helptext	= "NCT6791D SmartFan IV curve for CHA_FAN1..4 (Banks 1/3/8/9, SYSTIN temp source).",
	.default_value	= 0,
	.values		= (const struct sm_enum_value[]) {
				{ "Standard (30-65C -> 30-100%)",	0 },
				{ "Silent (35-70C -> 25-100%)",		1 },
				{ "Turbo (30-60C -> 50-100%)",		2 },
				{ "Full Speed (100% PWM)",		3 },
				{ "Keep Hardware Default",		4 },
				SM_ENUM_VALUE_END },
});

static const struct sm_object erp_ready = SM_DECLARE_ENUM({
	.opt_name	= "erp_ready",
	.ui_name	= "ErP Ready (Deep S4/S5)",
	.ui_helptext	= "Configure NCT6791D LDN 0x16 Deep Sleep to cut standby rail power in S4/S5.",
	.default_value	= 0,
	.values		= (const struct sm_enum_value[]) {
				{ "Disabled (WoL / USB Standby)",	0 },
				{ "Enabled (Deep S4/S5 Low Power)",	1 },
				SM_ENUM_VALUE_END },
});

static const struct sm_object wol_enable = SM_DECLARE_ENUM({
	.opt_name	= "wol_enable",
	.ui_name	= "Wake-on-LAN (S3/S4/S5)",
	.ui_helptext	= "Set PCH GEN_PMCON_3 WOL_EN_OVRD and keep PME_B0/PCI_EXP/PME GPEs enabled\n"
			  "in S5 for onboard I218-V and PCIe NIC wake. Requires ErP Ready = Disabled.",
	.default_value	= 1,
	.values		= (const struct sm_enum_value[]) {
				{ "Disabled",	0 },
				{ "Enabled",	1 },
				SM_ENUM_VALUE_END },
});

static struct sm_obj_form system = {
	.ui_name = "System & PCIe",
	.obj_list = (const struct sm_object *[]) {
		&peg_bifurcation,
		&me_disable,
		&nmi,
		NULL
	},
};

static struct sm_obj_form display_debug = {
	.ui_name = "Display & Debug",
	.obj_list = (const struct sm_object *[]) {
		&fb_debug_pause,
		&qled_post_indicators,
		NULL
	},
};

static struct sm_obj_form cooling = {
	.ui_name = "Cooling (Q-Fan)",
	.obj_list = (const struct sm_object *[]) {
		&qfan_cpu_profile,
		&qfan_chassis_profile,
		NULL
	},
};

static struct sm_obj_form cpu_perf = {
	.ui_name = "CPU Power & Turbo",
	.obj_list = (const struct sm_object *[]) {
		&cpu_turbo_ratio_all_core,
		&tdp_pl1_override,
		&tdp_pl2_override,
		&cpu_power_limit_lock,
		NULL
	},
};

static struct sm_obj_form digi_vrm = {
	.ui_name = "DIGI+ VRM (ASP1252)",
	.obj_list = (const struct sm_object *[]) {
		&digi_vrm_apply,
		&digi_vrm_llc,
		&digi_vrm_current_cap,
		&digi_vrm_sw_freq,
		&digi_vrm_phase_ctrl,
		&digi_vrm_duty_ctrl,
		NULL
	},
};

static struct sm_obj_form power = {
	.ui_name = "Power Management",
	.obj_list = (const struct sm_object *[]) {
		&power_on_after_fail,
		&wol_enable,
		&erp_ready,
		&epu_mode,
		NULL
	},
};

static struct sm_obj_form *sm_root[] = {
	&system,
	&display_debug,
	&cooling,
	&cpu_perf,
	&digi_vrm,
#if CONFIG(INTEL_OC_MAILBOX_ENABLE_UNDERVOLTING)
	&cpu_voltage,
#endif
	&power,
	NULL
};

void mb_cfr_setup_menu(struct lb_cfr *cfr_root)
{
	cfr_write_setup_menu(cfr_root, sm_root);
}
