//--------------------------------------------------------------
//File name:   mc_save_export.c
//PS3 .psv and .psx save exports and the PS1 card containers; see
//include/mc_save_export.h for the layouts.
//--------------------------------------------------------------
#include "mc_save_export.h"
#include "mc_sha256.h"
#include <stdlib.h>
#include <string.h>
#include <mbedtls/aes.h>
#include <mbedtls/md.h>

static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/* ------------------------------------------------------------ crypto */

static const uint8_t ps1_key[16] = {0xab, 0x5a, 0xbc, 0x9f, 0xc1, 0xf4, 0x9d, 0xe6, 0xa0, 0x51, 0xdb, 0xae, 0xfa, 0x51, 0x88, 0x59};
static const uint8_t ps2_key[16] = {0xea, 0x02, 0xce, 0xef, 0x5b, 0xb4, 0xd2, 0x99, 0x8f, 0x61, 0x19, 0x10, 0xd7, 0x7f, 0x51, 0xc6};
static const uint8_t sign_iv[16] = {0xb3, 0x0f, 0xfe, 0xed, 0xb7, 0xdc, 0x5e, 0xb7, 0x13, 0x3d, 0xa6, 0x0d, 0x1b, 0x6b, 0x2c, 0xdc};
static const uint8_t mcx_key[16] = {0x81, 0xd9, 0xcc, 0xe9, 0x71, 0xa9, 0x49, 0x9b, 0x04, 0xad, 0xdc, 0x48, 0x30, 0x7f, 0x07, 0x92};
static const uint8_t mcx_iv[16] = {0x13, 0xc2, 0xe7, 0x69, 0x4b, 0xec, 0x69, 0x6d, 0x52, 0xcf, 0x00, 0x09, 0x2a, 0xc1, 0xf2, 0x72};

/* AES-128 on one 16-byte block, or CBC over len bytes in place. 0 = ok. */
static int aes(const uint8_t key[16], int enc, const uint8_t *iv_in, uint8_t *buf, uint32_t len)
{
	mbedtls_aes_context c;
	uint8_t iv[16];
	int r;

	mbedtls_aes_init(&c);
	r = enc ? mbedtls_aes_setkey_enc(&c, key, 128) : mbedtls_aes_setkey_dec(&c, key, 128);
	if (r == 0 && iv_in) {
		memcpy(iv, iv_in, 16);
		r = mbedtls_aes_crypt_cbc(&c, enc ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT, len, iv, buf, buf);
	} else if (r == 0) {
		r = mbedtls_aes_crypt_ecb(&c, enc ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT, buf, buf);
	}
	mbedtls_aes_free(&c);
	return r;
}

/* HMAC-SHA1 over buf with the 20-byte signature at sig zeroed; the key comes
 * from the 20-byte seed at seed through the PS1 or PS2 key. */
static int sign(uint8_t *buf, uint32_t len, uint32_t seed, uint32_t sig, int ps1)
{
	uint8_t key[32];
	int i;

	if (len < sig + 20u)
		return -1;
	if (ps1) {
		memcpy(key, buf + seed, 16);
		memcpy(key + 16, buf + seed, 16);
		if (aes(ps1_key, 0, NULL, key, 16) || aes(ps1_key, 1, NULL, key + 16, 16))
			return -1;
		for (i = 0; i < 16; i++)
			key[i] ^= sign_iv[i];
		for (i = 0; i < 4; i++)
			key[16 + i] ^= buf[seed + 16 + i];
	} else {
		memset(key, 0, sizeof(key));
		memcpy(key, buf + seed, 20);
		if (aes(ps2_key, 0, sign_iv, key, 32))
			return -1;
	}
	memset(buf + sig, 0, 20);
	return mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), key, 20, buf, len, buf + sig) ? -1 : 0;
}

static void sha256(const uint8_t *buf, uint32_t len, uint8_t out[32])
{
	McSha256 h;

	mcSha256Init(&h);
	mcSha256Update(&h, buf, len);
	mcSha256Final(&h, out);
}

int mcPsvSign(uint8_t *buf, uint32_t len)
{
	if (!buf || len < 0x40u || memcmp(buf, "\0VSP", 4))
		return -1;
	return sign(buf, len, 0x08, 0x1C, buf[0x3C] == 1);
}

int ps1VmpSign(uint8_t *buf, uint32_t len)
{
	if (!buf || len != 0x80u + PS1_CARD_SIZE || memcmp(buf, "\0PMV", 4))
		return -1;
	return sign(buf, len, 0x0C, 0x20, 1);
}

/* ------------------------------------------------------------ .psv .psx */

/* Our seed; any 20 bytes work, since the signature is made from them. */
static void psvHeader(uint8_t *p, int type)
{
	memcpy(p, "\0VSP", 4);
	memcpy(p + 0x08, "Launchstation", 13);
	wr32(p + 0x38, type == 1 ? 0x14u : 0x2Cu);
	wr32(p + 0x3C, (uint32_t)type);
}

static int ps1Check(const Ps1Save *s, uint8_t **out, uint32_t *len)
{
	if (!out || !len || !s || !s->data || s->blocks < 1 || s->blocks > PS1_BLOCKS)
		return PS1_ERR_INVALID;
	*out = NULL;
	*len = 0;
	return ps1NameOk(s->name) ? PS1_OK : PS1_ERR_NAME;
}

int ps1SaveToPsv(const Ps1Save *s, uint8_t **out, uint32_t *len)
{
	uint32_t size, n;
	uint8_t *p;
	int r = ps1Check(s, out, len);

	if (r != PS1_OK)
		return r;
	size = (uint32_t)s->blocks * PS1_BLOCK;
	n = 0x84u + size;
	p = (uint8_t *)calloc(1, n);
	if (!p)
		return PS1_ERR_NOMEM;
	psvHeader(p, 1);
	wr32(p + 0x40, size);
	wr32(p + 0x44, 0x84u);
	wr32(p + 0x48, 0x200u);
	wr32(p + 0x5C, size);
	wr32(p + 0x60, 0x9003u);
	memcpy(p + 0x64, s->name, strlen(s->name));
	memcpy(p + 0x84, s->data, size);
	if (mcPsvSign(p, n) < 0) {
		free(p);
		return PS1_ERR_INVALID;
	}
	*out = p;
	*len = n;
	return PS1_OK;
}

int ps1SaveToPsx(const Ps1Save *s, uint8_t **out, uint32_t *len)
{
	uint32_t size;
	uint8_t *p;
	int r = ps1Check(s, out, len);

	if (r != PS1_OK)
		return r;
	size = (uint32_t)s->blocks * PS1_BLOCK;
	p = (uint8_t *)calloc(1, 54u + size);
	if (!p)
		return PS1_ERR_NOMEM;
	memcpy(p, s->name, strlen(s->name));
	memcpy(p + 54, s->data, size);
	*out = p;
	*len = 54u + size;
	return PS1_OK;
}

int mcSaveToPsv(const McSaveData *s, uint8_t **out, uint32_t *len)
{
	uint64_t n;
	uint32_t i, j, off;
	uint8_t *p;

	if (!s || !out || !len || (s->nfiles && !s->files) || s->nfiles > 0xFFFFu)
		return MCW_ERR_INVALID;
	*out = NULL;
	*len = 0;
	n = 0xA0u + (uint64_t)s->nfiles * 60u;
	for (i = 0; i < s->nfiles; i++)
		n += s->files[i].size;
	if (n > 0x7FFFFFFFu)
		return MCW_ERR_INVALID;
	p = (uint8_t *)calloc(1, (size_t)n);
	if (!p)
		return MCW_ERR_NOMEM;
	psvHeader(p, 2);
	wr32(p + 0x64, s->nfiles);
	memcpy(p + 0x68, s->entry + 0x08, 8);
	memcpy(p + 0x70, s->entry + 0x18, 8);
	wr32(p + 0x78, s->nfiles + 2u);
	wr32(p + 0x7C, (uint32_t)s->entry[0] | ((uint32_t)s->entry[1] << 8));
	memcpy(p + 0x80, s->entry + 0x40, 32);
	off = 0xA0u + s->nfiles * 60u;
	for (i = 0; i < s->nfiles; i++) {
		const McSaveFile *f = &s->files[i];
		uint8_t *e = p + 0xA0u + i * 60u;

		memcpy(e, f->entry + 0x08, 8);
		memcpy(e + 8, f->entry + 0x18, 8);
		wr32(e + 16, f->size);
		wr32(e + 20, (uint32_t)f->entry[0] | ((uint32_t)f->entry[1] << 8));
		memcpy(e + 24, f->entry + 0x40, 32);
		wr32(e + 56, off);
		if (f->size)
			memcpy(p + off, f->data, f->size);
		wr32(p + 0x40, rd32(p + 0x40) + f->size);
		if (!strncmp((const char *)f->entry + 0x40, "icon.sys", 32)) {
			wr32(p + 0x44, off);
			wr32(p + 0x48, f->size);
		}
		off += f->size;
	}
	/* icon.sys names the list, copy and delete icons at 0x104/0x144/0x184 */
	for (i = 0; i < s->nfiles; i++) {
		const McSaveFile *f = &s->files[i];

		if (strncmp((const char *)f->entry + 0x40, "icon.sys", 32) || f->size < 0x1C4u)
			continue;
		for (j = 0; j < 3; j++) {
			const char *icon = (const char *)f->data + 0x104u + j * 64u;
			uint32_t k;

			for (k = 0; k < s->nfiles; k++)
				if (memchr(icon, 0, 33) && !strncmp(icon, (const char *)s->files[k].entry + 0x40, 32)) {
					wr32(p + 0x4C + j * 8, rd32(p + 0xA0u + k * 60u + 56u));
					wr32(p + 0x50 + j * 8, s->files[k].size);
					break;
				}
		}
		break;
	}
	if (mcPsvSign(p, (uint32_t)n) < 0) {
		free(p);
		return MCW_ERR_INVALID;
	}
	*out = p;
	*len = (uint32_t)n;
	return MCW_OK;
}

void mcPsvFileName(char *out, uint32_t size, const char *name)
{
	static const char hex[] = "0123456789ABCDEF";
	uint32_t i, n = 0;

	if (!out || !size)
		return;
	for (i = 0; name[i] && i < 32 && n + 7 < size; i++) {
		if (i < 12) {
			out[n++] = name[i];
		} else {
			out[n++] = hex[(uint8_t)name[i] >> 4];
			out[n++] = hex[(uint8_t)name[i] & 15];
		}
	}
	memcpy(out + n, ".PSV", 5);
}

/* ------------------------------------------------------------ PS1 cards */

static const uint32_t head_size[] = {0, 3904u, 64u, 128u, 128u};

uint32_t ps1CardHeadSize(int kind) { return kind >= PS1C_RAW && kind <= PS1C_MCX ? head_size[kind] : 0; }

uint32_t ps1CardFileSize(int kind)
{
	return ps1CardHeadSize(kind) + PS1_CARD_SIZE + (kind == PS1C_MCX ? 32u : 0u);
}

int ps1CardKind(const uint8_t *file, uint32_t len)
{
	if (!file)
		return -1;
	if (len == PS1_CARD_SIZE && file[0] == 'M' && file[1] == 'C')
		return PS1C_RAW;
	if (len == ps1CardFileSize(PS1C_GME) && !memcmp(file, "123-456-STD", 11))
		return PS1C_GME;
	if (len == ps1CardFileSize(PS1C_VGS) && !memcmp(file, "VgsM", 4))
		return PS1C_VGS;
	if (len == ps1CardFileSize(PS1C_VMP) && !memcmp(file, "\0PMV", 4) && rd32(file + 4) == 0x80u)
		return PS1C_VMP;
	if (len == ps1CardFileSize(PS1C_MCX))
		return PS1C_MCX;
	return -1;
}

int ps1CardOpen(uint8_t *file, uint32_t len, int kind)
{
	uint32_t off = ps1CardHeadSize(kind);
	uint8_t digest[32];

	if (!file || kind < PS1C_RAW || kind > PS1C_MCX || len != ps1CardFileSize(kind))
		return -1;
	if (kind == PS1C_MCX) {
		if (aes(mcx_key, 0, mcx_iv, file, len))
			return -1;
		sha256(file, off + PS1_CARD_SIZE, digest);
		if (memcmp(digest, file + off + PS1_CARD_SIZE, 32))
			return -1;
	}
	return ps1CardIs(file + off, PS1_CARD_SIZE) ? (int)off : -1;
}

int ps1CardSeal(int kind, const uint8_t *raw, const uint8_t *head, uint8_t *out)
{
	uint32_t off = ps1CardHeadSize(kind), i;

	if (!raw || !out || kind < PS1C_RAW || kind > PS1C_MCX || !ps1CardIs(raw, PS1_CARD_SIZE))
		return -1;
	if (head)
		memcpy(out, head, off);
	else
		memset(out, 0, off);
	memcpy(out + off, raw, PS1_CARD_SIZE);
	switch (kind) {
		case PS1C_GME:
			if (!head) {
				memcpy(out, "123-456-STD", 11);
				out[18] = 1;
				out[20] = 1;
				out[21] = 'M';
			}
			for (i = 0; i < PS1_BLOCKS; i++) {
				out[0x16 + i] = raw[(i + 1) * 128u];
				out[0x26 + i] = raw[(i + 1) * 128u + 8u];
			}
			return 0;
		case PS1C_VGS:
			if (!head) {
				memcpy(out, "VgsM", 4);
				wr32(out + 4, 1);
				wr32(out + 8, 1);
				wr32(out + 12, 1);
				wr32(out + 16, 0x80u);
			}
			return 0;
		case PS1C_VMP:
			if (!head) {
				memcpy(out, "\0PMV", 4);
				wr32(out + 4, 0x80u);
			}
			return ps1VmpSign(out, off + PS1_CARD_SIZE);
		case PS1C_MCX:
			sha256(out, off + PS1_CARD_SIZE, out + off + PS1_CARD_SIZE);
			return aes(mcx_key, 1, mcx_iv, out, ps1CardFileSize(kind)) ? -1 : 0;
		default:
			return 0;
	}
}

const char *ps1CardKindExt(int kind)
{
	static const char *const ext[] = {"mcd", "gme", "vgs", "vmp", "mcx"};

	return kind >= PS1C_RAW && kind <= PS1C_MCX ? ext[kind] : "";
}

const char *ps1CardKindName(int kind)
{
	static const char *const name[] = {"raw card", "DexDrive .gme", "Connectix VGS", "PSP / Vita .vmp",
	                                   "PocketStation iOS .mcx"};

	return kind >= PS1C_RAW && kind <= PS1C_MCX ? name[kind] : "unknown";
}

/* ------------------------------------------------------------ .zip */

/* CRC-32 (zip's, reflected 0xEDB88320) four bits at a time: 64 bytes of
 * table instead of zlib's 8 KB ones. */
static uint32_t zipCrc(const uint8_t *p, uint32_t n)
{
	static const uint32_t t[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
	                               0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
	                               0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
	uint32_t c = 0xFFFFFFFFu;

	while (n--) {
		c ^= *p++;
		c = (c >> 4) ^ t[c & 15];
		c = (c >> 4) ^ t[c & 15];
	}
	return ~c;
}

/* A card time (sec, min, hour, day, month, year at 1..7) as DOS time and date. */
static void zipTime(uint8_t *p, const uint8_t *t)
{
	uint32_t y = (uint32_t)t[6] | ((uint32_t)t[7] << 8);

	if (y < 1980u || y > 2107u || !t[4] || !t[5]) {
		wr32(p, 0x00210000u); /* 1980-01-01 00:00 */
		return;
	}
	wr32(p, ((uint32_t)t[3] << 11) | ((uint32_t)t[2] << 5) | (t[1] >> 1) |
	            ((((y - 1980u) << 9) | ((uint32_t)t[5] << 5) | t[4]) << 16));
}

static uint32_t zipNameLen(const uint8_t *name)
{
	uint32_t n = 0;

	while (n < 32u && name[n])
		n++;
	return n;
}

/* One zip header: local (cd = 0) or central (cd = 1), for "folder/" (f NULL)
 * or "folder/name". Returns its length; p NULL only measures. */
static uint32_t zipHead(uint8_t *p, int cd, const char *folder, const McSaveFile *f, const McSaveData *s,
                        uint32_t crc, uint32_t at)
{
	uint32_t fl = zipNameLen((const uint8_t *)folder), nl = f ? zipNameLen(f->entry + 0x40) : 0;
	uint32_t h = cd ? 46u : 30u, size = f ? f->size : 0;
	uint8_t *q;

	if (!p)
		return h + fl + 1u + nl;
	memset(p, 0, h);
	wr32(p, cd ? 0x02014B50u : 0x04034B50u);
	q = p + (cd ? 6 : 4);
	q[0] = 10; /* version needed: stored */
	if (cd)
		p[4] = 20;
	/* the same fields from here on in both: flags, method 0, time, CRC, sizes, name length */
	zipTime(q + 6, (f ? f->entry : s->entry) + 0x18);
	wr32(q + 10, crc);
	wr32(q + 14, size);
	wr32(q + 18, size);
	q[22] = (uint8_t)(fl + 1u + nl);
	if (cd) {
		p[38] = f ? 0x00 : 0x10; /* MS-DOS directory attribute */
		wr32(p + 42, at);
	}
	memcpy(p + h, folder, fl);
	p[h + fl] = '/';
	if (nl)
		memcpy(p + h + fl + 1u, f->entry + 0x40, nl);
	return h + fl + 1u + nl;
}

int mcSaveToZip(const McSaveData *s, uint8_t **out, uint32_t *len)
{
	char folder[33];
	uint64_t n = 22;
	uint32_t i, at, *crc, *off;
	uint8_t *p;

	if (!s || !out || !len || (s->nfiles && !s->files) || s->nfiles > 0xFFFEu)
		return MCW_ERR_INVALID;
	*out = NULL;
	*len = 0;
	memcpy(folder, s->entry + 0x40, 32);
	folder[32] = 0;
	for (i = 0; i <= s->nfiles; i++) {
		const McSaveFile *f = i ? &s->files[i - 1] : NULL;

		n += zipHead(NULL, 0, folder, f, s, 0, 0) + zipHead(NULL, 1, folder, f, s, 0, 0) + (f ? f->size : 0);
	}
	if (n > 0x7FFFFFFFu)
		return MCW_ERR_INVALID;
	p = (uint8_t *)malloc((size_t)n);
	crc = (uint32_t *)malloc((s->nfiles + 1u) * 2u * sizeof(uint32_t));
	if (!p || !crc) {
		free(p);
		free(crc);
		return MCW_ERR_NOMEM;
	}
	off = crc + s->nfiles + 1u;
	for (i = 0, at = 0; i <= s->nfiles; i++) { /* the folder, then its files */
		const McSaveFile *f = i ? &s->files[i - 1] : NULL;

		crc[i] = f && f->size ? zipCrc(f->data, f->size) : 0;
		off[i] = at;
		at += zipHead(p + at, 0, folder, f, s, crc[i], 0);
		if (f && f->size) {
			memcpy(p + at, f->data, f->size);
			at += f->size;
		}
	}
	n = at; /* the central directory starts here */
	for (i = 0; i <= s->nfiles; i++)
		at += zipHead(p + at, 1, folder, i ? &s->files[i - 1] : NULL, s, crc[i], off[i]);
	free(crc);
	memset(p + at, 0, 22);
	wr32(p + at, 0x06054B50u);
	p[at + 8] = p[at + 10] = (uint8_t)(s->nfiles + 1u);
	p[at + 9] = p[at + 11] = (uint8_t)((s->nfiles + 1u) >> 8);
	wr32(p + at + 12, at - (uint32_t)n);
	wr32(p + at + 16, (uint32_t)n);
	*out = p;
	*len = at + 22u;
	return MCW_OK;
}
