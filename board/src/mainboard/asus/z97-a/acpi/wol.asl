/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * ASUS Z97-A Wake-on-LAN (S3/S4/S5) ACPI _PRW / _DSW definitions:
 * - GLAN (00:19.0, Intel I218-V): GPE 0x0d (PME_B0_EN), deepest wake S4
 * - PEGP/PEG1/PEG2 (00:01.0/.1/.2) & RP01..RP08 (00:1c.0..7): GPE 0x09 (PCI_EXP), wake S4
 */

Device (GLAN)
{
	Name (_ADR, 0x00190000)

	OperationRegion (GLBA, PCI_Config, 0x00, 0x100)
	Field (GLBA, AnyAcc, NoLock, Preserve)
	{
		Offset (0xcd),
		PMEE,	1,
		,	6,
		PMES,	1,
	}

	Name (_PRW, Package () { 0x0d, 4 })

	Method (_DSW, 3)
	{
		PMEE = Arg0
	}
}

Scope (\_SB.PCI0.PEGP)
{
	Name (_PRW, Package () { 0x09, 4 })
}

Scope (\_SB.PCI0.PEG1)
{
	Name (_PRW, Package () { 0x09, 4 })
}

Scope (\_SB.PCI0.PEG2)
{
	Name (_PRW, Package () { 0x09, 4 })
}

Scope (\_SB.PCI0.RP01)
{
	Name (_PRW, Package () { 0x09, 4 })
}

Scope (\_SB.PCI0.RP02)
{
	Name (_PRW, Package () { 0x09, 4 })
}

Scope (\_SB.PCI0.RP03)
{
	Name (_PRW, Package () { 0x09, 4 })
}

Scope (\_SB.PCI0.RP04)
{
	Name (_PRW, Package () { 0x09, 4 })
}

Scope (\_SB.PCI0.RP05)
{
	Name (_PRW, Package () { 0x09, 4 })
}

Scope (\_SB.PCI0.RP06)
{
	Name (_PRW, Package () { 0x09, 4 })
}

Scope (\_SB.PCI0.RP07)
{
	Name (_PRW, Package () { 0x09, 4 })
}

Scope (\_SB.PCI0.RP08)
{
	Name (_PRW, Package () { 0x09, 4 })
}
