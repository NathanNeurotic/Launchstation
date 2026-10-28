/* Host harness for mcViewFingerprint and mc_sha256: prints what they compute. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mc_card_view.h"
#include "mc_sha256.h"

static int rd(void *ctx, uint64_t off, void *buf, uint32_t len)
{
	FILE *f = (FILE *)ctx;

	if (fseek(f, (long)off, SEEK_SET) != 0)
		return -1;
	return fread(buf, 1, len, f) == len ? 0 : -1;
}

static void hex(const uint8_t *d, int n)
{
	int i;

	for (i = 0; i < n; i++)
		printf("%02x", d[i]);
	printf("\n");
}

int main(int argc, char **argv)
{
	FILE *f;
	McViewSource src;
	McView v;
	uint8_t fp[MCV_FINGERPRINT_SIZE];
	int r;

	if (argc == 3 && !strcmp(argv[1], "sha")) {
		/* sha <file>: digest of the whole file, fed in uneven pieces */
		McSha256 s;
		uint8_t buf[777];
		size_t n;
		f = fopen(argv[2], "rb");
		if (!f)
			return 3;
		mcSha256Init(&s);
		while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
			mcSha256Update(&s, buf, n);
		mcSha256Final(&s, fp);
		hex(fp, 32);
		fclose(f);
		return 0;
	}
	if (argc < 2)
		return 2;
	f = fopen(argv[1], "rb");
	if (!f)
		return 3;
	fseek(f, 0, SEEK_END);
	src.size = (uint64_t)ftell(f);
	src.read = rd;
	src.ctx = f;
	r = mcViewOpen(&v, &src);
	printf("open=%d\n", r);
	if (r == MCV_OK) {
		r = mcViewFingerprint(&v, fp);
		printf("fpr=%d\n", r);
		if (r == MCV_OK) {
			printf("fp=");
			hex(fp, MCV_FINGERPRINT_SIZE);
		}
	}
	fclose(f);
	return 0;
}
