"""Two wLaunchELF_R3Z 4.85 behaviours in the classic browser (saildot4k/wLaunchELF_R3Z#64):
"LaunchELF with Args" is offered only on text files, and the header shows free space only
when the device answered (a failed memory card or HDD query used to show a stale or bogus
size). The header now uses filerFreeSpace(), the checked helper the dashboard already uses."""
import shutil, subprocess, tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = (root / "src/filer_browser.c").read_text(encoding="utf-8")

assert "enable[LAUNCH_ELF_ARGS] = enable[OPEN_TEXTEDITOR] && IsTextEditorFileType(file->name);" in src
assert "enable[LAUNCH_ELF_ARGS] = enable[OPEN_TEXTEDITOR];" not in src
# Nothing later in filerMenuEnables may switch it back on (only the PC card CF clear).
menu = src[src.index("void filerMenuEnables("):]
menu = menu[:menu.index("\n}\n")]
assert [l.strip() for l in menu.splitlines() if "enable[LAUNCH_ELF_ARGS] =" in l] == [
    "enable[LAUNCH_ELF_ARGS] = enable[OPEN_TEXTEDITOR] && IsTextEditorFileType(file->name);",
    "enable[LAUNCH_ELF_ARGS] = FALSE;"]

a = src.index("static int browseFilePath(char *out, int cnfmode, FILEINFO *files)")
pos = src.index("{", a) + 1
depth = 1
while depth:
    depth += (src[pos] == "{") - (src[pos] == "}")
    pos += 1
browser = src[a:pos]
assert "vfreeSpace = filerFreeSpace(path, &freeSpace);" in browser
# The unchecked copies are gone: every query goes through the helper.
for unchecked in ("mcGetInfo(path[2] - '0', 0, &mctype_PSx, &mcfreeSpace", "PFSCTL_GET_ZONE_FREE", "vfreeSpace = TRUE;"):
    assert unchecked not in browser, unchecked

a = src.index("int filerFreeSpace(const char *path, u64 *out)")
helper = src[a:src.index("\n}\n", a)]
assert "if (ret < -1)" in helper  # mcGetInfo: 0 same card, -1 new card, below that an error
assert "if (mcGetInfo(path[2] - '0', 0, &type, &free_units, NULL) < 0)" in helper
hdd = helper[helper.index('if (!strncmp(path, "hdd", 3)'):helper.index("#ifdef DVRP")]
dvr = helper[helper.index('if (!strncmp(path, "dvr_hdd", 7)'):]
for branch in (hdd, dvr):
    assert "if (zone_free < 0 || zone_size <= 0)" in branch
# A PS1 card's free space is shown in its 15 blocks, as wLaunchELF_R3Z does,
# in the classic header (translated) and in the dashboard browser.
assert "return type == 1 ? 2 : 1;" in helper
assert "if (vfreeSpace == 2)\n\t\t\t\t\tsprintf(tmp, LNG(Dashboard_PS1_Blocks_Free), (int)(freeSpace / 8192), 15);" in browser
dash = (root / "src/dash_browser.c").read_text(encoding="utf-8")
assert 'if (fb.free_valid == 2) /* PS1 card: 15 blocks of 8 KB */\n\t\t\tsnprintf(space, sizeof(space), "%d/15 blocks", (int)(fb.free_bytes / 8192));' in dash
for lng in sorted((root / "Lang").glob("*.LNG")):
    line = [l for l in lng.read_text(encoding="utf-8").splitlines() if "PS1_Blocks_Free" in l]
    assert len(line) == 1 and line[0].startswith("lang(706, Dashboard_PS1_Blocks_Free, ") and line[0].count("%d") == 2 and line[0].count("%") == 2, lng
# A paste widens the fileXio transfer buffer only after its drivers are ready,
# and every exit (including a failed one) narrows it back to the SDK default.
init = (root / "src/init.c").read_text(encoding="utf-8")
widen = init[init.index("int setFileXioTransferWide(int wide)"):]
widen = widen[:widen.index("\n}\n")]
assert "{128 * 1024, 64 * 1024, 16 * 1024}" in widen and "for (i = wide ? 0 : 2; i < 3; i++)" in widen
assert widen.rstrip().endswith("return -1;")  # even 16 KB refused: fileXio has no buffer, the caller is told
paste = src[src.index("static void subfunc_Paste("):]
paste = paste[:paste.index("//endfunc subfunc_Paste")]
assert paste.count("setFileXioTransferWide(") == 2
assert paste.index("waitForClipboardSourceDevice()") < paste.index("widened = 1;") < paste.index("if (setFileXioTransferWide(1) < 0) {")
assert paste.count("widened = 1;") == 1  # a paste that never widened leaves the 16 KB buffer alone
assert "finished:\n\tno_buffer = widened && setFileXioTransferWide(0) < 0;" in paste  # a lost buffer is never reported as done...
assert paste.index("nclipFiles = kept;") < paste.index("if (no_buffer) {")  # ...but moved sources still leave the clipboard
fail = paste[paste.index("if (setFileXioTransferWide(1) < 0) {"):]
assert fail.index("ret = -ENOMEM;") < fail.index("goto finished;") < fail.index("for (i = 0; i < nclipFiles; i++)")
early = paste[:paste.index("if (setFileXioTransferWide(1) < 0) {")]
assert early.count("return;") == 1 and "goto finished" in early  # the one early return precedes the widening
# Run the real helper against a fileXio that refuses chosen sizes.
harness = r"""
#include <assert.h>
#include <string.h>
static int have_filexio_ready = 1, refuse_from, tried[8], ntried;
static int fileXioSetRWBufferSize(int n){tried[ntried++] = n;return n >= refuse_from ? -1 : 0;}
""" + widen + "\n}\n" + r"""
static void expect(int wide, int refuse, int ret, const int *sizes, int n){
 ntried = 0; refuse_from = refuse;
 assert(setFileXioTransferWide(wide) == ret && ntried == n && !memcmp(tried, sizes, n * sizeof(int)));
}
int main(void){
 const int all[] = {131072, 65536, 16384};
 expect(1, 1 << 30, 0, all, 1);      /* 128 KB granted */
 expect(1, 131072, 0, all, 2);       /* falls back to 64 KB */
 expect(1, 65536, 0, all, 3);        /* falls back to 16 KB */
 expect(1, 16384, -1, all, 3);       /* nothing left: the caller is told */
 expect(0, 1 << 30, 0, all + 2, 1);  /* narrowing asks only for 16 KB */
 expect(0, 16384, -1, all + 2, 1);
 have_filexio_ready = 0; expect(1, 0, 0, all, 0);  /* fileXio not up yet: nothing touched */
 return 0;
}
"""
cc = shutil.which("gcc") or shutil.which("cc")
if not cc: raise SystemExit("Host C compiler required")
with tempfile.TemporaryDirectory(prefix="filexio-wide-") as folder:
    c = Path(folder) / "t.c"; c.write_text(harness)
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Werror", str(c), "-o", str(Path(folder) / "t")], check=True)
    subprocess.run([str(Path(folder) / "t")], check=True, timeout=10)
print("ok")
