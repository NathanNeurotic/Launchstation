"""Run cardUpdateIgnoresBrowser() from src/main_actions.c against fake memory cards.

The PS2 system browser row adds SkipMc only on cards whose update ignores
BootBrowser: LoadBOOTer (path at offset 12440) and FMCB (SYS-CONF/FREEMCB.CNF
on that card, OSDMenu's SYS-CONF/OSDMENU.CNF on neither card) unless its
OSDSYS_Inner_Browser is 0 or 1, which pass the Browser request on. All checks
are read-only.
"""
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = (root / "src/main_actions.c").read_text(encoding="utf-8")
m = re.search(r"static int fmcbDropsBrowser\(int fd\)\n\{.*?\n\}\n\nstatic int cardUpdateIgnoresBrowser\(const char \*update_path\)\n\{.*?\n\}\n", src, re.S)
assert m, "fmcbDropsBrowser/cardUpdateIgnoresBrowser not found"
func = m.group(0)
assert "FIO_O_WRONLY" not in func and "genWrite" not in func and "genRemove" not in func, "detection must stay read-only"

harness = r'''
#include <stdio.h>
#include <string.h>
#define MAX_PATH 256
#define FIO_O_RDONLY 1
#define SEEK_SET 0
/* Fake files: name -> contents. */
static const char *names[8]; static const char *data[8]; static int lens[8]; static int nfiles;
static int pos[8]; static int opened, flagsSeen;
static void add(const char *n, const char *d, int l) { names[nfiles] = n; data[nfiles] = d; lens[nfiles++] = l; }
static int genOpen(const char *p, int f) { int i; flagsSeen |= f; for (i = 0; i < nfiles; i++) if (!strcmp(p, names[i])) { pos[i] = 0; opened++; return i; } return -1; }
static int genClose(int fd) { opened--; return 0; }
static int genLseek(int fd, int off, int w) { if (off > lens[fd]) return -1; pos[fd] = off; return off; }
static int genRead(int fd, void *b, int n) { int left = lens[fd] - pos[fd]; if (n > left) n = left; memcpy(b, data[fd] + pos[fd], n); pos[fd] += n; return n; }
FUNC
static char lb[13896], cnf[1400];
static void fmcb(const char *value) {
    memset(cnf, '#', sizeof(cnf));
    memcpy(cnf + 500, "\r\nOSDSYS_Inner_Browser = ", 25);
    memcpy(cnf + 525, value, strlen(value));
    add("mc0:/BAEXEC-SYSTEM/osdmain.elf", "x", 1); add("mc0:/SYS-CONF/FREEMCB.CNF", cnf, sizeof(cnf));
}
static int run(const char *label, int expect) {
    int got = cardUpdateIgnoresBrowser("mc:/BAEXEC-SYSTEM/osdmain.elf");
    if (got != expect || opened != 0 || flagsSeen != FIO_O_RDONLY) { printf("FAIL %s: got %d want %d open=%d flags=%d\n", label, got, expect, opened, flagsSeen); return 1; }
    nfiles = 0; flagsSeen = FIO_O_RDONLY; return 0;
}
int main(void) {
    int bad = 0;
    memcpy(lb + 12440, "mc0:/BOOT/BOOT.ELF", 18);
    flagsSeen = FIO_O_RDONLY;
    bad |= run("no update", 0);
    add("mc0:/BAEXEC-SYSTEM/osdmain.elf", lb, sizeof(lb)); bad |= run("LoadBOOTer mc0", 1);
    add("mc1:/BAEXEC-SYSTEM/osdmain.elf", lb, sizeof(lb)); bad |= run("LoadBOOTer mc1", 1);
    add("mc0:/BAEXEC-SYSTEM/osdmain.elf", "x", 1); bad |= run("other update, no config", 0);
    add("mc0:/BAEXEC-SYSTEM/osdmain.elf", "x", 1); add("mc0:/SYS-CONF/FREEMCB.CNF", "", 0); bad |= run("FMCB", 1);
    add("mc0:/BAEXEC-SYSTEM/osdmain.elf", "x", 1); add("mc1:/SYS-CONF/FREEMCB.CNF", "", 0); bad |= run("FMCB config on other card only", 0);
    add("mc0:/BAEXEC-SYSTEM/osdmain.elf", "x", 1); add("mc0:/SYS-CONF/FREEMCB.CNF", "", 0); add("mc0:/SYS-CONF/OSDMENU.CNF", "", 0); bad |= run("OSDMenu leftover FREEMCB.CNF", 0);
    add("mc0:/BAEXEC-SYSTEM/osdmain.elf", "x", 1); add("mc0:/SYS-CONF/FREEMCB.CNF", "", 0); add("mc1:/SYS-CONF/OSDMENU.CNF", "", 0); bad |= run("OSDMenu on other card", 0);
    add("mc0:/BAEXEC-SYSTEM/osdmain.elf", "x", 1); add("mc1:/BAEXEC-SYSTEM/osdmain.elf", lb, sizeof(lb)); bad |= run("first card wins", 0);
    fmcb("0"); bad |= run("FMCB Inner Browser AUTO, key across chunk boundary", 0);
    fmcb("1"); bad |= run("FMCB Inner Browser ON", 0);
    fmcb("2"); bad |= run("FMCB Inner Browser OFF", 1);
    if (!bad) printf("PASS: PS2 system browser adds SkipMc on LoadBOOTer cards and FMCB cards that drop the Browser request, read-only\n");
    return bad;
}
'''.replace("FUNC", func)

cc = shutil.which("cc") or shutil.which("gcc")
with tempfile.TemporaryDirectory() as d:
    c = Path(d) / "t.c"
    c.write_text(harness, encoding="utf-8")
    exe = Path(d) / "t"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-format-truncation", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
