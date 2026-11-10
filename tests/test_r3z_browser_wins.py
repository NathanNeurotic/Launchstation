"""Two wLaunchELF_R3Z 4.85 behaviours in the classic browser (saildot4k/wLaunchELF_R3Z#64):
"LaunchELF with Args" is offered only on text files, and the header shows free space only
when the device answered (a failed memory card or HDD query used to show a stale or bogus
size). The header now uses filerFreeSpace(), the checked helper the dashboard already uses."""
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
# A paste widens the fileXio transfer buffer only after its drivers are ready,
# and every exit (including a failed one) narrows it back to the SDK default.
init = (root / "src/init.c").read_text(encoding="utf-8")
widen = init[init.index("void setFileXioTransferWide(int wide)"):]
widen = widen[:widen.index("\n}\n")]
assert "{128 * 1024, 64 * 1024, 16 * 1024}" in widen and "for (i = wide ? 0 : 2; i < 3; i++)" in widen
paste = src[src.index("static void subfunc_Paste("):]
paste = paste[:paste.index("//endfunc subfunc_Paste")]
assert paste.count("setFileXioTransferWide(") == 2
assert paste.index("waitForClipboardSourceDevice()") < paste.index("setFileXioTransferWide(1);")
assert "finished:\n\tsetFileXioTransferWide(0);" in paste
early = paste[:paste.index("setFileXioTransferWide(1);")]
assert early.count("return;") == 1 and "goto finished" in early  # the one early return precedes the widening
print("ok")
