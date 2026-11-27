/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/azalia_codec/realtek.h>
#include <device/azalia_device.h>

static const u32 realtek_alc892_verbs[] = {
	AZALIA_SUBVENDOR(0, 0x1043860b),
	AZALIA_PIN_CFG(0, ALC892_SPDIF_OUT2, 0x90460130),
	AZALIA_PIN_CFG(0, ALC892_DMIC_LR, 0x40170000),
	AZALIA_PIN_CFG(0, ALC892_FRONT, 0x01014010),
	AZALIA_PIN_CFG(0, ALC892_SURROUND, 0x01011012),
	AZALIA_PIN_CFG(0, ALC892_CENTER_LFE, 0x01016011),
	AZALIA_PIN_CFG(0, ALC892_SIDE_SURROUND, 0x411111f0),
	AZALIA_PIN_CFG(0, ALC892_MIC1, 0x01a19050),
	AZALIA_PIN_CFG(0, ALC892_MIC2, 0x02a19060),
	AZALIA_PIN_CFG(0, ALC892_LINE1, 0x0181305f),
	AZALIA_PIN_CFG(0, ALC892_LINE2, 0x02214020),
	AZALIA_PIN_CFG(0, ALC892_CD, 0x411111f0),
	AZALIA_PIN_CFG(0, ALC892_PC_BEEP, 0x4047e629),
	AZALIA_PIN_CFG(0, ALC892_SPDIF_OUT1, 0x01456140),
	AZALIA_PIN_CFG(0, ALC892_SPDIF_IN, 0x411111f0),
	/* Stock SBPEI/0xfff954c0 trailing ALC892 processing coefficient verbs */
	0x02050007, 0x020409c8, 0x02050007, 0x020409c8,
};

const u32 pc_beep_verbs[0] = {};

struct azalia_codec mainboard_azalia_codecs[] = {
	{
		.name         = "Realtek ALC892",
		.vendor_id    = 0x10ec0892,
		.subsystem_id = 0x1043860b,
		.address      = 0,
		.verbs        = realtek_alc892_verbs,
		.verb_count   = ARRAY_SIZE(realtek_alc892_verbs),
	},
};

AZALIA_ARRAY_SIZES;
