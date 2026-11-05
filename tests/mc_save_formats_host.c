/* Host harness for src/mc_save_formats.c: reads a save in any supported
 * format and writes it out as a .psu, or runs the LZARI decoder alone. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mc_card_write.h"
#include "mc_save_formats.h"

static uint8_t *slurp(const char *path, uint32_t *len)
{
	FILE *f = fopen(path, "rb");
	uint8_t *buf;
	long n;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
	if (!buf || (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n)) {
		fclose(f);
		free(buf);
		return NULL;
	}
	fclose(f);
	*len = (uint32_t)n;
	return buf;
}

int main(int argc, char **argv)
{
	uint8_t *in;
	uint32_t len;

	/* lzari <in> <out_len> <out> */
	if (argc >= 5 && !strcmp(argv[1], "lzari")) {
		uint32_t out_len = (uint32_t)strtoul(argv[3], NULL, 0);
		uint8_t *out = (uint8_t *)malloc(out_len ? out_len : 1);
		FILE *o;
		int r;

		in = slurp(argv[2], &len);
		if (!in || !out)
			return 3;
		r = mcLzariDecode(in, len, out, out_len);
		free(in);
		printf("r=%d\n", r);
		o = fopen(argv[4], "wb");
		if (!o)
			return 3;
		fwrite(out, 1, out_len, o);
		fclose(o);
		free(out);
		return 0;
	}
	/* conv <in> <out.psu> [name_hint|-] [ext_hint|-] [stamp]
	 * prints format=<n> r=<r> and, on success, name=<folder> files=<n> */
	if (argc >= 4 && !strcmp(argv[1], "conv")) {
		static const uint8_t stamp[8] = {0, 56, 34, 12, 29, 10, 0xEA, 0x07}; /* 2026-10-29 12:34:56 */
		const char *name_hint = argc >= 5 && strcmp(argv[4], "-") ? argv[4] : NULL;
		const char *ext_hint = argc >= 6 && strcmp(argv[5], "-") ? argv[5] : NULL;
		McSaveData s;
		uint8_t *psu;
		uint32_t psu_len;
		int format = 0, r;

		in = slurp(argv[2], &len);
		if (!in)
			return 3;
		r = mcSaveFormatRead(in, len, ext_hint, name_hint, argc >= 7 ? stamp : NULL, &s, &format);
		free(in);
		printf("format=%d r=%d\n", format, r);
		if (r != MCW_OK)
			return 0;
		printf("name=%.32s files=%u\n", (const char *)s.entry + 0x40, (unsigned)s.nfiles);
		r = mcCardSaveToPsu(&s, &psu, &psu_len);
		mcCardFreeSave(&s);
		printf("psu=%d\n", r);
		if (r == MCW_OK) {
			FILE *o = fopen(argv[3], "wb");

			if (!o)
				return 3;
			fwrite(psu, 1, psu_len, o);
			fclose(o);
			free(psu);
		}
		return 0;
	}
	/* detect <in> [ext_hint] */
	if (argc >= 3 && !strcmp(argv[1], "detect")) {
		in = slurp(argv[2], &len);
		if (!in)
			return 3;
		printf("format=%d\n", mcSaveFormatDetect(in, len, argc >= 4 ? argv[3] : NULL));
		free(in);
		return 0;
	}
	if (argc >= 3 && !strcmp(argv[1], "blazepath")) {
		printf("blaze=%d\n", mcSaveFormatBlazePath(argv[2]));
		return 0;
	}
	return 2;
}
