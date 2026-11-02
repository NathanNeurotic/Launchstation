//--------------------------------------------------------------
//File name:   mc_save_formats.c
//Readers for the single-save formats other tools export (.mcm, .max, .cbs,
//.sps/.xps, .psv) into McSaveData. See include/mc_save_formats.h.
//--------------------------------------------------------------
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "mc_save_formats.h"

#define ENT 512u
#define M_EXISTS 0x8000u
#define M_HIDDEN 0x2000u
#define M_0400 0x0400u
#define M_0080 0x0080u
#define M_DIR 0x0020u
#define M_FILE 0x0010u
#define M_RWX 0x0007u
#define MODE_DIR (M_EXISTS | M_0400 | M_DIR | M_RWX)                     /* 0x8427 */
#define MODE_FILE (M_EXISTS | M_0400 | M_0080 | M_FILE | M_RWX)          /* 0x8497 */
#define MODE_DOTDOT (M_EXISTS | M_HIDDEN | M_0400 | M_DIR | 0x0006u)     /* 0xA426, as the PS2 makes it */
#define MAX_FILES 4094u
#define MAX_TOTAL (128u * 1024u * 1024u) /* every reader refuses more; the card writer does too */
#define MAX_INFLATE (64u * 1024u * 1024u)

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}
static void wr32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/* The same rule as mc_card_write.c: 1..31 printable ASCII bytes, not "." or
 * "..", without / \ * or ?. n is the field's size; the name may fill it. */
static int nameOk(const uint8_t *name, uint32_t n)
{
	uint32_t i;

	for (i = 0; i < n && name[i]; i++)
		if (name[i] < 0x20 || name[i] > 0x7E || name[i] == '/' || name[i] == '\\' || name[i] == '*' || name[i] == '?')
			return 0;
	if (i == 0 || i >= 32)
		return 0;
	if (name[0] == '.' && (i == 1 || (i == 2 && name[1] == '.')))
		return 0;
	return 1;
}

/* A directory entry in card layout: mode, length, created and modified
 * times (8 bytes each, card order), name. Everything else is zero; the card
 * writer fills in clusters. A NULL time is zero, "no time". */
static void makeEntry(uint8_t *e, uint32_t mode, uint32_t length, const uint8_t *created, const uint8_t *modified,
                      const uint8_t *name, uint32_t name_n)
{
	uint32_t n;

	memset(e, 0, ENT);
	wr16(e, mode);
	wr32(e + 4, length);
	if (created)
		memcpy(e + 0x08, created, 8);
	if (modified)
		memcpy(e + 0x18, modified, 8);
	for (n = 0; n < name_n && n < 31 && name[n]; n++)
		e[0x40 + n] = name[n];
}

/* A time is "set" when any byte but the reserved first one is non-zero. */
static const uint8_t *timeOr(const uint8_t *t, const uint8_t *fallback)
{
	int i;

	if (t)
		for (i = 1; i < 8; i++)
			if (t[i])
				return t;
	return fallback;
}

static int startSave(McSaveData *out, uint32_t nfiles, uint32_t mode, const uint8_t *created, const uint8_t *modified,
                     const uint8_t *name, uint32_t name_n)
{
	memset(out, 0, sizeof(*out));
	if (!nameOk(name, name_n) || nfiles > MAX_FILES)
		return MCW_ERR_INVALID;
	if ((mode & (M_EXISTS | M_DIR)) != (M_EXISTS | M_DIR) || (mode & M_FILE))
		mode = MODE_DIR;
	makeEntry(out->entry, mode, nfiles + 2u, created, modified, name, name_n);
	makeEntry(out->dot, MODE_DIR, 0, created, modified, (const uint8_t *)".", 1);
	makeEntry(out->dotdot, MODE_DOTDOT, 0, created, modified, (const uint8_t *)"..", 2);
	if (nfiles) {
		out->files = (McSaveFile *)calloc(nfiles, sizeof(McSaveFile));
		if (!out->files)
			return MCW_ERR_NOMEM;
	}
	out->nfiles = nfiles;
	return MCW_OK;
}

/* Adds file i: a copy of size bytes of data with its own entry. Refuses a
 * duplicate name and a save over MAX_TOTAL. */
static int addFile(McSaveData *out, uint32_t i, uint32_t mode, uint32_t size, const uint8_t *created,
                   const uint8_t *modified, const uint8_t *name, uint32_t name_n, const uint8_t *data, uint64_t *total)
{
	McSaveFile *f = &out->files[i];
	uint32_t j;

	if (!nameOk(name, name_n))
		return MCW_ERR_INVALID;
	*total += size;
	if (*total > MAX_TOTAL)
		return MCW_ERR_INVALID;
	if ((mode & (M_EXISTS | M_FILE)) != (M_EXISTS | M_FILE) || (mode & M_DIR))
		mode = MODE_FILE;
	makeEntry(f->entry, mode, size, created, modified, name, name_n);
	for (j = 0; j < i; j++)
		if (!strncmp((const char *)f->entry + 0x40, (const char *)out->files[j].entry + 0x40, 32))
			return MCW_ERR_INVALID;
	f->data = (uint8_t *)malloc(size ? size : 1);
	if (!f->data)
		return MCW_ERR_NOMEM;
	if (size)
		memcpy(f->data, data, size);
	f->size = size;
	return MCW_OK;
}

static int fail(McSaveData *out, int r)
{
	mcCardFreeSave(out);
	memset(out, 0, sizeof(*out));
	return r;
}

/* zlib inflate of src into a new buffer. want is the expected output size
 * when the format states one (0 when not); the result must then match it. */
static int inflateAll(const uint8_t *src, uint32_t src_len, uint32_t want, uint8_t **out, uint32_t *out_len)
{
	z_stream z;
	uint8_t *buf;
	/* one spare byte past a stated size, so the end marker fits and a stream
	 * longer than its header says is caught instead of cut */
	uint32_t cap = want ? want + 1u : (src_len < 4096u ? 16384u : src_len * 4u);
	int r, zr;

	*out = NULL;
	*out_len = 0;
	if (cap > MAX_INFLATE + 1u)
		return MCW_ERR_INVALID;
	buf = (uint8_t *)malloc(cap ? cap : 1);
	if (!buf)
		return MCW_ERR_NOMEM;
	memset(&z, 0, sizeof(z));
	if (inflateInit(&z) != Z_OK) {
		free(buf);
		return MCW_ERR_NOMEM;
	}
	z.next_in = (Bytef *)src;
	z.avail_in = src_len;
	r = MCW_OK;
	for (;;) {
		z.next_out = buf + z.total_out;
		z.avail_out = cap - (uint32_t)z.total_out;
		zr = inflate(&z, Z_NO_FLUSH);
		if (zr == Z_STREAM_END)
			break;
		if (zr == Z_BUF_ERROR && z.avail_in == 0 && z.avail_out != 0) {
			r = MCW_ERR_INVALID; /* the stream ends before its data does */
			break;
		}
		if (zr != Z_OK && !(zr == Z_BUF_ERROR && z.avail_out == 0)) {
			r = MCW_ERR_INVALID;
			break;
		}
		if (z.avail_out == 0) {
			uint8_t *grown;

			if (want || cap >= MAX_INFLATE) {
				r = MCW_ERR_INVALID; /* more data than the header declares */
				break;
			}
			cap = cap * 2u > MAX_INFLATE ? MAX_INFLATE : cap * 2u;
			grown = (uint8_t *)realloc(buf, cap);
			if (!grown) {
				r = MCW_ERR_NOMEM;
				break;
			}
			buf = grown;
		}
	}
	if (r == MCW_OK && want && z.total_out != want)
		r = MCW_ERR_INVALID;
	inflateEnd(&z);
	if (r != MCW_OK) {
		free(buf);
		return r;
	}
	*out = buf;
	*out_len = (uint32_t)z.total_out;
	return MCW_OK;
}

/* ------------------------------------------------------------ Blaze .mcm */

int mcSaveFromMcm(const uint8_t *buf, uint32_t len, const char *folder_name, const uint8_t stamp[8], McSaveData *out)
{
	uint8_t *raw = NULL;
	uint32_t raw_len, pos, n, i;
	uint64_t total = 0;
	int r;

	if (!buf || !out || !folder_name)
		return MCW_ERR_INVALID;
	memset(out, 0, sizeof(*out));
	if (len < 82u || rd32(buf + 4) != len || buf[80] != 0x78)
		return MCW_ERR_INVALID;
	r = inflateAll(buf + 80, len - 80u, 0, &raw, &raw_len);
	if (r != MCW_OK)
		return r;
	/* count the records first: [32-byte name][u32 size][data] */
	for (pos = 0, n = 0; pos < raw_len; n++) {
		uint32_t size;

		if (raw_len - pos < 36u || n >= MAX_FILES)
			goto bad;
		size = rd32(raw + pos + 32);
		if (size > raw_len - pos - 36u)
			goto bad;
		pos += 36u + size;
	}
	r = startSave(out, n, MODE_DIR, stamp, stamp, (const uint8_t *)folder_name, 32);
	if (r != MCW_OK)
		return fail(out, r);
	for (pos = 0, i = 0; i < n; i++) {
		uint32_t size = rd32(raw + pos + 32);

		r = addFile(out, i, MODE_FILE, size, stamp, stamp, raw + pos, 32, raw + pos + 36, &total);
		if (r != MCW_OK) {
			free(raw);
			return fail(out, r);
		}
		pos += 36u + size;
	}
	free(raw);
	return MCW_OK;
bad:
	free(raw);
	return fail(out, MCW_ERR_INVALID);
}

/* ------------------------------------------------------------ LZARI */

/* Haruhiko Okumura's LZARI (1989, public domain): LZSS with a 4 KB window
 * and 3..60-byte matches, arithmetic-coded with an adaptive model. Decoder
 * only. The constants are the algorithm's; changing one breaks the format. */
#define LZ_N 4096
#define LZ_F 60
#define LZ_THRESHOLD 2
#define LZ_NCHAR (256 - LZ_THRESHOLD + LZ_F) /* 314 */
#define LZ_Q1 (1u << 15)
#define LZ_Q2 (2u * LZ_Q1)
#define LZ_Q3 (3u * LZ_Q1)
#define LZ_Q4 (4u * LZ_Q1)
#define LZ_MAX_CUM (LZ_Q1 - 1u)

typedef struct {
	const uint8_t *src;
	uint32_t src_len, byte;
	int bit;
	uint32_t past; /* bits read past the end; a few are normal, many mean a cut stream */
	uint32_t low, high, value;
	uint16_t sym_to_char[LZ_NCHAR + 1], sym_freq[LZ_NCHAR + 1], sym_cum[LZ_NCHAR + 1];
	uint16_t position_cum[LZ_N + 1];
} Lzari;

static uint32_t lzBit(Lzari *z)
{
	uint32_t b;

	if (z->byte >= z->src_len) {
		z->past++;
		return 0;
	}
	b = (z->src[z->byte] >> (7 - z->bit)) & 1u;
	if (++z->bit == 8) {
		z->bit = 0;
		z->byte++;
	}
	return b;
}

static void lzStart(Lzari *z)
{
	int sym, i;

	z->low = 0;
	z->high = LZ_Q4;
	z->value = 0;
	for (i = 0; i < 15 + 2; i++)
		z->value = 2u * z->value + lzBit(z);
	z->sym_cum[LZ_NCHAR] = 0;
	for (sym = LZ_NCHAR; sym >= 1; sym--) {
		z->sym_to_char[sym] = (uint16_t)(sym - 1);
		z->sym_freq[sym] = 1;
		z->sym_cum[sym - 1] = (uint16_t)(z->sym_cum[sym] + 1u);
	}
	z->sym_freq[0] = 0;
	z->position_cum[LZ_N] = 0;
	for (i = LZ_N; i >= 1; i--)
		z->position_cum[i - 1] = (uint16_t)(z->position_cum[i] + 10000u / ((uint32_t)i + 200u));
}

static void lzUpdate(Lzari *z, int sym)
{
	int i;
	uint32_t c;

	if (z->sym_cum[0] >= LZ_MAX_CUM) {
		c = 0;
		for (i = LZ_NCHAR; i > 0; i--) {
			z->sym_cum[i] = (uint16_t)c;
			z->sym_freq[i] = (uint16_t)((z->sym_freq[i] + 1u) >> 1);
			c += z->sym_freq[i];
		}
		z->sym_cum[0] = (uint16_t)c;
	}
	for (i = sym; z->sym_freq[i] == z->sym_freq[i - 1]; i--)
		;
	if (i < sym) {
		uint16_t ch_i = z->sym_to_char[i], ch_sym = z->sym_to_char[sym];

		z->sym_to_char[i] = ch_sym;
		z->sym_to_char[sym] = ch_i;
	}
	z->sym_freq[i]++;
	while (--i >= 0)
		z->sym_cum[i]++;
}

static void lzNarrow(Lzari *z)
{
	for (;;) {
		if (z->low >= LZ_Q2) {
			z->value -= LZ_Q2;
			z->low -= LZ_Q2;
			z->high -= LZ_Q2;
		} else if (z->low >= LZ_Q1 && z->high <= LZ_Q3) {
			z->value -= LZ_Q1;
			z->low -= LZ_Q1;
			z->high -= LZ_Q1;
		} else if (z->high > LZ_Q2)
			break;
		z->low += z->low;
		z->high += z->high;
		z->value = 2u * z->value + lzBit(z);
	}
}

static int lzDecodeChar(Lzari *z)
{
	uint32_t range = z->high - z->low, x;
	int i = 1, j = LZ_NCHAR, k, sym, ch;

	x = (uint32_t)(((uint64_t)(z->value - z->low + 1u) * z->sym_cum[0] - 1u) / range);
	while (i < j) {
		k = (i + j) / 2;
		if (z->sym_cum[k] > x)
			i = k + 1;
		else
			j = k;
	}
	sym = i;
	z->high = z->low + (uint32_t)(((uint64_t)range * z->sym_cum[sym - 1]) / z->sym_cum[0]);
	z->low += (uint32_t)(((uint64_t)range * z->sym_cum[sym]) / z->sym_cum[0]);
	lzNarrow(z);
	ch = z->sym_to_char[sym];
	lzUpdate(z, sym);
	return ch;
}

static int lzDecodePosition(Lzari *z)
{
	uint32_t range = z->high - z->low, x;
	int i = 1, j = LZ_N, k, pos;

	x = (uint32_t)(((uint64_t)(z->value - z->low + 1u) * z->position_cum[0] - 1u) / range);
	while (i < j) {
		k = (i + j) / 2;
		if (z->position_cum[k] > x)
			i = k + 1;
		else
			j = k;
	}
	pos = i - 1;
	z->high = z->low + (uint32_t)(((uint64_t)range * z->position_cum[pos]) / z->position_cum[0]);
	z->low += (uint32_t)(((uint64_t)range * z->position_cum[pos + 1]) / z->position_cum[0]);
	lzNarrow(z);
	return pos;
}

int mcLzariDecode(const uint8_t *src, uint32_t src_len, uint8_t *out, uint32_t out_len)
{
	Lzari *z;
	uint8_t *text;
	uint32_t count = 0, r;
	int ok = 1;

	if (!src || !out)
		return -1;
	z = (Lzari *)calloc(1, sizeof(Lzari));
	text = (uint8_t *)malloc(LZ_N);
	if (!z || !text) {
		free(z);
		free(text);
		return -1;
	}
	z->src = src;
	z->src_len = src_len;
	lzStart(z);
	memset(text, ' ', LZ_N - LZ_F);
	memset(text + LZ_N - LZ_F, 0, LZ_F);
	r = LZ_N - LZ_F;
	while (count < out_len) {
		int c = lzDecodeChar(z);

		if (z->past > 64u) { /* the real stream never needs this many bits past its end */
			ok = 0;
			break;
		}
		if (c < 256) {
			out[count++] = (uint8_t)c;
			text[r++] = (uint8_t)c;
			r &= LZ_N - 1;
		} else {
			uint32_t i = ((uint32_t)r - (uint32_t)lzDecodePosition(z) - 1u) & (LZ_N - 1), k;
			int j = c - 255 + LZ_THRESHOLD;

			for (k = 0; k < (uint32_t)j && count < out_len; k++) {
				uint8_t b = text[(i + k) & (LZ_N - 1)];

				out[count++] = b;
				text[r++] = b;
				r &= LZ_N - 1;
			}
		}
	}
	free(z);
	free(text);
	return ok ? 0 : -1;
}

/* ------------------------------------------------------------ MAX Drive */

int mcSaveFromMax(const uint8_t *buf, uint32_t len, const uint8_t stamp[8], McSaveData *out)
{
	static const char magic[12] = "Ps2PowerSave";
	uint8_t *raw;
	uint32_t clen, dirlen, length, pos, i;
	uint64_t total = 0;
	int r;

	if (!buf || !out)
		return MCW_ERR_INVALID;
	memset(out, 0, sizeof(*out));
	if (len < 0x5Cu || memcmp(buf, magic, 12) != 0)
		return MCW_ERR_INVALID;
	clen = rd32(buf + 0x50);
	dirlen = rd32(buf + 0x54);
	length = rd32(buf + 0x58);
	/* clen counts itself (4 bytes) plus the stream; some tools wrote the
	 * uncompressed size there instead, in which case the stream is the rest */
	if (clen != length && (clen < 4u || clen - 4u > len - 0x5Cu))
		return MCW_ERR_INVALID;
	if (length > MAX_INFLATE || dirlen > MAX_FILES)
		return MCW_ERR_INVALID;
	raw = (uint8_t *)malloc(length ? length : 1);
	if (!raw)
		return MCW_ERR_NOMEM;
	if (mcLzariDecode(buf + 0x5C, len - 0x5Cu, raw, length) != 0) {
		free(raw);
		return MCW_ERR_INVALID;
	}
	r = startSave(out, dirlen, MODE_DIR, stamp, stamp, buf + 0x10, 32);
	if (r != MCW_OK) {
		free(raw);
		return fail(out, r);
	}
	for (pos = 0, i = 0; i < dirlen; i++) {
		uint32_t size;

		if (length - pos < 36u)
			goto bad;
		size = rd32(raw + pos);
		if (size > length - pos - 36u)
			goto bad;
		r = addFile(out, i, MODE_FILE, size, stamp, stamp, raw + pos + 4, 32, raw + pos + 36, &total);
		if (r != MCW_OK) {
			free(raw);
			return fail(out, r);
		}
		pos += 36u + size;
		pos = ((pos + 8u + 15u) & ~15u) - 8u; /* each record ends 8 bytes before a 16-byte boundary */
	}
	free(raw);
	return MCW_OK;
bad:
	free(raw);
	return fail(out, MCW_ERR_INVALID);
}

/* ------------------------------------------------------------ CodeBreaker */

/* The RC4 permutation CodeBreaker saves are scrambled with, used as-is: no
 * key schedule, and the stream starts at i = 1 (so the first byte uses S[1]). */
static const uint8_t cbs_rc4[256] = {
	0x5f, 0x1f, 0x85, 0x6f, 0x31, 0xaa, 0x3b, 0x18, 0x21, 0xb9, 0xce, 0x1c, 0x07, 0x4c, 0x9c, 0xb4,
	0x81, 0xb8, 0xef, 0x98, 0x59, 0xae, 0xf9, 0x26, 0xe3, 0x80, 0xa3, 0x29, 0x2d, 0x73, 0x51, 0x62,
	0x7c, 0x64, 0x46, 0xf4, 0x34, 0x1a, 0xf6, 0xe1, 0xba, 0x3a, 0x0d, 0x82, 0x79, 0x0a, 0x5c, 0x16,
	0x71, 0x49, 0x8e, 0xac, 0x8c, 0x9f, 0x35, 0x19, 0x45, 0x94, 0x3f, 0x56, 0x0c, 0x91, 0x00, 0x0b,
	0xd7, 0xb0, 0xdd, 0x39, 0x66, 0xa1, 0x76, 0x52, 0x13, 0x57, 0xf3, 0xbb, 0x4e, 0xe5, 0xdc, 0xf0,
	0x65, 0x84, 0xb2, 0xd6, 0xdf, 0x15, 0x3c, 0x63, 0x1d, 0x89, 0x14, 0xbd, 0xd2, 0x36, 0xfe, 0xb1,
	0xca, 0x8b, 0xa4, 0xc6, 0x9e, 0x67, 0x47, 0x37, 0x42, 0x6d, 0x6a, 0x03, 0x92, 0x70, 0x05, 0x7d,
	0x96, 0x2f, 0x40, 0x90, 0xc4, 0xf1, 0x3e, 0x3d, 0x01, 0xf7, 0x68, 0x1e, 0xc3, 0xfc, 0x72, 0xb5,
	0x54, 0xcf, 0xe7, 0x41, 0xe4, 0x4d, 0x83, 0x55, 0x12, 0x22, 0x09, 0x78, 0xfa, 0xde, 0xa7, 0x06,
	0x08, 0x23, 0xbf, 0x0f, 0xcc, 0xc1, 0x97, 0x61, 0xc5, 0x4a, 0xe6, 0xa0, 0x11, 0xc2, 0xea, 0x74,
	0x02, 0x87, 0xd5, 0xd1, 0x9d, 0xb7, 0x7e, 0x38, 0x60, 0x53, 0x95, 0x8d, 0x25, 0x77, 0x10, 0x5e,
	0x9b, 0x7f, 0xd8, 0x6e, 0xda, 0xa2, 0x2e, 0x20, 0x4f, 0xcd, 0x8f, 0xcb, 0xbe, 0x5a, 0xe0, 0xed,
	0x2c, 0x9a, 0xd4, 0xe2, 0xaf, 0xd0, 0xa9, 0xe8, 0xad, 0x7a, 0xbc, 0xa8, 0xf2, 0xee, 0xeb, 0xf5,
	0xa6, 0x99, 0x28, 0x24, 0x6c, 0x2b, 0x75, 0x5d, 0xf8, 0xd3, 0x86, 0x17, 0xfb, 0xc0, 0x7b, 0xb3,
	0x58, 0xdb, 0xc7, 0x4b, 0xff, 0x04, 0x50, 0xe9, 0x88, 0x69, 0xc9, 0x2a, 0xab, 0xfd, 0x5b, 0x1b,
	0x8a, 0xd9, 0xec, 0x27, 0x44, 0x0e, 0x33, 0xc8, 0x6b, 0x93, 0x32, 0x48, 0xb6, 0x30, 0x43, 0xa5
};

static void cbsRc4(uint8_t *t, uint32_t n)
{
	uint8_t s[256];
	uint32_t ii, i, j = 0;

	memcpy(s, cbs_rc4, 256);
	for (ii = 0; ii < n; ii++) {
		uint8_t tmp;

		i = (ii + 1u) & 255u;
		j = (j + s[i]) & 255u;
		tmp = s[i];
		s[i] = s[j];
		s[j] = tmp;
		t[ii] ^= s[(s[i] + s[j]) & 255u];
	}
}

int mcSaveFromCbs(const uint8_t *buf, uint32_t len, const uint8_t stamp[8], McSaveData *out)
{
	uint8_t *body = NULL, *raw = NULL;
	uint32_t hlen, dlen, flen, clen, raw_len, pos, n, i, dirmode;
	uint64_t total = 0;
	int r;

	if (!buf || !out)
		return MCW_ERR_INVALID;
	memset(out, 0, sizeof(*out));
	if (len < 12u || memcmp(buf, "CFU\0", 4) != 0)
		return MCW_ERR_INVALID;
	hlen = rd32(buf + 8);
	if (hlen < 92u + 32u || hlen > len)
		return MCW_ERR_INVALID;
	dlen = rd32(buf + 0x0C);
	flen = rd32(buf + 0x10);
	dirmode = rd32(buf + 0x4C);
	/* flen is the whole file's length in some saves and the body's in others */
	clen = len - hlen;
	if (flen != len && flen != clen)
		return MCW_ERR_INVALID;
	if (dlen == 0 || dlen > MAX_INFLATE || clen == 0)
		return MCW_ERR_INVALID;
	body = (uint8_t *)malloc(clen);
	if (!body)
		return MCW_ERR_NOMEM;
	memcpy(body, buf + hlen, clen);
	cbsRc4(body, clen);
	r = inflateAll(body, clen, dlen, &raw, &raw_len);
	free(body);
	if (r != MCW_OK)
		return r;
	for (pos = 0, n = 0; pos < raw_len; n++) { /* 64-byte entry, then the data */
		uint32_t size;

		if (raw_len - pos < 64u || n >= MAX_FILES)
			goto bad;
		size = rd32(raw + pos + 16);
		if (size > raw_len - pos - 64u)
			goto bad;
		pos += 64u + size;
	}
	r = startSave(out, n, dirmode, timeOr(buf + 0x34, stamp), timeOr(buf + 0x3C, stamp), buf + 0x14, 32);
	if (r != MCW_OK) {
		free(raw);
		return fail(out, r);
	}
	for (pos = 0, i = 0; i < n; i++) {
		const uint8_t *e = raw + pos;
		uint32_t size = rd32(e + 16), mode = rd16(e + 20);

		if (mode & M_DIR) {
			free(raw);
			return fail(out, MCW_ERR_UNSUPPORTED); /* a save holding sub-folders */
		}
		r = addFile(out, i, mode, size, timeOr(e, stamp), timeOr(e + 8, stamp), e + 32, 32, e + 64, &total);
		if (r != MCW_OK) {
			free(raw);
			return fail(out, r);
		}
		pos += 64u + size;
	}
	free(raw);
	return MCW_OK;
bad:
	free(raw);
	return fail(out, MCW_ERR_INVALID);
}

/* ------------------------------------------------------------ SharkPort / X-Port */

static const uint8_t sps_magic[17] = {0x0d, 0, 0, 0, 'S', 'h', 'a', 'r', 'k', 'P', 'o', 'r', 't', 'S', 'a', 'v', 'e'};

/* mode fields are stored byte-swapped */
static uint32_t spsMode(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }

int mcSaveFromSps(const uint8_t *buf, uint32_t len, McSaveData *out)
{
	uint32_t pos = 17u + 4u, i, hlen, dirlen, k, start;
	uint64_t total = 0;
	int r;

	if (!buf || !out)
		return MCW_ERR_INVALID;
	memset(out, 0, sizeof(*out));
	if (len < 21u || memcmp(buf, sps_magic, 17) != 0)
		return MCW_ERR_INVALID;
	for (k = 0; k < 3; k++) { /* name, date stamp, comment: u32 length, then the bytes */
		uint32_t sl;

		if (len - pos < 4u)
			return MCW_ERR_INVALID;
		sl = rd32(buf + pos);
		pos += 4u;
		if (sl > len - pos)
			return MCW_ERR_INVALID;
		pos += sl;
	}
	if (len - pos < 4u + 98u)
		return MCW_ERR_INVALID;
	pos += 4u; /* the data length that follows; the entries carry their own */
	hlen = rd16(buf + pos);
	dirlen = rd32(buf + pos + 2 + 64);
	if (hlen < 98u || hlen > len - pos || dirlen < 2u || dirlen - 2u > MAX_FILES)
		return MCW_ERR_INVALID;
	r = startSave(out, dirlen - 2u, spsMode(buf + pos + 78), buf + pos + 82, buf + pos + 90, buf + pos + 2, 64);
	if (r != MCW_OK)
		return fail(out, r);
	pos += hlen;
	for (i = 0; i < dirlen - 2u; i++) {
		uint32_t flen, mode;
		const uint8_t *e = buf + pos;

		if (len - pos < 98u)
			return fail(out, MCW_ERR_INVALID);
		hlen = rd16(e);
		flen = rd32(e + 66);
		mode = spsMode(e + 78);
		if (hlen < 98u || hlen > len - pos || flen > len - pos - hlen)
			return fail(out, MCW_ERR_INVALID);
		if (mode & M_DIR)
			return fail(out, MCW_ERR_UNSUPPORTED);
		start = pos + hlen;
		r = addFile(out, i, mode, flen, e + 82, e + 90, e + 2, 64, buf + start, &total);
		if (r != MCW_OK)
			return fail(out, r);
		pos = start + flen;
	}
	return MCW_OK; /* a 4-byte checksum may follow; it is not checked, as in mymc */
}

/* ------------------------------------------------------------ PS3 .psv */

int mcSaveFromPsv(const uint8_t *buf, uint32_t len, McSaveData *out)
{
	uint32_t nfiles, i, dirmode;
	uint64_t total = 0;
	int r;

	if (!buf || !out)
		return MCW_ERR_INVALID;
	memset(out, 0, sizeof(*out));
	if (len < 0xA0u || memcmp(buf, "\0VSP", 4) != 0)
		return MCW_ERR_INVALID;
	if (rd32(buf + 0x3C) != 2u)
		return MCW_ERR_UNSUPPORTED; /* a PS1 save: a flat file, not a folder */
	nfiles = rd32(buf + 0x64);
	dirmode = rd32(buf + 0x7C);
	if (nfiles > MAX_FILES || (uint64_t)nfiles * 60u > len - 0xA0u)
		return MCW_ERR_INVALID;
	r = startSave(out, nfiles, dirmode, buf + 0x68, buf + 0x70, buf + 0x80, 32);
	if (r != MCW_OK)
		return fail(out, r);
	for (i = 0; i < nfiles; i++) {
		const uint8_t *e = buf + 0xA0u + i * 60u;
		uint32_t size = rd32(e + 16), mode = rd32(e + 20), at = rd32(e + 56);

		if (at > len || size > len - at)
			return fail(out, MCW_ERR_INVALID);
		if (mode & M_DIR)
			return fail(out, MCW_ERR_UNSUPPORTED);
		r = addFile(out, i, mode, size, e, e + 8, e + 24, 32, buf + at, &total);
		if (r != MCW_OK)
			return fail(out, r);
	}
	return MCW_OK;
}

/* ------------------------------------------------------------ detection */

static int extIs(const char *ext, const char *want)
{
	size_t i;

	if (!ext)
		return 0;
	if (ext[0] == '.')
		ext++;
	for (i = 0; want[i]; i++) {
		char c = ext[i];

		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		if (c != want[i])
			return 0;
	}
	return ext[i] == 0;
}

int mcSaveFormatKnownExt(const char *ext)
{
	return extIs(ext, "psu") || extIs(ext, "mcm") || extIs(ext, "max") || extIs(ext, "cbs") || extIs(ext, "sps") ||
	       extIs(ext, "xps") || extIs(ext, "psv");
}

const char *mcSaveFormatExt(int format)
{
	switch (format) {
	case MCSF_PSU: return "psu";
	case MCSF_MCM: return "mcm";
	case MCSF_MAX: return "max";
	case MCSF_CBS: return "cbs";
	case MCSF_SPS: return "sps";
	case MCSF_PSV: return "psv";
	default: return "";
	}
}

const char *mcSaveFormatName(int format)
{
	switch (format) {
	case MCSF_PSU: return "EMS / uLaunchELF .psu";
	case MCSF_MCM: return "Blaze Memory Maxx .mcm";
	case MCSF_MAX: return "MAX Drive .max";
	case MCSF_CBS: return "CodeBreaker .cbs";
	case MCSF_SPS: return "SharkPort / X-Port .sps";
	case MCSF_PSV: return "PS3 .psv";
	default: return "unknown";
	}
}

int mcSaveFormatDetect(const uint8_t *buf, uint32_t len, const char *ext_hint)
{
	if (!buf || len < 12u)
		return MCSF_UNKNOWN;
	if (memcmp(buf, "Ps2PowerSave", 12) == 0)
		return MCSF_MAX;
	if (len >= 17u && memcmp(buf, sps_magic, 17) == 0)
		return MCSF_SPS;
	if (memcmp(buf, "CFU\0", 4) == 0)
		return MCSF_CBS;
	if (memcmp(buf, "\0VSP", 4) == 0)
		return MCSF_PSV;
	/* .mcm has no magic: the header's second word is the file's own size
	 * and a zlib stream starts at 80. The extension confirms it. */
	if (len > 82u && rd32(buf + 4) == len && buf[80] == 0x78 && (buf[81] == 0x9C || buf[81] == 0xDA || buf[81] == 0x01 ||
	                                                              buf[81] == 0x5E))
		if (extIs(ext_hint, "mcm") || !ext_hint)
			return MCSF_MCM;
	/* .psu: a directory entry for an existing folder, then "." and ".." */
	if (len >= 3u * ENT && (rd16(buf) & (M_EXISTS | M_DIR)) == (M_EXISTS | M_DIR) && buf[0x40] != 0 &&
	    buf[ENT + 0x40] == '.' && buf[ENT + 0x41] == 0 && buf[2u * ENT + 0x40] == '.' && buf[2u * ENT + 0x41] == '.')
		return MCSF_PSU;
	if (extIs(ext_hint, "mcm") && len > 82u && buf[80] == 0x78)
		return MCSF_MCM;
	return MCSF_UNKNOWN;
}

int mcSaveFormatRead(const uint8_t *buf, uint32_t len, const char *ext_hint, const char *name_hint,
                     const uint8_t stamp[8], McSaveData *out, int *format)
{
	int f = mcSaveFormatDetect(buf, len, ext_hint), r;

	if (format)
		*format = f;
	if (!out)
		return MCW_ERR_INVALID;
	memset(out, 0, sizeof(*out));
	switch (f) {
	case MCSF_PSU: r = mcCardSaveFromPsu(buf, len, out); break;
	case MCSF_MCM: r = name_hint ? mcSaveFromMcm(buf, len, name_hint, stamp, out) : MCW_ERR_INVALID; break;
	case MCSF_MAX: r = mcSaveFromMax(buf, len, stamp, out); break;
	case MCSF_CBS: r = mcSaveFromCbs(buf, len, stamp, out); break;
	case MCSF_SPS: r = mcSaveFromSps(buf, len, out); break;
	case MCSF_PSV: r = mcSaveFromPsv(buf, len, out); break;
	default: r = MCW_ERR_INVALID; break;
	}
	if (r != MCW_OK && format)
		*format = r == MCW_ERR_UNSUPPORTED ? f : (f == MCSF_UNKNOWN ? MCSF_UNKNOWN : f);
	return r;
}
