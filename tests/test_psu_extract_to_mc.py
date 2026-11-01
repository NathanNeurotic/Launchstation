"""A .psu in the File Browser extracts straight onto mc0:/ or mc1:/ from its own menu (A5, Berion),
through the classic PSU restore, leaving the user's clipboard as it was."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
src = (root / "src/dash_browser.c").read_text(encoding="utf-8")

# the rows: one per port, on .psu targets only, never inside a card, dimmed with a reason
rows = src[src.index("for (port = 0; npsu > 0 && port < 2; port++) {"):]
rows = rows[:rows.index("}\n\t\t}\n")]
assert 'strncmp(fb.dir, "mc", 2) && strncmp(fb.dir, "vmc", 3)' in src[src.index("int npsu = psuTargets") - 200:src.index("int npsu = psuTargets")]
assert "filerRootMcPresent(port, &type, NULL)" in rows and "type == 2" in rows
assert 'labels[] = {"Extract to mc0:/", "Extract to mc1:/"}' in rows
assert '"not a PS2 card" : "no card"' in rows and "!ok, port == 0 ? F_PSU_MC0 : F_PSU_MC1" in rows
assert "case F_PSU_MC0:\n\t\t\topExtractPsuTo(0);" in src and "case F_PSU_MC1:\n\t\t\topExtractPsuTo(1);" in src

# only .psu files qualify, folders and other files never do
helper = src[src.index("static int psuTargets("):src.index("static void opExtractPsuTo(")]
assert 'isDir(&fb.files[idx[i]]) || !genCmpFileExt(fb.files[idx[i]].name, "PSU")' in helper and "return 0;" in helper

op = src[src.index("static void opExtractPsuTo("):src.index("static void opDelete(")]
# refuses without a PS2 card, waits for the worker, honours the exploit-folder guard on the card
assert 'snprintf(dest, sizeof(dest), "mc%d:/", port);' in op
assert "filerRootMcPresent(port, &type, NULL) <= 0 || type != 2" in op
assert op.index("if (!prepareOp())") < op.index("filerConfirmExploitModify(dest, NULL)") < op.index("filerPsuPasteInto(fb.msg, dest);")
# the clipboard is borrowed: saved before, restored after, generation and cut flag included
for name in ("saved_n = nclipFiles", "saved_cut = browser_cut", "saved_gen = clipIopResetGeneration"):
    assert name in op, name
assert op.index("memcpy(saved, clipFiles") < op.index("filerPsuPasteInto") < op.index("memcpy(clipFiles, saved")
assert "clipIopResetGeneration = saved_gen;" in op and "browser_cut = saved_cut;" in op and "free(saved);" in op
assert "browser_cut = 0;" in op[:op.index("filerPsuPasteInto")]          # a restore is a copy, never a move
assert op.index("opDone();") < op.index("showPasteFailure(1)")
assert '"The clipboard was cleared"' in op                               # the one case it cannot be put back
# menu capacity grew with the two new rows
assert re.search(r"DashMenuItem items\[(\d+)\];\n\tint ids\[\1\],", src).group(1) == "56"
assert "Extract to mc0:/" in (root / "docs-site/content/file-browser.html").read_text(encoding="utf-8")
print("PASS: .psu files extract straight to mc0:/ or mc1:/ from their menu, clipboard preserved")
