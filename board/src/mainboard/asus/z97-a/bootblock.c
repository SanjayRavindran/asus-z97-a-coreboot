/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/pnp_ops.h>
#include <superio/nuvoton/common/nuvoton.h>
#include <superio/nuvoton/nct6791d/nct6791d.h>
#include <southbridge/intel/lynxpoint/pch.h>

#define GLOBAL_DEV	PNP_DEV(0x2e, 0)
#define SERIAL_DEV	PNP_DEV(0x2e, NCT6791D_SP1)
#define ACPI_DEV	PNP_DEV(0x2e, NCT6791D_ACPI)
#define GPIO_PP_OD_DEV	PNP_DEV(0x2e, NCT6791D_GPIO_PP_OD)

void mainboard_config_superio(void)
{
	nuvoton_pnp_enter_conf_state(GLOBAL_DEV);

	/* Select SIO pin mux states (captured from stock AMI 3503) */
	pnp_write_config(GLOBAL_DEV, 0x1a, 0x0a);
	pnp_write_config(GLOBAL_DEV, 0x1b, 0x20);
	pnp_write_config(GLOBAL_DEV, 0x1c, 0x10);
	pnp_write_config(GLOBAL_DEV, 0x24, 0x00);
	/* Clear bit 4 (HWM I/O space lock) so 0x295/0x296 is directly readable by OS nct6775 */
	pnp_write_config(GLOBAL_DEV, 0x28, 0x00);
	pnp_write_config(GLOBAL_DEV, 0x2a, 0x40);
	pnp_write_config(GLOBAL_DEV, 0x2c, 0x00);
	pnp_write_config(GLOBAL_DEV, 0x2f, 0x03);

	/* Select push-pull vs. open-drain output (LDN 0x0f) */
	pnp_set_logical_device(GPIO_PP_OD_DEV);
	pnp_write_config(GPIO_PP_OD_DEV, 0xe0, 0x7e);
	pnp_write_config(GPIO_PP_OD_DEV, 0xe2, 0x6f);
	pnp_write_config(GPIO_PP_OD_DEV, 0xe3, 0xbf);
	pnp_write_config(GPIO_PP_OD_DEV, 0xe4, 0xfa);
	pnp_write_config(GPIO_PP_OD_DEV, 0xe6, 0x7f);

	/* ACPI / S3 power config (LDN 0x0a) */
	pnp_set_logical_device(ACPI_DEV);
	pnp_write_config(ACPI_DEV, 0xe6, 0x0a);
	pnp_write_config(ACPI_DEV, 0xe7, 0x11);
	pnp_write_config(ACPI_DEV, 0xec, 0x80);
	pnp_write_config(ACPI_DEV, 0xed, 0x01);
	pnp_write_config(ACPI_DEV, 0xee, 0x10);
	pnp_write_config(ACPI_DEV, 0xf2, 0x5d);
	pnp_write_config(ACPI_DEV, 0xfc, 0x80);

	/* Light DRAM_LED (LDN 0x08 GP07, active-low) during pre-RAM / raminit; mainboard.c turns it off */
	pnp_set_logical_device(PNP_DEV(0x2e, NCT6791D_WDT1_WDTMEM_GPIO01));
	pnp_write_config(GLOBAL_DEV, 0x30, pnp_read_config(GLOBAL_DEV, 0x30) | 0x02);
	pnp_write_config(GLOBAL_DEV, 0xe0, pnp_read_config(GLOBAL_DEV, 0xe0) & ~0x80);
	pnp_write_config(GLOBAL_DEV, 0xe1, pnp_read_config(GLOBAL_DEV, 0xe1) & ~0x80);

	nuvoton_pnp_exit_conf_state(GLOBAL_DEV);

	nuvoton_enable_serial(SERIAL_DEV, CONFIG_TTYS0_BASE);
}
