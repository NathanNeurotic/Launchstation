"""smbCnfBuildBlock / smbCnfUpsertText: the SMB2.CNF editor's text core."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
work = Path(tempfile.mkdtemp(prefix="smb_cnf_edit_"))

test_c = r"""
#include <stdio.h>
#include <string.h>
#include "smb_shares.h"

static int fails;

static void check(int cond, const char *what)
{
    if (!cond) {
        fails++;
        printf("FAIL: %s\n", what);
    } else {
        printf("ok: %s\n", what);
    }
}

static int upsert(const char *text, const char *orig, const char *block,
                  char *out, int out_size, char *work, int work_size)
{
    return smbCnfUpsertText(text, orig, block, out, out_size, work, work_size);
}

/* The result must still parse, with `want` valid shares and no invalid one. */
static void checkParses(const char *what, const char *text, int want_valid)
{
    char work[8192];
    SmbConfig cfg;
    int valid;

    strcpy(work, text);
    valid = smbConfigParse(work, &cfg);
    check(valid == want_valid, what);
    for (int i = 0; i < cfg.count; i++)
        check(cfg.share[i].state != SMB_STATE_INVALID, what);
}

int main(void)
{
    char block[512], out[8192], work[8192];
    int n, len;

    /* ---- build ---- */
    n = smbCnfBuildBlock(block, sizeof(block), "PC", 2, "player", "secret",
                         "192.168.1.100", 445, "games", 0);
    check(n > 0, "build SMB2 block");
    check(strcmp(block,
                 "NAME = PC\n"
                 "USERNAME = player\n"
                 "PASSWORD = secret\n"
                 "URL = smb://192.168.1.100/games\n") == 0, "SMB2 block text");
    check(strstr(block, ":445") == NULL, "445 port omitted");

    n = smbCnfBuildBlock(block, sizeof(block), "NG", 3, "player", "s",
                         "192.168.1.100", 1445, "games", 0);
    check(n > 0 && strstr(block, "VERSION = 3\n") != NULL &&
          strstr(block, "URL = smb://192.168.1.100:1445/games") != NULL,
          "SMB3 block keeps a non-445 port");

    n = smbCnfBuildBlock(block, sizeof(block), "OLD", 1, NULL, NULL,
                         "192.168.0.30", 445, "PS2SMB", 0);
    check(n > 0 && strstr(block, "VERSION = 1\n") != NULL &&
          strstr(block, "USERNAME") == NULL, "SMB1 block has VERSION only");

    n = smbCnfBuildBlock(block, sizeof(block), "OLD", 1, "u", "p",
                         "192.168.0.30", 445, "PS2SMB", 1);
    check(n > 0 && strstr(block, "PASSWORD = p\nPLAINTEXT = 1\n") != NULL &&
          strstr(block, "URL =") != NULL, "SMB1 PLAINTEXT kept in the block");

    {
        char tiny[10];
        n = smbCnfBuildBlock(tiny, sizeof(tiny), "PC", 3, "player", "secret",
                             "192.168.1.100", 1445, "games", 0);
        check(n < 0, "tiny buffer refused, no out-of-bounds chain");
    }

    n = smbCnfBuildBlock(block, sizeof(block), "PC", 2, "player", "secret",
                         "not.an.ip", 445, "games", 0);
    check(n < 0, "bad IP refused");
    n = smbCnfBuildBlock(block, sizeof(block), "PC", 2, "player", "secret",
                         "192.168.1.100", 0, "games", 0);
    check(n < 0, "port 0 refused");

    /* ---- append to an empty and a commented file ---- */
    n = smbCnfBuildBlock(block, sizeof(block), "PC", 2, "player", "secret",
                         "192.168.1.100", 445, "games", 0);
    len = upsert("", NULL, block, out, sizeof(out), work, sizeof(work));
    check(len == n && strcmp(out, block) == 0, "append into empty file");

    {
        const char *seed = "# my shares\n\n";
        len = upsert(seed, NULL, block, out, sizeof(out), work, sizeof(work));
        check(len == (int)strlen(seed) + n && strcmp(out + strlen(seed), block) == 0,
              "append after a comment");
        checkParses("append", out, 1);
    }

    /* ---- replace keeps comments and neighbours ---- */
    {
        const char *file =
            "# header comment\n"
            "NAME = ONE\n"
            "USERNAME = a\n"
            "PASSWORD = b\n"
            "URL = smb://10.0.0.1/x\n"
            "\n"
            "# above TWO\n"
            "NAME = TWO\n"
            "URL = smb://10.0.0.2/y\n"
            "\n"
            "NAME = THREE\n"
            "URL = smb://10.0.0.3/z\n";
        char repl[512];

        smbCnfBuildBlock(repl, sizeof(repl), "TWO", 3, "u2", "p2",
                         "10.0.0.9", 1445, "y2", 0);
        len = upsert(file, "TWO", repl, out, sizeof(out), work, sizeof(work));
        check(len > 0, "replace middle share");
        check(strstr(out, "# header comment\nNAME = ONE\n") != NULL, "first block kept");
        check(strstr(out, "# above TWO\n") != NULL, "comment above the block kept");
        check(strstr(out, "NAME = TWO\nVERSION = 3\nUSERNAME = u2\nPASSWORD = p2\nURL = smb://10.0.0.9:1445/y2\n") != NULL,
              "new block spliced in");
        check(strstr(out, "\nNAME = THREE\nURL = smb://10.0.0.3/z\n") != NULL,
              "next block kept");
        check(strstr(out, "10.0.0.2") == NULL, "old URL gone");
        checkParses("replace", out, 3);

        /* remove the same block */
        len = upsert(file, "TWO", NULL, out, sizeof(out), work, sizeof(work));
        check(len > 0 && strstr(out, "NAME = TWO") == NULL &&
              strstr(out, "NAME = THREE") != NULL &&
              strstr(out, "# above TWO\n") != NULL &&
              strstr(out, "NAME = ONE") != NULL, "remove middle share");
        checkParses("remove", out, 2);

        /* rename = replace under the old name with a new NAME line */
        smbCnfBuildBlock(repl, sizeof(repl), "DOS", 2, NULL, NULL,
                         "10.0.0.9", 445, "y2", 0);
        len = upsert(file, "TWO", repl, out, sizeof(out), work, sizeof(work));
        check(len > 0 && strstr(out, "NAME = DOS\nURL = smb://10.0.0.9/y2") != NULL &&
              strstr(out, "NAME = TWO") == NULL, "rename via replace");
        checkParses("rename", out, 3);
    }

    /* ---- preserve annotations within edited blocks, not just above them ---- */
    {
        const char *file =
            "NAME = ONE\r\n"
            "# keep this annotation in its share\r\n"
            "USERNAME = player\r\n"
            "; another annotation\n"
            "FUTURE_OPTION = do-not-discard\n"
            "URL = smb://10.0.0.1/old\r\n"
            "NAME = TWO\nURL = smb://10.0.0.2/keep\n";
        char repl[512];
        smbCnfBuildBlock(repl, sizeof(repl), "ONE", 2, "new", "pw",
                         "10.0.0.9", 445, "new", 0);
        len = upsert(file, "ONE", repl, out, sizeof(out), work, sizeof(work));
        check(len > 0 && strstr(out, "# keep this annotation in its share\r\n") != NULL &&
              strstr(out, "; another annotation\n") != NULL &&
              strstr(out, "FUTURE_OPTION = do-not-discard\n") != NULL &&
              strstr(out, "NAME = TWO\nURL = smb://10.0.0.2/keep\n") != NULL,
              "edit retains all in-block comments, unknown keys and later shares");
        check(strstr(out, "# keep this annotation") <
              strstr(out, "URL = smb://10.0.0.9/new"),
              "notes remain within edited block before URL");
        checkParses("annotated edit", out, 2);
        len = upsert(file, "ONE", NULL, out, sizeof(out), work, sizeof(work));
        check(len >= 0 && strstr(out, "# keep this annotation in its share\r\n") != NULL &&
              strstr(out, "; another annotation\n") != NULL &&
              strstr(out, "FUTURE_OPTION") == NULL &&
              strstr(out, "NAME = ONE") == NULL &&
              strstr(out, "NAME = TWO\nURL = smb://10.0.0.2/keep\n") != NULL,
              "deletion retains notes but removes deleted share extension keys");
        checkParses("annotated deletion", out, 1);
    }
    {
        char invalid[512];
        check(smbCnfBuildBlock(invalid, sizeof(invalid), "OK\nURL = smb://bad/x", 2,
                               "user", "secret", "1.1.1.1", 445, "s", 0) < 0,
              "reject newline injection in NAME");
        check(smbCnfBuildBlock(invalid, sizeof(invalid), "OK", 2,
                               "user", "pw\rURL = smb://bad/x", "1.1.1.1", 445, "s", 0) < 0,
              "reject newline injection in PASSWORD");
        check(smbCnfBuildBlock(invalid, sizeof(invalid), "OK", 2,
                               "user", "secret", "1.1.1.1", 445, "s\nNAME = BAD", 0) < 0,
              "reject newline injection in share name");
    }

    /* A BOM belongs at file start, never to a retained obsolete share field. */
    {
        const char *files[] = {
            "\xEF\xBB\xBFNAME = ONE\nUSERNAME = olduser\nPASSWORD = stale_secret\nURL = smb://10.0.0.1/old\n"
            "NAME = TWO\nURL = smb://10.0.0.2/keep\n",
            "\xEF\xBB\xBFPASSWORD = stale_secret\nNAME = ONE\nUSERNAME = olduser\nURL = smb://10.0.0.1/old\n"
            "NAME = TWO\nURL = smb://10.0.0.2/keep\n"
        };
        char repl[512];
        check(smbCnfBuildBlock(repl, sizeof(repl), "ONE", 2, "newuser", "new_secret",
                               "10.0.0.9", 445, "new", 0) > 0, "build BOM replacement");
        for (int j = 0; j < 2; j++) {
            len = upsert(files[j], "ONE", repl, out, sizeof(out), work, sizeof(work));
            check(len > 0 && (unsigned char)out[0] == 0xEF &&
                  (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF,
                  "BOM stays at file start after edit");
            check(len > 0 && strstr(out + 3, "\xEF\xBB\xBF") == NULL &&
                  strstr(out, "stale_secret") == NULL &&
                  strstr(out, "olduser") == NULL,
                  "BOM edit removes all old credentials and embedded BOMs");
            check(len > 0 && strstr(out, "NAME = TWO\nURL = smb://10.0.0.2/keep\n") != NULL,
                  "BOM edit preserves later share");
            if (len > 0) checkParses("BOM edit parses", out, 2);
            len = upsert(files[j], "ONE", NULL, out, sizeof(out), work, sizeof(work));
            check(len > 0 && (unsigned char)out[0] == 0xEF &&
                  strstr(out, "NAME = ONE") == NULL &&
                  strstr(out, "NAME = TWO\nURL = smb://10.0.0.2/keep\n") != NULL,
                  "BOM delete preserves header and later share");
            if (len > 0) checkParses("BOM delete parses", out, 1);
        }
    }

    /* ---- line endings: splicing must not eat the neighbouring shares ---- */
    {
        const char *files[] = {
            "# header\rNAME = ONE\rURL = smb://10.0.0.1/a\r"
            "# second\rNAME = TWO\rURL = smb://10.0.0.2/b\r"
            "# third\rNAME = THREE\rURL = smb://10.0.0.3/c\r",
            "# header\nNAME = ONE\nURL = smb://10.0.0.1/a\n"
            "# second\nNAME = TWO\nURL = smb://10.0.0.2/b\n"
            "# third\nNAME = THREE\nURL = smb://10.0.0.3/c\n",
            "# header\r\nNAME = ONE\r\nURL = smb://10.0.0.1/a\r\n"
            "# second\r\nNAME = TWO\r\nURL = smb://10.0.0.2/b\r\n"
            "# third\r\nNAME = THREE\r\nURL = smb://10.0.0.3/c\r\n",
            "# header\rNAME = ONE\nURL = smb://10.0.0.1/a\r\n"
            "# second\rNAME = TWO\r\nURL = smb://10.0.0.2/b\n"
            "# third\nNAME = THREE\rURL = smb://10.0.0.3/c\r"
        };
        for (int j = 0; j < (int)(sizeof(files) / sizeof(files[0])); j++) {
            const char *file = files[j];
            const char *second = strstr(file, "# second");
            const char *third = strstr(file, "# third");
            char repl[512];
            checkParses("line endings: original parses", file, 3);

            smbCnfBuildBlock(repl, sizeof(repl), "ONE", 2, NULL, NULL,
                             "10.0.0.9", 445, "new", 0);
            len = upsert(file, "ONE", repl, out, sizeof(out), work, sizeof(work));
            check(len > 0 && strstr(out, "smb://10.0.0.9/new") != NULL,
                  "line endings: edit first");
            check(len > 0 && strstr(out, "# second") != NULL &&
                  strcmp(strstr(out, "# second"), second) == 0,
                  "line endings: all later shares byte-identical");
            if (len > 0)
                checkParses("line endings: edited first parses", out, 3);

            smbCnfBuildBlock(repl, sizeof(repl), "TWO", 2, NULL, NULL,
                             "10.0.0.8", 445, "changed", 0);
            len = upsert(file, "TWO", repl, out, sizeof(out), work, sizeof(work));
            check(len > 0 && strstr(out, "smb://10.0.0.8/changed") != NULL &&
                  strstr(out, "# third") != NULL &&
                  strcmp(strstr(out, "# third"), third) == 0,
                  "line endings: edit middle preserves third");
            if (len > 0)
                checkParses("line endings: edited middle parses", out, 3);

            len = upsert(file, "ONE", NULL, out, sizeof(out), work, sizeof(work));
            check(len > 0 && strstr(out, "NAME = ONE") == NULL &&
                  strstr(out, "# second") != NULL &&
                  strcmp(strstr(out, "# second"), second) == 0,
                  "line endings: remove first preserves later shares");
            if (len > 0)
                checkParses("line endings: removed first parses", out, 2);
        }
    }

    /* Refuse edits when the parser has silently dropped overflowing shares. */
    {
        char file[4096];
        int size = 0;
        for (int i = 0; i < SMB_SHARE_MAX + 1; i++)
            size += snprintf(file + size, sizeof(file) - size,
                             "NAME = P%d\nURL = smb://1.1.1.%d/share\n", i, i + 1);
        smbCnfBuildBlock(block, sizeof(block), "P0", 2, NULL, NULL,
                         "2.2.2.2", 445, "new", 0);
        len = upsert(file, "P0", block, out, sizeof(out), work, sizeof(work));
        check(len < 0, "share-limit overflow refused without silent loss");
    }
    {
        const char *file = "NAME = ONE\nURL = smb://1.1.1.1/one\n"
                           "USERNAME = orphan\n";
        smbCnfBuildBlock(block, sizeof(block), "TWO", 2, NULL, NULL,
                         "2.2.2.2", 445, "two", 0);
        len = upsert(file, NULL, block, out, sizeof(out), work, sizeof(work));
        check(len < 0, "unfinished trailing block cannot bleed into appended share");
    }

    /* ---- bounds ---- */
    {
        const char *file = "NAME = A\nURL = smb://1.1.1.1/a\n";
        smbCnfBuildBlock(block, sizeof(block), "B", 2, NULL, NULL, "2.2.2.2", 445, "b", 0);
        len = upsert(file, NULL, block, out, 8, work, sizeof(work));
        check(len < 0, "small out buffer refused");
        len = upsert(file, "A", block, out, sizeof(out), work, 4);
        check(len < 0, "small work buffer refused");
    }

    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails != 0;
}
"""

# fix the two check() calls that pass a format string without varargs
test_c = test_c.replace('"%s: parses, %d valid share(s)", );',
                        '"parses with the wanted valid count");')
test_c = test_c.replace('"%s: no invalid share", );',
                        '"no invalid share in the result");')

src = work / "test_smb_cnf_edit.c"
src.write_text(test_c)
cc = "gcc"
cmd = [cc, "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
       "-I", str(root / "include"), str(src),
       str(root / "src" / "smb_cnf.c"), "-o", str(work / "test_smb_cnf_edit")]
print("+", " ".join(cmd[1:]))
subprocess.run(cmd, check=True)
subprocess.run([str(work / "test_smb_cnf_edit")], check=True)
print("smb cnf edit: OK")
