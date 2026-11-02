//--------------------------------------------------------------
//File name:   mc_ps1_card.c
//PS1 memory card images and PS1 single-save files; see mc_ps1_card.h for
//the layouts. Pure C on buffers in memory, so the host tests run it as is.
//--------------------------------------------------------------
#include "mc_ps1_card.h"
#include <stdlib.h>
#include <string.h>

#define FRAME 128u
#define ST_FIRST 0x51u
#define ST_MIDDLE 0x52u
#define ST_LAST 0x53u
#define ST_FREE 0xA0u
#define NO_LINK 0xFFFFu

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
	wr16(p, v);
	wr16(p + 2, v >> 16);
}

static const uint8_t *dirFrame(const uint8_t *card, int block) { return card + (uint32_t)block * FRAME; }
static uint8_t *dirFrameW(uint8_t *card, int block) { return card + (uint32_t)block * FRAME; }
static const uint8_t *blockData(const uint8_t *card, int block) { return card + (uint32_t)block * PS1_BLOCK; }

static void checksum(uint8_t *f)
{
	uint8_t x = 0;
	uint32_t i;

	for (i = 0; i < FRAME - 1; i++)
		x ^= f[i];
	f[FRAME - 1] = x;
}

static int isFree(uint32_t state) { return (state & 0xF0u) == 0xA0u; }

int ps1NameOk(const char *name)
{
	size_t i, n;

	if (!name)
		return 0;
	n = strlen(name);
	if (n == 0 || n > PS1_NAME_MAX)
		return 0;
	for (i = 0; i < n; i++)
		if ((unsigned char)name[i] < 0x20 || (unsigned char)name[i] > 0x7E)
			return 0;
	return 1;
}

/* The name field: up to 20 bytes, ended by NUL or the field's end. */
static void readName(const uint8_t *field, char out[PS1_NAME_MAX + 1])
{
	int i;

	for (i = 0; i < PS1_NAME_MAX && field[i]; i++)
		out[i] = (char)field[i];
	out[i] = 0;
}

int ps1CardIs(const uint8_t *buf, uint32_t len)
{
	return buf && len == PS1_CARD_SIZE && buf[0] == 'M' && buf[1] == 'C';
}

void ps1CardFormat(uint8_t *card)
{
	int i;

	memset(card, 0, PS1_CARD_SIZE);
	card[0] = 'M';
	card[1] = 'C';
	checksum(card);
	for (i = 1; i <= PS1_BLOCKS; i++) {
		uint8_t *f = dirFrameW(card, i);

		wr32(f, ST_FREE);
		wr16(f + 8, NO_LINK);
		checksum(f);
	}
	for (i = 16; i <= 35; i++) {
		uint8_t *f = dirFrameW(card, i);

		wr32(f, 0xFFFFFFFFu);
		wr16(f + 8, NO_LINK);
		checksum(f);
	}
	memcpy(dirFrameW(card, 63), card, FRAME);
}

int ps1CardFreeBlocks(const uint8_t *card)
{
	int i, n = 0;

	for (i = 1; i <= PS1_BLOCKS; i++)
		if (isFree(rd32(dirFrame(card, i))))
			n++;
	return n;
}

/* Follows the links from a first block; fills chain[] (blocks, 1-based) and
 * returns the count, or -1 when a link leaves the card, loops, or lands on a
 * frame that is not a middle or last block. */
static int chainOf(const uint8_t *card, int first, int chain[PS1_BLOCKS])
{
	int n = 0, b = first, seen = 0;

	if (first < 1 || first > PS1_BLOCKS || rd32(dirFrame(card, first)) != ST_FIRST)
		return -1;
	for (;;) {
		uint32_t next;

		if (seen & (1 << b))
			return -1;
		seen |= 1 << b;
		chain[n++] = b;
		next = rd16(dirFrame(card, b) + 8);
		if (next == NO_LINK)
			return rd32(dirFrame(card, b)) == (n == 1 ? ST_FIRST : ST_LAST) ? n : -1;
		if (next > PS1_BLOCKS - 1u || n == PS1_BLOCKS)
			return -1;
		b = (int)next + 1;
		{
			uint32_t st = rd32(dirFrame(card, b));

			if (st != ST_MIDDLE && st != ST_LAST)
				return -1;
		}
	}
}

int ps1CardList(const uint8_t *card, Ps1CardSave *out, int max)
{
	int i, n = 0, chain[PS1_BLOCKS];

	for (i = 1; i <= PS1_BLOCKS && n < max; i++) {
		const uint8_t *f = dirFrame(card, i);
		Ps1CardSave *s;
		int len;

		if (rd32(f) != ST_FIRST)
			continue;
		s = &out[n++];
		memset(s, 0, sizeof(*s));
		s->first = i;
		readName(f + 0x0A, s->name);
		len = chainOf(card, i, chain);
		s->damaged = len < 0;
		s->blocks = len < 0 ? 1 : len;
		if (blockData(card, i)[0] == 'S' && blockData(card, i)[1] == 'C') {
			memcpy(s->title, blockData(card, i) + 4, 64);
			s->title[64] = 0;
		}
	}
	return n;
}

int ps1CardRead(const uint8_t *card, int first, Ps1Save *out)
{
	int chain[PS1_BLOCKS], n, i;

	if (!out)
		return PS1_ERR_INVALID;
	memset(out, 0, sizeof(*out));
	n = chainOf(card, first, chain);
	if (n < 0)
		return PS1_ERR_DAMAGED;
	out->data = (uint8_t *)malloc((size_t)n * PS1_BLOCK);
	if (!out->data)
		return PS1_ERR_NOMEM;
	for (i = 0; i < n; i++)
		memcpy(out->data + (uint32_t)i * PS1_BLOCK, blockData(card, chain[i]), PS1_BLOCK);
	out->blocks = n;
	readName(dirFrame(card, first) + 0x0A, out->name);
	return PS1_OK;
}

static int validSave(const Ps1Save *s)
{
	return s && s->data && s->blocks >= 1 && s->blocks <= PS1_BLOCKS && ps1NameOk(s->name);
}

int ps1CardWrite(uint8_t *card, const Ps1Save *s)
{
	int i, n = 0, pick[PS1_BLOCKS];
	char name[PS1_NAME_MAX + 1];

	if (!validSave(s))
		return s && s->data && s->blocks >= 1 && s->blocks <= PS1_BLOCKS ? PS1_ERR_NAME : PS1_ERR_INVALID;
	for (i = 1; i <= PS1_BLOCKS; i++) {
		uint32_t st = rd32(dirFrame(card, i));

		if (st == ST_FIRST) {
			readName(dirFrame(card, i) + 0x0A, name);
			if (!strcmp(name, s->name))
				return PS1_ERR_EXISTS;
		}
		if (isFree(st) && n < s->blocks)
			pick[n++] = i;
	}
	if (n < s->blocks)
		return PS1_ERR_FULL;
	for (i = 0; i < n; i++) {
		uint8_t *f = dirFrameW(card, pick[i]);

		memcpy(card + (uint32_t)pick[i] * PS1_BLOCK, s->data + (uint32_t)i * PS1_BLOCK, PS1_BLOCK);
		memset(f, 0, FRAME);
		wr32(f, i == 0 ? ST_FIRST : i == n - 1 ? ST_LAST : ST_MIDDLE);
		wr32(f + 4, i == 0 ? (uint32_t)n * PS1_BLOCK : 0u);
		wr16(f + 8, i == n - 1 ? NO_LINK : (uint32_t)(pick[i + 1] - 1));
		if (i == 0)
			memcpy(f + 0x0A, s->name, strlen(s->name));
		checksum(f);
	}
	return PS1_OK;
}

int ps1CardDelete(uint8_t *card, int first)
{
	int chain[PS1_BLOCKS], n, i;

	n = chainOf(card, first, chain);
	if (n < 0)
		return PS1_ERR_DAMAGED;
	for (i = 0; i < n; i++) {
		uint8_t *f = dirFrameW(card, chain[i]);

		wr32(f, rd32(f) - ST_FIRST + 0xA1u);
		checksum(f);
	}
	return PS1_OK;
}

/* ------------------------------------------------------------ single saves */

static int takeBlocks(const uint8_t *data, uint32_t len, const char *name, Ps1Save *out)
{
	if (len == 0 || len % PS1_BLOCK || len / PS1_BLOCK > PS1_BLOCKS)
		return PS1_ERR_INVALID;
	if (data[0] != 'S' || data[1] != 'C') /* every PS1 save's first block */
		return PS1_ERR_INVALID;
	if (!ps1NameOk(name))
		return PS1_ERR_NAME;
	out->data = (uint8_t *)malloc(len);
	if (!out->data)
		return PS1_ERR_NOMEM;
	memcpy(out->data, data, len);
	out->blocks = (int)(len / PS1_BLOCK);
	strcpy(out->name, name);
	return PS1_OK;
}

int ps1SaveFromFile(const uint8_t *buf, uint32_t len, const char *name_hint, Ps1Save *out, int *format)
{
	char name[PS1_NAME_MAX + 1];
	int fmt = PS1F_UNKNOWN, r;

	if (format)
		*format = PS1F_UNKNOWN;
	if (!buf || !out)
		return PS1_ERR_INVALID;
	memset(out, 0, sizeof(*out));
	if (len >= 0x84u && !memcmp(buf, "\0VSP", 4)) {
		fmt = PS1F_PSV;
		if (format)
			*format = fmt;
		if (buf[0x3C] != 1)
			return PS1_ERR_UNSUPPORTED; /* a PS2 save */
		readName(buf + 0x64, name);
		return takeBlocks(buf + 0x84, len - 0x84u, name, out);
	}
	if (len >= FRAME && buf[0] == ST_FIRST) {
		fmt = PS1F_MCS;
		readName(buf + 0x0A, name);
		r = takeBlocks(buf + FRAME, len - FRAME, name, out);
	} else if (len >= 2 && (buf[0] == 'S' || buf[0] == 's') && (buf[1] == 'C' || buf[1] == 'c')) {
		const char *base = name_hint ? name_hint : "";
		const char *dot = strrchr(base, '.');
		size_t n = dot ? (size_t)(dot - base) : strlen(base);

		fmt = PS1F_RAW;
		if (n > PS1_NAME_MAX) {
			r = PS1_ERR_NAME;
		} else {
			memcpy(name, base, n);
			name[n] = 0;
			/* "sc" was seen on one game's save; the BIOS itself writes "SC" */
			if (len % PS1_BLOCK == 0 && len && len / PS1_BLOCK <= PS1_BLOCKS && ps1NameOk(name)) {
				out->data = (uint8_t *)malloc(len);
				if (!out->data) {
					r = PS1_ERR_NOMEM;
				} else {
					memcpy(out->data, buf, len);
					out->blocks = (int)(len / PS1_BLOCK);
					strcpy(out->name, name);
					r = PS1_OK;
				}
			} else {
				r = ps1NameOk(name) ? PS1_ERR_INVALID : PS1_ERR_NAME;
			}
		}
	} else if (len >= 56u && buf[0x36] == 'S' && buf[0x37] == 'C') { /* reads bytes 0x36 and 0x37 */
		fmt = PS1F_PSX;
		readName(buf, name);
		r = takeBlocks(buf + 54, len - 54u, name, out);
	} else {
		return PS1_ERR_INVALID;
	}
	if (format)
		*format = fmt;
	return r;
}

int ps1SaveToMcs(const Ps1Save *s, uint8_t **out, uint32_t *len)
{
	uint32_t n;
	uint8_t *b;

	if (!out || !len || !s || !s->data || s->blocks < 1 || s->blocks > PS1_BLOCKS)
		return PS1_ERR_INVALID;
	if (!ps1NameOk(s->name)) /* a card's name field can hold any bytes */
		return PS1_ERR_NAME;
	n = FRAME + (uint32_t)s->blocks * PS1_BLOCK;
	b = (uint8_t *)calloc(1, n);
	if (!b)
		return PS1_ERR_NOMEM;
	wr32(b, ST_FIRST);
	wr32(b + 4, (uint32_t)s->blocks * PS1_BLOCK);
	wr16(b + 8, NO_LINK);
	memcpy(b + 0x0A, s->name, strlen(s->name));
	checksum(b);
	memcpy(b + FRAME, s->data, (uint32_t)s->blocks * PS1_BLOCK);
	*out = b;
	*len = n;
	return PS1_OK;
}

void ps1SaveFree(Ps1Save *s)
{
	if (s) {
		free(s->data);
		memset(s, 0, sizeof(*s));
	}
}

const char *ps1SaveFormatName(int format)
{
	switch (format) {
		case PS1F_MCS:
			return "PS1 single save (.mcs)";
		case PS1F_PSV:
			return "PS3 export of a PS1 save (.psv)";
		case PS1F_PSX:
			return "Action Replay / GameShark (.psx)";
		case PS1F_RAW:
			return "raw PS1 save";
		default:
			return "unknown";
	}
}

const char *ps1Error(int r)
{
	switch (r) {
		case PS1_OK:
			return "Done.";
		case PS1_ERR_NOT_CARD:
			return "This is not a 128 KB PS1 card image.";
		case PS1_ERR_INVALID:
			return "Not a PS1 save this can read, or its sizes do not fit.";
		case PS1_ERR_UNSUPPORTED:
			return "This .psv holds a PS2 save. Use it on a PS2 card instead.";
		case PS1_ERR_FULL:
			return "Not enough free blocks on this card.";
		case PS1_ERR_EXISTS:
			return "A save with that name is already on this card. Delete it first.";
		case PS1_ERR_DAMAGED:
			return "This save's block links are broken, so it cannot be read safely.";
		case PS1_ERR_NOMEM:
			return "Not enough memory.";
		case PS1_ERR_NAME:
			return "The save name must be 1 to 20 plain ASCII characters.";
		default:
			return "The card could not be read or written.";
	}
}
