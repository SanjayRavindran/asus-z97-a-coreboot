/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <northbridge/intel/haswell/haswell.h>
#include <southbridge/intel/lynxpoint/pch.h>

const struct usb2_port_config mainboard_usb2_ports[MAX_USB2_PORTS] = {
	/* Length, Enable, OCn#, Location (exact Z97-A stock SBPEI/0xfff8d825 table) */
	{ 0x0080, 1, 0, USB_PORT_INTERNAL },
	{ 0x0080, 1, USB_OC_PIN_SKIP, USB_PORT_INTERNAL },
	{ 0x0100, 1, 1, USB_PORT_BACK_PANEL },
	{ 0x0100, 1, 1, USB_PORT_BACK_PANEL },
	{ 0x0080, 1, 2, USB_PORT_INTERNAL },
	{ 0x0080, 1, 2, USB_PORT_INTERNAL },
	{ 0x0080, 1, 3, USB_PORT_INTERNAL },
	{ 0x0080, 1, 3, USB_PORT_INTERNAL },
	{ 0x0100, 1, 4, USB_PORT_BACK_PANEL },
	{ 0x0100, 1, 4, USB_PORT_BACK_PANEL },
	{ 0x0140, 1, 5, USB_PORT_BACK_PANEL },
	{ 0x0140, 1, 5, USB_PORT_BACK_PANEL },
	{ 0x0080, 1, 6, USB_PORT_INTERNAL },
	{ 0x0080, 1, 6, USB_PORT_INTERNAL },
};

const struct usb3_port_config mainboard_usb3_ports[MAX_USB3_PORTS] = {
	{ 1, 0 },
	{ 1, 0 },
	{ 1, 1 },
	{ 1, 1 },
	{ 1, 2 },
	{ 1, 2 },
};
