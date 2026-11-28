/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * ASUS Z97-A CPU PEG root port PCI interrupt routing tables (_PRT),
 * matching stock AMI 3503 DSDT (AR04/PR04, AR05/PR05, AR06/PR06).
 * Fixes INT B/C/D routing for multi-function PEG devices (e.g. dGPU HDMI audio).
 */

Scope (\_SB.PCI0.PEGP)
{
	Method (_PRT)
	{
		If (\PICM) {
			Return (Package () {
				Package () { 0x0000ffff, 0, 0, 16 },
				Package () { 0x0000ffff, 1, 0, 17 },
				Package () { 0x0000ffff, 2, 0, 18 },
				Package () { 0x0000ffff, 3, 0, 19 },
			})
		} Else {
			Return (Package () {
				Package () { 0x0000ffff, 0, \_SB.PCI0.LPCB.LNKA, 0 },
				Package () { 0x0000ffff, 1, \_SB.PCI0.LPCB.LNKB, 0 },
				Package () { 0x0000ffff, 2, \_SB.PCI0.LPCB.LNKC, 0 },
				Package () { 0x0000ffff, 3, \_SB.PCI0.LPCB.LNKD, 0 },
			})
		}
	}
}

Scope (\_SB.PCI0.PEG1)
{
	Method (_PRT)
	{
		If (\PICM) {
			Return (Package () {
				Package () { 0x0000ffff, 0, 0, 17 },
				Package () { 0x0000ffff, 1, 0, 18 },
				Package () { 0x0000ffff, 2, 0, 19 },
				Package () { 0x0000ffff, 3, 0, 16 },
			})
		} Else {
			Return (Package () {
				Package () { 0x0000ffff, 0, \_SB.PCI0.LPCB.LNKB, 0 },
				Package () { 0x0000ffff, 1, \_SB.PCI0.LPCB.LNKC, 0 },
				Package () { 0x0000ffff, 2, \_SB.PCI0.LPCB.LNKD, 0 },
				Package () { 0x0000ffff, 3, \_SB.PCI0.LPCB.LNKA, 0 },
			})
		}
	}
}

Scope (\_SB.PCI0.PEG2)
{
	Method (_PRT)
	{
		If (\PICM) {
			Return (Package () {
				Package () { 0x0000ffff, 0, 0, 18 },
				Package () { 0x0000ffff, 1, 0, 19 },
				Package () { 0x0000ffff, 2, 0, 16 },
				Package () { 0x0000ffff, 3, 0, 17 },
			})
		} Else {
			Return (Package () {
				Package () { 0x0000ffff, 0, \_SB.PCI0.LPCB.LNKC, 0 },
				Package () { 0x0000ffff, 1, \_SB.PCI0.LPCB.LNKD, 0 },
				Package () { 0x0000ffff, 2, \_SB.PCI0.LPCB.LNKA, 0 },
				Package () { 0x0000ffff, 3, \_SB.PCI0.LPCB.LNKB, 0 },
			})
		}
	}
}
