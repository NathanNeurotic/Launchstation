"""Execute the production memory-card folder icon stamp with a mocked card (#287, #301).

On a settings save the ELF writes the icon.sys + list/copy/del.icn the release
packages ship into the top-level mc0:/mc1: folder it runs from, so an
ELF-only install still shows a Launchstation icon in the browser.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
config = (root / "src/config.c").read_text(encoding="utf-8")
blobs = (root / "src/embed_blobs.S").read_text(encoding="utf-8")
icons = root / "res/memory-card"

# The ELF embeds one .icn for all three names: that is only right while the
# packaged files stay byte-identical.
listicn = (icons / "list.icn").read_bytes()
assert (icons / "copy.icn").read_bytes() == listicn, "copy.icn differs from list.icn; embed it separately"
assert (icons / "del.icn").read_bytes() == listicn, "del.icn differs from list.icn; embed it separately"
assert 'EMBED_FILE(mc_icon_sys, "res/memory-card/icon.sys")' in blobs
assert 'EMBED_FILE(mc_list_icn, "res/memory-card/list.icn")' in blobs
for f in ("src/dash_settings.c", "src/dash_store.c"):
    assert "configStampMcFolderIcon();" in (root / f).read_text(encoding="utf-8"), f


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef long long s64;
#define MAX_PATH 256
#define FIO_O_RDONLY 1
#define FIO_O_WRONLY 2
#define FIO_O_CREAT 0x200
#define FIO_O_TRUNC 0x400
#define SEEK_END 2
char LaunchElfDir[MAX_PATH];
unsigned char mc_icon_sys[964];
unsigned int size_mc_icon_sys = sizeof(mc_icon_sys);
unsigned char mc_list_icn[100];
unsigned int size_mc_list_icn = sizeof(mc_list_icn);
static int mc1_refused, room = 1 << 20, opens, open_error = -ENOENT, seek_fails;
#define NF 8
static struct { char path[MAX_PATH]; int size, used; } files[NF];
static int find(const char *p) { for (int i = 0; i < NF; i++) if (files[i].used && !strcmp(files[i].path, p)) return i; return -1; }
static int mc1AccessRefused(void) { return mc1_refused; }
static int genOpen(const char *p, int mode) {
    int i = find(p); opens++;
    if (!(mode & FIO_O_CREAT)) { if (i < 0) return open_error; return i; }
    if (i < 0) { for (i = 0; files[i].used; i++); files[i].used = 1; snprintf(files[i].path, MAX_PATH, "%s", p); }
    if (mode & FIO_O_TRUNC) files[i].size = 0;
    return i;
}
static s64 genLseek(int fd, s64 w, int how) { (void)w; (void)how; return seek_fails ? -EIO : files[fd].size; }
static int genWrite(int fd, void *b, int n) { (void)b; int w = n < room ? n : room; room -= w; files[fd].size += w; return w; }
static int genClose(int fd) { (void)fd; return 0; }
static void reset(const char *dir) { memset(files, 0, sizeof(files)); room = 1 << 20; opens = 0; mc1_refused = 0; open_error = -ENOENT; seek_fails = 0; snprintf(LaunchElfDir, MAX_PATH, "%s", dir); }
static int sz(const char *p) { int i = find(p); return i < 0 ? -1 : files[i].size; }
'''
code += function(config, "static int configWriteMcIconFile(")
code += function(config, "void configStampMcFolderIcon(void)")
code += r'''
int main(void) {
    /* an ELF-only top-level folder gets the full set */
    reset("mc0:/LAUNCHSTATION/");
    configStampMcFolderIcon();
    assert(sz("mc0:/LAUNCHSTATION/icon.sys") == 964);
    assert(sz("mc0:/LAUNCHSTATION/list.icn") == 100 && sz("mc0:/LAUNCHSTATION/copy.icn") == 100 && sz("mc0:/LAUNCHSTATION/del.icn") == 100);
    /* the next save only looks */
    opens = 0; configStampMcFolderIcon(); assert(opens == 1);

    /* someone's own complete icon is never replaced */
    reset("mc1:/APP_LAUNCHSTATION/");
    genOpen("mc1:/APP_LAUNCHSTATION/icon.sys", FIO_O_CREAT | FIO_O_WRONLY); files[0].size = 2000;
    configStampMcFolderIcon();
    assert(sz("mc1:/APP_LAUNCHSTATION/icon.sys") == 2000 && sz("mc1:/APP_LAUNCHSTATION/list.icn") == -1);

    /* a short icon.sys (an earlier stamp that ran out of room) is redone */
    reset("mc0:/LS/");
    genOpen("mc0:/LS/icon.sys", FIO_O_CREAT | FIO_O_WRONLY); files[0].size = 10;
    configStampMcFolderIcon();
    assert(sz("mc0:/LS/icon.sys") == 964);

    /* a full card: no icon.sys left behind, so the next save retries */
    reset("mc0:/LS/"); room = 150;
    configStampMcFolderIcon();
    assert(sz("mc0:/LS/icon.sys") == -1);
    room = 1 << 20; configStampMcFolderIcon();
    assert(sz("mc0:/LS/icon.sys") == 964 && sz("mc0:/LS/list.icn") == 100);

    /* an open refused for any reason but "no such file", or an unmeasurable
       icon.sys, may be somebody's icon: nothing is written */
    reset("mc0:/LS/"); open_error = -EACCES; configStampMcFolderIcon();
    assert(opens == 1 && sz("mc0:/LS/list.icn") == -1);
    reset("mc0:/LS/"); open_error = -EIO; configStampMcFolderIcon();
    assert(opens == 1 && sz("mc0:/LS/list.icn") == -1);
    reset("mc0:/LS/");
    genOpen("mc0:/LS/icon.sys", FIO_O_CREAT | FIO_O_WRONLY); files[0].size = 10; seek_fails = 1;
    configStampMcFolderIcon();
    assert(sz("mc0:/LS/icon.sys") == 10 && sz("mc0:/LS/list.icn") == -1);

    /* never the root, a nested folder, SYS-CONF, another device, or mc1 under MX4SIO */
    const char *skip[] = {"mc0:/", "mc0:/APPS/LAUNCHSTATION/", "mc0:/SYS-CONF/", "mc1:/SYS-CONF/",
                          "mmce0:/LS/", "mass:/LS/", "mc2:/LS/", "hdd0:/LS/", ""};
    for (unsigned k = 0; k < sizeof(skip) / sizeof(skip[0]); k++) {
        reset(skip[k]); configStampMcFolderIcon(); assert(opens == 0);
    }
    reset("mc1:/LS/"); mc1_refused = 1; configStampMcFolderIcon(); assert(opens == 0);
    puts("PASS: mc folder icon stamp");
    return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / "t.c"
    exe = Path(tmp) / "t"
    src.write_text(code, encoding="utf-8")
    cc = shutil.which("cc") or shutil.which("gcc")
    assert cc, "no C compiler (cc or gcc) found"
    subprocess.run([cc, "-std=c99", "-Wall", "-Werror", "-fsanitize=address,undefined", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("ok: memory-card folder icon stamp (#287, #301)")
