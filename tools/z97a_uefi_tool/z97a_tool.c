/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ASUS Z97-A Pre-OS Hardware Diagnostic & Direct SPI Flasher (UEFI + COM1)
 *
 * Freestanding x86_64 PE32+ EFI application:
 *  - Mode 1 (default / "diag"): Prints stock-vs-live register audit & runs
 *    16 MiB Gen3 AHCI BlockIO read stress test while logging PxSSTS/PxSERR.
 *  - Mode 2 ("serflash"): Polled 115200 8N1 COM1 (0x3f8) delta flash server
 *    over 4 KiB sectors in FMAP+COREBOOT (0x210000..0x7fffff).
 *  - Mode 3 (-DEMBED_BIOS_IMAGE): Flashes embedded FMAP+COREBOOT image delta,
 *    verifies full readback SHA-256, and cold-resets via 0xcf9=0x0e.
 *
 * Safety invariant: Write/erase is strictly restricted to FMAP+COREBOOT
 * [0x210000, 0x800000). IFD (0x0000..0x0fff), GbE (0x1000..0x2fff),
 * Intel ME (0x3000..0x17ffff), RW_MRC_CACHE (0x180000..0x18ffff),
 * SMMSTORE (0x190000..0x20ffff), and W25Q64FV QE=1 are never touched.
 */

#include <stdint.h>
#include <stddef.h>

#define BIOS_REGION_BASE 0x00210000U /* FMAP + COREBOOT; preserves RW_MRC_CACHE + SMMSTORE */
#define BIOS_REGION_END  0x00800000U
#define BIOS_REGION_SIZE (BIOS_REGION_END - BIOS_REGION_BASE)
#define SECTOR_SIZE      4096U
#define NUM_SECTORS      (BIOS_REGION_SIZE / SECTOR_SIZE) /* 1520 */

#define RCBA_BASE        0xfed1c000ULL
#define SPIBAR_BASE      0xfed1f800ULL
#define COM1_BASE        0x03f8

/* Freestanding intrinsics */
void *memset(void *dst, int c, size_t n)
{
	volatile uint8_t *p = (volatile uint8_t *)dst;
	for (size_t i = 0; i < n; i++)
		p[i] = (uint8_t)c;
	return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
	volatile uint8_t *d = (volatile uint8_t *)dst;
	const volatile uint8_t *s = (const volatile uint8_t *)src;
	for (size_t i = 0; i < n; i++)
		d[i] = s[i];
	return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const uint8_t *pa = (const uint8_t *)a;
	const uint8_t *pb = (const uint8_t *)b;
	for (size_t i = 0; i < n; i++) {
		if (pa[i] != pb[i])
			return (int)pa[i] - (int)pb[i];
	}
	return 0;
}

/* Low-level I/O & MMIO */
static inline uint8_t inb(uint16_t port)
{
	uint8_t v;
	__asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
	return v;
}

static inline void outb(uint8_t v, uint16_t port)
{
	__asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
	uint16_t v;
	__asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
	return v;
}

static inline void outw(uint16_t v, uint16_t port)
{
	__asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
	uint32_t v;
	__asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
	return v;
}

static inline void outl(uint32_t v, uint16_t port)
{
	__asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}

static inline uint64_t rdmsr64(uint32_t msr)
{
	uint32_t lo, hi;
	__asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
	return ((uint64_t)hi << 32) | lo;
}

static inline void io_wait(void)
{
	__asm__ volatile("outb %al, $0x80");
}

static inline uint16_t mmio_read16(uint64_t addr)
{
	return *(const volatile uint16_t *)(uintptr_t)addr;
}

static inline uint32_t mmio_read32(uint64_t addr)
{
	return *(const volatile uint32_t *)(uintptr_t)addr;
}

static inline void mmio_write16(uint64_t addr, uint16_t v)
{
	*(volatile uint16_t *)(uintptr_t)addr = v;
}

static inline void mmio_write32(uint64_t addr, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)addr = v;
}

/* Minimal UEFI ABI types */
typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;

typedef struct {
	uint32_t Data1;
	uint16_t Data2;
	uint16_t Data3;
	uint8_t  Data4[8];
} EFI_GUID;

typedef struct {
	uint64_t Signature;
	uint32_t Revision;
	uint32_t HeaderSize;
	uint32_t CRC32;
	uint32_t Reserved;
} EFI_TABLE_HEADER;

typedef struct {
	uint16_t ScanCode;
	uint16_t UnicodeChar;
} EFI_INPUT_KEY;

typedef struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
	EFI_STATUS (*Reset)(struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, uint8_t ExtendedVerification);
	EFI_STATUS (*ReadKeyStroke)(struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, EFI_INPUT_KEY *Key);
	void *WaitForKey;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
	EFI_STATUS (*Reset)(struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, uint8_t ExtendedVerification);
	EFI_STATUS (*OutputString)(struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, uint16_t *String);
	void *TestString;
	void *QueryMode;
	void *SetMode;
	void *SetAttribute;
	void *ClearScreen;
	void *SetCursorPosition;
	void *EnableCursor;
	void *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct {
	EFI_TABLE_HEADER Hdr;
	void *RaiseTPL;
	void *RestoreTPL;
	void *AllocatePages;
	void *FreePages;
	void *GetMemoryMap;
	EFI_STATUS (*AllocatePool)(uint32_t PoolType, uint64_t Size, void **Buffer);
	EFI_STATUS (*FreePool)(void *Buffer);
	void *CreateEvent;
	EFI_STATUS (*SetTimer)(void *Event, uint32_t Type, uint64_t TriggerTime);
	void *WaitForEvent;
	void *SignalEvent;
	void *CloseEvent;
	void *CheckEvent;
	void *InstallProtocolInterface;
	void *ReinstallProtocolInterface;
	void *UninstallProtocolInterface;
	EFI_STATUS (*HandleProtocol)(EFI_HANDLE Handle, EFI_GUID *Protocol, void **Interface);
	void *Reserved;
	void *RegisterProtocolNotify;
	void *LocateHandle;
	void *LocateDevicePath;
	void *InstallConfigurationTable;
	void *LoadImage;
	void *StartImage;
	void *Exit;
	void *UnloadImage;
	void *ExitBootServices;
	void *GetNextMonotonicCount;
	EFI_STATUS (*Stall)(uint64_t Microseconds);
	EFI_STATUS (*SetWatchdogTimer)(uint64_t Timeout, uint64_t WatchdogCode, uint64_t DataSize, uint16_t *WatchdogData);
	void *ConnectController;
	void *DisconnectController;
	void *OpenProtocol;
	void *CloseProtocol;
	void *OpenProtocolInformation;
	void *ProtocolsPerHandle;
	EFI_STATUS (*LocateHandleBuffer)(uint32_t SearchType, EFI_GUID *Protocol, void *SearchKey, uint64_t *NoHandles, EFI_HANDLE **Buffer);
	EFI_STATUS (*LocateProtocol)(EFI_GUID *Protocol, void *Registration, void **Interface);
} EFI_BOOT_SERVICES;

typedef struct {
	EFI_GUID VendorGuid;
	void *VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct {
	EFI_TABLE_HEADER Hdr;
	void *r1[6];
	EFI_STATUS (*GetVariable)(uint16_t *VariableName, EFI_GUID *VendorGuid, uint32_t *Attributes, uint64_t *DataSize, void *Data);
} EFI_RUNTIME_SERVICES;

typedef struct {
	EFI_TABLE_HEADER Hdr;
	uint16_t *FirmwareVendor;
	uint32_t FirmwareRevision;
	EFI_HANDLE ConsoleInHandle;
	EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
	EFI_HANDLE ConsoleOutHandle;
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
	EFI_HANDLE StandardErrorHandle;
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
	EFI_RUNTIME_SERVICES *RuntimeServices;
	EFI_BOOT_SERVICES *BootServices;
	uint64_t NumberOfTableEntries;
	EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct {
	uint32_t Revision;
	EFI_HANDLE ParentHandle;
	EFI_SYSTEM_TABLE *SystemTable;
	EFI_HANDLE DeviceHandle;
	void *FilePath;
	void *Reserved;
	uint32_t LoadOptionsSize;
	void *LoadOptions;
	void *ImageBase;
	uint64_t ImageSize;
} EFI_LOADED_IMAGE_PROTOCOL;

typedef struct {
	uint32_t MediaId;
	uint8_t  RemovableMedia;
	uint8_t  MediaPresent;
	uint8_t  LogicalPartition;
	uint8_t  ReadOnly;
	uint8_t  WriteCaching;
	uint32_t BlockSize;
	uint32_t IoAlign;
	uint64_t LastBlock;
} EFI_BLOCK_IO_MEDIA;

typedef struct _EFI_BLOCK_IO_PROTOCOL {
	uint64_t Revision;
	EFI_BLOCK_IO_MEDIA *Media;
	EFI_STATUS (*Reset)(struct _EFI_BLOCK_IO_PROTOCOL *This, uint8_t ExtendedVerification);
	EFI_STATUS (*ReadBlocks)(struct _EFI_BLOCK_IO_PROTOCOL *This, uint32_t MediaId, uint64_t LBA, uint64_t BufferSize, void *Buffer);
	EFI_STATUS (*WriteBlocks)(struct _EFI_BLOCK_IO_PROTOCOL *This, uint32_t MediaId, uint64_t LBA, uint64_t BufferSize, void *Buffer);
	EFI_STATUS (*FlushBlocks)(struct _EFI_BLOCK_IO_PROTOCOL *This);
} EFI_BLOCK_IO_PROTOCOL;

#define UNUSED __attribute__((unused))

static UNUSED EFI_GUID gLoadedImageGuid  = { 0x5B1B31A1, 0x9562, 0x11d2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } };
static UNUSED EFI_GUID gBlockIoGuid      = { 0x964E5B21, 0x6459, 0x11d2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } };
static UNUSED EFI_GUID gAcpi20Guid       = { 0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } };
static UNUSED EFI_GUID gEfiGlobalVarGuid = { 0x8be4df61, 0x93ca, 0x11d2, { 0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c } };

typedef struct {
	uint32_t Version;
	uint32_t HorizontalResolution;
	uint32_t VerticalResolution;
	uint32_t PixelFormat;
	uint32_t RedMask;
	uint32_t GreenMask;
	uint32_t BlueMask;
	uint32_t ReservedMask;
	uint32_t PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
	uint32_t MaxMode;
	uint32_t Mode;
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
	uint64_t SizeOfInfo;
	uint64_t FrameBufferBase;
	uint64_t FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {
	void *QueryMode;
	EFI_STATUS (*SetMode)(struct _EFI_GRAPHICS_OUTPUT_PROTOCOL *This, uint32_t ModeNumber);
	EFI_STATUS (*Blt)(struct _EFI_GRAPHICS_OUTPUT_PROTOCOL *This, void *BltBuffer,
			  uint32_t BltOperation, uint64_t SrcX, uint64_t SrcY,
			  uint64_t DstX, uint64_t DstY, uint64_t Width, uint64_t Height,
			  uint64_t Delta);
	EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

static UNUSED EFI_GUID gGopGuid = {
	0x9042a9deU, 0x23dcU, 0x4a38U, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a }
};

static EFI_SYSTEM_TABLE *gST;
static EFI_BOOT_SERVICES *gBS;
static uint8_t g_mirror_conout = 1;

/* Direct COM1 (0x3f8) polled UART helpers */
static void com1_putc_raw(uint8_t c)
{
	while ((inb(COM1_BASE + 5) & 0x20) == 0)
		;
	outb(c, COM1_BASE + 0);
}

static int com1_tstc(void)
{
	return (inb(COM1_BASE + 5) & 0x01) != 0;
}

static uint8_t com1_getc_blocking(void)
{
	while ((inb(COM1_BASE + 5) & 0x01) == 0)
		;
	return inb(COM1_BASE + 0);
}

static void com1_drain_rx(void)
{
	while (com1_tstc())
		(void)inb(COM1_BASE + 0);
}

static void udelay_approx(uint32_t us)
{
	if (gBS && gBS->Stall) {
		gBS->Stall(us);
		return;
	}
	for (uint32_t i = 0; i < us; i++)
		io_wait();
}

/* Output helpers (static buffers keep stack tiny) */
static void out_char(char c)
{
	static uint16_t ubuf[2];
	if (c == '\n')
		out_char('\r');
	if (g_mirror_conout && gST && gST->ConOut) {
		ubuf[0] = (uint16_t)(uint8_t)c;
		ubuf[1] = 0;
		gST->ConOut->OutputString(gST->ConOut, ubuf);
	} else {
		com1_putc_raw((uint8_t)c);
	}
}

static void out_str(const char *s)
{
	while (*s)
		out_char(*s++);
}

static void out_hex32(uint32_t v)
{
	static const char hex[] = "0123456789abcdef";
	for (int i = 28; i >= 0; i -= 4)
		out_char(hex[(v >> i) & 0xf]);
}

static UNUSED void out_hex64(uint64_t v)
{
	out_hex32((uint32_t)(v >> 32));
	out_hex32((uint32_t)v);
}

static void out_hex16(uint16_t v)
{
	static const char hex[] = "0123456789abcdef";
	for (int i = 12; i >= 0; i -= 4)
		out_char(hex[(v >> i) & 0xf]);
}

static void out_hex8(uint8_t v)
{
	static const char hex[] = "0123456789abcdef";
	out_char(hex[(v >> 4) & 0xf]);
	out_char(hex[v & 0xf]);
}

static void out_dec(uint32_t v)
{
	char buf[12];
	int i = 0;
	if (v == 0) {
		out_char('0');
		return;
	}
	while (v && i < 11) {
		buf[i++] = (char)('0' + (v % 10));
		v /= 10;
	}
	while (i > 0)
		out_char(buf[--i]);
}

/* PCI configuration space helpers (CF8/CFC + dynamic PCIEXBAR for >=0x100) */
static uint64_t g_pciexbar = 0;

static uint32_t pci_cf8_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg)
{
	uint32_t addr = 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
			((uint32_t)fn << 8) | (reg & 0xfcU);
	outl(addr, 0xcf8);
	return inl(0xcfc);
}

static void pci_cf8_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint32_t val)
{
	uint32_t addr = 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
			((uint32_t)fn << 8) | (reg & 0xfcU);
	outl(addr, 0xcf8);
	outl(val, 0xcfc);
}

static void pci_init_mmcfg(void)
{
	uint32_t lo = pci_cf8_read32(0, 0, 0, 0x60);
	if (lo & 1U)
		g_pciexbar = (uint64_t)(lo & 0xf0000000U);
	else
		g_pciexbar = 0xe0000000ULL;
}

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t reg)
{
	if (reg < 0x100)
		return pci_cf8_read32(bus, dev, fn, (uint8_t)reg);
	if (!g_pciexbar)
		pci_init_mmcfg();
	return mmio_read32(g_pciexbar + ((uint64_t)bus << 20) + ((uint64_t)dev << 15) +
			   ((uint64_t)fn << 12) + (reg & 0xffcU));
}

static UNUSED void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t reg, uint32_t val)
{
	if (reg < 0x100) {
		pci_cf8_write32(bus, dev, fn, (uint8_t)reg, val);
		return;
	}
	if (!g_pciexbar)
		pci_init_mmcfg();
	mmio_write32(g_pciexbar + ((uint64_t)bus << 20) + ((uint64_t)dev << 15) +
		     ((uint64_t)fn << 12) + (reg & 0xffcU), val);
}

static uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t reg)
{
	uint32_t v = pci_read32(bus, dev, fn, reg & ~3U);
	return (uint16_t)((v >> ((reg & 2U) * 8U)) & 0xffffU);
}

static uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t reg)
{
	uint32_t v = pci_read32(bus, dev, fn, reg & ~3U);
	return (uint8_t)((v >> ((reg & 3U) * 8U)) & 0xffU);
}

static void pci_write8(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint8_t val)
{
	uint32_t shift = (reg & 3U) * 8U;
	uint32_t v = pci_cf8_read32(bus, dev, fn, reg & ~3U);
	v = (v & ~(0xffU << shift)) | ((uint32_t)val << shift);
	pci_cf8_write32(bus, dev, fn, reg & ~3U, v);
}

static UNUSED void pci_write16(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t reg, uint16_t val)
{
	if (reg < 0x100) {
		uint32_t addr = 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
				((uint32_t)fn << 8) | (reg & 0xfcU);
		outl(addr, 0xcf8);
		outw(val, (uint16_t)(0xcfc + (reg & 2U)));
		return;
	}
	if (!g_pciexbar)
		pci_init_mmcfg();
	mmio_write16(g_pciexbar + ((uint64_t)bus << 20) + ((uint64_t)dev << 15) +
		     ((uint64_t)fn << 12) + (reg & 0xffeU), val);
}

/* IOBP read via RCBA+0x2330..0x233a (mirrors coreboot iobp.c) */
static uint32_t pch_iobp_read(uint32_t addr, uint16_t *out_sts)
{
	for (int i = 0; i < 1000; i++) {
		if ((mmio_read16(RCBA_BASE + 0x2338) & 1U) == 0)
			break;
		udelay_approx(10);
	}
	mmio_write32(RCBA_BASE + 0x2330, addr);
	uint16_t st = (mmio_read16(RCBA_BASE + 0x2338) & 0x00ffU) | 0x0600U;
	mmio_write16(RCBA_BASE + 0x2338, st);
	mmio_write16(RCBA_BASE + 0x233a, 0xf000U);
	mmio_write16(RCBA_BASE + 0x2338, st | 0x0001U);
	for (int i = 0; i < 1000; i++) {
		if ((mmio_read16(RCBA_BASE + 0x2338) & 1U) == 0)
			break;
		udelay_approx(10);
	}
	*out_sts = mmio_read16(RCBA_BASE + 0x2338);
	return mmio_read32(RCBA_BASE + 0x2334);
}

/* CRC32 (IEEE 802.3) */
static UNUSED uint32_t crc32_ieee(const uint8_t *data, size_t len)
{
	uint32_t crc = 0xffffffffU;
	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int b = 0; b < 8; b++)
			crc = (crc >> 1) ^ (0xedb88320U & (uint32_t)(-(int32_t)(crc & 1U)));
	}
	return ~crc;
}

/* Compact SHA-256 (FIPS 180-4) using static workspace */
typedef struct {
	uint32_t state[8];
	uint64_t bitlen;
	uint32_t datalen;
	uint8_t  data[64];
} sha256_ctx_t;

static sha256_ctx_t g_sha;
static uint32_t g_sha_m[64];

static const uint32_t sha256_k[64] = {
	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t data[64])
{
	uint32_t a, b, c, d, e, f, g, h, t1, t2;
	for (int i = 0, j = 0; i < 16; i++, j += 4)
		g_sha_m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j+1] << 16) |
			     ((uint32_t)data[j+2] << 8) | data[j+3];
	for (int i = 16; i < 64; i++) {
		uint32_t s0 = ROR32(g_sha_m[i-15], 7) ^ ROR32(g_sha_m[i-15], 18) ^ (g_sha_m[i-15] >> 3);
		uint32_t s1 = ROR32(g_sha_m[i-2], 17) ^ ROR32(g_sha_m[i-2], 19) ^ (g_sha_m[i-2] >> 10);
		g_sha_m[i] = g_sha_m[i-16] + s0 + g_sha_m[i-7] + s1;
	}
	a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
	e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
	for (int i = 0; i < 64; i++) {
		uint32_t S1 = ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25);
		uint32_t ch = (e & f) ^ ((~e) & g);
		t1 = h + S1 + ch + sha256_k[i] + g_sha_m[i];
		uint32_t S0 = ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22);
		uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
		t2 = S0 + maj;
		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}
	ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
	ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx_t *ctx)
{
	ctx->datalen = 0;
	ctx->bitlen = 0;
	ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
	ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
	ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
	ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		ctx->data[ctx->datalen++] = data[i];
		if (ctx->datalen == 64) {
			sha256_transform(ctx, ctx->data);
			ctx->bitlen += 512;
			ctx->datalen = 0;
		}
	}
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t hash[32])
{
	uint32_t i = ctx->datalen;
	if (ctx->datalen < 56) {
		ctx->data[i++] = 0x80;
		while (i < 56) ctx->data[i++] = 0x00;
	} else {
		ctx->data[i++] = 0x80;
		while (i < 64) ctx->data[i++] = 0x00;
		sha256_transform(ctx, ctx->data);
		for (int z = 0; z < 56; z++) ctx->data[z] = 0x00;
	}
	ctx->bitlen += (uint64_t)ctx->datalen * 8ULL;
	for (int b = 0; b < 8; b++)
		ctx->data[63 - b] = (uint8_t)(ctx->bitlen >> (b * 8));
	sha256_transform(ctx, ctx->data);
	for (int s = 0; s < 8; s++) {
		hash[s*4 + 0] = (uint8_t)(ctx->state[s] >> 24);
		hash[s*4 + 1] = (uint8_t)(ctx->state[s] >> 16);
		hash[s*4 + 2] = (uint8_t)(ctx->state[s] >> 8);
		hash[s*4 + 3] = (uint8_t)(ctx->state[s]);
	}
}

static void out_sha256(const uint8_t h[32])
{
	for (int i = 0; i < 32; i++)
		out_hex8(h[i]);
}

/* Intel 9-series PCH SPI Hardware Sequencing (SPIBAR = 0xfed1f800) */
#define SPI_HSFS  (SPIBAR_BASE + 0x04)
#define SPI_HSFC  (SPIBAR_BASE + 0x06)
#define SPI_FADDR (SPIBAR_BASE + 0x08)
#define SPI_FDATA (SPIBAR_BASE + 0x10)
#define SPI_FRAP  (SPIBAR_BASE + 0x50)
#define SPI_FREG0 (SPIBAR_BASE + 0x54)
#define SPI_FREG1 (SPIBAR_BASE + 0x58)
#define SPI_FREG2 (SPIBAR_BASE + 0x5c)
#define SPI_FREG3 (SPIBAR_BASE + 0x60)
#define SPI_PR0   (SPIBAR_BASE + 0x74)

#define HSFS_FDONE (1U << 0)
#define HSFS_FCERR (1U << 1)
#define HSFS_AEL   (1U << 2)
#define HSFS_SCIP  (1U << 5)
#define HSFS_FDOPSS (1U << 13)
#define HSFS_FDV   (1U << 14)

static int spi_wait_scip_clear(void)
{
	for (uint32_t i = 0; i < 500000; i++) {
		if ((mmio_read16(SPI_HSFS) & HSFS_SCIP) == 0)
			return 0;
		udelay_approx(4);
	}
	return -1;
}

static int spi_wait_cycle_done(uint32_t timeout_us)
{
	uint32_t tries = timeout_us / 4U + 1U;
	uint16_t hsfs = 0;
	while (tries--) {
		hsfs = mmio_read16(SPI_HSFS);
		if (hsfs & (HSFS_FDONE | HSFS_FCERR | HSFS_AEL))
			break;
		udelay_approx(4);
	}
	/* Clear W1C status bits */
	mmio_write16(SPI_HSFS, HSFS_FDONE | HSFS_FCERR | HSFS_AEL);
	if ((hsfs & (HSFS_FCERR | HSFS_AEL)) || (hsfs & HSFS_FDONE) == 0)
		return -1;
	return 0;
}

static int spi_check_ready(int verbose)
{
	uint8_t bc = pci_read8(0, 0x1f, 0, 0xdc);
	uint16_t hsfs = mmio_read16(SPI_HSFS);
	uint32_t frap = mmio_read32(SPI_FRAP);
	uint32_t freg1 = mmio_read32(SPI_FREG1);

	if (verbose) {
		out_str("SPI: BIOS_CNTL=0x"); out_hex8(bc);
		out_str(" HSFS=0x"); out_hex16(hsfs);
		out_str(" FRAP=0x"); out_hex32(frap);
		out_str(" FREG1=0x"); out_hex32(freg1);
		out_str("\n");
	}

	/* Ensure BIOSWE=1 if not locked by BLE/SMM_BWP */
	if ((bc & 1U) == 0) {
		if (bc & 0x22U) {
			if (verbose) out_str("SPI ERROR: BIOS_CNTL BLE/SMM_BWP set!\n");
			return -1;
		}
		pci_write8(0, 0x1f, 0, 0xdc, bc | 1U);
		bc = pci_read8(0, 0x1f, 0, 0xdc);
		if ((bc & 1U) == 0) {
			if (verbose) out_str("SPI ERROR: Cannot set BIOSWE!\n");
			return -1;
		}
	}

	if ((hsfs & HSFS_FDV) == 0 || (hsfs & HSFS_FDOPSS) == 0) {
		if (verbose) out_str("SPI ERROR: FDV or FDOPSS not valid\n");
		return -1;
	}

	for (int p = 0; p < 5; p++) {
		uint32_t pr = mmio_read32(SPI_PR0 + (uint64_t)p * 4U);
		if (pr & 0x80008000U) {
			if (verbose) {
				out_str("SPI ERROR: PR"); out_dec((uint32_t)p);
				out_str(" active: 0x"); out_hex32(pr); out_str("\n");
			}
			return -1;
		}
	}

	return 0;
}

static int spi_hwseq_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
	if (addr >= BIOS_REGION_END || len > BIOS_REGION_END - addr)
		return -1;
	if (spi_wait_scip_clear() != 0)
		return -1;
	mmio_write16(SPI_HSFS, HSFS_FDONE | HSFS_FCERR | HSFS_AEL);

	while (len > 0) {
		uint32_t chunk = (len > 64U) ? 64U : len;
		uint32_t to_256 = (~addr & 0xffU) + 1U;
		if (chunk > to_256)
			chunk = to_256;

		uint32_t faddr = (mmio_read32(SPI_FADDR) & ~0x01ffffffU) | (addr & 0x01ffffffU);
		mmio_write32(SPI_FADDR, faddr);

		uint16_t hsfc = mmio_read16(SPI_HSFC);
		hsfc &= (uint16_t)~((0x3fU << 8) | (0x7U << 1));
		hsfc |= (uint16_t)(((chunk - 1U) << 8) | (0U << 1) | 1U);
		mmio_write16(SPI_HSFC, hsfc);

		if (spi_wait_cycle_done(200000U) != 0)
			return -1;

		uint32_t tmp = 0;
		for (uint32_t i = 0; i < chunk; i++) {
			if ((i & 3U) == 0)
				tmp = mmio_read32(SPI_FDATA + (i & ~3U));
			buf[i] = (uint8_t)(tmp >> ((i & 3U) * 8U));
		}

		addr += chunk;
		buf += chunk;
		len -= chunk;
	}
	return 0;
}

static int spi_hwseq_erase_4k(uint32_t addr)
{
	/* Strict BIOS region guard */
	if (addr < BIOS_REGION_BASE || addr >= BIOS_REGION_END || (addr & 0xfffU) != 0)
		return -1;
	if (spi_wait_scip_clear() != 0)
		return -1;
	mmio_write16(SPI_HSFS, HSFS_FDONE | HSFS_FCERR | HSFS_AEL);

	uint32_t faddr = (mmio_read32(SPI_FADDR) & ~0x01ffffffU) | (addr & 0x01ffffffU);
	mmio_write32(SPI_FADDR, faddr);

	uint16_t hsfc = mmio_read16(SPI_HSFC);
	hsfc &= (uint16_t)~(0x7U << 1);
	hsfc |= (uint16_t)((3U << 1) | 1U); /* FCYCLE=3 (Erase) | FGO */
	mmio_write16(SPI_HSFC, hsfc);

	return spi_wait_cycle_done(2000000U);
}

static int spi_hwseq_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
	/* Strict BIOS region guard */
	if (addr < BIOS_REGION_BASE || addr >= BIOS_REGION_END || len > BIOS_REGION_END - addr)
		return -1;
	if (spi_wait_scip_clear() != 0)
		return -1;
	mmio_write16(SPI_HSFS, HSFS_FDONE | HSFS_FCERR | HSFS_AEL);

	while (len > 0) {
		uint32_t chunk = (len > 64U) ? 64U : len;
		uint32_t to_256 = (~addr & 0xffU) + 1U;
		if (chunk > to_256)
			chunk = to_256;

		for (uint32_t i = 0; i < chunk; i += 4U) {
			uint32_t w = 0;
			for (uint32_t b = 0; b < 4U && (i + b) < chunk; b++)
				w |= ((uint32_t)buf[i + b]) << (b * 8U);
			mmio_write32(SPI_FDATA + i, w);
		}

		uint32_t faddr = (mmio_read32(SPI_FADDR) & ~0x01ffffffU) | (addr & 0x01ffffffU);
		mmio_write32(SPI_FADDR, faddr);

		uint16_t hsfc = mmio_read16(SPI_HSFC);
		hsfc &= (uint16_t)~((0x3fU << 8) | (0x7U << 1));
		hsfc |= (uint16_t)(((chunk - 1U) << 8) | (2U << 1) | 1U); /* FCYCLE=2 (Write) | FGO */
		mmio_write16(SPI_HSFC, hsfc);

		if (spi_wait_cycle_done(200000U) != 0)
			return -1;

		addr += chunk;
		buf += chunk;
		len -= chunk;
	}
	return 0;
}

static uint8_t g_sec_buf[SECTOR_SIZE];
static uint8_t g_sec_vrf[SECTOR_SIZE];
static uint8_t g_hash_out[32];

static int spi_compute_bios_sha256(uint8_t out_hash[32])
{
	sha256_init(&g_sha);
	for (uint32_t off = BIOS_REGION_BASE; off < BIOS_REGION_END; off += SECTOR_SIZE) {
		if (spi_hwseq_read(off, g_sec_buf, SECTOR_SIZE) != 0)
			return -1;
		sha256_update(&g_sha, g_sec_buf, SECTOR_SIZE);
	}
	sha256_final(&g_sha, out_hash);
	return 0;
}

static int spi_write_and_verify_sector(uint32_t addr, const uint8_t *data)
{
	for (int attempt = 0; attempt < 2; attempt++) {
		if (spi_hwseq_erase_4k(addr) != 0)
			continue;
		if (spi_hwseq_write(addr, data, SECTOR_SIZE) != 0)
			continue;
		if (spi_hwseq_read(addr, g_sec_vrf, SECTOR_SIZE) != 0)
			continue;
		if (memcmp(g_sec_vrf, data, SECTOR_SIZE) == 0)
			return 0;
	}
	return -1;
}

static UNUSED int spi_flash_bios_delta(const uint8_t *img, uint32_t len, const uint8_t expected_sha[32])
{
	if (len != BIOS_REGION_SIZE) {
		out_str("FLASH ERROR: bad BIOS length\n");
		return -1;
	}
	if (spi_check_ready(1) != 0)
		return -1;

	sha256_init(&g_sha);
	sha256_update(&g_sha, img, len);
	sha256_final(&g_sha, g_hash_out);
	out_str("RAM BIOS SHA-256: "); out_sha256(g_hash_out); out_str("\n");
	if (memcmp(g_hash_out, expected_sha, 32) != 0) {
		out_str("FLASH ERROR: embedded/RAM image SHA-256 mismatch!\n");
		return -1;
	}

	uint32_t changed = 0, skipped = 0;
	out_str("Delta flashing FMAP+COREBOOT 0x210000..0x7fffff (1520 sectors)...\n");
	for (uint32_t s = 0; s < NUM_SECTORS; s++) {
		uint32_t addr = BIOS_REGION_BASE + s * SECTOR_SIZE;
		const uint8_t *target = img + s * SECTOR_SIZE;
		if (spi_hwseq_read(addr, g_sec_buf, SECTOR_SIZE) != 0) {
			out_str("\nREAD ERROR @ 0x"); out_hex32(addr); out_str("\n");
			return -1;
		}
		if (memcmp(g_sec_buf, target, SECTOR_SIZE) == 0) {
			skipped++;
		} else {
			if (spi_write_and_verify_sector(addr, target) != 0) {
				out_str("\nWRITE/VERIFY ERROR @ 0x"); out_hex32(addr); out_str("\n");
				return -1;
			}
			changed++;
		}
		if ((s & 63U) == 63U)
			out_char('.');
	}
	out_str("\nDelta summary: updated="); out_dec(changed);
	out_str(" skipped="); out_dec(skipped); out_str("\n");

	out_str("Computing full FMAP+COREBOOT SPI readback SHA-256...\n");
	if (spi_compute_bios_sha256(g_hash_out) != 0) {
		out_str("VERIFY READ ERROR\n");
		return -1;
	}
	out_str("SPI BIOS SHA-256: "); out_sha256(g_hash_out); out_str("\n");
	if (memcmp(g_hash_out, expected_sha, 32) != 0) {
		out_str("Z97A_FLASH_VERIFY_FAILED\n");
		return -1;
	}
	out_str("Z97A_FLASH_VERIFIED_OK\n");
	return 0;
}

/* Pre-OS Hardware Diagnostic & 16 MiB Gen3 AHCI Stress Test */
static uint8_t g_blk_buf[65536];

static void print_reg_pair(const char *label, uint32_t val, uint32_t stock)
{
	out_str("  "); out_str(label); out_str(": 0x"); out_hex32(val);
	if (val == stock)
		out_str(" (OK == stock)");
	else {
		out_str(" (DIFF! stock=0x"); out_hex32(stock); out_str(")");
	}
	out_str("\n");
}

static UNUSED uint64_t pci_read_bar64(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg)
{
	uint32_t lo = pci_read32(bus, dev, fn, reg);
	if (lo == 0 || lo == 0xffffffffU)
		return 0;
	if ((lo & 0x06U) == 0x04U) {
		uint32_t hi = pci_read32(bus, dev, fn, (uint16_t)(reg + 4U));
		return (((uint64_t)hi) << 32) | (lo & ~0xfULL);
	}
	return (uint64_t)(lo & ~0xfULL);
}

static UNUSED uint8_t pci_find_cap(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t target_id)
{
	if ((pci_read16(bus, dev, fn, 0x06) & 0x10U) == 0)
		return 0;
	uint8_t pos = pci_read8(bus, dev, fn, 0x34) & 0xfcU;
	for (int steps = 0; steps < 48 && pos >= 0x40U; steps++) {
		uint8_t id = pci_read8(bus, dev, fn, pos);
		if (id == target_id)
			return pos;
		uint8_t nxt = pci_read8(bus, dev, fn, (uint16_t)(pos + 1U)) & 0xfcU;
		if (nxt == 0 || nxt == pos)
			break;
		pos = nxt;
	}
	return 0;
}

static UNUSED uint8_t sio_read(uint8_t reg)
{
	outb(reg, 0x2e);
	return inb(0x2f);
}

static UNUSED uint8_t hwm_read(uint16_t base, uint8_t bank, uint8_t reg)
{
	outb(0x4e, (uint16_t)(base + 5U));
	outb(bank, (uint16_t)(base + 6U));
	outb(reg, (uint16_t)(base + 5U));
	return inb((uint16_t)(base + 6U));
}

static UNUSED void out_mac_from_ral_rah(uint32_t ral, uint32_t rah)
{
	out_hex8((uint8_t)(ral & 0xffU)); out_char(':');
	out_hex8((uint8_t)((ral >> 8) & 0xffU)); out_char(':');
	out_hex8((uint8_t)((ral >> 16) & 0xffU)); out_char(':');
	out_hex8((uint8_t)((ral >> 24) & 0xffU)); out_char(':');
	out_hex8((uint8_t)(rah & 0xffU)); out_char(':');
	out_hex8((uint8_t)((rah >> 8) & 0xffU));
}

static UNUSED void run_diag(void)
{
	out_str("\n=== Z97-A PRE-OS HARDWARE DIAGNOSTIC (FULL BOARD) ===\n");
	pci_init_mmcfg();

	/* Section A: Full PCI Bus Scan (bus 0..8) */
	uint8_t i226_found = 0, i226_bus = 0, i226_dev = 0, i226_fn = 0;
	out_str("PCI Scan (bus 0..8):\n");
	for (uint8_t bus = 0; bus <= 8; bus++) {
		for (uint8_t dev = 0; dev < 32; dev++) {
			uint16_t vid0 = pci_read16(bus, dev, 0, 0x00);
			if (vid0 == 0xffffU)
				continue;
			uint8_t max_fn = (pci_read8(bus, dev, 0, 0x0e) & 0x80U) ? 8U : 1U;
			for (uint8_t fn = 0; fn < max_fn; fn++) {
				uint16_t vid = (fn == 0) ? vid0 : pci_read16(bus, dev, fn, 0x00);
				if (vid == 0xffffU)
					continue;
				uint16_t did = pci_read16(bus, dev, fn, 0x02);
				uint16_t cmd = pci_read16(bus, dev, fn, 0x04);
				uint32_t cls_rev = pci_read32(bus, dev, fn, 0x08);
				uint16_t svid = pci_read16(bus, dev, fn, 0x2c);
				uint16_t sdid = pci_read16(bus, dev, fn, 0x2e);
				uint8_t iline = pci_read8(bus, dev, fn, 0x3c);
				uint8_t ipin = pci_read8(bus, dev, fn, 0x3d);
				out_str("  "); out_hex8(bus); out_char(':'); out_hex8(dev); out_char('.'); out_dec(fn);
				out_str(" "); out_hex16(vid); out_char(':'); out_hex16(did);
				out_str(" rev="); out_hex8((uint8_t)(cls_rev & 0xffU));
				out_str(" cls="); out_hex8((uint8_t)(cls_rev >> 24));
				out_hex8((uint8_t)(cls_rev >> 16));
				out_hex8((uint8_t)(cls_rev >> 8));
				out_str(" sub="); out_hex16(svid); out_char(':'); out_hex16(sdid);
				out_str(" cmd="); out_hex16(cmd);
				out_str(" irq="); out_dec(ipin); out_char('/'); out_dec(iline);
				out_str("\n");
				if (!i226_found && vid == 0x8086U && did == 0x125cU) {
					i226_found = 1;
					i226_bus = bus; i226_dev = dev; i226_fn = fn;
				}
			}
		}
	}

	/* Section B: Northbridge, VT-d/IOMMU, Power & CPU MSRs */
	uint32_t pavpc = pci_read32(0, 0, 0, 0x58);
	uint32_t tolud = pci_read32(0, 0, 0, 0xbc);
	uint32_t bdsm  = pci_read32(0, 0, 0, 0xb0);
	uint32_t bgsm  = pci_read32(0, 0, 0, 0xb4);
	uint32_t tseg  = pci_read32(0, 0, 0, 0xb8);
	uint32_t deven = pci_read32(0, 0, 0, 0x54);
	uint16_t ggc   = pci_read16(0, 0, 0, 0x50);
	uint64_t touud = ((uint64_t)pci_read32(0, 0, 0, 0xac) << 32) | pci_read32(0, 0, 0, 0xa8);
	uint32_t mch5998 = mmio_read32(0xfed15998ULL);
	out_str("Northbridge & VT-d:\n");
	out_str("  TOLUD=0x"); out_hex32(tolud);
	out_str(" BDSM=0x"); out_hex32(bdsm);
	out_str(" BGSM=0x"); out_hex32(bgsm);
	out_str(" TSEG=0x"); out_hex32(tseg);
	out_str(" PAVPC=0x"); out_hex32(pavpc);
	out_str(" MCH+5998=0x"); out_hex32(mch5998); out_str("\n");
	out_str("  DEVEN=0x"); out_hex32(deven);
	out_str(" GGC=0x"); out_hex16(ggc);
	out_str(" TOUUD=0x"); out_hex64(touud); out_str("\n");
	print_reg_pair("MCH GFXVTBAR(5400)", mmio_read32(0xfed15400ULL), 0xfed90001U);
	print_reg_pair("MCH VTVC0BAR(5410)", mmio_read32(0xfed15410ULL), 0xfed91001U);
	print_reg_pair("MCH +5418         ", mmio_read32(0xfed15418ULL), 0x00000004U);
	print_reg_pair("MCH +5880         ", mmio_read32(0xfed15880ULL), 0x00000060U);
	print_reg_pair("MCH +5f00         ", mmio_read32(0xfed15f00ULL), 0x00002707U);
	out_str("  MCH PKG_PL(59a0/59a4)="); out_hex32(mmio_read32(0xfed159a0ULL));
	out_str("/"); out_hex32(mmio_read32(0xfed159a4ULL)); out_str("\n");
	out_str("CPU MSRs:\n");
	out_str("  FEAT_CTL(3a)="); out_hex64(rdmsr64(0x3a));
	out_str(" PLAT_INFO(ce)="); out_hex64(rdmsr64(0xce));
	out_str(" TURBO_LIM(1ad)="); out_hex64(rdmsr64(0x1ad)); out_str("\n");
	out_str("  FLEX(194)="); out_hex64(rdmsr64(0x194));
	out_str(" TEMP_TGT(1a2)="); out_hex64(rdmsr64(0x1a2));
	out_str(" PERF_STS(198)="); out_hex64(rdmsr64(0x198)); out_str("\n");
	out_str("  PERF_CTL(199)="); out_hex64(rdmsr64(0x199));
	out_str(" PKG_PL(610)="); out_hex64(rdmsr64(0x610)); out_str("\n");

	/* Section C: IGD & Full Display MMIO */
	if (pci_read16(0, 2, 0, 0x00) == 0x8086) {
		uint16_t igd_cmd = pci_read16(0, 2, 0, 0x04);
		uint64_t gmadr = pci_read_bar64(0, 2, 0, 0x10);
		uint64_t fb_bar = pci_read_bar64(0, 2, 0, 0x18);
		out_str("  IGD CMD=0x"); out_hex16(igd_cmd);
		out_str(" GTTMMADR=0x"); out_hex32((uint32_t)gmadr);
		out_str(" GMADR=0x"); out_hex32((uint32_t)fb_bar);
		if (gmadr && (igd_cmd & 2U)) {
			out_str(" RP1(a008)=0x"); out_hex32(mmio_read32(gmadr + 0xa008));
			out_str(" AUXB(64010)=0x"); out_hex32(mmio_read32(gmadr + 0x64010));
			out_str(" AUXC(64810)=0x"); out_hex32(mmio_read32(gmadr + 0x64810));
			out_str("\n  Display MMIO:\n");
			out_str("    DDI_BUF_CTL B="); out_hex32(mmio_read32(gmadr + 0x64100));
			out_str(" C="); out_hex32(mmio_read32(gmadr + 0x64200));
			out_str(" D="); out_hex32(mmio_read32(gmadr + 0x64300)); out_str("\n");
			out_str("    HDMI_TRANS9 B="); out_hex32(mmio_read32(gmadr + 0x64ea8));
			out_str("/"); out_hex32(mmio_read32(gmadr + 0x64eac));
			out_str(" C="); out_hex32(mmio_read32(gmadr + 0x64f08));
			out_str("/"); out_hex32(mmio_read32(gmadr + 0x64f0c));
			out_str(" D="); out_hex32(mmio_read32(gmadr + 0x64f68));
			out_str("/"); out_hex32(mmio_read32(gmadr + 0x64f6c)); out_str("\n");
			out_str("    TRANS_DDI A="); out_hex32(mmio_read32(gmadr + 0x60400));
			out_str(" B="); out_hex32(mmio_read32(gmadr + 0x61400));
			out_str(" C="); out_hex32(mmio_read32(gmadr + 0x62400)); out_str("\n");
			out_str("    PIPEA CONF="); out_hex32(mmio_read32(gmadr + 0x70008));
			out_str(" DSP="); out_hex32(mmio_read32(gmadr + 0x70180));
			out_str(" STRIDE="); out_hex32(mmio_read32(gmadr + 0x70188));
			out_str(" SURF="); out_hex32(mmio_read32(gmadr + 0x7019c)); out_str("\n");
			out_str("    PIPEA HTOT="); out_hex32(mmio_read32(gmadr + 0x60000));
			out_str(" HBLK="); out_hex32(mmio_read32(gmadr + 0x60004));
			out_str(" HSYNC="); out_hex32(mmio_read32(gmadr + 0x60008));
			out_str(" VTOT="); out_hex32(mmio_read32(gmadr + 0x6000c));
			out_str(" VBLK="); out_hex32(mmio_read32(gmadr + 0x60010));
			out_str(" VSYNC="); out_hex32(mmio_read32(gmadr + 0x60014));
			out_str(" SRCSZ="); out_hex32(mmio_read32(gmadr + 0x6001c)); out_str("\n");
			uint32_t frm0 = mmio_read32(gmadr + 0x70040);
			udelay_approx(100000U);
			uint32_t frm1 = mmio_read32(gmadr + 0x70040);
			out_str("    PIPEA FRM0="); out_hex32(frm0);
			out_str(" FRM1="); out_hex32(frm1);
			out_str(" dFRM="); out_dec(frm1 - frm0); out_str("\n");
			out_str("    WRPLL1="); out_hex32(mmio_read32(gmadr + 0x46040));
			out_str(" WRPLL2="); out_hex32(mmio_read32(gmadr + 0x46060));
			out_str(" PCLK_SEL B="); out_hex32(mmio_read32(gmadr + 0x46104));
			out_str(" C="); out_hex32(mmio_read32(gmadr + 0x46108));
			out_str(" D="); out_hex32(mmio_read32(gmadr + 0x4610c)); out_str("\n");
			out_str("    LCPLL="); out_hex32(mmio_read32(gmadr + 0x130040));
			out_str(" CDCLK_FREQ="); out_hex32(mmio_read32(gmadr + 0x46200));
			out_str(" PIPEA_CLK="); out_hex32(mmio_read32(gmadr + 0x46140));
			out_str(" RAWCLK="); out_hex32(mmio_read32(gmadr + 0xc6204)); out_str("\n");
			out_str("    DEIIR="); out_hex32(mmio_read32(gmadr + 0x44008));
			out_str(" SERR_INT="); out_hex32(mmio_read32(gmadr + 0x44040));
			out_str(" DISP_ARB="); out_hex32(mmio_read32(gmadr + 0x45000));
			out_str(" PWR_BIOS="); out_hex32(mmio_read32(gmadr + 0x45400));
			out_str(" PWR_DRV="); out_hex32(mmio_read32(gmadr + 0x45404)); out_str("\n");
			out_str("    WM_PIPE_A="); out_hex32(mmio_read32(gmadr + 0x45100));
			out_str(" LP1="); out_hex32(mmio_read32(gmadr + 0x45108));
			out_str(" LP2="); out_hex32(mmio_read32(gmadr + 0x4510c));
			out_str(" LP3="); out_hex32(mmio_read32(gmadr + 0x45110));
			out_str(" WMLT_A="); out_hex32(mmio_read32(gmadr + 0x45270)); out_str("\n");
			out_str("    DIP_CTL_A="); out_hex32(mmio_read32(gmadr + 0x60200));
			out_str(" AVI0="); out_hex32(mmio_read32(gmadr + 0x60220));
			out_str(" AVI1="); out_hex32(mmio_read32(gmadr + 0x60224));
			out_str(" AVI2="); out_hex32(mmio_read32(gmadr + 0x60228)); out_str("\n");
			out_str("    SFUSE="); out_hex32(mmio_read32(gmadr + 0xc2014));
			out_str(" SHOTPLUG="); out_hex32(mmio_read32(gmadr + 0xc4030));
			out_str(" BMU="); out_hex32(mmio_read32(gmadr + 0x6c00c));
		}
		out_str("\n");
	}

	/* Section D: HD Audio (00:03.0 Haswell HDMI + 00:1b.0 PCH ALC892) */
	out_str("HD Audio:\n");
	static const uint8_t hda_devs[2] = { 0x03, 0x1b };
	for (int i = 0; i < 2; i++) {
		uint8_t d = hda_devs[i];
		uint16_t vid = pci_read16(0, d, 0, 0x00);
		if (vid != 0x8086U) {
			out_str("  00:"); out_hex8(d); out_str(".0 ABSENT\n");
			continue;
		}
		uint16_t cmd = pci_read16(0, d, 0, 0x04);
		uint64_t hdbar = pci_read_bar64(0, d, 0, 0x10);
		out_str("  00:"); out_hex8(d); out_str(".0 CMD="); out_hex16(cmd);
		out_str(" HDBAR=0x"); out_hex64(hdbar);
		if (hdbar && (cmd & 2U)) {
			out_str(" GCAP="); out_hex16(mmio_read16(hdbar + 0x00));
			out_str(" GCTL="); out_hex32(mmio_read32(hdbar + 0x08));
			out_str(" STATESTS="); out_hex16(mmio_read16(hdbar + 0x0e));
		}
		out_str("\n");
	}

	/* Section E: USB Controllers (00:14.0 xHCI + 00:1d.0 EHCI #1) */
	out_str("USB Controllers:\n");
	if (pci_read16(0, 0x14, 0, 0x00) == 0x8086U) {
		uint16_t cmd = pci_read16(0, 0x14, 0, 0x04);
		uint64_t xbar = pci_read_bar64(0, 0x14, 0, 0x10);
		out_str("  xHCI 00:14.0 CMD="); out_hex16(cmd);
		out_str(" XUSB2PR="); out_hex32(pci_read32(0, 0x14, 0, 0xd0));
		out_str(" XUSB2PRM="); out_hex32(pci_read32(0, 0x14, 0, 0xd4));
		out_str(" USB3_PSSEN="); out_hex32(pci_read32(0, 0x14, 0, 0xd8));
		out_str(" USB3PRM="); out_hex32(pci_read32(0, 0x14, 0, 0xdc));
		out_str(" BAR0=0x"); out_hex64(xbar); out_str("\n");
		if (xbar && (cmd & 2U)) {
			uint32_t cap0 = mmio_read32(xbar);
			uint32_t caplen = cap0 & 0xffU;
			uint32_t hcs1 = mmio_read32(xbar + 0x04);
			uint32_t maxp = (hcs1 >> 24) & 0xffU;
			if (maxp > 24U) maxp = 24U;
			out_str("    CAPLEN="); out_hex8((uint8_t)caplen);
			out_str(" HCS1="); out_hex32(hcs1);
			out_str(" USBCMD="); out_hex32(mmio_read32(xbar + caplen + 0x00));
			out_str(" USBSTS="); out_hex32(mmio_read32(xbar + caplen + 0x04));
			out_str(" PORTS_CCS:");
			for (uint32_t p = 0; p < maxp; p++) {
				uint32_t ps = mmio_read32(xbar + caplen + 0x400U + p * 0x10U);
				if (ps & 1U) {
					out_str(" p"); out_dec(p + 1U); out_char('='); out_hex32(ps);
				}
			}
			out_str("\n");
		}
	}
	if (pci_read16(0, 0x1d, 0, 0x00) == 0x8086U) {
		uint16_t cmd = pci_read16(0, 0x1d, 0, 0x04);
		uint64_t ebar = pci_read_bar64(0, 0x1d, 0, 0x10);
		out_str("  EHCI 00:1d.0 CMD="); out_hex16(cmd);
		out_str(" BAR0=0x"); out_hex64(ebar);
		if (ebar && (cmd & 2U)) {
			uint32_t caplen = mmio_read32(ebar) & 0xffU;
			out_str(" HCS="); out_hex32(mmio_read32(ebar + 0x04));
			out_str(" USBCMD="); out_hex32(mmio_read32(ebar + caplen + 0x00));
			out_str(" USBSTS="); out_hex32(mmio_read32(ebar + caplen + 0x04));
			out_str(" P0="); out_hex32(mmio_read32(ebar + caplen + 0x44));
			out_str(" P1="); out_hex32(mmio_read32(ebar + caplen + 0x48));
		}
		out_str("\n");
	} else {
		out_str("  EHCI 00:1d.0 ABSENT\n");
	}

	/* Section F: Intel ME (00:16.0) */
	if (pci_read16(0, 0x16, 0, 0x00) == 0x8086U) {
		out_str("Intel ME (00:16.0):\n");
		out_str("  CMD="); out_hex16(pci_read16(0, 0x16, 0, 0x04));
		out_str(" HFS1="); out_hex32(pci_read32(0, 0x16, 0, 0x40));
		out_str(" HFS2="); out_hex32(pci_read32(0, 0x16, 0, 0x48));
		out_str(" HFS3="); out_hex32(pci_read32(0, 0x16, 0, 0x60));
		out_str(" HFS4="); out_hex32(pci_read32(0, 0x16, 0, 0x64));
		out_str(" HFS5="); out_hex32(pci_read32(0, 0x16, 0, 0x68));
		out_str(" HFS6="); out_hex32(pci_read32(0, 0x16, 0, 0x6c)); out_str("\n");
	} else {
		out_str("Intel ME (00:16.0): ABSENT\n");
	}

	/* Section G: Dual Intel NICs (00:19.0 I218-V + I226-V) */
	out_str("NICs:\n");
	if (pci_read16(0, 0x19, 0, 0x00) == 0x8086U) {
		uint16_t cmd = pci_read16(0, 0x19, 0, 0x04);
		uint64_t nbar = pci_read_bar64(0, 0x19, 0, 0x10);
		out_str("  I218-V 00:19.0 CMD="); out_hex16(cmd);
		out_str(" BAR0=0x"); out_hex64(nbar);
		if (nbar && (cmd & 2U)) {
			uint32_t ral = mmio_read32(nbar + 0x5400U);
			uint32_t rah = mmio_read32(nbar + 0x5404U);
			out_str(" CTRL="); out_hex32(mmio_read32(nbar + 0x0000U));
			out_str(" STS="); out_hex32(mmio_read32(nbar + 0x0008U));
			out_str(" MAC="); out_mac_from_ral_rah(ral, rah);
		}
		out_str("\n");
	} else {
		out_str("  I218-V 00:19.0 ABSENT\n");
	}
	if (i226_found) {
		uint16_t cmd = pci_read16(i226_bus, i226_dev, i226_fn, 0x04);
		uint64_t nbar = pci_read_bar64(i226_bus, i226_dev, i226_fn, 0x10);
		uint8_t pcie_cap = pci_find_cap(i226_bus, i226_dev, i226_fn, 0x10);
		out_str("  I226-V "); out_hex8(i226_bus); out_char(':'); out_hex8(i226_dev); out_char('.'); out_dec(i226_fn);
		out_str(" CMD="); out_hex16(cmd);
		out_str(" BAR0=0x"); out_hex64(nbar);
		if (pcie_cap) {
			out_str(" LCTL="); out_hex16(pci_read16(i226_bus, i226_dev, i226_fn, (uint16_t)(pcie_cap + 0x10U)));
			out_str(" LSTS="); out_hex16(pci_read16(i226_bus, i226_dev, i226_fn, (uint16_t)(pcie_cap + 0x12U)));
		}
		if (nbar && (cmd & 2U)) {
			uint32_t ral = mmio_read32(nbar + 0x5400U);
			uint32_t rah = mmio_read32(nbar + 0x5404U);
			out_str(" CTRL="); out_hex32(mmio_read32(nbar + 0x0000U));
			out_str(" STS="); out_hex32(mmio_read32(nbar + 0x0008U));
			out_str(" MAC="); out_mac_from_ral_rah(ral, rah);
		}
		out_str("\n");
	} else {
		out_str("  I226-V ABSENT\n");
	}

	/* Section H: PCH LPC, GPIOs, PMBASE & RCBA */
	uint16_t pmbase = (uint16_t)(pci_read32(0, 0x1f, 0, 0x40) & 0xfffeU);
	uint16_t gpiobase = (uint16_t)(pci_read32(0, 0x1f, 0, 0x48) & 0xfffeU);
	out_str("PCH LPC (00:1f.0):\n");
	out_str("  PMBASE=0x"); out_hex16(pmbase);
	out_str(" GPIOBASE=0x"); out_hex16(gpiobase);
	out_str(" GEN1..4="); out_hex32(pci_read32(0, 0x1f, 0, 0x84));
	out_char('/'); out_hex32(pci_read32(0, 0x1f, 0, 0x88));
	out_char('/'); out_hex32(pci_read32(0, 0x1f, 0, 0x8c));
	out_char('/'); out_hex32(pci_read32(0, 0x1f, 0, 0x90)); out_str("\n");
	out_str("  PMCON1..3="); out_hex16(pci_read16(0, 0x1f, 0, 0xa0));
	out_char('/'); out_hex16(pci_read16(0, 0x1f, 0, 0xa2));
	out_char('/'); out_hex32(pci_read32(0, 0x1f, 0, 0xa4));
	out_str(" BIOS_CNTL=0x"); out_hex8(pci_read8(0, 0x1f, 0, 0xdc)); out_str("\n");

	if (pmbase >= 0x100U) {
		out_str("  PMBASE: PM1_STS="); out_hex16(inw((uint16_t)(pmbase + 0x00U)));
		out_str(" PM1_EN="); out_hex16(inw((uint16_t)(pmbase + 0x02U)));
		out_str(" PM1_CNT="); out_hex32(inl((uint16_t)(pmbase + 0x04U)));
		out_str(" GPE0_EN="); out_hex32(inl((uint16_t)(pmbase + 0x28U)));
		out_str(" SMI_EN="); out_hex32(inl((uint16_t)(pmbase + 0x30U)));
		out_str(" TCO1_CNT="); out_hex16(inw((uint16_t)(pmbase + 0x68U))); out_str("\n");
	}
	if (gpiobase >= 0x100U) {
		static const struct { const char *name; uint16_t off; uint32_t stock; } gpio_chk[] = {
			{ "GPIO_USE_SEL (00)", 0x00U, 0xfbfee1ffU },
			{ "GP_IO_SEL    (04)", 0x04U, 0xceff6effU },
			{ "GP_LVL       (0c)", 0x0cU, 0xaebd7f7cU },
			{ "GPI_INV      (2c)", 0x2cU, 0x00006045U },
			{ "GPIO_USE_SEL2(30)", 0x30U, 0x02fd70ffU },
			{ "GP_IO_SEL2   (34)", 0x34U, 0x1f56ff74U },
			{ "GP_LVL2      (38)", 0x38U, 0xbefe7f07U },
			{ "GPIO_USE_SEL3(40)", 0x40U, 0x000001f0U },
			{ "GP_IO_SEL3   (44)", 0x44U, 0x00000bf0U },
			{ "GP_LVL3      (48)", 0x48U, 0x000009ffU },
			{ "GP_RST_SEL1  (60)", 0x60U, 0x00000100U },
		};
		out_str("PCH GPIOs vs stock:\n");
		for (unsigned int i = 0; i < sizeof(gpio_chk)/sizeof(gpio_chk[0]); i++)
			print_reg_pair(gpio_chk[i].name, inl((uint16_t)(gpiobase + gpio_chk[i].off)), gpio_chk[i].stock);
	}

	/* PCH RCBA IRQ & FD */
	out_str("PCH RCBA:\n");
	out_str("  D31..20IR:");
	for (uint32_t o = 0x3140; o <= 0x3160; o += 4) {
		out_str(" "); out_hex32(mmio_read32(RCBA_BASE + o));
	}
	out_str("\n  FD(3418)=0x"); out_hex32(mmio_read32(RCBA_BASE + 0x3418));
	out_str(" FD2(3428)=0x"); out_hex32(mmio_read32(RCBA_BASE + 0x3428));
	out_str(" GCS(3410)=0x"); out_hex32(mmio_read32(RCBA_BASE + 0x3410));
	out_str(" HPTC(3404)=0x"); out_hex32(mmio_read32(RCBA_BASE + 0x3404));
	out_str(" 33d4=0x"); out_hex32(mmio_read32(RCBA_BASE + 0x33d4));
	out_str(" 38b0=0x"); out_hex32(mmio_read32(RCBA_BASE + 0x38b0));
	out_str(" 2b14=0x"); out_hex32(mmio_read32(RCBA_BASE + 0x2b14));
	out_str("\n");

	/* Section I: SuperIO NCT6791D (0x2e) & HWM Sensors */
	outb(0x87, 0x2e); outb(0x87, 0x2e);
	uint16_t sio_id = ((uint16_t)sio_read(0x20) << 8) | sio_read(0x21);
	out_str("SuperIO NCT6791D (0x2e): ID=0x"); out_hex16(sio_id);
	outb(0x07, 0x2e); outb(0x02, 0x2f);
	uint8_t com1_en = sio_read(0x30);
	uint16_t com1_io = ((uint16_t)sio_read(0x60) << 8) | sio_read(0x61);
	uint8_t com1_irq = sio_read(0x70);
	outb(0x07, 0x2e); outb(0x05, 0x2f);
	uint8_t kbc_en = sio_read(0x30);
	outb(0x07, 0x2e); outb(0x0a, 0x2f);
	uint8_t acpi_e6 = sio_read(0xe6), acpi_e7 = sio_read(0xe7), acpi_ec = sio_read(0xec);
	uint8_t acpi_ed = sio_read(0xed), acpi_ee = sio_read(0xee), acpi_f2 = sio_read(0xf2), acpi_fc = sio_read(0xfc);
	outb(0x07, 0x2e); outb(0x0b, 0x2f);
	uint8_t hwm_en = sio_read(0x30);
	uint16_t hwm_base = ((uint16_t)sio_read(0x60) << 8) | sio_read(0x61);
	outb(0xaa, 0x2e);
	out_str(" COM1(en="); out_hex8(com1_en); out_str(",io=0x"); out_hex16(com1_io); out_str(",irq="); out_dec(com1_irq);
	out_str(") PS2(en="); out_hex8(kbc_en);
	out_str(") ACPI(e6..fc="); out_hex8(acpi_e6); out_hex8(acpi_e7); out_hex8(acpi_ec);
	out_hex8(acpi_ed); out_hex8(acpi_ee); out_hex8(acpi_f2); out_hex8(acpi_fc);
	out_str(") HWM(en="); out_hex8(hwm_en); out_str(",base=0x"); out_hex16(hwm_base); out_str(")\n");
	if ((hwm_en & 1U) && hwm_base >= 0x100U && hwm_base < 0x1000U) {
		uint8_t vcore = hwm_read(hwm_base, 0x00, 0x20);
		uint8_t avcc  = hwm_read(hwm_base, 0x00, 0x22);
		uint8_t v33   = hwm_read(hwm_base, 0x00, 0x23);
		uint8_t systin = hwm_read(hwm_base, 0x00, 0x27);
		uint8_t cputin = hwm_read(hwm_base, 0x01, 0x50);
		uint16_t fan_sys = ((uint16_t)hwm_read(hwm_base, 0x04, 0xc0) << 8) | hwm_read(hwm_base, 0x04, 0xc1);
		uint16_t fan_cpu = ((uint16_t)hwm_read(hwm_base, 0x04, 0xc2) << 8) | hwm_read(hwm_base, 0x04, 0xc3);
		uint16_t fan_a0  = ((uint16_t)hwm_read(hwm_base, 0x04, 0xc4) << 8) | hwm_read(hwm_base, 0x04, 0xc5);
		uint16_t fan_a1  = ((uint16_t)hwm_read(hwm_base, 0x04, 0xc6) << 8) | hwm_read(hwm_base, 0x04, 0xc7);
		uint16_t fan_a2  = ((uint16_t)hwm_read(hwm_base, 0x04, 0xc8) << 8) | hwm_read(hwm_base, 0x04, 0xc9);
		outb(0x4e, (uint16_t)(hwm_base + 5U));
		outb(0x00, (uint16_t)(hwm_base + 6U));
		out_str("  HWM Raw: Vcore="); out_hex8(vcore);
		out_str(" AVCC="); out_hex8(avcc);
		out_str(" 3VCC="); out_hex8(v33);
		out_str(" SYSTIN="); out_dec(systin); out_str("C CPUTIN="); out_dec(cputin);
		out_str("C RPMs(sys/cpu/a0/a1/a2)=");
		out_dec(fan_sys); out_char('/'); out_dec(fan_cpu); out_char('/');
		out_dec(fan_a0); out_char('/'); out_dec(fan_a1); out_char('/'); out_dec(fan_a2); out_str("\n");
	}

	/* Section J: SMBus (00:1f.3) */
	if (pci_read16(0, 0x1f, 3, 0x00) == 0x8086U) {
		uint16_t scmd = pci_read16(0, 0x1f, 3, 0x04);
		uint8_t hostc = pci_read8(0, 0x1f, 3, 0x40);
		uint16_t smbar = (uint16_t)(pci_read32(0, 0x1f, 3, 0x20) & 0xffe0U);
		out_str("SMBus (00:1f.3): CMD="); out_hex16(scmd);
		out_str(" HOSTC="); out_hex8(hostc);
		out_str(" SMBBAR=0x"); out_hex16(smbar);
		if (smbar && (scmd & 1U)) {
			out_str(" HST_STS=0x"); out_hex8(inb(smbar));
		}
		out_str("\n");
	}

	/* Section K: ACPI XSDT Signatures & UEFI Boot Variables */
	out_str("ACPI & UEFI Vars:\n");
	if (gST && gST->ConfigurationTable && gST->NumberOfTableEntries < 128U) {
		const uint8_t *rsdp = NULL;
		for (uint64_t t = 0; t < gST->NumberOfTableEntries; t++) {
			if (memcmp(&gST->ConfigurationTable[t].VendorGuid, &gAcpi20Guid, sizeof(EFI_GUID)) == 0) {
				rsdp = (const uint8_t *)gST->ConfigurationTable[t].VendorTable;
				break;
			}
		}
		out_str("  XSDT Tables:");
		if (rsdp && memcmp(rsdp, "RSD PTR ", 8) == 0) {
			uint64_t xsdt_addr = ((uint64_t)mmio_read32((uint64_t)(uintptr_t)(rsdp + 28)) << 32) |
					     mmio_read32((uint64_t)(uintptr_t)(rsdp + 24));
			if (xsdt_addr && memcmp((const void *)(uintptr_t)xsdt_addr, "XSDT", 4) == 0) {
				uint32_t xlen = mmio_read32(xsdt_addr + 4U);
				if (xlen >= 36U && xlen <= 4096U) {
					uint32_t nent = (xlen - 36U) / 8U;
					for (uint32_t e = 0; e < nent; e++) {
						uint64_t tp = ((uint64_t)mmio_read32(xsdt_addr + 36U + e * 8U + 4U) << 32) |
							      mmio_read32(xsdt_addr + 36U + e * 8U);
						if (!tp) continue;
						out_char(' ');
						for (uint32_t k = 0; k < 4; k++) {
							uint8_t ch = ((const volatile uint8_t *)(uintptr_t)tp)[k];
							out_char((ch >= 0x20 && ch < 0x7f) ? (char)ch : '?');
						}
					}
				}
			}
		}
		out_str("\n");
	}
	if (gST && gST->RuntimeServices && gST->RuntimeServices->GetVariable) {
		static uint16_t vname_to[] = { 'T','i','m','e','o','u','t',0 };
		static uint16_t vname_bo[] = { 'B','o','o','t','O','r','d','e','r',0 };
		uint16_t to_val = 0xffffU;
		uint64_t sz = sizeof(to_val);
		uint32_t attr = 0;
		EFI_STATUS st1 = gST->RuntimeServices->GetVariable(vname_to, &gEfiGlobalVarGuid, &attr, &sz, &to_val);
		out_str("  UEFI Timeout st="); out_hex32((uint32_t)st1);
		out_str(" val="); out_dec(to_val);
		uint16_t bo[16];
		sz = sizeof(bo);
		EFI_STATUS st2 = gST->RuntimeServices->GetVariable(vname_bo, &gEfiGlobalVarGuid, &attr, &sz, bo);
		out_str(" BootOrder st="); out_hex32((uint32_t)st2); out_str(" [");
		if (st2 == 0) {
			uint64_t cnt = sz / 2U;
			if (cnt > 16U) cnt = 16U;
			for (uint64_t i = 0; i < cnt; i++) {
				if (i) out_char(',');
				out_hex16(bo[i]);
			}
		}
		out_str("]\n");
	}

	/* Section L: SATA HSIO IOBP, SATA 00:1f.2, ABAR, PCIe RPs + PEG, SPI & BlockIO */

	/* SATA IOBP Port 1 & Common */
	static const struct { const char *name; uint32_t addr; uint32_t stock; } iobp_chk[] = {
		{ "SECRT88P0(2488)", 0xea002488U, 0x5580803aU },
		{ "SECRT8CP0(248c)", 0xea00248cU, 0x0c692040U },
		{ "SECRT90P0(2490)", 0xea002490U, 0x2b403e67U },
		{ "SECRR50P0(2550)", 0xea002550U, 0x023f3f3fU },
		{ "SECRR54P0(2554)", 0xea002554U, 0x00020000U },
		{ "SECRT88P1(2688)", 0xea002688U, 0x5580803aU },
		{ "SECRT8CP1(268c)", 0xea00268cU, 0x0c692040U },
		{ "SECRT90P1(2690)", 0xea002690U, 0x2b403e67U },
		{ "SECRT98P1(2698)", 0xea002698U, 0x1f203b41U },
		{ "SECRR1CP1(271c)", 0xea00271cU, 0x00001810U },
		{ "SECRR50P1(2750)", 0xea002750U, 0x023f3f3fU },
		{ "SECRR54P1(2754)", 0xea002754U, 0x00020000U },
		{ "SECRR58P1(2758)", 0xea002758U, 0xa0809000U },
		{ "SATA_CMN(8008) ", 0xea008008U, 0x1c00217fU },
		{ "SECRF00  (8100)", 0xea008100U, 0x7880fffcU },
		{ "SECRF04  (8104)", 0xea008104U, 0x69777888U },
	};
	out_str("SATA HSIO IOBP:\n");
	for (unsigned int i = 0; i < sizeof(iobp_chk)/sizeof(iobp_chk[0]); i++) {
		uint16_t st = 0;
		uint32_t v = pch_iobp_read(iobp_chk[i].addr, &st);
		print_reg_pair(iobp_chk[i].name, v, iobp_chk[i].stock);
	}
	static const uint32_t p1_full_iobp[] = {
		0xea002600U, 0xea002608U, 0xea002610U, 0xea002618U, 0xea00261cU,
		0xea002628U, 0xea00262cU, 0xea002638U, 0xea002640U, 0xea0026a4U,
		0xea0026acU, 0xea00270cU, 0xea002740U, 0xea002744U, 0xea002748U,
		0xea002778U, 0xea00277cU
	};
	out_str("SATA P1 Full HSIO:\n");
	for (unsigned int i = 0; i < sizeof(p1_full_iobp)/sizeof(p1_full_iobp[0]); i++) {
		uint16_t st = 0;
		uint32_t v = pch_iobp_read(p1_full_iobp[i], &st);
		if ((i % 5U) == 0) out_str(" ");
		out_str(" "); out_hex16((uint16_t)(p1_full_iobp[i] & 0xffffU));
		out_char('='); out_hex32(v);
		if ((i % 5U) == 4U || i + 1U == sizeof(p1_full_iobp)/sizeof(p1_full_iobp[0]))
			out_str("\n");
	}

	/* SATA 00:1f.2 & ABAR */
	uint16_t s90 = pci_read16(0, 0x1f, 2, 0x90);
	uint16_t s92 = pci_read16(0, 0x1f, 2, 0x92);
	uint32_t s94 = pci_read32(0, 0x1f, 2, 0x94);
	uint32_t s98 = pci_read32(0, 0x1f, 2, 0x98);
	uint32_t s9c = pci_read32(0, 0x1f, 2, 0x9c);
	uint32_t s300 = pci_read32(0, 0x1f, 2, 0x300);
	uint64_t abar = pci_read32(0, 0x1f, 2, 0x24) & ~0x7ffULL;
	out_str("SATA 00:1f.2:\n");
	out_str("  90="); out_hex16(s90);
	out_str(" 92="); out_hex16(s92);
	out_str(" 94="); out_hex32(s94);
	out_str(" 98="); out_hex32(s98);
	out_str(" 9c="); out_hex32(s9c);
	out_str(" 300="); out_hex32(s300); out_str("\n");
	static const uint8_t sir_idx[] = {
		0x00, 0x04, 0x54, 0x58, 0x60, 0x64, 0x68, 0x70, 0x80, 0x84, 0x88, 0xa0, 0xa4, 0xa8
	};
	out_str("  SATA SIR:");
	for (unsigned int i = 0; i < sizeof(sir_idx); i++) {
		pci_write8(0, 0x1f, 2, 0xa0, sir_idx[i]);
		out_char(' '); out_hex8(sir_idx[i]); out_char('=');
		out_hex32(pci_read32(0, 0x1f, 2, 0xa4));
	}
	out_str("\n");

	if (abar) {
		out_str("ABAR=0x"); out_hex32((uint32_t)abar);
		out_str(" CAP="); out_hex32(mmio_read32(abar + 0x00));
		out_str(" PI="); out_hex32(mmio_read32(abar + 0x0c));
		out_str(" CAP2="); out_hex32(mmio_read32(abar + 0x24));
		out_str(" a0="); out_hex32(mmio_read32(abar + 0xa0));
		out_str(" a4="); out_hex32(mmio_read32(abar + 0xa4));
		out_str(" c0="); out_hex32(mmio_read32(abar + 0xc0));
		out_str(" c4="); out_hex32(mmio_read32(abar + 0xc4));
		out_str(" c8="); out_hex32(mmio_read32(abar + 0xc8)); out_str("\n");
		out_str("  P1 PxCMD="); out_hex32(mmio_read32(abar + 0x198));
		out_str(" PxSSTS="); out_hex32(mmio_read32(abar + 0x1a8));
		out_str(" PxSCTL="); out_hex32(mmio_read32(abar + 0x1ac));
		out_str(" PxSERR="); out_hex32(mmio_read32(abar + 0x1b0)); out_str("\n");
	}

	/* PCIe RPs 1c.0..7 & PEG 01.0..2 */
	out_str("PCIe RPs (00:1c.0..7 & 00:01.0..2):\n");
	for (uint8_t fn = 0; fn < 8; fn++) {
		uint16_t vid = pci_read16(0, 0x1c, fn, 0x00);
		if (vid == 0xffff)
			continue;
		uint16_t lctl = pci_read16(0, 0x1c, fn, 0x50);
		uint16_t lsts = pci_read16(0, 0x1c, fn, 0x52);
		uint16_t slsts = pci_read16(0, 0x1c, fn, 0x5a);
		uint32_t e0 = pci_read32(0, 0x1c, fn, 0xe0);
		uint32_t e8 = pci_read32(0, 0x1c, fn, 0xe8);
		uint32_t ce = pci_read32(0, 0x1c, fn, 0x110);
		out_str("  1c."); out_dec(fn);
		out_str(" LCTL="); out_hex16(lctl);
		out_str(" LSTS="); out_hex16(lsts);
		out_str(" SLSTS="); out_hex16(slsts);
		out_str(" e0="); out_hex32(e0);
		out_str(" e8="); out_hex32(e8);
		out_str(" ce_aer="); out_hex32(ce); out_str("\n");
	}
	for (uint8_t fn = 0; fn < 3; fn++) {
		uint16_t vid = pci_read16(0, 0x01, fn, 0x00);
		if (vid == 0xffff)
			continue;
		out_str("  01."); out_dec(fn);
		out_str(" LCTL="); out_hex16(pci_read16(0, 0x01, fn, 0xb0));
		out_str(" LSTS="); out_hex16(pci_read16(0, 0x01, fn, 0xb2));
		out_str(" SLSTS="); out_hex16(pci_read16(0, 0x01, fn, 0xba)); out_str("\n");
	}

	/* SPI Status (skip slow full readback SHA-256 during diag; verified in serflash/flash) */
	(void)spi_check_ready(1);

	/* BlockIO 16 MiB Gen3 Read Stress Test */
	if (gBS && gBS->LocateHandleBuffer && abar) {
		uint64_t nh = 0;
		EFI_HANDLE *handles = NULL;
		if (gBS->LocateHandleBuffer(2 /* ByProtocol */, &gBlockIoGuid, NULL, &nh, &handles) == 0 && handles) {
			for (uint64_t i = 0; i < nh; i++) {
				EFI_BLOCK_IO_PROTOCOL *bio = NULL;
				if (gBS->HandleProtocol(handles[i], &gBlockIoGuid, (void **)&bio) != 0 || !bio || !bio->Media)
					continue;
				if (bio->Media->LogicalPartition || !bio->Media->MediaPresent || bio->Media->BlockSize == 0)
					continue;
				if (bio->Media->BlockSize > 4096)
					continue;

				/* Clear latched PxSERR before test by writing 1s */
				mmio_write32(abar + 0x1b0, 0xffffffffU);
				uint32_t serr_pre = mmio_read32(abar + 0x1b0);
				uint32_t ssts_pre = mmio_read32(abar + 0x1a8);
				uint32_t blk_sz = bio->Media->BlockSize;
				uint64_t lbas_per_64k = 65536ULL / blk_sz;
				uint64_t n_chunks = 256; /* 16 MiB */
				if ((n_chunks * lbas_per_64k) > bio->Media->LastBlock)
					n_chunks = bio->Media->LastBlock / lbas_per_64k;

				out_str("BlockIO 16 MiB Stress Test (handle "); out_dec((uint32_t)i);
				out_str(", blk="); out_dec(blk_sz);
				out_str("): Pre PxSSTS="); out_hex32(ssts_pre);
				out_str(" PxSERR="); out_hex32(serr_pre); out_str("\n");

				EFI_STATUS st = 0;
				uint64_t ok_chunks = 0;
				for (uint64_t c = 0; c < n_chunks; c++) {
					st = bio->ReadBlocks(bio, bio->Media->MediaId,
							     c * lbas_per_64k, 65536ULL, g_blk_buf);
					if (st != 0)
						break;
					ok_chunks++;
					if (c == 0 || c == 15 || c == 63 || c == 127 || c == 191 || c == 255 || c + 1 == n_chunks) {
						out_str("  chunk="); out_dec((uint32_t)c);
						out_str(" PxSSTS="); out_hex32(mmio_read32(abar + 0x1a8));
						out_str(" PxSERR="); out_hex32(mmio_read32(abar + 0x1b0)); out_str("\n");
					}
				}
				uint32_t ssts_post = mmio_read32(abar + 0x1a8);
				uint32_t serr_post = mmio_read32(abar + 0x1b0);
				out_str("  Result: status=0x"); out_hex32((uint32_t)st);
				out_str(" chunks="); out_dec((uint32_t)ok_chunks);
				out_str("/"); out_dec((uint32_t)n_chunks);
				out_str(" Post PxSSTS="); out_hex32(ssts_post);
				out_str(" PxSERR="); out_hex32(serr_post); out_str("\n");
				if (st == 0 && serr_post == 0)
					out_str("Z97A_SATA_STRESS_PASS\n");
				else
					out_str("Z97A_SATA_STRESS_FAIL\n");
			}
			gBS->FreePool(handles);
		}
	}
	out_str("Z97A_DIAG_DONE\n");
}

/* Interactive Pre-OS HDMI Register & Pattern Matrix Test */
static UNUSED void hdmi_step_report(int step, const char *desc, uint64_t gmadr)
{
	uint32_t frm0 = mmio_read32(gmadr + 0x70040);
	udelay_approx(100000U);
	uint32_t frm1 = mmio_read32(gmadr + 0x70040);

	out_str("HDMITEST_STEP "); out_dec((uint32_t)step);
	out_str(" "); out_str(desc);
	out_str(" TRANS_DDI="); out_hex32(mmio_read32(gmadr + 0x60400));
	out_str(" DDI_D="); out_hex32(mmio_read32(gmadr + 0x64300));
	out_str(" DIP="); out_hex32(mmio_read32(gmadr + 0x60200));
	out_str(" TRANS9="); out_hex32(mmio_read32(gmadr + 0x64f68));
	out_str("/"); out_hex32(mmio_read32(gmadr + 0x64f6c));
	out_str(" CDCLK="); out_hex32(mmio_read32(gmadr + 0x46200));
	out_str(" DEIIR="); out_hex32(mmio_read32(gmadr + 0x44008));
	out_str(" SERR="); out_hex32(mmio_read32(gmadr + 0x44040));
	out_str(" dFRM="); out_dec(frm1 - frm0);
	out_str("\n");

	for (int k = 0; k < 45; k++)
		udelay_approx(100000U);
}

static UNUSED void hdmi_toggle_ddi_d(uint64_t gmadr, int enable)
{
	uint32_t v = mmio_read32(gmadr + 0x64300);
	if (enable)
		mmio_write32(gmadr + 0x64300, v | (1U << 31));
	else
		mmio_write32(gmadr + 0x64300, v & ~(1U << 31));
	(void)mmio_read32(gmadr + 0x64300);
}

static UNUSED void run_hdmitest(void)
{
	out_str("\n=== Z97-A PRE-OS HDMI MATRIX TEST ===\n");
	if (pci_read16(0, 2, 0, 0x00) != 0x8086) {
		out_str("HDMITEST ERROR: IGD not found\nZ97A_HDMITEST_DONE\n");
		return;
	}
	uint64_t gmadr = pci_read32(0, 2, 0, 0x10) & ~0xfULL;
	uint64_t fb_bar = pci_read32(0, 2, 0, 0x18) & ~0xfULL;
	if (!gmadr || !fb_bar) {
		out_str("HDMITEST ERROR: zero IGD BARs\nZ97A_HDMITEST_DONE\n");
		return;
	}

	uint32_t srcsz = mmio_read32(gmadr + 0x6001c);
	uint32_t stride = mmio_read32(gmadr + 0x70188);
	uint32_t surf = mmio_read32(gmadr + 0x7019c);
	uint32_t w = ((srcsz >> 16) & 0xfffU) + 1U;
	uint32_t h = (srcsz & 0xfffU) + 1U;
	if (w > 3840U) w = 3840U;
	if (h > 2160U) h = 2160U;
	if (stride > 16384U) stride = 16384U;

	out_str("FB target: GMADR=0x"); out_hex32((uint32_t)fb_bar);
	out_str(" SURF=0x"); out_hex32(surf);
	out_str(" STRIDE="); out_dec(stride);
	out_str(" "); out_dec(w); out_str("x"); out_dec(h); out_str("\n");

	if (w > 0 && h > 0 && stride >= w * 4U) {
		uint64_t fb_base = fb_bar + (uint64_t)(surf & ~0xfffU);
		uint32_t q1 = h / 4U, q2 = h / 2U, q3 = (h * 3U) / 4U;
		for (uint32_t y = 0; y < h; y++) {
			uint32_t color = (y < q1) ? 0x00ff0000U :
					 (y < q2) ? 0x0000ff00U :
					 (y < q3) ? 0x000000ffU : 0x00ffffffU;
			volatile uint32_t *row = (volatile uint32_t *)(uintptr_t)(fb_base + (uint64_t)y * stride);
			for (uint32_t x = 0; x < w; x++)
				row[x] = color;
		}
		mmio_write32(gmadr + 0x7019c, surf);
		(void)mmio_read32(gmadr + 0x7019c);
	}

	/* Step 0: baseline v0.13 + bright RGBW test pattern */
	hdmi_step_report(0, "baseline_v013_rgbw", gmadr);

	/* Step 1: re-toggle DDI_BUF_CTL_D enable bit 31 */
	hdmi_toggle_ddi_d(gmadr, 0);
	udelay_approx(50000U);
	hdmi_toggle_ddi_d(gmadr, 1);
	hdmi_step_report(1, "ddi_d_retoggle", gmadr);

	/* Step 2: switch TRANS_DDI A to HDMI mode (0) + +H/+V sync + write AVI InfoFrame (1080p VIC 16) */
	hdmi_toggle_ddi_d(gmadr, 0);
	udelay_approx(20000U);
	{
		uint32_t tddi = mmio_read32(gmadr + 0x60400);
		tddi &= ~(0x7U << 24);
		tddi |= 0x00030000U;
		mmio_write32(gmadr + 0x60400, tddi);
		(void)mmio_read32(gmadr + 0x60400);

		uint32_t dip = mmio_read32(gmadr + 0x60200);
		mmio_write32(gmadr + 0x60200, dip & ~(1U << 12));
		(void)mmio_read32(gmadr + 0x60200);
		static const uint32_t avi_1080p[8] = {
			0x000d0282U, 0x00281027U, 0x00000010U, 0U, 0U, 0U, 0U, 0U
		};
		for (uint32_t i = 0; i < 8U; i++)
			mmio_write32(gmadr + 0x60220 + i * 4U, avi_1080p[i]);
		mmio_write32(gmadr + 0x60200, dip | (1U << 12));
		(void)mmio_read32(gmadr + 0x60200);
	}
	hdmi_toggle_ddi_d(gmadr, 1);
	hdmi_step_report(2, "hdmi_mode_avi_1080p", gmadr);

	/* Step 3: set CDCLK_FREQ 0x46200 = 0x21b + W1C DEIIR / SERR_INT */
	mmio_write32(gmadr + 0x46200, 0x21bU);
	mmio_write32(gmadr + 0x44008, mmio_read32(gmadr + 0x44008));
	mmio_write32(gmadr + 0x44040, mmio_read32(gmadr + 0x44040));
	udelay_approx(100000U);
	hdmi_step_report(3, "cdclk_0x21b_clear_underrun", gmadr);

	/* Step 4: DDI_D HDMI TX entry 0 (400mV / 0dB) */
	hdmi_toggle_ddi_d(gmadr, 0);
	udelay_approx(20000U);
	mmio_write32(gmadr + 0x64f68, 0x00ffffffU);
	mmio_write32(gmadr + 0x64f6c, 0x0006000eU);
	hdmi_toggle_ddi_d(gmadr, 1);
	hdmi_step_report(4, "hdmi_entry0_400mv", gmadr);

	/* Step 5: DDI_D HDMI TX entry 9 (1000mV / 0dB) */
	hdmi_toggle_ddi_d(gmadr, 0);
	udelay_approx(20000U);
	mmio_write32(gmadr + 0x64f68, 0x00ffffffU);
	mmio_write32(gmadr + 0x64f6c, 0x000c0004U);
	hdmi_toggle_ddi_d(gmadr, 1);
	hdmi_step_report(5, "hdmi_entry9_1000mv", gmadr);

	/* Step 6: DDI_D HDMI TX entry 6 (800mV stock) */
	hdmi_toggle_ddi_d(gmadr, 0);
	udelay_approx(20000U);
	mmio_write32(gmadr + 0x64f68, 0x00ffffffU);
	mmio_write32(gmadr + 0x64f6c, 0x00040006U);
	hdmi_toggle_ddi_d(gmadr, 1);
	hdmi_step_report(6, "hdmi_entry6_800mv_stock", gmadr);

	out_str("Z97A_HDMITEST_DONE\n");
}

static UNUSED void paint_rgbw_fb(uint64_t gmadr, uint64_t fb_bar, uint32_t w, uint32_t h)
{
	uint32_t stride = mmio_read32(gmadr + 0x70188);
	uint32_t surf = mmio_read32(gmadr + 0x7019c);
	if (w > 3840U) w = 3840U;
	if (h > 2160U) h = 2160U;
	if (stride > 16384U) stride = 16384U;
	if (w > 0 && h > 0 && stride >= w * 4U) {
		uint64_t fb_base = fb_bar + (uint64_t)(surf & ~0xfffU);
		uint32_t q1 = h / 4U, q2 = h / 2U, q3 = (h * 3U) / 4U;
		for (uint32_t y = 0; y < h; y++) {
			uint32_t color = (y < q1) ? 0x00ff0000U :
					 (y < q2) ? 0x0000ff00U :
					 (y < q3) ? 0x000000ffU : 0x00ffffffU;
			volatile uint32_t *row = (volatile uint32_t *)(uintptr_t)(fb_base + (uint64_t)y * stride);
			for (uint32_t x = 0; x < w; x++)
				row[x] = color;
		}
		mmio_write32(gmadr + 0x7019c, surf);
		(void)mmio_read32(gmadr + 0x7019c);
	}
}

static UNUSED void hsw_ddi_d_modeset(uint64_t gmadr, uint64_t fb_bar, int hdmi_mode, int is_720p, uint32_t trans1, uint32_t trans2)
{
	/* 1. Disable Primary Plane A */
	mmio_write32(gmadr + 0x70180, mmio_read32(gmadr + 0x70180) & ~(1U << 31));
	mmio_write32(gmadr + 0x7019c, 0);
	(void)mmio_read32(gmadr + 0x7019c);
	udelay_approx(20000U);

	/* 2. Disable PIPEA_CONF & wait for status bit 30 clear */
	mmio_write32(gmadr + 0x70008, mmio_read32(gmadr + 0x70008) & ~(1U << 31));
	for (int i = 0; i < 50; i++) {
		if ((mmio_read32(gmadr + 0x70008) & (1U << 30)) == 0)
			break;
		udelay_approx(1000U);
	}

	/* 3. Disable TRANS_DDI_FUNC_CTL_A, PIPEA_CLK_SEL, VIDEO_DIP_CTL_A */
	mmio_write32(gmadr + 0x60400, mmio_read32(gmadr + 0x60400) & ~(1U << 31));
	mmio_write32(gmadr + 0x46140, 0);
	mmio_write32(gmadr + 0x60200, 0);

	/* 4. Disable DDI_BUF_CTL_D, wait for idle bit 7, hold 200ms drop */
	mmio_write32(gmadr + 0x64300, mmio_read32(gmadr + 0x64300) & ~(1U << 31));
	for (int i = 0; i < 50; i++) {
		if ((mmio_read32(gmadr + 0x64300) & (1U << 7)) != 0)
			break;
		udelay_approx(1000U);
	}
	udelay_approx(200000U);

	/* 5. Program CDCLK_FREQ & HDMI_TRANS9_D */
	mmio_write32(gmadr + 0x46200, 0x21bU);
	mmio_write32(gmadr + 0x64f68, trans1);
	mmio_write32(gmadr + 0x64f6c, trans2);

	/* 6-7. WRPLL1 & Pipe A timings + FB RGBW repaint */
	mmio_write32(gmadr + 0x46040, mmio_read32(gmadr + 0x46040) & ~(1U << 31));
	udelay_approx(5000U);
	if (is_720p) {
		mmio_write32(gmadr + 0x46040, 0xb0210c14U);
		udelay_approx(10000U);
		mmio_write32(gmadr + 0x60000, 0x067104ffU);
		mmio_write32(gmadr + 0x60004, 0x067104ffU);
		mmio_write32(gmadr + 0x60008, 0x0595056dU);
		mmio_write32(gmadr + 0x6000c, 0x02ed02cfU);
		mmio_write32(gmadr + 0x60010, 0x02ed02cfU);
		mmio_write32(gmadr + 0x60014, 0x02d902d4U);
		mmio_write32(gmadr + 0x6001c, 0x04ff02cfU);
		mmio_write32(gmadr + 0x45270, 0x000000b2U);
		paint_rgbw_fb(gmadr, fb_bar, 1280U, 720U);
	} else {
		mmio_write32(gmadr + 0x46040, 0xb0210614U);
		udelay_approx(10000U);
		mmio_write32(gmadr + 0x60000, 0x0897077fU);
		mmio_write32(gmadr + 0x60004, 0x0897077fU);
		mmio_write32(gmadr + 0x60008, 0x080307d7U);
		mmio_write32(gmadr + 0x6000c, 0x04640437U);
		mmio_write32(gmadr + 0x60010, 0x04640437U);
		mmio_write32(gmadr + 0x60014, 0x043f043aU);
		mmio_write32(gmadr + 0x6001c, 0x077f0437U);
		mmio_write32(gmadr + 0x45270, 0x00000077U);
		paint_rgbw_fb(gmadr, fb_bar, 1920U, 1080U);
	}

	/* 8. Port & Pipe Clock Select */
	mmio_write32(gmadr + 0x4610c, 0x80000000U);
	mmio_write32(gmadr + 0x46140, 0x80000000U);

	/* 9. TRANS_DDI_FUNC_CTL_A (DDI_D, 8bpc, +H/+V sync, HDMI vs DVI) */
	mmio_write32(gmadr + 0x60400, hdmi_mode ? 0xb0030000U : 0xb1030000U);

	/* 10. AVI InfoFrame if HDMI mode */
	if (hdmi_mode) {
		static const uint32_t avi_720p[8] = {
			0x000d0282U, 0x00281033U, 0x00000004U, 0U, 0U, 0U, 0U, 0U
		};
		static const uint32_t avi_1080p[8] = {
			0x000d0282U, 0x00281027U, 0x00000010U, 0U, 0U, 0U, 0U, 0U
		};
		const uint32_t *avi = is_720p ? avi_720p : avi_1080p;
		for (uint32_t i = 0; i < 8U; i++)
			mmio_write32(gmadr + 0x60220 + i * 4U, avi[i]);
		mmio_write32(gmadr + 0x60200, 0x00001000U);
		(void)mmio_read32(gmadr + 0x60200);
	}

	/* 11-13. Enable PIPEA, DSPACNTR/DSPASURF, DDI_BUF_CTL_D */
	mmio_write32(gmadr + 0x70008, 0xc0000000U);
	udelay_approx(20000U);
	mmio_write32(gmadr + 0x70180, 0x98000000U);
	mmio_write32(gmadr + 0x7019c, 0);
	(void)mmio_read32(gmadr + 0x7019c);
	mmio_write32(gmadr + 0x64300, 0x80000000U);
	(void)mmio_read32(gmadr + 0x64300);
	udelay_approx(50000U);
}

static UNUSED void hdmi2_step_report(int step, const char *desc, uint64_t gmadr)
{
	uint32_t frm0 = mmio_read32(gmadr + 0x70040);
	udelay_approx(100000U);
	uint32_t frm1 = mmio_read32(gmadr + 0x70040);

	out_str("HDMITEST2_STEP "); out_dec((uint32_t)step);
	out_str(" "); out_str(desc);
	out_str(" TRANS_DDI="); out_hex32(mmio_read32(gmadr + 0x60400));
	out_str(" DDI_D="); out_hex32(mmio_read32(gmadr + 0x64300));
	out_str(" WRPLL1="); out_hex32(mmio_read32(gmadr + 0x46040));
	out_str(" SRCSZ="); out_hex32(mmio_read32(gmadr + 0x6001c));
	out_str(" DIP="); out_hex32(mmio_read32(gmadr + 0x60200));
	out_str(" dFRM="); out_dec(frm1 - frm0);
	out_str("\n");

	for (int k = 0; k < 50; k++)
		udelay_approx(100000U);
}

static UNUSED void run_hdmitest2(void)
{
	out_str("\n=== Z97-A STRICT HASWELL MODESET MATRIX (HDMITEST2) ===\n");
	if (pci_read16(0, 2, 0, 0x00) != 0x8086) {
		out_str("HDMITEST2 ERROR: IGD not found\nZ97A_HDMITEST2_DONE\n");
		return;
	}
	uint16_t cmd = pci_read16(0, 2, 0, 0x04);
	uint64_t gmadr = pci_read_bar64(0, 2, 0, 0x10);
	uint64_t fb_bar = pci_read_bar64(0, 2, 0, 0x18);
	out_str("IGD CMD=0x"); out_hex16(cmd);
	out_str(" GTTMMADR=0x"); out_hex32((uint32_t)gmadr);
	out_str(" GMADR=0x"); out_hex32((uint32_t)fb_bar);
	out_str(" SHOTPLUG="); out_hex32(mmio_read32(gmadr + 0xc4030));
	out_str(" SFUSE="); out_hex32(mmio_read32(gmadr + 0xc2014));
	out_str(" GMBUS2="); out_hex32(mmio_read32(gmadr + 0xc5108));
	out_str("\n");
	out_str("DSP CTL="); out_hex32(mmio_read32(gmadr + 0x70180));
	out_str(" SURF="); out_hex32(mmio_read32(gmadr + 0x7019c));
	out_str(" STRIDE="); out_hex32(mmio_read32(gmadr + 0x70188)); out_str("\n");

	if (!gmadr || !fb_bar) {
		out_str("HDMITEST2 ERROR: zero IGD BARs\nZ97A_HDMITEST2_DONE\n");
		return;
	}

	hsw_ddi_d_modeset(gmadr, fb_bar, 1, 0, 0x00ffffffU, 0x00040006U);
	hdmi2_step_report(0, "1080p60_hdmi_avi_entry6", gmadr);

	hsw_ddi_d_modeset(gmadr, fb_bar, 0, 0, 0x00ffffffU, 0x00040006U);
	hdmi2_step_report(1, "1080p60_dvi_pos_entry6", gmadr);

	hsw_ddi_d_modeset(gmadr, fb_bar, 1, 1, 0x00ffffffU, 0x00040006U);
	hdmi2_step_report(2, "720p60_hdmi_avi_entry6", gmadr);

	hsw_ddi_d_modeset(gmadr, fb_bar, 0, 1, 0x00ffffffU, 0x00040006U);
	hdmi2_step_report(3, "720p60_dvi_pos_entry6", gmadr);

	out_str("Z97A_HDMITEST2_DONE\n");
}

/* Targeted SATA Gen3 vs Clock Gating vs Gen2 Isolation Test */
static UNUSED int sata_run_pass(const char *label, EFI_BLOCK_IO_PROTOCOL *bio, uint64_t abar, uint64_t start_chunk, uint64_t count)
{
	uint32_t blk_sz = bio->Media->BlockSize;
	uint64_t lbas_per_64k = 65536ULL / blk_sz;

	mmio_write32(abar + 0x1b0, 0xffffffffU);
	uint32_t ssts_pre = mmio_read32(abar + 0x1a8);
	uint32_t serr_pre = mmio_read32(abar + 0x1b0);

	EFI_STATUS st = 0;
	uint64_t ok = 0;
	for (uint64_t i = 0; i < count; i++) {
		uint64_t c = start_chunk + i;
		if ((c + 1ULL) * lbas_per_64k > bio->Media->LastBlock)
			break;
		st = bio->ReadBlocks(bio, bio->Media->MediaId, c * lbas_per_64k, 65536ULL, g_blk_buf);
		if (st != 0)
			break;
		ok++;
	}

	uint32_t pxis = mmio_read32(abar + 0x190);
	uint32_t pxtfd = mmio_read32(abar + 0x1a0);
	uint32_t pxssts = mmio_read32(abar + 0x1a8);
	uint32_t pxserr = mmio_read32(abar + 0x1b0);

	out_str("SATATEST "); out_str(label);
	out_str(": pre_ssts="); out_hex32(ssts_pre);
	out_str(" pre_serr="); out_hex32(serr_pre);
	out_str(" -> st=0x"); out_hex32((uint32_t)st);
	out_str(" ok="); out_dec((uint32_t)ok); out_char('/'); out_dec((uint32_t)count);
	out_str(" PxIS="); out_hex32(pxis);
	out_str(" PxTFD="); out_hex32(pxtfd);
	out_str(" PxSSTS="); out_hex32(pxssts);
	out_str(" PxSERR="); out_hex32(pxserr);
	out_str("\n");

	return (st != 0 || pxserr != 0 || (pxssts & 0x0fU) != 3U) ? 1 : 0;
}

static UNUSED void run_satatest(void)
{
	out_str("\n=== Z97-A TARGETED SATA ISOLATION TEST (SATATEST) ===\n");
	pci_init_mmcfg();
	uint64_t abar = pci_read32(0, 0x1f, 2, 0x24) & ~0x7ffULL;
	if (!abar || !gBS || !gBS->LocateHandleBuffer) {
		out_str("SATATEST ERROR: missing ABAR/BootServices\nZ97A_SATATEST_DONE\n");
		return;
	}

	EFI_BLOCK_IO_PROTOCOL *target_bio = NULL;
	uint64_t nh = 0;
	EFI_HANDLE *handles = NULL;
	if (gBS->LocateHandleBuffer(2, &gBlockIoGuid, NULL, &nh, &handles) == 0 && handles) {
		for (uint64_t i = 0; i < nh; i++) {
			EFI_BLOCK_IO_PROTOCOL *bio = NULL;
			if (gBS->HandleProtocol(handles[i], &gBlockIoGuid, (void **)&bio) != 0 || !bio || !bio->Media)
				continue;
			if (bio->Media->LogicalPartition || !bio->Media->MediaPresent || bio->Media->BlockSize != 512U)
				continue;
			target_bio = bio;
			out_str("Found physical BlockIO handle "); out_dec((uint32_t)i);
			out_str(" LastBlock="); out_dec((uint32_t)bio->Media->LastBlock); out_str("\n");
			break;
		}
		gBS->FreePool(handles);
	}

	if (!target_bio) {
		out_str("SATATEST ERROR: no 512B physical BlockIO handle\nZ97A_SATATEST_DONE\n");
		return;
	}

	/* Test 1: Gen3 baseline 16 MiB from chunk 0 */
	sata_run_pass("gen3_baseline_16m", target_bio, abar, 0, 256);

	/* Test 2: Gen3 chunks 140..159 (determinism vs transient error check) */
	sata_run_pass("gen3_lba140_to_160", target_bio, abar, 140, 20);

	/* Test 3: Gen3 with clock gating disabled (SIR 0x70 = 0, 0x300 &= ~0xe0000000) */
	pci_write8(0, 0x1f, 2, 0xa0, 0x70);
	pci_write32(0, 0x1f, 2, 0xa4, 0x00000000U);
	pci_write32(0, 0x1f, 2, 0x300, pci_read32(0, 0x1f, 2, 0x300) & ~0xe0000000U);
	sata_run_pass("gen3_no_clk_gate_16m", target_bio, abar, 0, 256);

	/* Test 4: Restore clock gating, cap Port 1 to Gen2 (3.0 Gbps) via PxSCTL, Reset & 16 MiB */
	pci_write8(0, 0x1f, 2, 0xa0, 0x70);
	pci_write32(0, 0x1f, 2, 0xa4, 0x3f00bf1fU);
	pci_write32(0, 0x1f, 2, 0x300, pci_read32(0, 0x1f, 2, 0x300) | 0xe0000000U);
	mmio_write32(abar + 0x1ac, 0x00000321U);
	(void)mmio_read32(abar + 0x1ac);
	udelay_approx(2000U);
	mmio_write32(abar + 0x1ac, 0x00000320U);
	(void)mmio_read32(abar + 0x1ac);
	udelay_approx(100000U);
	mmio_write32(abar + 0x1b0, 0xffffffffU);
	out_str("Post Gen2 COMRESET: PxSSTS="); out_hex32(mmio_read32(abar + 0x1a8));
	out_str(" PxSCTL="); out_hex32(mmio_read32(abar + 0x1ac));
	out_str(" PxSERR="); out_hex32(mmio_read32(abar + 0x1b0)); out_str("\n");
	if (target_bio->Reset)
		(void)target_bio->Reset(target_bio, 0);
	out_str("Post BioReset: PxSSTS="); out_hex32(mmio_read32(abar + 0x1a8)); out_str("\n");
	sata_run_pass("gen2_3gbps_16m", target_bio, abar, 0, 256);

	out_str("Z97A_SATATEST_DONE\n");
}

/* Pre-OS HDMI Root-Cause Audit + Strict Haswell PRM Modeset (HDMITEST3) */
static UNUSED void gmbus_probe_ddi_d_edid(uint64_t gmadr)
{
	uint8_t edid[16];
	for (int i = 0; i < 16; i++)
		edid[i] = 0;

	/* Wait for GMBUS2 INUSE clear (max 10ms) */
	for (int i = 0; i < 100; i++) {
		if ((mmio_read32(gmadr + 0xc5108) & (1U << 15)) == 0)
			break;
		udelay_approx(100U);
	}

	/* Clear pending state/interrupt if GMBUS active or errored */
	mmio_write32(gmadr + 0xc5110, 0U); /* GMBUS4 */
	mmio_write32(gmadr + 0xc5120, 0U); /* GMBUS5 */
	mmio_write32(gmadr + 0xc5100, (1U << 8) | 6U); /* GMBUS0: 50kHz, DPD (pin 6) */
	uint32_t g2 = mmio_read32(gmadr + 0xc5108);
	if (g2 & (1U << 9)) {
		mmio_write32(gmadr + 0xc5104, (1U << 30) | (1U << 27));
		for (int i = 0; i < 50; i++) {
			if ((mmio_read32(gmadr + 0xc5108) & (1U << 9)) == 0)
				break;
			udelay_approx(100U);
		}
	}
	g2 = mmio_read32(gmadr + 0xc5108);
	if (g2 & ((1U << 14) | (1U << 13) | (1U << 12) | (1U << 10) | (1U << 9))) {
		mmio_write32(gmadr + 0xc5104, 1U << 31);
		mmio_write32(gmadr + 0xc5104, 0U);
	}

	/* Indexed 16-byte read from slave 0x50 offset 0x00 with WAIT phase */
	mmio_write32(gmadr + 0xc5104,
		     (1U << 30) | (1U << 26) | (1U << 25) |
		     (16U << 16) | (0U << 8) | (0x50U << 1) | 1U);

	int ok = 1;
	uint32_t xfer = 0;
	while (xfer < 16U) {
		uint32_t st = 0;
		for (int t = 0; t < 500; t++) {
			st = mmio_read32(gmadr + 0xc5108);
			if (st & ((1U << 11) | (1U << 10) | (1U << 13)))
				break;
			udelay_approx(100U);
		}
		if ((st & (1U << 11)) == 0 || (st & (1U << 10)) != 0) {
			ok = 0;
			break;
		}
		uint32_t g3 = mmio_read32(gmadr + 0xc510c);
		for (uint32_t b = 0; b < 4U && xfer < 16U; b++) {
			edid[xfer++] = (uint8_t)(g3 & 0xffU);
			g3 >>= 8;
		}
	}

	if (ok) {
		for (int t = 0; t < 200; t++) {
			if (mmio_read32(gmadr + 0xc5108) & (1U << 14))
				break;
			udelay_approx(100U);
		}
		mmio_write32(gmadr + 0xc5104, (1U << 30) | (1U << 27));
		for (int t = 0; t < 200; t++) {
			if ((mmio_read32(gmadr + 0xc5108) & (1U << 9)) == 0)
				break;
			udelay_approx(100U);
		}
	} else {
		mmio_write32(gmadr + 0xc5104, 1U << 31);
		mmio_write32(gmadr + 0xc5104, 0U);
	}

	uint32_t g2_end = mmio_read32(gmadr + 0xc5108);
	mmio_write32(gmadr + 0xc5100, 0U);
	mmio_write32(gmadr + 0xc5108, 1U << 15); /* Release INUSE */

	out_str("  GMBUS_DPD_EDID ok="); out_dec((uint32_t)ok);
	out_str(" GMBUS2_end="); out_hex32(g2_end);
	out_str(" bytes=");
	for (int i = 0; i < 16; i++)
		out_hex8(edid[i]);
	out_str("\n");
}

static UNUSED void hsw_ddi_d_modeset_prm(uint64_t gmadr, uint64_t fb_bar,
					 uint32_t trans_ddi_val, int is_720p, int hdmi_mode)
{
	/* Disable sequence per Intel Haswell PRM / libgfxinit */
	/* 1. Disable Primary Plane A */
	mmio_write32(gmadr + 0x70180, mmio_read32(gmadr + 0x70180) & ~(1U << 31));
	mmio_write32(gmadr + 0x7019c, 0U);
	(void)mmio_read32(gmadr + 0x7019c);
	udelay_approx(20000U);

	/* 2. Disable PIPEA_CONF & wait status bit 30 == 0 */
	mmio_write32(gmadr + 0x70008, mmio_read32(gmadr + 0x70008) & ~(1U << 31));
	for (int i = 0; i < 50; i++) {
		if ((mmio_read32(gmadr + 0x70008) & (1U << 30)) == 0)
			break;
		udelay_approx(1000U);
	}

	/* 3. Disable TRANS_DDI_FUNC_CTL_A, VIDEO_DIP_CTL_A, TRANS_CLK_SEL_A */
	mmio_write32(gmadr + 0x60400, mmio_read32(gmadr + 0x60400) & ~(1U << 31));
	mmio_write32(gmadr + 0x60200, 0U);
	mmio_write32(gmadr + 0x46140, 0U);

	/* 4. Disable DDI_BUF_CTL_D, wait IDLE bit 7 == 1 */
	mmio_write32(gmadr + 0x64300, mmio_read32(gmadr + 0x64300) & ~(1U << 31));
	for (int i = 0; i < 50; i++) {
		if ((mmio_read32(gmadr + 0x64300) & (1U << 7)) != 0)
			break;
		udelay_approx(1000U);
	}

	/* 5. Set PORT_CLK_SEL_D = NONE BEFORE disabling WRPLL1 */
	mmio_write32(gmadr + 0x4610c, 0xe0000000U);
	(void)mmio_read32(gmadr + 0x4610c);

	/* 6. Disable WRPLL1 & hold 250ms full TMDS drop */
	mmio_write32(gmadr + 0x46040, mmio_read32(gmadr + 0x46040) & ~(1U << 31));
	(void)mmio_read32(gmadr + 0x46040);
	udelay_approx(250000U);

	/* Enable sequence per Intel Haswell PRM / libgfxinit */
	/* 1. Power well, disable VGA overlay, CDCLK_FREQ=540MHz, DDI_BUF_TRANS_D entry 6 */
	mmio_write32(gmadr + 0x45404, 0x80000000U);
	for (int i = 0; i < 50; i++) {
		if ((mmio_read32(gmadr + 0x45404) & (1U << 30)) != 0)
			break;
		udelay_approx(1000U);
	}
	mmio_write32(gmadr + 0x41000, 0x80000000U);
	mmio_write32(gmadr + 0x46200, 0x0000021bU);
	mmio_write32(gmadr + 0x64f68, 0x00ffffffU);
	mmio_write32(gmadr + 0x64f6c, 0x00040006U);

	/* 2. Enable WRPLL1 */
	mmio_write32(gmadr + 0x46040, is_720p ? 0xb0210c14U : 0xb0210614U);
	(void)mmio_read32(gmadr + 0x46040);
	udelay_approx(20000U);

	/* 3. Set PORT_CLK_SEL_D = WRPLL1 */
	mmio_write32(gmadr + 0x4610c, 0x80000000U);
	(void)mmio_read32(gmadr + 0x4610c);

	/* 4. Set TRANS_CLK_SEL_A = DDI_D */
	mmio_write32(gmadr + 0x46140, 0x80000000U);
	(void)mmio_read32(gmadr + 0x46140);

	/* 5. Pipe A timings, PIPEA_MISC, stride/linoff, repaint RGBW + wbinvd */
	if (is_720p) {
		mmio_write32(gmadr + 0x60000, 0x067104ffU);
		mmio_write32(gmadr + 0x60004, 0x067104ffU);
		mmio_write32(gmadr + 0x60008, 0x0595056dU);
		mmio_write32(gmadr + 0x6000c, 0x02ed02cfU);
		mmio_write32(gmadr + 0x60010, 0x02ed02cfU);
		mmio_write32(gmadr + 0x60014, 0x02d902d4U);
		mmio_write32(gmadr + 0x6001c, 0x04ff02cfU);
		mmio_write32(gmadr + 0x45270, 0x000000b2U);
		mmio_write32(gmadr + 0x70030, 0x00000000U);
		mmio_write32(gmadr + 0x70188, 5120U);
		mmio_write32(gmadr + 0x70184, 0x00000000U);
		paint_rgbw_fb(gmadr, fb_bar, 1280U, 720U);
	} else {
		mmio_write32(gmadr + 0x60000, 0x0897077fU);
		mmio_write32(gmadr + 0x60004, 0x0897077fU);
		mmio_write32(gmadr + 0x60008, 0x080307d7U);
		mmio_write32(gmadr + 0x6000c, 0x04640437U);
		mmio_write32(gmadr + 0x60010, 0x04640437U);
		mmio_write32(gmadr + 0x60014, 0x043f043aU);
		mmio_write32(gmadr + 0x6001c, 0x077f0437U);
		mmio_write32(gmadr + 0x45270, 0x00000077U);
		mmio_write32(gmadr + 0x70030, 0x00000000U);
		mmio_write32(gmadr + 0x70188, 7680U);
		mmio_write32(gmadr + 0x70184, 0x00000000U);
		paint_rgbw_fb(gmadr, fb_bar, 1920U, 1080U);
	}
	__asm__ volatile("wbinvd" ::: "memory");

	/* 6. Enable TRANS_DDI_FUNC_CTL_A */
	mmio_write32(gmadr + 0x60400, trans_ddi_val);
	(void)mmio_read32(gmadr + 0x60400);

	/* 7. Enable PIPEA_CONF & wait status bit 30 == 1 */
	mmio_write32(gmadr + 0x70008, 0xc0000000U);
	for (int i = 0; i < 50; i++) {
		if ((mmio_read32(gmadr + 0x70008) & (1U << 30)) != 0)
			break;
		udelay_approx(1000U);
	}

	/* 8. Enable Primary Plane A */
	mmio_write32(gmadr + 0x70180, 0x98000000U);
	mmio_write32(gmadr + 0x7019c, 0x00000000U);
	(void)mmio_read32(gmadr + 0x7019c);

	/* 9. Enable DDI_BUF_CTL_D */
	mmio_write32(gmadr + 0x64300, 0x80000000U);
	(void)mmio_read32(gmadr + 0x64300);
	udelay_approx(2000U);

	/* 10. AVI InfoFrame if HDMI mode */
	if (hdmi_mode) {
		static const uint32_t avi_720p[8] = {
			0x000d0282U, 0x00281033U, 0x00000004U, 0U, 0U, 0U, 0U, 0U
		};
		static const uint32_t avi_1080p[8] = {
			0x000d0282U, 0x00281027U, 0x00000010U, 0U, 0U, 0U, 0U, 0U
		};
		const uint32_t *avi = is_720p ? avi_720p : avi_1080p;
		for (uint32_t i = 0; i < 8U; i++)
			mmio_write32(gmadr + 0x60220 + i * 4U, avi[i]);
		mmio_write32(gmadr + 0x60200, 0x00001000U);
		(void)mmio_read32(gmadr + 0x60200);
	}
}

static UNUSED void hdmi3_step_report(int step, const char *desc, uint64_t gmadr)
{
	uint32_t frm0 = mmio_read32(gmadr + 0x70040);
	udelay_approx(100000U);
	uint32_t frm1 = mmio_read32(gmadr + 0x70040);

	out_str("HDMITEST3_STEP "); out_dec((uint32_t)step);
	out_str(" "); out_str(desc);
	out_str(" TRANS_DDI="); out_hex32(mmio_read32(gmadr + 0x60400));
	out_str(" DDI_D="); out_hex32(mmio_read32(gmadr + 0x64300));
	out_str(" WRPLL1="); out_hex32(mmio_read32(gmadr + 0x46040));
	out_str(" PCLK_D="); out_hex32(mmio_read32(gmadr + 0x4610c));
	out_str(" VGACNTRL="); out_hex32(mmio_read32(gmadr + 0x41000));
	out_str(" SDEISR="); out_hex32(mmio_read32(gmadr + 0xc4000));
	out_str(" dFRM="); out_dec(frm1 - frm0);
	out_str("\n");

	for (int k = 0; k < 50; k++)
		udelay_approx(100000U);
}

static UNUSED void run_hdmitest3(void)
{
	out_str("\n=== Z97-A FULL HDMI ROOT-CAUSE & PRM MODESET (HDMITEST3) ===\n");
	if (pci_read16(0, 2, 0, 0x00) != 0x8086U) {
		out_str("HDMITEST3 ERROR: IGD not found\nZ97A_HDMITEST3_DONE\n");
		return;
	}

	static const uint16_t pci_offs[11] = {
		0x00, 0x04, 0x10, 0x18, 0x50, 0x54, 0x5c, 0xd0, 0xd4, 0xe8, 0xfc
	};
	out_str("IGD PCI:");
	for (int i = 0; i < 11; i++) {
		out_str(" "); out_hex8((uint8_t)pci_offs[i]);
		out_char('='); out_hex32(pci_read32(0, 2, 0, pci_offs[i]));
	}
	out_str("\n");

	uint64_t gmadr = pci_read_bar64(0, 2, 0, 0x10);
	uint64_t fb_bar = pci_read_bar64(0, 2, 0, 0x18);
	if (!gmadr || !fb_bar) {
		out_str("HDMITEST3 ERROR: zero IGD BARs\nZ97A_HDMITEST3_DONE\n");
		return;
	}

	outb(0x01, 0x3c4);
	uint8_t sr01 = inb(0x3c5);
	uint8_t misc = inb(0x3cc);
	out_str("Pre-modeset Display MMIO:\n");
	out_str("  CPU_VGACNTRL(41000)="); out_hex32(mmio_read32(gmadr + 0x41000));
	out_str(" SR01="); out_hex8(sr01);
	out_str(" MISC="); out_hex8(misc); out_str("\n");

	out_str("  SDEISR(c4000)="); out_hex32(mmio_read32(gmadr + 0xc4000));
	out_str(" SDEIMR="); out_hex32(mmio_read32(gmadr + 0xc4004));
	out_str(" SDEIIR="); out_hex32(mmio_read32(gmadr + 0xc4008));
	out_str(" SDEIER="); out_hex32(mmio_read32(gmadr + 0xc400c));
	out_str(" SHOTPLUG="); out_hex32(mmio_read32(gmadr + 0xc4030)); out_str("\n");

	out_str("  PWR_WELL 1..6:");
	for (uint32_t i = 0; i < 6U; i++) {
		out_char(' '); out_hex32(mmio_read32(gmadr + 0x45400U + i * 4U));
	}
	out_str("\n");

	out_str("  FUSE_STRAP(42014)="); out_hex32(mmio_read32(gmadr + 0x42014));
	out_str(" SFUSE(c2014)="); out_hex32(mmio_read32(gmadr + 0xc2014));
	out_str(" PIPEA_MISC="); out_hex32(mmio_read32(gmadr + 0x70030));
	out_str(" PIPEA_STAT="); out_hex32(mmio_read32(gmadr + 0x70024));
	out_str(" PIPEA_DSL="); out_hex32(mmio_read32(gmadr + 0x70000)); out_str("\n");

	out_str("  PCH_DREF="); out_hex32(mmio_read32(gmadr + 0xc6200));
	out_str(" RAWCLK="); out_hex32(mmio_read32(gmadr + 0xc6204));
	out_str(" LCPLL="); out_hex32(mmio_read32(gmadr + 0x130040));
	out_str(" SPLL="); out_hex32(mmio_read32(gmadr + 0x46020)); out_str("\n");

	out_str("  PCLK_SEL B..E=");
	for (uint32_t i = 0; i < 4U; i++) {
		if (i) out_char('/');
		out_hex32(mmio_read32(gmadr + 0x46104U + i * 4U));
	}
	out_str("\n  GTT[0..7]:");
	for (uint32_t i = 0; i < 8U; i++) {
		out_char(' '); out_hex32(mmio_read32(gmadr + 0x200000U + i * 4U));
	}
	out_str("\n");

	gmbus_probe_ddi_d_edid(gmadr);

	hsw_ddi_d_modeset_prm(gmadr, fb_bar, 0xb1010000U, 0, 0);
	hdmi3_step_report(0, "1080p60_dvi_libgfx_exact", gmadr);

	hsw_ddi_d_modeset_prm(gmadr, fb_bar, 0xb0030000U, 0, 1);
	hdmi3_step_report(1, "1080p60_hdmi_avi_prm", gmadr);

	hsw_ddi_d_modeset_prm(gmadr, fb_bar, 0xb0030000U, 1, 1);
	hdmi3_step_report(2, "720p60_hdmi_avi_prm", gmadr);

	out_str("Z97A_HDMITEST3_DONE\n");
}

/* Deep Haswell Display MMIO/GOP/MTRR/SBI Audit & Long-Off + WRPLL2 Test (HDMITEST4) */
static UNUSED uint32_t sbi_read_iclk(uint64_t gmadr, uint16_t offset, uint32_t *out_sts)
{
	for (int i = 0; i < 1000; i++) {
		if ((mmio_read32(gmadr + 0xc6008U) & 1U) == 0)
			break;
		udelay_approx(5U);
	}
	mmio_write32(gmadr + 0xc6000U, ((uint32_t)offset) << 16);
	mmio_write32(gmadr + 0xc6008U, (6U << 8) | 1U); /* ICLK CRRD | BUSY */
	for (int i = 0; i < 1000; i++) {
		if ((mmio_read32(gmadr + 0xc6008U) & 1U) == 0)
			break;
		udelay_approx(5U);
	}
	if (out_sts)
		*out_sts = mmio_read32(gmadr + 0xc6008U);
	return mmio_read32(gmadr + 0xc6004U);
}

static UNUSED void dump_mmio_list(uint64_t gmadr, const char *hdr, const uint32_t *offs, int count)
{
	out_str(hdr);
	for (int i = 0; i < count; i++) {
		if ((i % 4) == 0)
			out_str("\n ");
		out_str(" ");
		out_hex32(offs[i]);
		out_char('=');
		out_hex32(mmio_read32(gmadr + offs[i]));
	}
	out_str("\n");
}

static UNUSED void hsw_ddi_d_modeset_ex(uint64_t gmadr, uint64_t fb_bar,
					uint32_t pll_reg_off, uint32_t pll_val,
					uint32_t port_clk_sel_val, uint32_t trans_ddi_val,
					int hdmi_mode, uint32_t off_hold_ds)
{
	/* Full PRM disable of Pipe A / Plane A / Transcoder A / DDI_D / WRPLL1 & WRPLL2 */
	mmio_write32(gmadr + 0x70180U, mmio_read32(gmadr + 0x70180U) & ~(1U << 31));
	mmio_write32(gmadr + 0x7019cU, 0U);
	(void)mmio_read32(gmadr + 0x7019cU);
	udelay_approx(20000U);

	mmio_write32(gmadr + 0x70008U, mmio_read32(gmadr + 0x70008U) & ~(1U << 31));
	for (int i = 0; i < 50; i++) {
		if ((mmio_read32(gmadr + 0x70008U) & (1U << 30)) == 0)
			break;
		udelay_approx(1000U);
	}

	mmio_write32(gmadr + 0x60400U, mmio_read32(gmadr + 0x60400U) & ~(1U << 31));
	mmio_write32(gmadr + 0x60200U, 0U);
	mmio_write32(gmadr + 0x46140U, 0U);

	mmio_write32(gmadr + 0x64300U, mmio_read32(gmadr + 0x64300U) & ~(1U << 31));
	for (int i = 0; i < 50; i++) {
		if ((mmio_read32(gmadr + 0x64300U) & (1U << 7)) != 0)
			break;
		udelay_approx(1000U);
	}

	mmio_write32(gmadr + 0x4610cU, 0xe0000000U);
	(void)mmio_read32(gmadr + 0x4610cU);

	mmio_write32(gmadr + 0x46040U, mmio_read32(gmadr + 0x46040U) & ~(1U << 31));
	mmio_write32(gmadr + 0x46060U, mmio_read32(gmadr + 0x46060U) & ~(1U << 31));
	(void)mmio_read32(gmadr + 0x46040U);

	for (uint32_t d = 0; d < off_hold_ds; d++)
		udelay_approx(100000U);

	/* Enable sequence: Power well, VGA off, CDCLK, DDI_BUF_TRANS_D entry 6 */
	mmio_write32(gmadr + 0x45404U, 0x80000000U);
	for (int i = 0; i < 50; i++) {
		if ((mmio_read32(gmadr + 0x45404U) & (1U << 30)) != 0)
			break;
		udelay_approx(1000U);
	}
	mmio_write32(gmadr + 0x41000U, 0x80000000U);
	mmio_write32(gmadr + 0x46200U, 0x0000021bU);
	mmio_write32(gmadr + 0x64f68U, 0x00ffffffU);
	mmio_write32(gmadr + 0x64f6cU, 0x00040006U);

	/* Enable selected WRPLL1 or WRPLL2 */
	mmio_write32(gmadr + pll_reg_off, pll_val);
	(void)mmio_read32(gmadr + pll_reg_off);
	udelay_approx(20000U);

	/* Port clock -> Transcoder clock */
	mmio_write32(gmadr + 0x4610cU, port_clk_sel_val);
	(void)mmio_read32(gmadr + 0x4610cU);
	mmio_write32(gmadr + 0x46140U, 0x80000000U);
	(void)mmio_read32(gmadr + 0x46140U);

	/* 1080p60 timings & fresh RGBW framebuffer + wbinvd */
	mmio_write32(gmadr + 0x60000U, 0x0897077fU);
	mmio_write32(gmadr + 0x60004U, 0x0897077fU);
	mmio_write32(gmadr + 0x60008U, 0x080307d7U);
	mmio_write32(gmadr + 0x6000cU, 0x04640437U);
	mmio_write32(gmadr + 0x60010U, 0x04640437U);
	mmio_write32(gmadr + 0x60014U, 0x043f043aU);
	mmio_write32(gmadr + 0x6001cU, 0x077f0437U);
	mmio_write32(gmadr + 0x45270U, 0x00000077U);
	mmio_write32(gmadr + 0x70030U, 0x00000000U);
	mmio_write32(gmadr + 0x70188U, 7680U);
	mmio_write32(gmadr + 0x70184U, 0x00000000U);
	paint_rgbw_fb(gmadr, fb_bar, 1920U, 1080U);
	__asm__ volatile("wbinvd" ::: "memory");

	mmio_write32(gmadr + 0x60400U, trans_ddi_val);
	(void)mmio_read32(gmadr + 0x60400U);

	mmio_write32(gmadr + 0x70008U, 0xc0000000U);
	for (int i = 0; i < 50; i++) {
		if ((mmio_read32(gmadr + 0x70008U) & (1U << 30)) != 0)
			break;
		udelay_approx(1000U);
	}

	mmio_write32(gmadr + 0x70180U, 0x98000000U);
	mmio_write32(gmadr + 0x7019cU, 0x00000000U);
	(void)mmio_read32(gmadr + 0x7019cU);

	mmio_write32(gmadr + 0x64300U, 0x80000000U);
	(void)mmio_read32(gmadr + 0x64300U);
	udelay_approx(2000U);

	if (hdmi_mode) {
		static const uint32_t avi_1080p[8] = {
			0x000d0282U, 0x00281027U, 0x00000010U, 0U, 0U, 0U, 0U, 0U
		};
		for (uint32_t i = 0; i < 8U; i++)
			mmio_write32(gmadr + 0x60220U + i * 4U, avi_1080p[i]);
		mmio_write32(gmadr + 0x60200U, 0x00001000U);
		(void)mmio_read32(gmadr + 0x60200U);
	}
}

static UNUSED void hdmi4_step_report(int step, const char *desc, uint64_t gmadr)
{
	uint32_t frm0 = mmio_read32(gmadr + 0x70040U);
	udelay_approx(100000U);
	uint32_t frm1 = mmio_read32(gmadr + 0x70040U);

	out_str("HDMITEST4_STEP "); out_dec((uint32_t)step);
	out_str(" "); out_str(desc);
	out_str(" TRANS_DDI="); out_hex32(mmio_read32(gmadr + 0x60400U));
	out_str(" DDI_D="); out_hex32(mmio_read32(gmadr + 0x64300U));
	out_str(" WRPLL1="); out_hex32(mmio_read32(gmadr + 0x46040U));
	out_str(" WRPLL2="); out_hex32(mmio_read32(gmadr + 0x46060U));
	out_str(" PCLK_D="); out_hex32(mmio_read32(gmadr + 0x4610cU));
	out_str(" PWR1="); out_hex32(mmio_read32(gmadr + 0x45400U));
	out_str(" dFRM="); out_dec(frm1 - frm0);
	out_str("\n");

	for (int k = 0; k < 50; k++)
		udelay_approx(100000U);
}

static UNUSED void run_hdmitest4(void)
{
	out_str("\n=== Z97-A DEEP GOP/MTRR/MMIO/SBI AUDIT & LONG-OFF HDMI TEST (HDMITEST4) ===\n");
	if (pci_read16(0, 2, 0, 0x00) != 0x8086U) {
		out_str("HDMITEST4 ERROR: IGD not found\nZ97A_HDMITEST4_DONE\n");
		return;
	}

	/* 1. Check UEFI GOP protocol */
	EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
	EFI_STATUS gop_st = (gBS && gBS->LocateProtocol) ?
		gBS->LocateProtocol(&gGopGuid, NULL, (void **)&gop) : (EFI_STATUS)0x800000000000000eULL;
	out_str("GOP st=0x"); out_hex64(gop_st);
	if (gop_st == 0 && gop && gop->Mode && gop->Mode->Info) {
		out_str(" mode="); out_dec(gop->Mode->Mode);
		out_str("/"); out_dec(gop->Mode->MaxMode);
		out_str(" FB=0x"); out_hex64(gop->Mode->FrameBufferBase);
		out_str(" sz=0x"); out_hex64(gop->Mode->FrameBufferSize);
		out_str(" WxH="); out_dec(gop->Mode->Info->HorizontalResolution);
		out_char('x'); out_dec(gop->Mode->Info->VerticalResolution);
		out_str(" ppsl="); out_dec(gop->Mode->Info->PixelsPerScanLine);
		out_str(" fmt="); out_dec(gop->Mode->Info->PixelFormat);
	}
	out_str("\n");

	uint64_t gmadr = pci_read_bar64(0, 2, 0, 0x10);
	uint64_t fb_bar = pci_read_bar64(0, 2, 0, 0x18);
	if (!gmadr || !fb_bar) {
		out_str("HDMITEST4 ERROR: zero IGD BARs\nZ97A_HDMITEST4_DONE\n");
		return;
	}

	/* 2. Verify GMADR write/readback + BDSM/MSAC + MTRRs (do NOT dereference BDSM: unmapped in EDK2) */
	uint32_t bdsm = pci_read32(0, 2, 0, 0x5c) & ~0xfffffU;
	uint32_t msac = pci_read32(0, 2, 0, 0x60);
	volatile uint32_t *p_gmadr = (volatile uint32_t *)(uintptr_t)fb_bar;
	uint32_t saved_px = *p_gmadr;
	*p_gmadr = 0x00aa55ccU;
	__asm__ volatile("wbinvd" ::: "memory");
	uint32_t rd_gmadr = *p_gmadr;
	*p_gmadr = saved_px;
	__asm__ volatile("wbinvd" ::: "memory");

	out_str("FB_ALIAS: BDSM=0x"); out_hex32(bdsm);
	out_str(" MSAC=0x"); out_hex32(msac);
	out_str(" wr=00aa55cc rd_gmadr=0x"); out_hex32(rd_gmadr); out_str("\n");

	out_str("MTRR_DEF_TYPE(2ff)=0x"); out_hex64(rdmsr64(0x2ffU)); out_str("\n");
	uint32_t mtrr_vcnt = (uint32_t)(rdmsr64(0xfeU) & 0xffU);
	if (mtrr_vcnt > 16U)
		mtrr_vcnt = 16U;
	for (uint32_t i = 0; i < mtrr_vcnt; i++) {
		uint64_t base = rdmsr64(0x200U + i * 2U);
		uint64_t mask = rdmsr64(0x201U + i * 2U);
		if (base || mask) {
			out_str("  MTRR["); out_dec(i); out_str("] base=0x"); out_hex64(base);
			out_str(" mask=0x"); out_hex64(mask); out_str("\n");
		}
	}

	/* 2b. Read-only pre-Step-0 PCI, Core Display, and DMIBAR diagnostics */
	out_str("IGD 00:02.0 CMDSTS=0x"); out_hex32(pci_read32(0, 2, 0, 0x04)); out_str("\n");
	out_str("HDA 00:03.0 VIDDID=0x"); out_hex32(pci_read32(0, 3, 0, 0x00));
	out_str(" CMDSTS=0x"); out_hex32(pci_read32(0, 3, 0, 0x04));
	out_str(" BAR0=0x"); out_hex32(pci_read32(0, 3, 0, 0x10));
	out_str(" PMCSR=0x"); out_hex32(pci_read32(0, 3, 0, 0x54)); out_str("\n");

	static const uint32_t pre_disp_regs[12] = {
		0x45400U, 0x45404U, 0x46040U, 0x46060U, 0x4610cU, 0x46140U,
		0x46200U, 0x60400U, 0x64300U, 0x70008U, 0x70180U, 0x7019cU
	};
	dump_mmio_list(gmadr, "Core Display Pre:", pre_disp_regs, 12);

	uint64_t dmibar = (uint64_t)(pci_read32(0, 0, 0, 0x68) & ~0xfffU);
	if (!dmibar)
		dmibar = 0xfed18000ULL;
	static const uint32_t dmi_pre_regs[18] = {
		0x0014U, 0x0020U, 0x0084U, 0x0088U, 0x0098U, 0x01c4U,
		0x01d0U, 0x0250U, 0x0254U, 0x0258U, 0x071cU, 0x0720U,
		0x080cU, 0x082cU, 0x0910U, 0x0930U, 0x0c0cU, 0x0c28U
	};
	dump_mmio_list(dmibar, "DMIBAR Pre:", dmi_pre_regs, 18);

	/* 3. Extended Display / DDI / PF / Audio / PCH-SBI / Buffer Trans dumps */
	static const uint32_t misc_regs[11] = {
		0x42000U, 0x42014U, 0x42020U, 0x42080U, 0x42084U, 0x420c0U,
		0x45000U, 0x46000U, 0x46408U, 0x09840U, 0x6c00cU
	};
	dump_mmio_list(gmadr, "Display Misc:", misc_regs, 11);

	static const uint32_t ddi_regs[14] = {
		0x64000U, 0x64100U, 0x64200U, 0x64300U, 0x64400U,
		0x64040U, 0x64140U, 0x64240U, 0x64340U, 0x64440U,
		0x64144U, 0x64244U, 0x64344U, 0x64444U
	};
	dump_mmio_list(gmadr, "DDI/DP_TP A..E:", ddi_regs, 14);

	static const uint32_t pipe_pf_regs[6] = {
		0x68000U, 0x68070U, 0x68074U, 0x60030U, 0x701c4U, 0x49028U
	};
	dump_mmio_list(gmadr, "PF/PipeA/CSC:", pipe_pf_regs, 6);

	static const uint32_t aud_regs[10] = {
		0x65010U, 0x65050U, 0x65060U, 0x6506cU, 0x65070U,
		0x650b4U, 0x650c0U, 0x650c4U, 0xc2000U, 0xc2004U
	};
	dump_mmio_list(gmadr, "AUD & SOUTH_CHICKEN:", aud_regs, 10);

	static const uint16_t sbi_offs[11] = {
		0x0200U, 0x0204U, 0x020cU, 0x0600U, 0x060cU, 0x1f00U,
		0x2000U, 0x2004U, 0x200cU, 0x2100U, 0x2104U
	};
	out_str("PCH SBI (ICLK CRRD):");
	for (int i = 0; i < 11; i++) {
		uint32_t sts = 0;
		uint32_t val = sbi_read_iclk(gmadr, sbi_offs[i], &sts);
		if ((i % 4) == 0) out_str("\n ");
		out_str(" "); out_hex16(sbi_offs[i]); out_char('=');
		out_hex32(val); out_str("(s:"); out_hex8((uint8_t)(sts & 0xffU)); out_char(')');
	}
	out_str("\nDDI_BUF_TRANS_D[0..9]:");
	for (uint32_t i = 0; i < 20U; i++) {
		if ((i % 4U) == 0U) out_str("\n ");
		out_str(" "); out_dec(i / 2U); out_char('.'); out_dec(i & 1U); out_char('=');
		out_hex32(mmio_read32(gmadr + 0x64f20U + i * 4U));
	}
	out_str("\n");

	/* 4. Step 0: Post-satatest3 check (fresh RGBW on current mode) */
	{
		uint32_t srcsz = mmio_read32(gmadr + 0x6001cU);
		uint32_t cw = ((srcsz >> 16) & 0xfffU) + 1U;
		uint32_t ch = (srcsz & 0xfffU) + 1U;
		paint_rgbw_fb(gmadr, fb_bar, cw, ch);
		__asm__ volatile("wbinvd" ::: "memory");
	}
	hdmi4_step_report(0, "post_satatest3_check", gmadr);

	/* 5. Step 1: Apply PeiGfxDriver missing regs + 2.5s FULL OFF + clean 1080p60 DVI */
	mmio_write32(gmadr + 0x45400U, 0x80000000U);
	for (int i = 0; i < 50; i++) {
		if ((mmio_read32(gmadr + 0x45400U) & (1U << 30)) != 0)
			break;
		udelay_approx(1000U);
	}
	mmio_write32(gmadr + 0x42080U, mmio_read32(gmadr + 0x42080U) | 0x4000U);
	mmio_write32(gmadr + 0x46408U, mmio_read32(gmadr + 0x46408U) | 0x10U);
	mmio_write32(gmadr + 0x09840U, mmio_read32(gmadr + 0x09840U) | 0x80U);
	mmio_write32(gmadr + 0x64340U, 0U);
	mmio_write32(gmadr + 0x68000U, 0U);
	hsw_ddi_d_modeset_ex(gmadr, fb_bar, 0x46040U, 0xb0210614U, 0x80000000U, 0xb1010000U, 0, 25U);
	hdmi4_step_report(1, "peigfx_regs_long_off_1080p_dvi", gmadr);

	/* 6. Step 2: 2.5s FULL OFF + WRPLL2 (0x46060) 1080p60 DVI */
	hsw_ddi_d_modeset_ex(gmadr, fb_bar, 0x46060U, 0xb0210614U, 0xa0000000U, 0xb1010000U, 0, 25U);
	hdmi4_step_report(2, "wrpll2_1080p_dvi", gmadr);

	/* 7. Step 3: Disable Audio ELD/config + 2.5s FULL OFF + WRPLL1 1080p60 HDMI + AVI */
	mmio_write32(gmadr + 0x650c0U, 0U);
	mmio_write32(gmadr + 0x65000U, 0U);
	hsw_ddi_d_modeset_ex(gmadr, fb_bar, 0x46040U, 0xb0210614U, 0x80000000U, 0xb0030000U, 1, 25U);
	hdmi4_step_report(3, "wrpll1_1080p_hdmi_no_aud", gmadr);

	out_str("Z97A_HDMITEST4_DONE\n");
}

/* Deep SATA + RCBA DMI/PM + PCIe RP7 I226-V Audit & Clean Gen3/Gen2 COMRESET Test (SATATEST2) */
static UNUSED void ahci_p1_comreset_sctl(uint64_t abar, uint32_t sctl_base)
{
	/* Stop Port 1 cleanly: clear ST then FRE */
	uint32_t cmd = mmio_read32(abar + 0x198);
	if (cmd & 1U) {
		mmio_write32(abar + 0x198, cmd & ~1U);
		for (int i = 0; i < 500; i++) {
			if ((mmio_read32(abar + 0x198) & (1U << 15)) == 0)
				break;
			udelay_approx(1000U);
		}
	}
	cmd = mmio_read32(abar + 0x198);
	if (cmd & (1U << 4)) {
		mmio_write32(abar + 0x198, cmd & ~(1U << 4));
		for (int i = 0; i < 500; i++) {
			if ((mmio_read32(abar + 0x198) & (1U << 14)) == 0)
				break;
			udelay_approx(1000U);
		}
	}

	/* Clear PxSERR and PxIS */
	mmio_write32(abar + 0x1b0, 0xffffffffU);
	mmio_write32(abar + 0x190, 0xffffffffU);

	/* Issue COMRESET with caller-specified SCTL base (e.g. 0x300, 0x320, or stock 0x700) */
	mmio_write32(abar + 0x1ac, sctl_base | 1U);
	(void)mmio_read32(abar + 0x1ac);
	udelay_approx(5000U);
	mmio_write32(abar + 0x1ac, sctl_base);
	(void)mmio_read32(abar + 0x1ac);

	for (int i = 0; i < 1000; i++) {
		if ((mmio_read32(abar + 0x1a8) & 0x0fU) == 3U)
			break;
		udelay_approx(1000U);
	}

	mmio_write32(abar + 0x1b0, 0xffffffffU);
	mmio_write32(abar + 0x198, mmio_read32(abar + 0x198) | (1U << 4) | (1U << 2) | (1U << 1));
	for (int i = 0; i < 1000; i++) {
		if ((mmio_read32(abar + 0x1a0) & 0x88U) == 0)
			break;
		udelay_approx(1000U);
	}
	mmio_write32(abar + 0x198, mmio_read32(abar + 0x198) | 1U);

	out_str("  COMRESET(sctl=0x"); out_hex32(sctl_base);
	out_str("): PxSSTS="); out_hex32(mmio_read32(abar + 0x1a8));
	out_str(" PxSCTL="); out_hex32(mmio_read32(abar + 0x1ac));
	out_str(" PxTFD="); out_hex32(mmio_read32(abar + 0x1a0));
	out_str(" PxSERR="); out_hex32(mmio_read32(abar + 0x1b0));
	out_str("\n");
}

static UNUSED void ahci_p1_comreset(uint64_t abar, uint32_t spd_nibble)
{
	ahci_p1_comreset_sctl(abar, 0x00000300U | ((spd_nibble & 0xfU) << 4));
}

static UNUSED int sata_run_4k_pass(const char *label, EFI_BLOCK_IO_PROTOCOL *bio, uint64_t abar, uint64_t count)
{
	uint32_t blk_sz = bio->Media->BlockSize;
	uint64_t lbas_per_4k = 4096ULL / blk_sz;

	mmio_write32(abar + 0x1b0, 0xffffffffU);
	uint32_t ssts_pre = mmio_read32(abar + 0x1a8);
	uint32_t serr_pre = mmio_read32(abar + 0x1b0);

	EFI_STATUS st = 0;
	uint64_t ok = 0;
	for (uint64_t i = 0; i < count; i++) {
		if ((i + 1ULL) * lbas_per_4k > bio->Media->LastBlock)
			break;
		st = bio->ReadBlocks(bio, bio->Media->MediaId, i * lbas_per_4k, 4096ULL, g_blk_buf);
		if (st != 0)
			break;
		ok++;
	}

	uint32_t pxis = mmio_read32(abar + 0x190);
	uint32_t pxtfd = mmio_read32(abar + 0x1a0);
	uint32_t pxssts = mmio_read32(abar + 0x1a8);
	uint32_t pxserr = mmio_read32(abar + 0x1b0);

	out_str("SATATEST2 "); out_str(label);
	out_str(": pre_ssts="); out_hex32(ssts_pre);
	out_str(" pre_serr="); out_hex32(serr_pre);
	out_str(" -> st=0x"); out_hex32((uint32_t)st);
	out_str(" ok="); out_dec((uint32_t)ok); out_char('/'); out_dec((uint32_t)count);
	out_str(" PxIS="); out_hex32(pxis);
	out_str(" PxTFD="); out_hex32(pxtfd);
	out_str(" PxSSTS="); out_hex32(pxssts);
	out_str(" PxSERR="); out_hex32(pxserr);
	out_str("\n");

	return (st != 0 || pxserr != 0 || (pxssts & 0x0fU) != 3U) ? 1 : 0;
}

static UNUSED void run_satatest2(void)
{
	out_str("\n=== Z97-A DEEP RCBA/DMIBAR/RP7 & CLEAN SATA ISOLATION (SATATEST2) ===\n");
	pci_init_mmcfg();

	/* 1. RCBA DMI / PM registers */
	static const uint16_t rcba_offs[33] = {
		0x2088, 0x20ac, 0x21a4, 0x21a8, 0x2320, 0x2324, 0x2340, 0x2348,
		0x2b00, 0x2b14, 0x2b20, 0x2b24, 0x2b28, 0x2b2c, 0x2b54, 0x2b68,
		0x3314, 0x3318, 0x3324, 0x3340, 0x3368, 0x3378, 0x337c, 0x33c0,
		0x33c8, 0x33d0, 0x33d4, 0x33f0, 0x3a28, 0x3a2c, 0x3a80, 0x3a84,
		0x3a88
	};
	out_str("RCBA DMI/PM:");
	for (int i = 0; i < 33; i++) {
		if ((i % 4) == 0) out_str("\n ");
		out_str(" "); out_hex16(rcba_offs[i]); out_char('=');
		out_hex32(mmio_read32(RCBA_BASE + rcba_offs[i]));
	}
	out_str("\n");

	/* 2. DMIBAR registers */
	uint64_t dmibar = (uint64_t)(pci_read32(0, 0, 0, 0x68) & ~0xfffU);
	if (!dmibar) dmibar = 0xfed18000ULL;
	static const uint16_t dmi_offs[12] = {
		0x0014, 0x0084, 0x0088, 0x0098, 0x022c, 0x0230,
		0x0234, 0x0238, 0x0250, 0x0254, 0x0258, 0x025c
	};
	out_str("DMIBAR (0x"); out_hex32((uint32_t)dmibar); out_str("):");
	for (int i = 0; i < 12; i++) {
		if ((i % 4) == 0) out_str("\n ");
		out_str(" "); out_hex16(dmi_offs[i]); out_char('=');
		out_hex32(mmio_read32(dmibar + dmi_offs[i]));
	}
	out_str("\n");

	/* 3. PCIe RP7 (00:1c.2 / I226-V lane 0xe90022xx) IOBP & config/AER */
	static const uint32_t rp7_iobp[12] = {
		0xe9002200U, 0xe9002208U, 0xe900222cU, 0xe9002238U,
		0xe9002240U, 0xe9002288U, 0xe900228cU, 0xe9002290U,
		0xe9002340U, 0xe9002344U, 0xe9002378U, 0xe900237cU
	};
	out_str("RP7 IOBP (0xe90022xx/23xx):");
	for (int i = 0; i < 12; i++) {
		uint16_t sts = 0;
		uint32_t v = pch_iobp_read(rp7_iobp[i], &sts);
		if ((i % 4) == 0) out_str("\n ");
		out_str(" "); out_hex16((uint16_t)(rp7_iobp[i] & 0xffffU));
		out_char('='); out_hex32(v);
	}
	out_str("\nRP7 00:1c.2 cfg: ");
	out_str("50="); out_hex32(pci_read32(0, 0x1c, 2, 0x50));
	out_str(" 70="); out_hex32(pci_read32(0, 0x1c, 2, 0x70));
	out_str(" AER_104="); out_hex32(pci_read32(0, 0x1c, 2, 0x104));
	out_str(" AER_110="); out_hex32(pci_read32(0, 0x1c, 2, 0x110));
	out_str("\n");

	/* 4. Clean AHCI Port 1 Gen3 vs 4K vs Gen2 tests (do NOT touch SIR 0x70) */
	uint64_t abar = pci_read32(0, 0x1f, 2, 0x24) & ~0x7ffULL;
	if (!abar || !gBS || !gBS->LocateHandleBuffer) {
		out_str("SATATEST2 ERROR: missing ABAR/BootServices\nZ97A_SATATEST2_DONE\n");
		return;
	}

	EFI_BLOCK_IO_PROTOCOL *target_bio = NULL;
	uint64_t nh = 0;
	EFI_HANDLE *handles = NULL;
	if (gBS->LocateHandleBuffer(2, &gBlockIoGuid, NULL, &nh, &handles) == 0 && handles) {
		for (uint64_t i = 0; i < nh; i++) {
			EFI_BLOCK_IO_PROTOCOL *bio = NULL;
			if (gBS->HandleProtocol(handles[i], &gBlockIoGuid, (void **)&bio) != 0 || !bio || !bio->Media)
				continue;
			if (bio->Media->LogicalPartition || !bio->Media->MediaPresent || bio->Media->BlockSize != 512U)
				continue;
			target_bio = bio;
			out_str("Found physical BlockIO handle "); out_dec((uint32_t)i);
			out_str(" LastBlock="); out_dec((uint32_t)bio->Media->LastBlock); out_str("\n");
			break;
		}
		gBS->FreePool(handles);
	}

	if (!target_bio) {
		out_str("SATATEST2 ERROR: no 512B physical BlockIO handle\nZ97A_SATATEST2_DONE\n");
		return;
	}

	/* Ensure clock gating is at stock default */
	pci_write8(0, 0x1f, 2, 0xa0, 0x70);
	pci_write32(0, 0x1f, 2, 0xa4, 0x3f00bf1fU);
	pci_write32(0, 0x1f, 2, 0x300, pci_read32(0, 0x1f, 2, 0x300) | 0xe0000000U);

	/* Pass 1: Clean Gen3 (spd=0) COMRESET + Bio->Reset + 16 MiB (256 x 64K) */
	ahci_p1_comreset(abar, 0U);
	if (target_bio->Reset) {
		EFI_STATUS rst = target_bio->Reset(target_bio, 0);
		out_str("  Pass1 Bio->Reset st=0x"); out_hex32((uint32_t)rst);
		out_str(" PxSSTS="); out_hex32(mmio_read32(abar + 0x1a8)); out_str("\n");
	}
	uint32_t pass1_ssts = mmio_read32(abar + 0x1a8);
	sata_run_pass("t2_gen3_64k_16m", target_bio, abar, 0, 256);
	uint32_t pass1_err = mmio_read32(abar + 0x190) & 0x0c000000U;
	uint32_t pass1_serr = mmio_read32(abar + 0x1b0);

	/* Pass 2: If Pass 1 had any error or failure, test Gen3 4 KiB transfers (1024 x 4K = 4 MiB) */
	if (pass1_err || pass1_serr || (pass1_ssts & 0xf0U) == 0x30U) {
		ahci_p1_comreset(abar, 0U);
		if (target_bio->Reset)
			(void)target_bio->Reset(target_bio, 0);
		sata_run_4k_pass("gen3_4k_4m", target_bio, abar, 1024);
	}

	/* Pass 3: Clean Gen2 (spd=2) COMRESET + Bio->Reset + 16 MiB (256 x 64K) */
	ahci_p1_comreset(abar, 2U);
	if (target_bio->Reset) {
		EFI_STATUS rst = target_bio->Reset(target_bio, 0);
		out_str("  Pass3 Bio->Reset st=0x"); out_hex32((uint32_t)rst);
		out_str(" PxSSTS="); out_hex32(mmio_read32(abar + 0x1a8)); out_str("\n");
	}
	sata_run_pass("t2_gen2_64k_16m", target_bio, abar, 0, 256);

	/* Pass 4: Restore clean Gen3 (spd=0) so Port 1 is back online at Gen3 */
	ahci_p1_comreset(abar, 0U);
	if (target_bio->Reset) {
		EFI_STATUS rst = target_bio->Reset(target_bio, 0);
		out_str("  Restore Gen3 Bio->Reset st=0x"); out_hex32((uint32_t)rst);
		out_str(" PxSSTS="); out_hex32(mmio_read32(abar + 0x1a8)); out_str("\n");
	}

	out_str("Z97A_SATATEST2_DONE\n");
}

/* Live RAM Fix & Verification: Desktop PCH Clock Gating + DMI ASPM OFF (SATATEST3) */
static UNUSED void run_satatest3(void)
{
	out_str("\n=== Z97-A LIVE DESKTOP CG + DMI ASPM FIX VERIFICATION (SATATEST3) ===\n");
	pci_init_mmcfg();

	uint64_t dmibar = (uint64_t)(pci_read32(0, 0, 0, 0x68) & ~0xfffU);
	if (!dmibar) dmibar = 0xfed18000ULL;
	uint64_t abar = pci_read32(0, 0x1f, 2, 0x24) & ~0x7ffULL;

	out_str("BEFORE:\n");
	out_str("  00:1f.0: a0="); out_hex16(pci_read16(0, 0x1f, 0, 0xa0));
	out_str(" a2="); out_hex16(pci_read16(0, 0x1f, 0, 0xa2));
	out_str(" a4="); out_hex32(pci_read32(0, 0x1f, 0, 0xa4)); out_str("\n");

	out_str("  RCBA: 0900="); out_hex32(mmio_read32(RCBA_BASE + 0x0900U));
	out_str(" 21a8="); out_hex32(mmio_read32(RCBA_BASE + 0x21a8U));
	out_str(" 2234="); out_hex32(mmio_read32(RCBA_BASE + 0x2234U));
	out_str(" 2304="); out_hex32(mmio_read32(RCBA_BASE + 0x2304U));
	out_str(" 341c="); out_hex32(mmio_read32(RCBA_BASE + 0x341cU));
	out_str(" 38c0="); out_hex32(mmio_read32(RCBA_BASE + 0x38c0U)); out_str("\n");

	out_str("  DMIBAR: 0088="); out_hex32(mmio_read32(dmibar + 0x0088U));
	out_str(" 0098="); out_hex32(mmio_read32(dmibar + 0x0098U));
	out_str(" UESTS(1c4)="); out_hex32(mmio_read32(dmibar + 0x01c4U));
	out_str(" CESTS(1d0)="); out_hex32(mmio_read32(dmibar + 0x01d0U)); out_str("\n");

	out_str("  RP7(00:1c.2): 50="); out_hex16(pci_read16(0, 0x1c, 2, 0x50));
	out_str(" 52="); out_hex16(pci_read16(0, 0x1c, 2, 0x52));
	out_str(" AER104="); out_hex32(pci_read32(0, 0x1c, 2, 0x104));
	out_str(" AER110="); out_hex32(pci_read32(0, 0x1c, 2, 0x110)); out_str("\n  RP7 d0..f4:");
	for (uint16_t r = 0xd0; r <= 0xf4; r += 4) {
		out_char(' '); out_hex8((uint8_t)r); out_char('='); out_hex32(pci_read32(0, 0x1c, 2, r));
	}
	out_str("\n  I226-V(05:00.0): a8="); out_hex32(pci_read32(5, 0, 0, 0xa8));
	out_str(" b0="); out_hex32(pci_read32(5, 0, 0, 0xb0)); out_str("\n");

	out_str("  SATA(00:1f.2): 90="); out_hex16(pci_read16(0, 0x1f, 2, 0x90));
	out_str(" 92="); out_hex16(pci_read16(0, 0x1f, 2, 0x92));
	out_str(" 94="); out_hex32(pci_read32(0, 0x1f, 2, 0x94));
	if (abar) {
		out_str(" P1(CMD="); out_hex32(mmio_read32(abar + 0x198U));
		out_str(" SSTS="); out_hex32(mmio_read32(abar + 0x1a8U));
		out_str(" SCTL="); out_hex32(mmio_read32(abar + 0x1acU));
		out_str(" SERR="); out_hex32(mmio_read32(abar + 0x1b0U)); out_char(')');
	}
	out_str("\n");

	/* Apply stock Z97-A Desktop PCH clock gating + DMI ASPM OFF live in RAM (no DMI retrain) */
	mmio_write16(RCBA_BASE + 0x21a8U, (uint16_t)(mmio_read16(RCBA_BASE + 0x21a8U) & ~0x0003U));
	mmio_write16(dmibar + 0x0088U, (uint16_t)(mmio_read16(dmibar + 0x0088U) & ~0x0003U));
	pci_write16(0, 0x1f, 0, 0xa0, (uint16_t)(pci_read16(0, 0x1f, 0, 0xa0) & ~0x5804U));
	mmio_write32(RCBA_BASE + 0x2234U, 0x00000000U);
	mmio_write32(RCBA_BASE + 0x0900U, mmio_read32(RCBA_BASE + 0x0900U) & ~(1U << 14));
	mmio_write32(RCBA_BASE + 0x341cU, 0x00000000U);
	mmio_write32(RCBA_BASE + 0x38c0U, 0x00000008U);

	mmio_write32(dmibar + 0x01c4U, 0xffffffffU);
	mmio_write32(dmibar + 0x01d0U, 0xffffffffU);
	pci_write32(0, 0x1c, 2, 0x104, 0xffffffffU);
	pci_write32(0, 0x1c, 2, 0x110, 0xffffffffU);
	if (abar)
		mmio_write32(abar + 0x1b0U, 0xffffffffU);

	out_str("AFTER:\n");
	out_str("  00:1f.0: a0="); out_hex16(pci_read16(0, 0x1f, 0, 0xa0));
	out_str(" RCBA: 0900="); out_hex32(mmio_read32(RCBA_BASE + 0x0900U));
	out_str(" 21a8="); out_hex32(mmio_read32(RCBA_BASE + 0x21a8U));
	out_str(" 2234="); out_hex32(mmio_read32(RCBA_BASE + 0x2234U));
	out_str(" 341c="); out_hex32(mmio_read32(RCBA_BASE + 0x341cU));
	out_str(" 38c0="); out_hex32(mmio_read32(RCBA_BASE + 0x38c0U)); out_str("\n");
	out_str("  DMIBAR: 0088="); out_hex32(mmio_read32(dmibar + 0x0088U));
	out_str(" 1c4="); out_hex32(mmio_read32(dmibar + 0x01c4U));
	out_str(" 1d0="); out_hex32(mmio_read32(dmibar + 0x01d0U));
	out_str(" 1c.2: 104="); out_hex32(pci_read32(0, 0x1c, 2, 0x104));
	out_str(" 110="); out_hex32(pci_read32(0, 0x1c, 2, 0x110)); out_str("\n");

	if (!abar || !gBS || !gBS->LocateHandleBuffer) {
		out_str("SATATEST3 ERROR: missing ABAR/BootServices\nZ97A_SATATEST3_DONE\n");
		return;
	}

	/* 3-second IDLE stability check */
	uint32_t drops = 0;
	uint32_t last_ssts = 0;
	for (int i = 0; i < 30; i++) {
		udelay_approx(100000U);
		last_ssts = mmio_read32(abar + 0x1a8U);
		if ((last_ssts & 0x0fU) != 3U)
			drops++;
	}
	uint32_t idle_serr = mmio_read32(abar + 0x1b0U);
	uint32_t idle_rp7 = pci_read32(0, 0x1c, 2, 0x110);
	uint32_t idle_dmi = mmio_read32(dmibar + 0x01d0U);
	out_str("IDLE_3S: drops="); out_dec(drops);
	out_str(" last_ssts="); out_hex32(last_ssts);
	out_str(" serr="); out_hex32(idle_serr);
	out_str(" rp7_aer110="); out_hex32(idle_rp7);
	out_str(" dmi_cests="); out_hex32(idle_dmi); out_str("\n");

	EFI_BLOCK_IO_PROTOCOL *target_bio = NULL;
	uint64_t nh = 0;
	EFI_HANDLE *handles = NULL;
	if (gBS->LocateHandleBuffer(2, &gBlockIoGuid, NULL, &nh, &handles) == 0 && handles) {
		for (uint64_t i = 0; i < nh; i++) {
			EFI_BLOCK_IO_PROTOCOL *bio = NULL;
			if (gBS->HandleProtocol(handles[i], &gBlockIoGuid, (void **)&bio) != 0 || !bio || !bio->Media)
				continue;
			if (bio->Media->LogicalPartition || !bio->Media->MediaPresent || bio->Media->BlockSize != 512U)
				continue;
			target_bio = bio;
			out_str("Found physical BlockIO handle "); out_dec((uint32_t)i);
			out_str(" LastBlock="); out_dec((uint32_t)bio->Media->LastBlock); out_str("\n");
			break;
		}
		gBS->FreePool(handles);
	}

	if (!target_bio) {
		out_str("SATATEST3 ERROR: no 512B physical BlockIO handle\nZ97A_SATATEST3_DONE\n");
		return;
	}

	/* Stage A: Clean COMRESET with stock PxSCTL=0x700 + Bio->Reset + 3 Gen3 passes */
	ahci_p1_comreset_sctl(abar, 0x00000700U);
	if (target_bio->Reset) {
		EFI_STATUS rst = target_bio->Reset(target_bio, 0);
		out_str("  StageA Bio->Reset st=0x"); out_hex32((uint32_t)rst);
		out_str(" PxSSTS="); out_hex32(mmio_read32(abar + 0x1a8U)); out_str("\n");
	}
	int f1 = sata_run_pass("t3_a1_gen3_64k_16m", target_bio, abar, 0, 256);
	int f2 = sata_run_4k_pass("t3_a2_gen3_4k_4m", target_bio, abar, 1024);
	int f3 = sata_run_pass("t3_a3_gen3_64k_16m", target_bio, abar, 0, 256);

	uint32_t a_rp7 = pci_read32(0, 0x1c, 2, 0x110);
	uint32_t a_uests = mmio_read32(dmibar + 0x01c4U);
	uint32_t a_cests = mmio_read32(dmibar + 0x01d0U);
	out_str("StageA Post: 1c.2_AER110="); out_hex32(a_rp7);
	out_str(" DMI_UESTS="); out_hex32(a_uests);
	out_str(" DMI_CESTS="); out_hex32(a_cests); out_str("\n");

	/* Conditional Stage B */
	if (a_rp7 != 0U) {
		pci_write16(0, 0x1c, 2, 0x50, (uint16_t)(pci_read16(0, 0x1c, 2, 0x50) & ~0x0003U));
		pci_write32(0, 0x1c, 2, 0x110, 0xffffffffU);
		for (int i = 0; i < 10; i++)
			udelay_approx(100000U);
		out_str("StageB_RP7_noASPM AER_110="); out_hex32(pci_read32(0, 0x1c, 2, 0x110)); out_str("\n");
	}

	if (drops > 0U || f1 || f2 || f3) {
		pci_write16(0, 0x1f, 2, 0x90, 0x3d60U);
		pci_write16(0, 0x1f, 2, 0x92, 0x8202U);
		pci_write32(0, 0x1f, 2, 0x94, 0x3d000183U);
		ahci_p1_comreset_sctl(abar, 0x00000700U);
		if (target_bio->Reset) {
			EFI_STATUS rst = target_bio->Reset(target_bio, 0);
			out_str("  StageB Bio->Reset st=0x"); out_hex32((uint32_t)rst);
			out_str(" PxSSTS="); out_hex32(mmio_read32(abar + 0x1a8U)); out_str("\n");
		}
		(void)sata_run_pass("t3_b1_gen3_64k_16m", target_bio, abar, 0, 256);
		(void)sata_run_4k_pass("t3_b2_gen3_4k_4m", target_bio, abar, 1024);
		out_str("StageB Post: 1c.2_AER110="); out_hex32(pci_read32(0, 0x1c, 2, 0x110));
		out_str(" DMI_CESTS="); out_hex32(mmio_read32(dmibar + 0x01d0U)); out_str("\n");
	}

	out_str("Z97A_SATATEST3_DONE\n");
}

static UNUSED uint16_t pci_find_ext_cap(uint8_t bus, uint8_t dev, uint8_t func, uint16_t cap_id)
{
	uint16_t off = 0x100U;
	for (int iter = 0; iter < 32 && off >= 0x100U && off < 0xffcU; iter++) {
		uint32_t hdr = pci_read32(bus, dev, func, off);
		if (hdr == 0U || hdr == 0xffffffffU)
			break;
		if ((uint16_t)(hdr & 0xffffU) == cap_id)
			return off;
		uint16_t next = (uint16_t)((hdr >> 20) & 0xffcU);
		if (next == 0U || next == off)
			break;
		off = next;
	}
	return 0U;
}

static UNUSED void dump_pci_cfg_256(const char *label, uint8_t bus, uint8_t dev, uint8_t func)
{
	out_str(label); out_str("_CFG_BEGIN\n");
	for (uint16_t off = 0; off < 0x100U; off += 16U) {
		out_char(' '); out_hex8((uint8_t)off); out_char(':');
		for (uint16_t j = 0; j < 16U; j += 4U) {
			out_char(' ');
			out_hex32(pci_read32(bus, dev, func, off + j));
		}
		out_str("\n");
	}
	out_str(label); out_str("_CFG_END\n");
}

static UNUSED void run_v014audit(void)
{
	out_str("\n=== Z97-A V0.14 FULL RCBA/DMIBAR/AER & PHY-IDLE/GEN1-3 SATA AUDIT ===\n");
	pci_init_mmcfg();

	uint64_t dmibar = (uint64_t)(pci_read32(0, 0, 0, 0x68) & ~0xfffU);
	if (!dmibar)
		dmibar = 0xfed18000ULL;
	uint64_t abar = pci_read32(0, 0x1f, 2, 0x24) & ~0x7ffULL;
	uint16_t aer_05 = pci_find_ext_cap(5, 0, 0, 0x0001U);

	/* A. Full non-zero DMIBAR dump (0x0000..0x0e40) */
	out_str("DMIBAR_FULL_BEGIN\n");
	for (uint32_t off = 0; off <= 0x0e40U; off += 4U) {
		uint32_t v = mmio_read32(dmibar + off);
		if (v != 0U) {
			out_str("0x"); out_hex16((uint16_t)off);
			out_str(": 0x"); out_hex32(v); out_str("\n");
		}
	}
	out_str("DMIBAR_FULL_END\n");
	out_str("DMIBAR_KEY:");
	static const uint16_t dmi_keys[12] = {
		0x0014U, 0x0020U, 0x0084U, 0x0088U, 0x0098U, 0x01c4U,
		0x01d0U, 0x071cU, 0x0720U, 0x080cU, 0x082cU, 0x0c28U
	};
	for (int i = 0; i < 12; i++) {
		out_char(' '); out_hex16(dmi_keys[i]); out_char('=');
		out_hex32(mmio_read32(dmibar + dmi_keys[i]));
	}
	out_str("\n");

	/* B. Full non-zero RCBA dump (0x0000..0x3f00, skipping SPI 0x3800..0x38ac) */
	out_str("RCBA_FULL_BEGIN\n");
	for (uint32_t off = 0; off <= 0x3f00U; off += 4U) {
		if (off >= 0x3800U && off <= 0x38acU)
			continue;
		uint32_t v = mmio_read32(RCBA_BASE + off);
		if (v != 0U) {
			out_str("0x"); out_hex16((uint16_t)off);
			out_str(": 0x"); out_hex32(v); out_str("\n");
		}
	}
	out_str("RCBA_FULL_END\n");
	out_str("RCBA_KEY:");
	static const uint16_t rcba_keys[7] = {
		0x0900U, 0x21a4U, 0x21a8U, 0x2234U, 0x2238U, 0x341cU, 0x38c0U
	};
	for (int i = 0; i < 7; i++) {
		out_char(' '); out_hex16(rcba_keys[i]); out_char('=');
		out_hex32(mmio_read32(RCBA_BASE + rcba_keys[i]));
	}
	out_str(" 00:1f.0_a0="); out_hex16(pci_read16(0, 0x1f, 0, 0xa0));
	out_str(" aer_05=0x"); out_hex16(aer_05); out_str("\n");

	/* C. Full 00:1f.2 & 00:1c.2 256B PCI cfg + ABAR P1 (0x180..0x1cc) */
	dump_pci_cfg_256("SATA_1F2", 0, 0x1f, 2);
	dump_pci_cfg_256("RP7_1C2", 0, 0x1c, 2);
	if (abar) {
		out_str("ABAR_P1:");
		for (uint32_t off = 0x180U; off <= 0x1ccU; off += 4U) {
			if (((off - 0x180U) % 16U) == 0U)
				out_str("\n ");
			out_char(' '); out_hex16((uint16_t)off); out_char('=');
			out_hex32(mmio_read32(abar + off));
		}
		out_str("\n");
	}

	/* D. W1C Clear + 2.5s Active-Idle + 2.5s Pure-PHY-Idle */
	mmio_write32(dmibar + 0x01c4U, 0xffffffffU);
	mmio_write32(dmibar + 0x01d0U, 0xffffffffU);
	pci_write32(0, 0x1c, 0, 0x104U, 0xffffffffU);
	pci_write32(0, 0x1c, 0, 0x110U, 0xffffffffU);
	pci_write32(0, 0x1c, 2, 0x104U, 0xffffffffU);
	pci_write32(0, 0x1c, 2, 0x110U, 0xffffffffU);
	if (aer_05) {
		pci_write32(5, 0, 0, aer_05 + 0x04U, 0xffffffffU);
		pci_write32(5, 0, 0, aer_05 + 0x10U, 0xffffffffU);
	}
	if (abar)
		mmio_write32(abar + 0x1b0U, 0xffffffffU);

	out_str("POST_CLEAR: dmi_1d0="); out_hex32(mmio_read32(dmibar + 0x01d0U));
	out_str(" 1c.0_110="); out_hex32(pci_read32(0, 0x1c, 0, 0x110U));
	out_str(" 1c.2_110="); out_hex32(pci_read32(0, 0x1c, 2, 0x110U));
	out_str(" 05:00_110="); out_hex32(aer_05 ? pci_read32(5, 0, 0, aer_05 + 0x10U) : 0xffffffffU);
	out_str(" p1_ssts="); out_hex32(abar ? mmio_read32(abar + 0x1a8U) : 0U);
	out_str(" p1_serr="); out_hex32(abar ? mmio_read32(abar + 0x1b0U) : 0U); out_str("\n");

	if (!abar || !gBS || !gBS->LocateHandleBuffer) {
		out_str("V014AUDIT ERROR: missing ABAR/BootServices\nZ97A_V014AUDIT_DONE\n");
		return;
	}

	uint32_t act_drops = 0, act_ssts = 0;
	for (int i = 0; i < 2500; i++) {
		udelay_approx(1000U);
		act_ssts = mmio_read32(abar + 0x1a8U);
		if ((act_ssts & 0x0fU) != 3U)
			act_drops++;
	}
	out_str("ACTIVE_IDLE_2.5S: drops="); out_dec(act_drops);
	out_str(" ssts="); out_hex32(act_ssts);
	out_str(" serr="); out_hex32(mmio_read32(abar + 0x1b0U));
	out_str(" dmi_0088="); out_hex32(mmio_read32(dmibar + 0x0088U));
	out_str(" dmi_01d0="); out_hex32(mmio_read32(dmibar + 0x01d0U));
	out_str(" 1c.0_110="); out_hex32(pci_read32(0, 0x1c, 0, 0x110U));
	out_str(" 1c.2_110="); out_hex32(pci_read32(0, 0x1c, 2, 0x110U));
	out_str(" 05:00_110="); out_hex32(aer_05 ? pci_read32(5, 0, 0, aer_05 + 0x10U) : 0xffffffffU);
	out_str("\n");

	/* Pure PHY Idle 2.5s (ST=0, FRE=0, PxSCTL=0x700) */
	uint32_t cmd = mmio_read32(abar + 0x198U);
	if (cmd & 1U) {
		mmio_write32(abar + 0x198U, cmd & ~1U);
		for (int i = 0; i < 500 && (mmio_read32(abar + 0x198U) & (1U << 15)); i++)
			udelay_approx(1000U);
	}
	cmd = mmio_read32(abar + 0x198U);
	if (cmd & (1U << 4)) {
		mmio_write32(abar + 0x198U, cmd & ~(1U << 4));
		for (int i = 0; i < 500 && (mmio_read32(abar + 0x198U) & (1U << 14)); i++)
			udelay_approx(1000U);
	}
	mmio_write32(abar + 0x1acU, 0x00000700U);
	(void)mmio_read32(abar + 0x1acU);
	mmio_write32(abar + 0x1b0U, 0xffffffffU);
	uint32_t phy_drops = 0, phy_ssts = 0;
	for (int i = 0; i < 2500; i++) {
		udelay_approx(1000U);
		phy_ssts = mmio_read32(abar + 0x1a8U);
		if ((phy_ssts & 0x0fU) != 3U)
			phy_drops++;
	}
	out_str("PHY_IDLE_2.5S: drops="); out_dec(phy_drops);
	out_str(" ssts="); out_hex32(phy_ssts);
	out_str(" serr="); out_hex32(mmio_read32(abar + 0x1b0U));
	out_str(" devslp="); out_hex32(mmio_read32(abar + 0x1c4U));
	out_str(" dmi_01d0="); out_hex32(mmio_read32(dmibar + 0x01d0U));
	out_str(" 1c.2_110="); out_hex32(pci_read32(0, 0x1c, 2, 0x110U));
	out_str("\n");

	/* E. SATA Gen1 / Gen2 / Gen3 3.0s idle + 4 MiB read isolation */
	EFI_BLOCK_IO_PROTOCOL *target_bio = NULL;
	uint64_t nh = 0;
	EFI_HANDLE *handles = NULL;
	if (gBS->LocateHandleBuffer(2, &gBlockIoGuid, NULL, &nh, &handles) == 0 && handles) {
		for (uint64_t i = 0; i < nh; i++) {
			EFI_BLOCK_IO_PROTOCOL *bio = NULL;
			if (gBS->HandleProtocol(handles[i], &gBlockIoGuid, (void **)&bio) != 0 || !bio || !bio->Media)
				continue;
			if (bio->Media->LogicalPartition || !bio->Media->MediaPresent || bio->Media->BlockSize != 512U)
				continue;
			target_bio = bio;
			out_str("Found physical BlockIO handle "); out_dec((uint32_t)i);
			out_str(" LastBlock="); out_dec((uint32_t)bio->Media->LastBlock); out_str("\n");
			break;
		}
		gBS->FreePool(handles);
	}

	static const struct {
		uint32_t sctl;
		const char *name;
		const char *label;
	} spd_tests[3] = {
		{ 0x00000710U, "gen1", "v14_gen1_64k_4m" },
		{ 0x00000720U, "gen2", "v14_gen2_64k_4m" },
		{ 0x00000700U, "gen3", "v14_gen3_64k_4m" },
	};
	for (int s = 0; s < 3; s++) {
		ahci_p1_comreset_sctl(abar, spd_tests[s].sctl);
		mmio_write32(abar + 0x1b0U, 0xffffffffU);
		uint32_t sdrops = 0, lssts = 0;
		for (int i = 0; i < 3000; i++) {
			udelay_approx(1000U);
			lssts = mmio_read32(abar + 0x1a8U);
			if ((lssts & 0x0fU) != 3U)
				sdrops++;
		}
		out_str("SPEED_IDLE_3S "); out_str(spd_tests[s].name);
		out_str(": drops="); out_dec(sdrops);
		out_str(" ssts="); out_hex32(lssts);
		out_str(" sctl="); out_hex32(mmio_read32(abar + 0x1acU));
		out_str(" serr="); out_hex32(mmio_read32(abar + 0x1b0U)); out_str("\n");
		if (target_bio) {
			if (target_bio->Reset)
				(void)target_bio->Reset(target_bio, 0);
			(void)sata_run_pass(spd_tests[s].label, target_bio, abar, 0, 64);
		}
	}

	/* F. 00:1c.2 (I226-V) ASPM-OFF isolation (2.0s), then restore ASPM */
	uint16_t save_1c2_50 = pci_read16(0, 0x1c, 2, 0x50U);
	uint16_t save_05_b0 = pci_read16(5, 0, 0, 0xb0U);
	pci_write16(5, 0, 0, 0xb0U, (uint16_t)(save_05_b0 & ~0x0003U));
	pci_write16(0, 0x1c, 2, 0x50U, (uint16_t)(save_1c2_50 & ~0x0003U));
	pci_write32(0, 0x1c, 2, 0x110U, 0xffffffffU);
	if (aer_05)
		pci_write32(5, 0, 0, aer_05 + 0x10U, 0xffffffffU);
	for (int i = 0; i < 2000; i++)
		udelay_approx(1000U);
	out_str("RP7_NO_ASPM_2S: 1c.2_50="); out_hex16(pci_read16(0, 0x1c, 2, 0x50U));
	out_str(" 1c.2_110="); out_hex32(pci_read32(0, 0x1c, 2, 0x110U));
	out_str(" 05:00_b0="); out_hex16(pci_read16(5, 0, 0, 0xb0U));
	out_str(" 05:00_110="); out_hex32(aer_05 ? pci_read32(5, 0, 0, aer_05 + 0x10U) : 0xffffffffU);
	out_str("\n");
	pci_write16(0, 0x1c, 2, 0x50U, save_1c2_50);
	pci_write16(5, 0, 0, 0xb0U, save_05_b0);

	out_str("Z97A_V014AUDIT_DONE\n");
}

/* Raw COM1 Framed-CRC32 Serial Delta Flasher Protocol */
static uint32_t recv_u32_le(void)
{
	uint32_t v = 0;
	for (int i = 0; i < 4; i++)
		v |= ((uint32_t)com1_getc_blocking()) << (i * 8);
	return v;
}

static void send_u32_le(uint32_t v)
{
	for (int i = 0; i < 4; i++)
		com1_putc_raw((uint8_t)(v >> (i * 8)));
}

static UNUSED void run_serflash(void)
{
	out_str("\nZ97A_SERFLASH_READY\n");
	if (spi_check_ready(1) != 0) {
		out_str("SERFLASH_ABORT_SPI_LOCKED\n");
		return;
	}
	if (gBS && gBS->SetWatchdogTimer)
		gBS->SetWatchdogTimer(0, 0, 0, NULL);

	/* Switch to binary-clean UART mode without ConOut interference */
	g_mirror_conout = 0;
	com1_drain_rx();

	while (1) {
		/* Sync to 'Z','9' magic */
		uint8_t b0 = com1_getc_blocking();
		if (b0 != 'Z') continue;
		uint8_t b1 = com1_getc_blocking();
		if (b1 != '9') continue;

		uint8_t cmd[4];
		for (int i = 0; i < 4; i++)
			cmd[i] = com1_getc_blocking();

		if (memcmp(cmd, "PING", 4) == 0) {
			out_str("PONG");
		} else if (memcmp(cmd, "INFO", 4) == 0) {
			uint8_t bc = pci_read8(0, 0x1f, 0, 0xdc);
			uint16_t hsfs = mmio_read16(SPI_HSFS);
			if (spi_compute_bios_sha256(g_hash_out) == 0) {
				out_str("INF0");
				com1_putc_raw(bc);
				com1_putc_raw((uint8_t)hsfs);
				com1_putc_raw((uint8_t)(hsfs >> 8));
				for (int i = 0; i < 32; i++)
					com1_putc_raw(g_hash_out[i]);
			} else {
				out_str("ERR!");
			}
		} else if (memcmp(cmd, "CRCS", 4) == 0) {
			out_str("CRC0");
			send_u32_le(NUM_SECTORS);
			for (uint32_t s = 0; s < NUM_SECTORS; s++) {
				uint32_t addr = BIOS_REGION_BASE + s * SECTOR_SIZE;
				uint32_t crc = 0;
				if (spi_hwseq_read(addr, g_sec_buf, SECTOR_SIZE) == 0)
					crc = crc32_ieee(g_sec_buf, SECTOR_SIZE);
				send_u32_le(crc);
			}
		} else if (memcmp(cmd, "WR4K", 4) == 0) {
			uint32_t addr = recv_u32_le();
			uint32_t exp_crc = recv_u32_le();
			for (uint32_t i = 0; i < SECTOR_SIZE; i++)
				g_sec_buf[i] = com1_getc_blocking();
			uint32_t got_crc = crc32_ieee(g_sec_buf, SECTOR_SIZE);
			if (addr < BIOS_REGION_BASE || addr >= BIOS_REGION_END ||
			    (addr & 0xfffU) != 0 || got_crc != exp_crc) {
				out_str("NAKC");
			} else if (spi_write_and_verify_sector(addr, g_sec_buf) != 0) {
				out_str("NAKW");
			} else {
				out_str("ACK4");
			}
		} else if (memcmp(cmd, "VSHA", 4) == 0) {
			if (spi_compute_bios_sha256(g_hash_out) == 0) {
				out_str("SHA0");
				for (int i = 0; i < 32; i++)
					com1_putc_raw(g_hash_out[i]);
			} else {
				out_str("ERR!");
			}
		} else if (memcmp(cmd, "DIAG", 4) == 0) {
			g_mirror_conout = 1;
			run_diag();
			out_str("DIAG_END\n");
			g_mirror_conout = 0;
		} else if (memcmp(cmd, "REBT", 4) == 0) {
			out_str("REBT");
			udelay_approx(20000U);
			outb(0x0e, 0x0cf9);
			while (1) __asm__ volatile("hlt");
		} else if (memcmp(cmd, "EXIT", 4) == 0) {
			out_str("BYE!");
			g_mirror_conout = 1;
			return;
		}
	}
}

#ifdef EMBED_BIOS_IMAGE
extern const uint8_t z97a_bios_bin[];
extern const uint8_t z97a_bios_end[];
extern const uint8_t z97a_bios_sha256[32];
#endif

static UNUSED int has_arg_ascii(const uint16_t *opt, uint32_t bytes, const char *needle)
{
	if (!opt || bytes < 2)
		return 0;
	uint32_t chars = bytes / 2U;
	uint32_t nlen = 0;
	while (needle[nlen]) nlen++;
	if (nlen == 0 || chars < nlen) return 0;
	for (uint32_t i = 0; i <= chars - nlen; i++) {
		int match = 1;
		for (uint32_t j = 0; j < nlen; j++) {
			uint16_t c = opt[i + j];
			if (c >= 'A' && c <= 'Z') c += 32;
			if (c != (uint16_t)(uint8_t)needle[j]) {
				match = 0;
				break;
			}
		}
		if (match) return 1;
	}
	return 0;
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	(void)ImageHandle;
	gST = SystemTable;
	gBS = SystemTable ? SystemTable->BootServices : NULL;
	if (gBS && gBS->SetWatchdogTimer)
		gBS->SetWatchdogTimer(0, 0, 0, NULL);

#ifdef EMBED_BIOS_IMAGE
	out_str("\n=== Z97-A SELF-CONTAINED BIOS FLASHER ===\n");
	uint32_t img_len = (uint32_t)(z97a_bios_end - z97a_bios_bin);
	if (spi_flash_bios_delta(z97a_bios_bin, img_len, z97a_bios_sha256) == 0) {
		out_str("Cold resetting via CF9=0x0E in 50ms...\n");
		udelay_approx(50000U);
		outb(0x0e, 0x0cf9);
		while (1) __asm__ volatile("hlt");
	}
	out_str("FLASH FAILED - staying in UEFI (no reset).\n");
	return 1;
#else
	EFI_LOADED_IMAGE_PROTOCOL *li = NULL;
	const uint16_t *opts = NULL;
	uint32_t opt_sz = 0;
	if (gBS && gBS->HandleProtocol &&
	    gBS->HandleProtocol(ImageHandle, &gLoadedImageGuid, (void **)&li) == 0 && li) {
		opts = (const uint16_t *)li->LoadOptions;
		opt_sz = li->LoadOptionsSize;
	}

	if (has_arg_ascii(opts, opt_sz, "serflash")) {
		run_serflash();
		return 0;
	}
	if (has_arg_ascii(opts, opt_sz, "hdmitest4")) {
		run_hdmitest4();
		return 0;
	}
	if (has_arg_ascii(opts, opt_sz, "hdmitest3")) {
		run_hdmitest3();
		return 0;
	}
	if (has_arg_ascii(opts, opt_sz, "hdmitest2")) {
		run_hdmitest2();
		return 0;
	}
	if (has_arg_ascii(opts, opt_sz, "hdmitest")) {
		run_hdmitest();
		return 0;
	}
	if (has_arg_ascii(opts, opt_sz, "satatest3")) {
		run_satatest3();
		return 0;
	}
	if (has_arg_ascii(opts, opt_sz, "satatest2")) {
		run_satatest2();
		return 0;
	}
	if (has_arg_ascii(opts, opt_sz, "satatest")) {
		run_satatest();
		return 0;
	}
	if (has_arg_ascii(opts, opt_sz, "v014audit")) {
		run_v014audit();
		return 0;
	}
	if (has_arg_ascii(opts, opt_sz, "diag")) {
		run_diag();
		return 0;
	}

	run_diag();
	out_str("\nPress 's' within 5s for COM1 serflash, 'r' to cold-reset, else exiting to Shell...\n");
	com1_drain_rx();
	for (int i = 0; i < 50; i++) {
		int ch = com1_tstc() ? (int)com1_getc_blocking() : -1;
		if (ch < 0 && gST && gST->ConIn && gST->ConIn->ReadKeyStroke) {
			EFI_INPUT_KEY k;
			if (gST->ConIn->ReadKeyStroke(gST->ConIn, &k) == 0 && k.UnicodeChar)
				ch = (int)(k.UnicodeChar & 0xff);
		}
		if (ch == 's' || ch == 'S') {
			run_serflash();
			return 0;
		}
		if (ch == 'r' || ch == 'R') {
			out_str("Cold resetting...\n");
			udelay_approx(20000U);
			outb(0x0e, 0x0cf9);
		}
		udelay_approx(100000U);
	}
	return 0;
#endif
}
