//--------------------------------------------------------------
//File name:   mc_sha256.h
//Small SHA-256 (FIPS 180-4) for memory card fingerprints. Pure C, no
//dependencies, so it is linked in builds without the cloud TLS stack and
//runs on the host in tests. Written from the FIPS 180-4 specification.
//--------------------------------------------------------------
#ifndef MC_SHA256_H
#define MC_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct McSha256 {
	uint32_t h[8];
	uint64_t bytes;
	uint8_t block[64];
	uint32_t used;
} McSha256;

void mcSha256Init(McSha256 *s);
void mcSha256Update(McSha256 *s, const void *data, size_t len);
void mcSha256Final(McSha256 *s, uint8_t out[32]);

#endif
