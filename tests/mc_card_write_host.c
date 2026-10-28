/* Host harness for src/mc_card_write.c: copies, deletes and lists saves in image files. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mc_card_view.h"
#include "mc_card_write.h"
#include "mc_sha256.h"

static long writes, fail_after = -1;

static int rd(void *ctx, uint64_t off, void *buf, uint32_t len)
{
	FILE *f = (FILE *)ctx;

	if (fseek(f, (long)off, SEEK_SET) != 0)
		return -1;
	return fread(buf, 1, len, f) == len ? 0 : -1;
}

/* Fails (without writing) on write number fail_after, and on every later one:
 * the card is "unplugged" mid-operation. */
static int wr(void *ctx, uint64_t off, const void *buf, uint32_t len)
{
	FILE *f = (FILE *)ctx;

	if (fail_after >= 0 && writes >= fail_after)
		return -1;
	writes++;
	if (fseek(f, (long)off, SEEK_SET) != 0 || fwrite(buf, 1, len, f) != len || fflush(f) != 0)
		return -1;
	return 0;
}

static FILE *openCard(const char *path, const char *mode, McView *v)
{
	McViewSource src;
	FILE *f = fopen(path, mode);
	int r;

	if (!f)
		exit(3);
	fseek(f, 0, SEEK_END);
	src.size = (uint64_t)ftell(f);
	src.read = rd;
	src.ctx = f;
	r = mcViewOpen(v, &src);
	if (r != MCV_OK) {
		printf("open=%d\n", r);
		fclose(f);
		exit(0);
	}
	return f;
}

static void list(const char *path)
{
	static McView v;
	McViewSave saves[200];
	McSaveData s;
	uint32_t freec, tot, i, j;
	uint8_t d[32];
	int n, total, r, k;
	FILE *f = openCard(path, "rb", &v);

	printf("open=0\n");
	r = mcViewFreeClusters(&v, &freec, &tot);
	printf("free=%d %u %u\n", r, freec, tot);
	n = mcViewListSaves(&v, saves, 200, &total);
	printf("nsaves=%d\n", n);
	for (k = 0; k < n; k++) {
		r = mcCardReadSave(&v, saves[k].name, &s);
		printf("save|%s|%d|%u", saves[k].name, r, r == 0 ? s.nfiles : 0);
		for (i = 0; r == 0 && i < s.nfiles; i++) {
			McSha256 h;
			mcSha256Init(&h);
			mcSha256Update(&h, s.files[i].data, s.files[i].size);
			mcSha256Final(&h, d);
			printf("|%.32s:%u:", (char *)s.files[i].entry + 0x40, s.files[i].size);
			for (j = 0; j < 8; j++)
				printf("%02x", d[j]);
		}
		printf("\n");
		if (r == 0)
			mcCardFreeSave(&s);
	}
	fclose(f);
}

int main(int argc, char **argv)
{
	static McView sv, dv;
	McSaveData s;
	FILE *sf, *df;
	int r;

	if (argc >= 3 && !strcmp(argv[1], "list")) {
		list(argv[2]);
		return 0;
	}
	if (argc >= 5 && !strcmp(argv[1], "copy")) {
		if (argc >= 6)
			fail_after = atol(argv[5]);
		sf = openCard(argv[2], "rb", &sv);
		r = mcCardReadSave(&sv, argv[3], &s);
		fclose(sf); /* MMCE allows one open file: never hold both */
		if (r != 0) {
			printf("read=%d\n", r);
			return 0;
		}
		df = openCard(argv[4], "r+b", &dv);
		r = mcCardWriteSave(&dv, wr, df, &s);
		printf("r=%d writes=%ld\n", r, writes);
		mcCardFreeSave(&s);
		fclose(df);
		return 0;
	}
	if (argc >= 5 && !strcmp(argv[1], "psuout")) {
		uint8_t *buf;
		uint32_t len;
		FILE *o;

		sf = openCard(argv[2], "rb", &sv);
		r = mcCardReadSave(&sv, argv[3], &s);
		fclose(sf);
		if (r != 0) {
			printf("read=%d\n", r);
			return 0;
		}
		r = mcCardSaveToPsu(&s, &buf, &len);
		if (r == 0) {
			o = fopen(argv[4], "wb");
			if (!o || fwrite(buf, 1, len, o) != len || fclose(o) != 0)
				return 3;
			free(buf);
		}
		printf("r=%d size=%u\n", r, r == 0 ? mcCardPsuSize(&s) : 0);
		mcCardFreeSave(&s);
		return 0;
	}
	if (argc >= 4 && !strcmp(argv[1], "psuin")) {
		uint8_t *buf;
		long len;
		FILE *in = fopen(argv[2], "rb");

		if (argc >= 5)
			fail_after = atol(argv[4]);
		if (!in)
			return 3;
		fseek(in, 0, SEEK_END);
		len = ftell(in);
		fseek(in, 0, SEEK_SET);
		buf = (uint8_t *)malloc(len ? (size_t)len : 1);
		if (!buf || fread(buf, 1, (size_t)len, in) != (size_t)len)
			return 3;
		fclose(in);
		r = mcCardSaveFromPsu(buf, (uint32_t)len, &s);
		free(buf); /* the parsed save owns copies of everything */
		if (r != 0) {
			printf("parse=%d\n", r);
			return 0;
		}
		df = openCard(argv[3], "r+b", &dv);
		r = mcCardWriteSave(&dv, wr, df, &s);
		printf("r=%d writes=%ld\n", r, writes);
		mcCardFreeSave(&s);
		fclose(df);
		return 0;
	}
	if (argc >= 5 && !strcmp(argv[1], "format")) {
		FILE *o;

		uint8_t sb[512], root[1024];

		sf = openCard(argv[2], "rb", &sv);
		r = mcCardReadIdentity(&sv, sb, root);
		fclose(sf);
		if (r != 0) {
			printf("identity=%d\n", r);
			return 0;
		}
		o = fopen(argv[4], "wb");
		if (!o)
			return 3;
		r = mcCardFormat(sb, root, (uint32_t)atoi(argv[3]), wr, o);
		fclose(o);
		printf("r=%d\n", r);
		return 0;
	}
	if (argc >= 4 && !strcmp(argv[1], "delete")) {
		if (argc >= 5)
			fail_after = atol(argv[4]);
		df = openCard(argv[2], "r+b", &dv);
		r = mcCardDeleteSave(&dv, wr, df, argv[3]);
		printf("r=%d writes=%ld\n", r, writes);
		fclose(df);
		return 0;
	}
	return 2;
}
