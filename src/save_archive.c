/* Save format layouts cross-checked against Apollo Save Tool PS2
 * (bucanero/apollo-ps2), import_ps1.c, import_ps2.c and ps1card.c.
 * Parse and validate the entire archive before any filesystem changes.
 */
#include "save_archive.h"
#include "save_lzari.h"
#include "save_sign.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <zlib.h>

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t le16(const uint8_t *p)
{
	return p[0] | (uint16_t)p[1] << 8;
}

static int extension(const char *name, const char *ext)
{
	const char *p = strrchr(name, '.');
	if (!p) return 0;
	p++;
	while (*p && *ext && toupper((unsigned char)*p) == *ext) { p++; ext++; }
	return !*p && !*ext;
}

int saveArchiveSupported(const char *name)
{
	static const char *const exts[] = {"PSU", "CBS", "MAX", "XPS", "SPS", "PSV", "MCS", "PSX", "PS1", "MCB", "PDA"};
	unsigned i;
	for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++)
		if (extension(name, exts[i])) return 1;
	return 0;
}

static int nameCopy(char out[33], const uint8_t *p, size_t width, size_t limit)
{
	size_t n = 0;
	while (n < width && p[n]) n++;
	if (!n || n > limit || (n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')) return 0;
	for (size_t i = 0; i < n; i++)
		if (p[i] < 32 || p[i] == '/' || p[i] == '\\' || p[i] == ':') return 0;
	memcpy(out, p, n);
	out[n] = 0;
	return 1;
}

static int entry(SaveArchiveEntry *e, const uint8_t *name, size_t width, uint16_t mode,
				 const uint8_t *created, const uint8_t *modified, const uint8_t *data, size_t size)
{
	if (!nameCopy(e->name, name, width, 32)) return 0;
	e->mode = mode;
	if (created) memcpy(e->created, created, 8);
	if (modified) memcpy(e->modified, modified, 8);
	e->data = data;
	e->size = size;
	return 1;
}

static int allocate(SaveArchive *s, unsigned count)
{
	if (!count || count > SAVE_ARCHIVE_FILES) return 0;
	s->files = calloc(count, sizeof(*s->files));
	s->count = count;
	return s->files != NULL;
}

static int ps1(SaveArchive *s, const uint8_t *d, size_t n, const char *filename)
{
	size_t offset;
	const uint8_t *name;
	size_t width = 20;
	if (n >= 132 && !memcmp(d, "\0VSP", 4) && le32(d + 60) == 1) { offset = 132; name = d + 100; }
	else if (n >= 130 && d[0] == 0x51 && !((n - 128) % PS1_BLOCK_SIZE) &&
	         toupper(d[128]) == 'S' && toupper(d[129]) == 'C') { offset = 128; name = d + 10; }
	else if (n >= 56 && d[54] == 'S' && d[55] == 'C') { offset = 54; name = d; }
	else if (n >= 2 && (d[0] == 'S' || d[0] == 's') && (d[1] == 'C' || d[1] == 'c')) {
		offset = 0;
		name = (const uint8_t *)strrchr(filename, '/');
		name = name ? name + 1 : (const uint8_t *)filename;
		width = strlen((const char *)name);
	} else return 0;
	if (n <= offset || (n - offset) % PS1_BLOCK_SIZE || n - offset > 15 * PS1_BLOCK_SIZE ||
	    toupper(d[offset]) != 'S' || toupper(d[offset + 1]) != 'C') return 0;
	if (!allocate(s, 1) || !nameCopy(s->files[0].name, name, width, 20)) return 0;
	s->ps1 = 1;
	s->files[0].data = d + offset;
	s->files[0].size = n - offset;
	s->files[0].mode = 0x8497;
	return 1;
}

static int psu(SaveArchive *s, const uint8_t *d, size_t n)
{
	size_t pos = 1536;
	unsigned count;
	if (n < pos || !(le16(d) & 0x20) || le32(d + 4) < 3) return 0;
	count = le32(d + 4) - 2;
	if (!allocate(s, count) || !entry(&s->folder, d + 64, 32, le16(d), d + 8, d + 24, NULL, 0)) return 0;
	if (memcmp(d + 512 + 64, ".\0", 2) || memcmp(d + 1024 + 64, "..\0", 3)) return 0;
	for (unsigned i = 0; i < count; i++) {
		size_t len, padded;
		const uint8_t *h;
		if (pos > n || n - pos < 512) return 0;
		h = d + pos;
		len = le32(h + 4);
		pos += 512;
		if (len > n - pos || (le16(h) & 0x20)) return 0;
		if (!entry(&s->files[i], h + 64, 32, le16(h), h + 8, h + 24, d + pos, len)) return 0;
		padded = (len + 1023) & ~(size_t)1023;
		if (padded > n - pos) return 0;
		pos += padded;
	}
	return pos == n;
}

static int psv(SaveArchive *s, const uint8_t *d, size_t n)
{
	unsigned count;
	size_t pos = 168, table_end;
	if (n < pos || le32(d + 60) != 2 || le32(d + 120) < 3) return 0;
	count = le32(d + 120) - 2;
	if (!allocate(s, count) || !entry(&s->folder, d + 128, 32, le32(d + 124), d + 104, d + 112, NULL, 0)) return 0;
	table_end = pos + count * 68;
	if (table_end > n) return 0;
	for (unsigned i = 0; i < count; i++, pos += 68) {
		size_t len = le32(d + pos + 16), off = le32(d + pos + 64);
		if (off < table_end || off > n || len > n - off || (le32(d + pos + 20) & 0x20)) return 0;
		if (!entry(&s->files[i], d + pos + 24, 32, le32(d + pos + 20), d + pos, d + pos + 8, d + off, len)) return 0;
	}
	return 1;
}

static int xps(SaveArchive *s, const uint8_t *d, size_t n)
{
	size_t pos = 21;
	unsigned count;
	if (n < pos || memcmp(d + 4, "SharkPortSave\0\0\0", 16)) return 0;
	for (unsigned i = 0; i < 2; i++) {
		size_t len;
		if (n - pos < 4) return 0;
		len = le32(d + pos);
		pos += 4;
		if (len > n - pos) return 0;
		pos += len;
	}
	if (n - pos < 8 + 250) return 0;
	pos += 8;
	count = le32(d + pos + 66);
	if (count < 3 || !allocate(s, count - 2) || le16(d + pos) != 250) return 0;
	if (!entry(&s->folder, d + pos + 2, 64, ((uint16_t)d[pos + 78] << 8 | d[pos + 79]),
	           d + pos + 82, d + pos + 90, NULL, 0)) return 0;
	pos += 250;
	for (unsigned i = 0; i < s->count; i++) {
		const uint8_t *h;
		size_t len;
		uint16_t mode;
		if (n - pos < 250 || le16(d + pos) != 250) return 0;
		h = d + pos;
		len = le32(h + 66);
		mode = (uint16_t)h[78] << 8 | h[79];
		pos += 250;
		if (len > n - pos || (mode & 0x20)) return 0;
		if (!entry(&s->files[i], h + 2, 64, mode, h + 82, h + 90, d + pos, len)) return 0;
		pos += len;
	}
	/* XPS writers may append a checksum. */
	return pos == n || n - pos == 4;
}

/* CodeBreaker uses an already scheduled RC4 state, not a password key.
 * State published by ps2dev/mymc (ps2save.py), also used by Apollo. */
static const uint8_t cbs_state[256] = {
	0x5f, 0x1f, 0x85, 0x6f, 0x31, 0xaa, 0x3b, 0x18,
	0x21, 0xb9, 0xce, 0x1c, 0x07, 0x4c, 0x9c, 0xb4,
	0x81, 0xb8, 0xef, 0x98, 0x59, 0xae, 0xf9, 0x26,
	0xe3, 0x80, 0xa3, 0x29, 0x2d, 0x73, 0x51, 0x62,
	0x7c, 0x64, 0x46, 0xf4, 0x34, 0x1a, 0xf6, 0xe1,
	0xba, 0x3a, 0x0d, 0x82, 0x79, 0x0a, 0x5c, 0x16,
	0x71, 0x49, 0x8e, 0xac, 0x8c, 0x9f, 0x35, 0x19,
	0x45, 0x94, 0x3f, 0x56, 0x0c, 0x91, 0x00, 0x0b,
	0xd7, 0xb0, 0xdd, 0x39, 0x66, 0xa1, 0x76, 0x52,
	0x13, 0x57, 0xf3, 0xbb, 0x4e, 0xe5, 0xdc, 0xf0,
	0x65, 0x84, 0xb2, 0xd6, 0xdf, 0x15, 0x3c, 0x63,
	0x1d, 0x89, 0x14, 0xbd, 0xd2, 0x36, 0xfe, 0xb1,
	0xca, 0x8b, 0xa4, 0xc6, 0x9e, 0x67, 0x47, 0x37,
	0x42, 0x6d, 0x6a, 0x03, 0x92, 0x70, 0x05, 0x7d,
	0x96, 0x2f, 0x40, 0x90, 0xc4, 0xf1, 0x3e, 0x3d,
	0x01, 0xf7, 0x68, 0x1e, 0xc3, 0xfc, 0x72, 0xb5,
	0x54, 0xcf, 0xe7, 0x41, 0xe4, 0x4d, 0x83, 0x55,
	0x12, 0x22, 0x09, 0x78, 0xfa, 0xde, 0xa7, 0x06,
	0x08, 0x23, 0xbf, 0x0f, 0xcc, 0xc1, 0x97, 0x61,
	0xc5, 0x4a, 0xe6, 0xa0, 0x11, 0xc2, 0xea, 0x74,
	0x02, 0x87, 0xd5, 0xd1, 0x9d, 0xb7, 0x7e, 0x38,
	0x60, 0x53, 0x95, 0x8d, 0x25, 0x77, 0x10, 0x5e,
	0x9b, 0x7f, 0xd8, 0x6e, 0xda, 0xa2, 0x2e, 0x20,
	0x4f, 0xcd, 0x8f, 0xcb, 0xbe, 0x5a, 0xe0, 0xed,
	0x2c, 0x9a, 0xd4, 0xe2, 0xaf, 0xd0, 0xa9, 0xe8,
	0xad, 0x7a, 0xbc, 0xa8, 0xf2, 0xee, 0xeb, 0xf5,
	0xa6, 0x99, 0x28, 0x24, 0x6c, 0x2b, 0x75, 0x5d,
	0xf8, 0xd3, 0x86, 0x17, 0xfb, 0xc0, 0x7b, 0xb3,
	0x58, 0xdb, 0xc7, 0x4b, 0xff, 0x04, 0x50, 0xe9,
	0x88, 0x69, 0xc9, 0x2a, 0xab, 0xfd, 0x5b, 0x1b,
	0x8a, 0xd9, 0xec, 0x27, 0x44, 0x0e, 0x33, 0xc8,
	0x6b, 0x93, 0x32, 0x48, 0xb6, 0x30, 0x43, 0xa5
};

static int cbs(SaveArchive *s, uint8_t *d, size_t n)
{
	uint8_t state[256], a = 0, b = 0, tmp;
	uLongf length;
	size_t pos;
	unsigned count = 0;
	if (n < 296) return 0;
	length = le32(d + 12);
	if (!length || length > SAVE_ARCHIVE_LIMIT) return 0;
	s->decoded = malloc(length);
	if (!s->decoded) return 0;
	memcpy(state, cbs_state, sizeof(state));
	/* Ignore the compressedSize field: several exporters write it incorrectly. */
	for (pos = 296; pos < n; pos++) {
		a++; b += state[a]; tmp = state[a]; state[a] = state[b]; state[b] = tmp;
		d[pos] ^= state[(uint8_t)(state[a] + state[b])];
	}
	if (uncompress(s->decoded, &length, d + 296, n - 296) != Z_OK) return 0;
	for (pos = 0; pos < length; count++) {
		size_t len;
		if (length - pos < 64 || count >= SAVE_ARCHIVE_FILES) return 0;
		len = le32(s->decoded + pos + 16);
		pos += 64;
		if (len > length - pos) return 0;
		pos += len;
	}
	if (!allocate(s, count) || !entry(&s->folder, d + 20, 32, le32(d + 72), d + 52, d + 60, NULL, 0)) return 0;
	pos = 0;
	for (unsigned i = 0; i < count; i++) {
		const uint8_t *h = s->decoded + pos;
		size_t len = le32(h + 16);
		if ((le32(h + 20) & 0x20) || !entry(&s->files[i], h + 32, 32, le32(h + 20), h, h + 8, h + 64, len)) return 0;
		pos += 64 + len;
	}
	return 1;
}

static int maxSave(SaveArchive *s, uint8_t *d, size_t n)
{
	size_t pos = 0, length;
	unsigned count;
	if (n < 96) return 0;
	length = le32(d + 88);
	count = le32(d + 84);
	if (!length || length > SAVE_ARCHIVE_LIMIT || !allocate(s, count)) return 0;
	s->decoded = malloc(length);
	if (!s->decoded || unlzari(d + 88, n - 88, s->decoded, length) != (int)length) return 0;
	if (!entry(&s->folder, d + 16, 32, 0x8427, NULL, NULL, NULL, 0)) return 0;
	for (unsigned i = 0; i < count; i++) {
		size_t len;
		const uint8_t *h;
		if (pos > length || length - pos < 36) return 0;
		h = s->decoded + pos;
		len = le32(h);
		pos += 36;
		if (len > length - pos || !entry(&s->files[i], h + 4, 32, 0x8497, NULL, NULL, s->decoded + pos, len)) return 0;
		pos = ((pos + len + 8 + 15) & ~(size_t)15) - 8;
	}
	return pos <= length + 15;
}

void saveArchiveFree(SaveArchive *s)
{
	free(s->files);
	free(s->decoded);
	memset(s, 0, sizeof(*s));
}

int saveArchiveParse(SaveArchive *s, uint8_t *d, size_t n, const char *filename)
{
	int ok;
	memset(s, 0, sizeof(*s));
	if (n < 4 || n > SAVE_ARCHIVE_LIMIT) return 0;
	if (!memcmp(d, "\0VSP", 4)) ok = n >= 64 && (le32(d + 60) == 1 ? ps1(s, d, n, filename) : psv(s, d, n));
	else if (!memcmp(d, "CFU\0", 4)) ok = cbs(s, d, n);
	else if (n >= 12 && !memcmp(d, "Ps2PowerSave", 12)) ok = maxSave(s, d, n);
	else if (n >= 20 && !memcmp(d + 4, "SharkPortSave\0\0\0", 16)) ok = xps(s, d, n);
	else if (extension(filename, "PSU")) ok = psu(s, d, n);
	else ok = ps1(s, d, n, filename);
	if (ok) {
		for (unsigned i = 0; i < s->count; i++)
			for (unsigned j = 0; j < i; j++)
				if (!strcmp(s->files[i].name, s->files[j].name)) ok = 0;
	}
	if (!ok) saveArchiveFree(s);
	return ok;
}

static void put32(uint8_t *p, uint32_t n)
{
	for (unsigned i = 0; i < 4; i++) p[i] = n >> (i * 8);
}

static void psuHeader(uint8_t *h, const SaveArchiveEntry *e, const char *name, uint32_t size)
{
	put32(h, e->mode); put32(h + 4, size);
	memcpy(h + 8, e->created, 8); memcpy(h + 24, e->modified, 8);
	memcpy(h + 64, name, strlen(name));
}

int saveArchiveFilename(const SaveArchive *s, int format, char *out, size_t size)
{
	const char *name = s->ps1 ? s->files[0].name : s->folder.name;
	const char *ext = format == SAVE_OUTPUT_PSU ? "psu" : format == SAVE_OUTPUT_MCS ? "mcs" :
	                  format == SAVE_OUTPUT_PSX ? "psx" : "PSV";
	size_t used = 0;
	for (unsigned i = 0; name[i]; i++) {
		if (format == SAVE_OUTPUT_PSV && i >= 12) {
			if (used + 2 >= size) return 0;
			snprintf(out + used, size - used, "%02X", (unsigned char)name[i]); used += 2;
		} else {
			if (used + 1 >= size) return 0;
			out[used++] = name[i];
		}
	}
	return snprintf(out + used, size - used, ".%s", ext) < (int)(size - used);
}

typedef struct {
	const SaveArchive *save;
	uint8_t *output;
	size_t length;
} MemoryArchive;

static int memoryArchiveRead(void *context, unsigned file, size_t offset, void *data, size_t size)
{
	MemoryArchive *memory = context;
	const SaveArchiveEntry *entry = &memory->save->files[file];
	if (offset > entry->size || size > entry->size - offset || (size && !entry->data)) return 0;
	if (size) memcpy(data, entry->data + offset, size);
	return 1;
}

static int memoryArchiveWrite(void *context, size_t offset, const void *data, size_t size)
{
	MemoryArchive *memory = context;
	if (offset > memory->length || size > memory->length - offset) return 0;
	memcpy(memory->output + offset, data, size);
	return 1;
}

int saveArchiveBuild(const SaveArchive *s, int format, uint8_t **output, size_t *length)
{
	uint8_t buffer[4096];
	MemoryArchive memory = {s, NULL, 0};
	SaveArchiveStreamIo io = {memoryArchiveRead, NULL, &memory, buffer, NULL, sizeof(buffer), NULL};
	*output = NULL; *length = 0;
	if (!s || !s->files || !s->count || s->count > SAVE_ARCHIVE_FILES) return 0;
	for (unsigned i = 0; i < s->count; i++) if (s->files[i].size && !s->files[i].data) return 0;
	if (!saveArchiveStream(s, format, &io, &memory.length)) return 0;
	memory.output = malloc(memory.length);
	if (!memory.output) return 0;
	io.write = memoryArchiveWrite;
	if (!saveArchiveStream(s, format, &io, length)) { free(memory.output); *length = 0; return 0; }
	*output = memory.output;
	return 1;
}

/* Plan headers from bounded metadata, then emit payloads through caller-owned
 * buffers. A NULL writer performs all format checks without creating output. */
int saveArchiveStream(const SaveArchive *s, int format, const SaveArchiveStreamIo *io, size_t *length)
{
	uint8_t header[1536] = {0}, icon_data[452], first[2], signature[20];
	SaveSigner signer;
	size_t payload = 0, total, pos, header_size;
	int icon = -1, refs[3] = {-1, -1, -1}, buffer_index = 0;
	char checked[33];
	*length = 0;
	if (!io || !io->read || !s->files || !s->count || s->count > SAVE_ARCHIVE_FILES ||
	    format < SAVE_OUTPUT_PSU || format > SAVE_OUTPUT_PSX ||
	    (s->ps1 ? s->count != 1 || format == SAVE_OUTPUT_PSU : format >= SAVE_OUTPUT_MCS)) return 0;
	if (!s->ps1 && (!(s->folder.mode & 0x20) || !nameCopy(checked, (const uint8_t *)s->folder.name, 33, 32))) return 0;
	for (unsigned i = 0; i < s->count; i++) {
		const SaveArchiveEntry *e = &s->files[i];
		if (!nameCopy(checked, (const uint8_t *)e->name, 33, s->ps1 ? 20 : 32) ||
		    e->size > SAVE_ARCHIVE_LIMIT || (e->mode & 0x20)) return 0;
		for (unsigned j = 0; j < i; j++) if (!strcmp(e->name, s->files[j].name)) return 0;
		payload += e->size;
		if (payload > SAVE_ARCHIVE_LIMIT) return 0;
		if (!strcmp(e->name, "icon.sys")) icon = i;
	}
	if (s->ps1 && (!payload || payload % PS1_BLOCK_SIZE || payload > 15 * PS1_BLOCK_SIZE ||
	    !io->read(io->context, 0, 0, first, 2) || toupper(first[0]) != 'S' || toupper(first[1]) != 'C')) return 0;
	if (s->ps1) total = payload + (format == SAVE_OUTPUT_MCS ? 128 : format == SAVE_OUTPUT_PSX ? 54 : 132);
	else if (format == SAVE_OUTPUT_PSU) {
		total = 1536;
		for (unsigned i = 0; i < s->count; i++) total += 512 + ((s->files[i].size + 1023) & ~(size_t)1023);
	} else {
		if (icon < 0 || s->files[icon].size < sizeof(icon_data) ||
		    !io->read(io->context, icon, 0, icon_data, sizeof(icon_data)) || memcmp(icon_data, "PS2D", 4)) return 0;
		for (unsigned j = 0; j < 3; j++) {
			const uint8_t *ref = icon_data + 260 + j * 64;
			if (!ref[0]) ref = icon_data + 260;
			for (unsigned i = 0; i < s->count; i++)
				if (!strncmp(s->files[i].name, (const char *)ref, 64)) refs[j] = i;
			if (refs[j] < 0) return 0;
		}
		total = 168 + s->count * 68 + payload;
	}
	if (total > SAVE_ARCHIVE_LIMIT) return 0;
	*length = total;
	if (!io->write) return 1;
	if (!io->buffer || !io->buffer_size) return 0;

	if (format == SAVE_OUTPUT_PSX) {
		memcpy(header, s->files[0].name, strlen(s->files[0].name)); header_size = 54;
	} else if (format == SAVE_OUTPUT_MCS) {
		header[0] = 0x51; put32(header + 4, payload); header[8] = header[9] = 0xff;
		memcpy(header + 10, s->files[0].name, strlen(s->files[0].name));
		for (unsigned i = 0; i < 127; i++) header[127] ^= header[i];
		header_size = 128;
	} else if (format == SAVE_OUTPUT_PSU) {
		psuHeader(header, &s->folder, s->folder.name, s->count + 2);
		psuHeader(header + 512, &s->folder, ".", 0); psuHeader(header + 1024, &s->folder, "..", 0);
		header_size = 1536;
	} else {
		memcpy(header, "\0VSP", 4); memcpy(header + 8, "Launchstation PSV", 17);
		put32(header + 56, s->ps1 ? 20 : 44); put32(header + 60, s->ps1 ? 1 : 2);
		put32(header + 64, payload);
		if (s->ps1) {
			put32(header + 68, 132); put32(header + 72, 512);
			put32(header + 92, payload); put32(header + 96, 0x9003);
			memcpy(header + 100, s->files[0].name, strlen(s->files[0].name)); header_size = 132;
		} else {
			put32(header + 100, s->count);
			memcpy(header + 104, s->folder.created, 8); memcpy(header + 112, s->folder.modified, 8);
			put32(header + 120, s->count + 2); put32(header + 124, s->folder.mode);
			memcpy(header + 128, s->folder.name, strlen(s->folder.name)); header_size = 168;
			pos = header_size + s->count * 68;
			for (unsigned i = 0; i < s->count; i++) {
				if ((int)i == icon) { put32(header + 68, pos); put32(header + 72, s->files[i].size); }
				for (unsigned j = 0; j < 3; j++) if ((int)i == refs[j]) {
					put32(header + 76 + j * 8, pos); put32(header + 80 + j * 8, s->files[i].size);
				}
				pos += s->files[i].size;
			}
		}
		saveSignInit(&signer, header + 8, s->ps1 ? 1 : 2);
	}
	if (!io->write(io->context, 0, header, header_size)) return 0;
	if (format == SAVE_OUTPUT_PSV) saveSignUpdate(&signer, header, header_size);
	pos = header_size;
	if (format == SAVE_OUTPUT_PSV && !s->ps1) {
		size_t data_pos = header_size + s->count * 68;
		for (unsigned i = 0; i < s->count; i++) {
			const SaveArchiveEntry *e = &s->files[i];
			memset(header, 0, 68); memcpy(header, e->created, 8); memcpy(header + 8, e->modified, 8);
			put32(header + 16, e->size); put32(header + 20, e->mode);
			memcpy(header + 24, e->name, strlen(e->name)); put32(header + 64, data_pos);
			if (!io->write(io->context, pos, header, 68)) return 0;
			saveSignUpdate(&signer, header, 68); pos += 68; data_pos += e->size;
		}
	}
	for (unsigned i = 0; i < s->count; i++) {
		const SaveArchiveEntry *e = &s->files[i];
		size_t offset = 0;
		if (!e->size && !io->read(io->context, i, 0, io->buffer, 0)) return 0;
		if (format == SAVE_OUTPUT_PSU) {
			memset(header, 0, 512); psuHeader(header, e, e->name, e->size);
			if (!io->write(io->context, pos, header, 512)) return 0;
			pos += 512;
		}
		while (offset < e->size) {
			size_t count = e->size - offset;
			uint8_t *buffer = buffer_index && io->buffer2 ? io->buffer2 : io->buffer;
			if (count > io->buffer_size) count = io->buffer_size;
			if (!io->read(io->context, i, offset, buffer, count)) return 0;
			if (format == SAVE_OUTPUT_PSV) {
				for (size_t hashed = 0; hashed < count;) {
					size_t block = count - hashed;
					if (block > 65536) block = 65536;
					saveSignUpdate(&signer, buffer + hashed, block); hashed += block;
					if (io->yield) io->yield(io->context);
				}
			}
			if (!io->write(io->context, pos, buffer, count)) return 0;
			offset += count; pos += count; buffer_index ^= 1;
		}
		if (format == SAVE_OUTPUT_PSU) {
			size_t pad = (1024 - (e->size & 1023)) & 1023;
			memset(header, 0, pad);
			if (pad && !io->write(io->context, pos, header, pad)) return 0;
			pos += pad;
		}
	}
	if (format == SAVE_OUTPUT_PSV) {
		saveSignFinish(&signer, signature);
		if (!io->write(io->context, 28, signature, sizeof(signature))) return 0;
	}
	return pos == total;
}

int ps1CardOffset(const uint8_t *d, size_t n)
{
	int offset;
	if (n == PS1_CARD_SIZE) offset = 0;
	else if (n == PS1_CARD_SIZE + 3904 && !memcmp(d, "123-456-STD", 11)) offset = 3904;
	else if (n == PS1_CARD_SIZE + 64 && !memcmp(d, "VgsM", 4)) offset = 64;
	else if (n == PS1_CARD_SIZE + 128 && !memcmp(d, "\0PMV", 4)) offset = 128;
	else return -1;
	return !memcmp(d + offset, "MC", 2) ? offset : -1;
}

static void checksum(uint8_t *page)
{
	page[127] = 0;
	for (unsigned i = 0; i < 127; i++) page[127] ^= page[i];
}

int ps1CardAppend(uint8_t *card, const char *name, const uint8_t *data, size_t size)
{
	unsigned slots[15], count = 0, blocks;
	char checked[33];
	if (!size || size % PS1_BLOCK_SIZE || size > 15 * PS1_BLOCK_SIZE ||
	    !nameCopy(checked, (const uint8_t *)name, strlen(name), 20) || memcmp(data, "SC", 2)) return 0;
	blocks = size / PS1_BLOCK_SIZE;
	for (unsigned i = 1; i <= 15; i++) {
		if (card[i * 128] == 0x51 && !memcmp(card + i * 128 + 10, checked, strlen(checked) + 1)) return 0;
		if (card[i * 128] == 0xa0) slots[count++] = i;
	}
	if (count < blocks) return 0;
	for (unsigned i = 0; i < blocks; i++) {
		uint8_t *page = card + slots[i] * 128;
		unsigned next = i + 1 == blocks ? 0xffff : slots[i + 1] - 1;
		memset(page, 0, 128);
		page[0] = i == 0 ? 0x51 : i + 1 == blocks ? 0x53 : 0x52;
		page[8] = next; page[9] = next >> 8;
		if (!i) {
			page[4] = size; page[5] = size >> 8; page[6] = size >> 16;
			memcpy(page + 10, checked, strlen(checked));
		}
		checksum(page);
		memcpy(card + slots[i] * PS1_BLOCK_SIZE, data + i * PS1_BLOCK_SIZE, PS1_BLOCK_SIZE);
	}
	return 1;
}
