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
