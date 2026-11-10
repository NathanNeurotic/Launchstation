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

    n = smbCnfBuildBlock(block, sizeof(block), "PC", 2, "", "secret",
                         "192.168.1.100", 445, "games", 0);
    check(n > 0 && strstr(block, "USERNAME") == NULL &&
          strstr(block, "PASSWORD = secret\n") != NULL, "password kept without USERNAME");

    /* ---- CR-only line endings: splice the right block ---- */
    {
        const char *cr = "NAME = ONE\rURL = smb://10.0.0.1/x\r\rNAME = TWO\rURL = smb://10.0.0.2/y\r";
        char repl[512];

        smbCnfBuildBlock(repl, sizeof(repl), "TWO", 2, NULL, NULL, "10.0.0.9", 445, "y2", 0);
        len = upsert(cr, "TWO", repl, out, sizeof(out), work, sizeof(work));
        check(len > 0 && strstr(out, "NAME = ONE\rURL = smb://10.0.0.1/x\r") != NULL &&
              strstr(out, "10.0.0.2") == NULL && strstr(out, "smb://10.0.0.9/y2") != NULL,
              "CR-only file: other share kept, target replaced");
        checkParses("cr-only", out, 2);
    }

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
