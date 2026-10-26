/* Host harness for src/mc_card_view.c: prints what the viewer sees. */
#include <stdio.h>
#include <stdlib.h>
#include "mc_card_view.h"

static int rd(void *ctx, uint64_t off, void *buf, uint32_t len)
{
	FILE *f = (FILE *)ctx;

	if (fseek(f, (long)off, SEEK_SET) != 0)
		return -1;
	return fread(buf, 1, len, f) == len ? 0 : -1;
}

int main(int argc, char **argv)
{
	FILE *f;
	McViewSource src;
	McView v;
	McViewSave saves[64];
	uint32_t freec, tot;
	int r, total, n, i, j;

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
	if (r != MCV_OK)
		return 0;
	printf("ecc=%d clusters=%u usable=%u\n", v.has_ecc, v.clusters_per_card, v.alloc_usable);
	r = mcViewFreeClusters(&v, &freec, &tot);
	printf("free=%d %u %u\n", r, freec, tot);
	n = mcViewListSaves(&v, saves, 64, &total);
	printf("nsaves=%d total=%d\n", n, total);
	for (i = 0; i < n; i++) {
		printf("save|%s|%u|%u|%d|%d|%u|", saves[i].name, saves[i].files, saves[i].bytes,
		       saves[i].has_icon_sys, saves[i].is_ps1, saves[i].title_break);
		for (j = 0; saves[i].title[j]; j++)
			printf("%02x", (unsigned char)saves[i].title[j]);
		printf("\n");
	}
	fclose(f);
	return 0;
}
