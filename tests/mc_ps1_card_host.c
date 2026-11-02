/* Host driver for src/mc_ps1_card.c, used by tests/test_mc_ps1_card.py.
 *   format OUT                 write a blank card
 *   list CARD                  "free=N" then "first|blocks|damaged|name|titlehex" per save
 *   import CARD FILE NAMEHINT  read a single save and write it into CARD
 *   export CARD FIRST OUT      write one save as .mcs
 *   delete CARD FIRST
 *   parse FILE NAMEHINT        "r=R fmt=F name=N blocks=B"
 * Every command prints r=<result> on failure and exits 0, so the test can
 * check refusals; the card file is rewritten only after PS1_OK. */
#include "mc_ps1_card.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *path, uint32_t *len)
{
	FILE *f = fopen(path, "rb");
	long n;
	uint8_t *b;

	if (!f)
		exit(2);
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = (uint8_t *)malloc(n ? (size_t)n : 1);
	if (!b || (n && fread(b, 1, (size_t)n, f) != (size_t)n))
		exit(2);
	fclose(f);
	*len = (uint32_t)n;
	return b;
}

static void spit(const char *path, const uint8_t *b, uint32_t len)
{
	FILE *f = fopen(path, "wb");

	if (!f || fwrite(b, 1, len, f) != len || fclose(f))
		exit(2);
}

static uint8_t *card(const char *path)
{
	uint32_t len;
	uint8_t *b = slurp(path, &len);

	if (!ps1CardIs(b, len)) {
		printf("r=%d\n", PS1_ERR_NOT_CARD);
		exit(0);
	}
	return b;
}

int main(int argc, char **argv)
{
	uint32_t len;
	uint8_t *c, *b;
	Ps1Save s;
	int r, fmt, i, n;

	if (argc < 3)
		return 2;
	if (!strcmp(argv[1], "format")) {
		c = (uint8_t *)malloc(PS1_CARD_SIZE);
		ps1CardFormat(c);
		spit(argv[2], c, PS1_CARD_SIZE);
		free(c);
		printf("r=0\n");
	} else if (!strcmp(argv[1], "list")) {
		Ps1CardSave l[PS1_BLOCKS];

		c = card(argv[2]);
		n = ps1CardList(c, l, PS1_BLOCKS);
		printf("free=%d\n", ps1CardFreeBlocks(c));
		for (i = 0; i < n; i++) {
			int k;

			printf("%d|%d|%d|%s|", l[i].first, l[i].blocks, l[i].damaged, l[i].name);
			for (k = 0; l[i].title[k]; k++)
				printf("%02x", l[i].title[k]);
			printf("\n");
		}
		free(c);
	} else if (!strcmp(argv[1], "import") && argc == 5) {
		c = card(argv[2]);
		b = slurp(argv[3], &len);
		r = ps1SaveFromFile(b, len, argv[4], &s, &fmt);
		if (r == PS1_OK)
			r = ps1CardWrite(c, &s);
		if (r == PS1_OK)
			spit(argv[2], c, PS1_CARD_SIZE);
		printf("r=%d fmt=%d\n", r, fmt);
		ps1SaveFree(&s);
		free(b);
		free(c);
	} else if (!strcmp(argv[1], "export") && argc == 5) {
		c = card(argv[2]);
		r = ps1CardRead(c, atoi(argv[3]), &s);
		if (r == PS1_OK) {
			r = ps1SaveToMcs(&s, &b, &len);
			if (r == PS1_OK) {
				spit(argv[4], b, len);
				free(b);
			}
		}
		printf("r=%d\n", r);
		ps1SaveFree(&s);
		free(c);
	} else if (!strcmp(argv[1], "delete") && argc == 4) {
		c = card(argv[2]);
		r = ps1CardDelete(c, atoi(argv[3]));
		if (r == PS1_OK)
			spit(argv[2], c, PS1_CARD_SIZE);
		printf("r=%d\n", r);
		free(c);
	} else if (!strcmp(argv[1], "parse") && argc == 4) {
		b = slurp(argv[2], &len);
		r = ps1SaveFromFile(b, len, argv[3], &s, &fmt);
		printf("r=%d fmt=%d name=%s blocks=%d\n", r, fmt, r == PS1_OK ? s.name : "-", s.blocks);
		ps1SaveFree(&s);
		free(b);
	} else {
		return 2;
	}
	return 0;
}
