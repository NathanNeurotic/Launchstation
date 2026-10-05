//--------------------------------------------------------------
// File name: mc_card_export.c
//
// Verified PS2 memory-card image export for the dashboard File Browser.
//
// The 512-byte page -> 528-byte page ECC calculation is derived from this
// repository's AFL-2.0 MCMAN implementation (iop/vmcman/src/main.c,
// McDataChecksum / mcman_xortable), not from SD2Cloud's GPL source.
//--------------------------------------------------------------
#include "launchelf.h"
#include "mc_card_export.h"

#define MC_PAGE_DATA 512
#define MC_PAGE_SPARE 16
#define MC_PAGE_PCSX2 (MC_PAGE_DATA + MC_PAGE_SPARE)
#define MC_EXPORT_PAGES 128
#define MC_EXPORT_IN (MC_PAGE_DATA * MC_EXPORT_PAGES)
#define MC_EXPORT_OUT (MC_PAGE_PCSX2 * MC_EXPORT_PAGES)

static u8 export_in[MC_EXPORT_IN] __attribute__((aligned(64)));
static u8 export_out[MC_EXPORT_OUT] __attribute__((aligned(64)));
static u8 verify_out[MC_EXPORT_OUT] __attribute__((aligned(64)));

/* PS2SDK MCMAN XOR table, AFL-2.0. */
static const u8 mc_ecc_xor[256] = {
	0x00, 0x87, 0x96, 0x11, 0xA5, 0x22, 0x33, 0xB4,
	0xB4, 0x33, 0x22, 0xA5, 0x11, 0x96, 0x87, 0x00,
	0xC3, 0x44, 0x55, 0xD2, 0x66, 0xE1, 0xF0, 0x77,
	0x77, 0xF0, 0xE1, 0x66, 0xD2, 0x55, 0x44, 0xC3,
	0xD2, 0x55, 0x44, 0xC3, 0x77, 0xF0, 0xE1, 0x66,
	0x66, 0xE1, 0xF0, 0x77, 0xC3, 0x44, 0x55, 0xD2,
	0x11, 0x96, 0x87, 0x00, 0xB4, 0x33, 0x22, 0xA5,
	0xA5, 0x22, 0x33, 0xB4, 0x00, 0x87, 0x96, 0x11,
	0xE1, 0x66, 0x77, 0xF0, 0x44, 0xC3, 0xD2, 0x55,
	0x55, 0xD2, 0xC3, 0x44, 0xF0, 0x77, 0x66, 0xE1,
	0x22, 0xA5, 0xB4, 0x33, 0x87, 0x00, 0x11, 0x96,
	0x96, 0x11, 0x00, 0x87, 0x33, 0xB4, 0xA5, 0x22,
	0x33, 0xB4, 0xA5, 0x22, 0x96, 0x11, 0x00, 0x87,
	0x87, 0x00, 0x11, 0x96, 0x22, 0xA5, 0xB4, 0x33,
	0xF0, 0x77, 0x66, 0xE1, 0x55, 0xD2, 0xC3, 0x44,
	0x44, 0xC3, 0xD2, 0x55, 0xE1, 0x66, 0x77, 0xF0,
	0xF0, 0x77, 0x66, 0xE1, 0x55, 0xD2, 0xC3, 0x44,
	0x44, 0xC3, 0xD2, 0x55, 0xE1, 0x66, 0x77, 0xF0,
	0x33, 0xB4, 0xA5, 0x22, 0x96, 0x11, 0x00, 0x87,
	0x87, 0x00, 0x11, 0x96, 0x22, 0xA5, 0xB4, 0x33,
	0x22, 0xA5, 0xB4, 0x33, 0x87, 0x00, 0x11, 0x96,
	0x96, 0x11, 0x00, 0x87, 0x33, 0xB4, 0xA5, 0x22,
	0xE1, 0x66, 0x77, 0xF0, 0x44, 0xC3, 0xD2, 0x55,
	0x55, 0xD2, 0xC3, 0x44, 0xF0, 0x77, 0x66, 0xE1,
	0x11, 0x96, 0x87, 0x00, 0xB4, 0x33, 0x22, 0xA5,
	0xA5, 0x22, 0x33, 0xB4, 0x00, 0x87, 0x96, 0x11,
	0xD2, 0x55, 0x44, 0xC3, 0x77, 0xF0, 0xE1, 0x66,
	0x66, 0xE1, 0xF0, 0x77, 0xC3, 0x44, 0x55, 0xD2,
	0xC3, 0x44, 0x55, 0xD2, 0x66, 0xE1, 0xF0, 0x77,
	0x77, 0xF0, 0xE1, 0x66, 0xD2, 0x55, 0x44, 0xC3,
	0x00, 0x87, 0x96, 0x11, 0xA5, 0x22, 0x33, 0xB4,
	0xB4, 0x33, 0x22, 0xA5, 0x11, 0x96, 0x87, 0x00
};

static void page_ecc_128(const u8 *data, u8 *ecc)
{
	int i, c1 = 0, c2 = 0, c3 = 0;

	for (i = 0; i < 128; i++) {
		int v = mc_ecc_xor[data[i]];
		c1 ^= v;
		if (v & 0x80) {
			c2 ^= ~i;
			c3 ^= i;
		}
	}
	ecc[0] = (u8)(~c1 & 0x77);
	ecc[1] = (u8)(~c2 & 0x7F);
	ecc[2] = (u8)(~c3 & 0x7F);
}

void mcCardPcsx2Page(const u8 in[MC_PAGE_DATA], u8 out[MC_PAGE_PCSX2])
{
	int part;

	memcpy(out, in, MC_PAGE_DATA);
	for (part = 0; part < 4; part++)
		page_ecc_128(in + part * 128, out + MC_PAGE_DATA + part * 3);
	memset(out + MC_PAGE_DATA + 12, 0, 4);
}

static int read_exact_at(const char *path, u64 pos, u8 *buf, int size)
{
	int fd, total = 0;

	fd = genOpen(path, FIO_O_RDONLY);
	if (fd < 0)
		return -1;
	if (genLseek(fd, (s64)pos, SEEK_SET) != (s64)pos) {
		genClose(fd);
		return -1;
	}
	while (total < size) {
		int r = genRead(fd, buf + total, size - total);
		if (r <= 0) {
			genClose(fd);
			return -1;
		}
		total += r;
	}
	return genClose(fd) < 0 ? -1 : 0;
}

static int write_exact_at(const char *path, u64 pos, const u8 *buf, int size, int first)
{
	int fd, total = 0;
	int flags = FIO_O_WRONLY | FIO_O_CREAT | (first ? FIO_O_TRUNC : 0);

	fd = genOpen(path, flags);
	if (fd < 0)
		return -1;
	if (pos && genLseek(fd, (s64)pos, SEEK_SET) != (s64)pos) {
		genClose(fd);
		return -1;
	}
	while (total < size) {
		int r = genWrite(fd, (void *)(buf + total), size - total);
		if (r <= 0) {
			genClose(fd);
			return -1;
		}
		total += r;
	}
	return genClose(fd) < 0 ? -1 : 0;
}

static u16 read_le16(const u8 *p)
{
	return (u16)(p[0] | (p[1] << 8));
}

static u32 read_le32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static int source_info(const char *path, u64 *size)
{
	static const char magic[] = "Sony PS2 Memory Card Format ";
	u8 header[64];
	u16 page_size, pages_per_cluster;
	u32 clusters_per_card;
	u64 expected;
	int fd;
	s64 end;

	fd = genOpen(path, FIO_O_RDONLY);
	if (fd < 0)
		return MC_CARD_EXPORT_OPEN;
	if (genRead(fd, header, sizeof(header)) != (int)sizeof(header) ||
	    memcmp(header, magic, sizeof(magic) - 1) != 0) {
		genClose(fd);
		return MC_CARD_EXPORT_FORMAT;
	}
	end = genLseek(fd, 0, SEEK_END);
	if (genClose(fd) < 0)
		return MC_CARD_EXPORT_IO;

	/* MCMAN superblock geometry (AFL-2.0 mcman-internal.h):
	 * magic[28], version[12], pagesize, pages_per_cluster, blocksize,
	 * unused, clusters_per_card. This distinguishes a real no-ECC .mcd
	 * from a PCSX2 528-byte/page image that was merely renamed .mcd. */
	page_size = read_le16(header + 40);
	pages_per_cluster = read_le16(header + 42);
	clusters_per_card = read_le32(header + 48);
	if (page_size != MC_PAGE_DATA || pages_per_cluster == 0 || clusters_per_card == 0)
		return MC_CARD_EXPORT_FORMAT;
	expected = (u64)page_size * pages_per_cluster * clusters_per_card;
	if (end <= 0 || (u64)end != expected)
		return MC_CARD_EXPORT_FORMAT;

	*size = expected;
	return MC_CARD_EXPORT_OK;
}

static int convert_block(const u8 *in, int in_size, u8 *out)
{
	int at, made = 0;

	if (in_size <= 0 || (in_size % MC_PAGE_DATA) != 0)
		return -1;
	for (at = 0; at < in_size; at += MC_PAGE_DATA) {
		mcCardPcsx2Page(in + at, out + made);
		made += MC_PAGE_PCSX2;
	}
	return made;
}

int mcCardExportPcsx2(const char *source, const char *dest, McCardExportProgress progress)
{
	u64 src_size = 0, src_pos = 0, dst_pos = 0;
	int ret, chunk, converted;
	char remove_path[MAX_PATH];

	if (source == NULL || dest == NULL || source[0] == '\0' || dest[0] == '\0')
		return MC_CARD_EXPORT_FORMAT;
	ret = source_info(source, &src_size);
	if (ret != MC_CARD_EXPORT_OK)
		return ret;

	/* Write pass. Source and destination are never open together: this is
	 * deliberate for sd2psx/MMCE, whose filesystem is happiest with one
	 * operation outstanding at a time. */
	while (src_pos < src_size) {
		chunk = (src_size - src_pos > MC_EXPORT_IN) ? MC_EXPORT_IN : (int)(src_size - src_pos);
		if (read_exact_at(source, src_pos, export_in, chunk) < 0)
			goto io_fail;
		converted = convert_block(export_in, chunk, export_out);
		if (converted < 0 || write_exact_at(dest, dst_pos, export_out, converted, src_pos == 0) < 0)
			goto io_fail;
		src_pos += chunk;
		dst_pos += converted;
		if (progress)
			progress(MC_CARD_EXPORT_WRITE, src_pos, src_size);
	}

	/* Verification pass: regenerate every expected 528-byte page from the
	 * source, then read the destination back and compare byte-for-byte. */
	src_pos = dst_pos = 0;
	while (src_pos < src_size) {
		chunk = (src_size - src_pos > MC_EXPORT_IN) ? MC_EXPORT_IN : (int)(src_size - src_pos);
		if (read_exact_at(source, src_pos, export_in, chunk) < 0)
			goto verify_fail;
		converted = convert_block(export_in, chunk, export_out);
		if (converted < 0 || read_exact_at(dest, dst_pos, verify_out, converted) < 0 ||
		    memcmp(export_out, verify_out, converted) != 0)
			goto verify_fail;
		src_pos += chunk;
		dst_pos += converted;
		if (progress)
			progress(MC_CARD_EXPORT_VERIFY, src_pos, src_size);
	}
	return MC_CARD_EXPORT_OK;

io_fail:
	ret = MC_CARD_EXPORT_IO;
	goto remove_bad;
verify_fail:
	ret = MC_CARD_EXPORT_VERIFY_FAILED;
remove_bad:
	snprintf(remove_path, sizeof(remove_path), "%s", dest);
	genRemove(remove_path);
	return ret;
}

static int pcsx2_source_info(const char *path, u64 *raw_size, u64 *encoded_size)
{
	static const char magic[] = "Sony PS2 Memory Card Format ";
	u8 header[64];
	u16 page_size, pages_per_cluster;
	u32 clusters_per_card;
	u64 pages, expected;
	int fd;
	s64 end;

	fd = genOpen(path, FIO_O_RDONLY);
	if (fd < 0)
		return MC_CARD_EXPORT_OPEN;
	if (genRead(fd, header, sizeof(header)) != (int)sizeof(header) ||
	    memcmp(header, magic, sizeof(magic) - 1) != 0) {
		genClose(fd);
		return MC_CARD_EXPORT_FORMAT;
	}
	end = genLseek(fd, 0, SEEK_END);
	if (genClose(fd) < 0)
		return MC_CARD_EXPORT_IO;

	page_size = read_le16(header + 40);
	pages_per_cluster = read_le16(header + 42);
	clusters_per_card = read_le32(header + 48);
	if (page_size != MC_PAGE_DATA || pages_per_cluster == 0 || clusters_per_card == 0)
		return MC_CARD_EXPORT_FORMAT;
	pages = (u64)pages_per_cluster * clusters_per_card;
	expected = pages * MC_PAGE_PCSX2;
	if (end <= 0 || (u64)end != expected)
		return MC_CARD_EXPORT_FORMAT;

	*raw_size = pages * MC_PAGE_DATA;
	*encoded_size = expected;
	return MC_CARD_EXPORT_OK;
}

static int strip_pcsx2_block(const u8 *in, int in_size, u8 *out)
{
	int at, made = 0;

	if (in_size <= 0 || (in_size % MC_PAGE_PCSX2) != 0)
		return -1;
	for (at = 0; at < in_size; at += MC_PAGE_PCSX2) {
		memcpy(out + made, in + at, MC_PAGE_DATA);
		made += MC_PAGE_DATA;
	}
	return made;
}

int mcCardImportPcsx2(const char *source, const char *dest, McCardExportProgress progress)
{
	u64 raw_size = 0, encoded_size = 0, src_pos = 0, dst_pos = 0;
	int ret, chunk, stripped;
	char remove_path[MAX_PATH];

	if (source == NULL || dest == NULL || source[0] == '\0' || dest[0] == '\0')
		return MC_CARD_EXPORT_FORMAT;
	ret = pcsx2_source_info(source, &raw_size, &encoded_size);
	if (ret != MC_CARD_EXPORT_OK)
		return ret;

	while (src_pos < encoded_size) {
		chunk = (encoded_size - src_pos > MC_EXPORT_OUT) ? MC_EXPORT_OUT : (int)(encoded_size - src_pos);
		/* MC_EXPORT_OUT is an exact multiple of 528 bytes. */
		if (read_exact_at(source, src_pos, verify_out, chunk) < 0)
			goto io_fail;
		stripped = strip_pcsx2_block(verify_out, chunk, export_in);
		if (stripped < 0 || write_exact_at(dest, dst_pos, export_in, stripped, src_pos == 0) < 0)
			goto io_fail;
		src_pos += chunk;
		dst_pos += stripped;
		if (progress)
			progress(MC_CARD_EXPORT_WRITE, dst_pos, raw_size);
	}

	/* Verify the no-ECC image against the data area of every source page. */
	src_pos = dst_pos = 0;
	while (src_pos < encoded_size) {
		chunk = (encoded_size - src_pos > MC_EXPORT_OUT) ? MC_EXPORT_OUT : (int)(encoded_size - src_pos);
		if (read_exact_at(source, src_pos, verify_out, chunk) < 0)
			goto verify_fail;
		stripped = strip_pcsx2_block(verify_out, chunk, export_in);
		if (stripped < 0 || read_exact_at(dest, dst_pos, export_out, stripped) < 0 ||
		    memcmp(export_in, export_out, stripped) != 0)
			goto verify_fail;
		src_pos += chunk;
		dst_pos += stripped;
		if (progress)
			progress(MC_CARD_EXPORT_VERIFY, dst_pos, raw_size);
	}
	return MC_CARD_EXPORT_OK;

io_fail:
	ret = MC_CARD_EXPORT_IO;
	goto remove_bad;
verify_fail:
	ret = MC_CARD_EXPORT_VERIFY_FAILED;
remove_bad:
	snprintf(remove_path, sizeof(remove_path), "%s", dest);
	genRemove(remove_path);
	return ret;
}

//--------------------------------------------------------------
// End of file: mc_card_export.c
//--------------------------------------------------------------
