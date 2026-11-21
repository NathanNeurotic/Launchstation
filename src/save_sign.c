/* PSV/VMP signature layout and key derivation: dots_tb's Vita MCR2VMP
 * and PSV resigner, as documented in Apollo PS2 source/psv_resign.c.
 * Independent streaming AES-128/HMAC-SHA1 implementation for these formats.
 */
#include "../include/save_sign.h"
#include <string.h>

static uint32_t rol(uint32_t n, unsigned bits)
{
	return (n << bits) | (n >> (32 - bits));
}

static void sha_block(SaveSha1 *s)
{
	uint32_t w[80], a = s->hash[0], b = s->hash[1], c = s->hash[2], d = s->hash[3], e = s->hash[4];
	unsigned i;
	for (i = 0; i < 16; i++) {
		const uint8_t *p = s->block + i * 4;
		w[i] = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
	}
	for (; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	for (i = 0; i < 80; i++) {
		uint32_t f, k, next;
		if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
		else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
		else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
		else { f = b ^ c ^ d; k = 0xca62c1d6; }
		next = rol(a, 5) + f + e + k + w[i];
		e = d; d = c; c = rol(b, 30); b = a; a = next;
	}
	s->hash[0] += a; s->hash[1] += b; s->hash[2] += c; s->hash[3] += d; s->hash[4] += e;
}

void saveHashInit(SaveSha1 *s)
{
	static const uint32_t initial[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
	memcpy(s->hash, initial, sizeof(initial)); s->bytes = s->used = 0;
}

void saveHashUpdate(SaveSha1 *s, const uint8_t *data, unsigned length)
{
	s->bytes += length;
	while (length) {
		unsigned n = 64 - s->used;
		if (n > length) n = length;
		memcpy(s->block + s->used, data, n);
		s->used += n; data += n; length -= n;
		if (s->used == 64) { sha_block(s); s->used = 0; }
	}
}

void saveHashFinish(SaveSha1 *s, uint8_t digest[20])
{
	uint32_t bits = s->bytes << 3;
	uint8_t padding[72] = {0x80};
	unsigned n = s->used < 56 ? 56 - s->used : 120 - s->used;
	padding[n + 4] = bits >> 24; padding[n + 5] = bits >> 16;
	padding[n + 6] = bits >> 8; padding[n + 7] = bits;
	saveHashUpdate(s, padding, n + 8);
	for (unsigned i = 0; i < 20; i++) digest[i] = s->hash[i / 4] >> (24 - (i % 4) * 8);
}

static uint8_t multiply(uint8_t a, uint8_t b)
{
	uint8_t result = 0;
	for (unsigned i = 0; i < 8; i++) {
		if (b & 1) result ^= a;
		a = (a << 1) ^ ((a & 0x80) ? 0x1b : 0); b >>= 1;
	}
	return result;
}

static uint8_t aes_sbox[256], aes_inverse[256];
static int aes_ready;

static void aes_tables(void)
{
	if (aes_ready) return;
	for (unsigned n = 0; n < 256; n++) {
		uint8_t inverse = 0, value, x = n, product = 1;
		/* Multiplicative inverse x^254, followed by the AES affine map. */
		if (n) {
			for (unsigned power = 254; power; power >>= 1) {
				if (power & 1) product = multiply(product, x);
				x = multiply(x, x);
			}
			inverse = product;
		}
		value = inverse ^ 0x63;
		for (unsigned i = 1; i <= 4; i++) value ^= (inverse << i) | (inverse >> (8 - i));
		aes_sbox[n] = value; aes_inverse[value] = n;
	}
	aes_ready = 1;
}

static void aes_expand(const uint8_t key[16], uint8_t round_key[176])
{
	uint8_t rcon = 1;
	memcpy(round_key, key, 16);
	for (unsigned i = 16; i < 176; i += 4) {
		uint8_t t[4]; memcpy(t, round_key + i - 4, 4);
		if (!(i % 16)) {
			uint8_t first = t[0];
			t[0] = aes_sbox[t[1]] ^ rcon; t[1] = aes_sbox[t[2]];
			t[2] = aes_sbox[t[3]]; t[3] = aes_sbox[first]; rcon = multiply(rcon, 2);
		}
		for (unsigned j = 0; j < 4; j++) round_key[i + j] = round_key[i + j - 16] ^ t[j];
	}
}

static void aes_block(const uint8_t input[16], uint8_t output[16], const uint8_t key[176], int decrypt)
{
	uint8_t state[16], next[16];
	memcpy(state, input, 16);
	for (unsigned i = 0; i < 16; i++) state[i] ^= key[(decrypt ? 160 : 0) + i];
	for (unsigned round = 1; round <= 10; round++) {
		unsigned offset = (decrypt ? 10 - round : round) * 16;
		for (unsigned col = 0; col < 4; col++) for (unsigned row = 0; row < 4; row++) {
			unsigned source = ((col + (decrypt ? 4 - row : row)) % 4) * 4 + row;
			next[col * 4 + row] = decrypt ? aes_inverse[state[source]] : aes_sbox[state[source]];
		}
		if (decrypt) for (unsigned i = 0; i < 16; i++) next[i] ^= key[offset + i];
		for (unsigned col = 0; col < 4; col++) {
			uint8_t *p = next + col * 4, a = p[0], b = p[1], c = p[2], d = p[3];
			if (round < 10 && decrypt) {
				p[0] = multiply(a,14)^multiply(b,11)^multiply(c,13)^multiply(d,9);
				p[1] = multiply(a,9)^multiply(b,14)^multiply(c,11)^multiply(d,13);
				p[2] = multiply(a,13)^multiply(b,9)^multiply(c,14)^multiply(d,11);
				p[3] = multiply(a,11)^multiply(b,13)^multiply(c,9)^multiply(d,14);
			} else if (round < 10) {
				p[0] = multiply(a,2)^multiply(b,3)^c^d; p[1] = a^multiply(b,2)^multiply(c,3)^d;
				p[2] = a^b^multiply(c,2)^multiply(d,3); p[3] = multiply(a,3)^b^c^multiply(d,2);
			}
		}
		for (unsigned i = 0; i < 16; i++) state[i] = next[i] ^ (decrypt ? 0 : key[offset + i]);
	}
	memcpy(output, state, 16);
}

int saveSignInit(SaveSigner *s, const uint8_t seed[20], int type)
{
    static const uint8_t keys[2][16] = {
        {0xab,0x5a,0xbc,0x9f,0xc1,0xf4,0x9d,0xe6,0xa0,0x51,0xdb,0xae,0xfa,0x51,0x88,0x59},
        {0xea,0x02,0xce,0xef,0x5b,0xb4,0xd2,0x99,0x8f,0x61,0x19,0x10,0xd7,0x7f,0x51,0xc6}
    };
    static const uint8_t iv[16] = {0xb3,0x0f,0xfe,0xed,0xb7,0xdc,0x5e,0xb7,0x13,0x3d,0xa6,0x0d,0x1b,0x6b,0x2c,0xdc};
    uint8_t expanded[176], key[64] = {0}, block[16];
    if (type != 1 && type != 2) return 0;
    aes_tables(); aes_expand(keys[type - 1], expanded);
    if (type == 1) {
        aes_block(seed, key, expanded, 1);
        aes_block(seed, block, expanded, 0);
        for (unsigned i = 0; i < 16; i++) key[i] ^= iv[i];
        for (unsigned i = 0; i < 4; i++) key[16 + i] = block[i] ^ seed[16 + i];
    } else {
        /* CBC-decrypt the zero-padded 64-byte seed, then retain twenty bytes. */
        uint8_t input[64] = {0};
        memcpy(input, seed, 20);
        for (unsigned pos = 0; pos < 32; pos += 16) {
            aes_block(input + pos, block, expanded, 1);
            for (unsigned i = 0; i < 16; i++) key[pos + i] = block[i] ^ (pos ? input[pos - 16 + i] : iv[i]);
        }
        memset(key + 20, 0, 44);
    }
    for (unsigned i = 0; i < 64; i++) { s->outer[i] = key[i] ^ 0x5c; key[i] ^= 0x36; }
    saveHashInit(&s->sha); saveHashUpdate(&s->sha, key, 64);
    return 1;
}

void saveSignUpdate(SaveSigner *s, const uint8_t *data, unsigned length)
{
    saveHashUpdate(&s->sha, data, length);
}

void saveSignFinish(SaveSigner *s, uint8_t digest[20])
{
    uint8_t inner[20];
    saveHashFinish(&s->sha, inner);
    saveHashInit(&s->sha); saveHashUpdate(&s->sha, s->outer, 64);
    saveHashUpdate(&s->sha, inner, 20); saveHashFinish(&s->sha, digest);
}
