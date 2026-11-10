/* Host driver for src/mc_save_export.c, used by tests/test_mc_save_export.py.
 *   ps1 FILE NAMEHINT PSV PSX  read a PS1 single save, write it as .psv and .psx
 *   ps2 PSU PSV                read a .psu, write it as .psv
 *   seal KIND RAW HEAD|- OUT   wrap a raw card (HEAD: an earlier header)
 *   open FILE - RAW HEAD       "kind=K" and the raw card and header it holds
 *   name NAME                  the PS3 file name
 * A refusal prints r=<result> and exits 0; nothing is written then. */
#include "mc_save_export.h"
#include "mc_save_formats.h"
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

/* The PS1 reader takes an export back as the same save. */
static void back(const Ps1Save *s, const uint8_t *buf, uint32_t len, int want)
{
	Ps1Save b;
	int fmt;

	if (ps1SaveFromFile(buf, len, NULL, &b, &fmt) != PS1_OK || fmt != want || strcmp(b.name, s->name) ||
	    b.blocks != s->blocks || memcmp(b.data, s->data, (size_t)s->blocks * PS1_BLOCK))
		exit(4);
	ps1SaveFree(&b);
}

int main(int argc, char **argv)
{
	uint8_t *in, *out;
	uint32_t len, olen;
	int r, kind;

	if (argc == 6 && !strcmp(argv[1], "ps1")) {
		Ps1Save s;
		int fmt;

		in = slurp(argv[2], &len);
		r = ps1SaveFromFile(in, len, argv[3], &s, &fmt);
		free(in);
		if (r == PS1_OK && (r = ps1SaveToPsv(&s, &out, &olen)) == PS1_OK) {
			spit(argv[4], out, olen);
			back(&s, out, olen, PS1F_PSV);
			free(out);
			r = ps1SaveToPsx(&s, &out, &olen);
			if (r == PS1_OK) {
				spit(argv[5], out, olen);
				back(&s, out, olen, PS1F_PSX);
				free(out);
			}
		}
		ps1SaveFree(&s);
		printf("r=%d\n", r);
	} else if (argc == 4 && !strcmp(argv[1], "ps2")) {
		McSaveData s;

		in = slurp(argv[2], &len);
		r = mcCardSaveFromPsu(in, len, &s);
		free(in);
		if (r == MCW_OK) {
			r = mcSaveToPsv(&s, &out, &olen);
			if (r == MCW_OK) {
				McSaveData b;

				spit(argv[3], out, olen); /* our own reader takes it back as the same save */
				if (mcSaveFromPsv(out, olen, &b) != MCW_OK || !mcCardSameSave(&s, &b))
					exit(4);
				mcCardFreeSave(&b);
				free(out);
			}
			mcCardFreeSave(&s);
		}
		printf("r=%d\n", r);
	} else if (argc == 4 && !strcmp(argv[1], "zip")) { /* zip <in.psu> <out.zip> */
		McSaveData s;

		in = slurp(argv[2], &len);
		r = mcCardSaveFromPsu(in, len, &s);
		free(in);
		if (r == MCW_OK) {
			r = mcSaveToZip(&s, &out, &olen);
			if (r == MCW_OK) {
				spit(argv[3], out, olen);
				free(out);
			}
			mcCardFreeSave(&s);
		}
		printf("r=%d\n", r);
	} else if (argc == 5 && !strcmp(argv[1], "zipname")) { /* zipname <in.psu> <folder> <first file name> */
		McSaveData s;

		in = slurp(argv[2], &len);
		r = mcCardSaveFromPsu(in, len, &s);
		free(in);
		if (r == MCW_OK) { /* names a crafted card could hold, past the .psu reader's own check */
			memset(s.entry + 0x40, 0, 32);
			strncpy((char *)s.entry + 0x40, argv[3], 31);
			if (s.nfiles) {
				memset(s.files[0].entry + 0x40, 0, 32);
				strncpy((char *)s.files[0].entry + 0x40, argv[4], 31);
			}
			out = NULL;
			r = mcSaveToZip(&s, &out, &olen);
			free(out);
			mcCardFreeSave(&s);
		}
		printf("r=%d\n", r);
	} else if (argc == 6 && !strcmp(argv[1], "seal")) {
		uint8_t *head = NULL;
		uint32_t hlen = 0;

		kind = atoi(argv[2]);
		in = slurp(argv[3], &len);
		if (strcmp(argv[4], "-"))
			head = slurp(argv[4], &hlen);
		if (head && hlen != ps1CardHeadSize(kind))
			exit(3);
		out = (uint8_t *)malloc(PS1_CARD_FILE_MAX + 32u);
		r = len == PS1_CARD_SIZE && out ? ps1CardSeal(kind, in, head, out) : -1;
		if (r == 0)
			spit(argv[5], out, ps1CardFileSize(kind));
		free(out);
		free(head);
		free(in);
		printf("r=%d\n", r);
	} else if (argc == 6 && !strcmp(argv[1], "open")) {
		in = slurp(argv[2], &len);
		kind = ps1CardKind(in, len);
		r = kind < 0 ? -1 : ps1CardOpen(in, len, kind);
		if (r >= 0) {
			spit(argv[4], in + r, PS1_CARD_SIZE);
			spit(argv[5], in, (uint32_t)r);
		}
		free(in);
		printf("kind=%d r=%d\n", kind, r);
	} else if (argc == 3 && !strcmp(argv[1], "name")) {
		char name[2 * 32 + 5];

		mcPsvFileName(name, sizeof(name), argv[2]);
		printf("name=%s\n", name);
	} else {
		return 1;
	}
	return 0;
}
