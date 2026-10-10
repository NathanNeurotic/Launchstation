"""Exercise production non-destructive local memory-card recovery safeguards."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_browser.c").read_text(encoding="utf-8")


def extract(signature):
    start = source.index(signature)
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos] + "\n"


picker = extract("static int recoveryTargetPath(")
action = extract("static void opRecoverCard(void)")

# UI actions must not expose destructive writes to a possibly emulated card.
assert "Recover as new .mcd..." in source
assert "F_RECOVER_NEW_MCD" in source
assert 'opRecoverCard();' in source
assert 'mcCardValidatePcsx2(source)' in action
assert 'mcCardValidateNoEcc(source)' in action
assert action.index("if (r != MC_CARD_EXPORT_OK)") < action.index("dashPickPath(")
assert "prepareTransferDeviceStacks(source, dest)" in action
assert "wleExists(dest) || wleExists(stage)" in action
assert "fileXioRename(stage, dest)" in action
assert action.index("mcCardBackupNoEcc(source, stage") < action.index("fileXioRename(stage, dest)")
assert "genRemove(clean)" in action
assert 'mcCardImportPcsx2(source, stage' in action

c = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int occupied;
static int wleExists(const char *name) {
    return (occupied == 1 && strstr(name,"RECOVER-001.mcd") != NULL) ||
           (occupied == 2 && strstr(name,"RECOVER-001.mcd.PART") != NULL) ||
           occupied == 3;
}
'''
c += picker
c += r'''
int main(void) {
    char dest[1024], stage[1024];
    occupied=0;
    assert(recoveryTargetPath("mass0:/restores",dest,sizeof(dest),stage,sizeof(stage)));
    assert(!strcmp(dest,"mass0:/restores/RECOVER-001.mcd"));
    assert(!strcmp(stage,"mass0:/restores/RECOVER-001.mcd.PART"));
    assert(recoveryTargetPath("usb0:/",dest,sizeof(dest),stage,sizeof(stage)));
    assert(!strcmp(dest,"usb0:/RECOVER-001.mcd"));
    assert(recoveryTargetPath("ata0:/",dest,sizeof(dest),stage,sizeof(stage)));
    assert(recoveryTargetPath("hdd0:__common:pfs:/",dest,sizeof(dest),stage,sizeof(stage)));
    for (int i=0;i<2;i++) {
        occupied=i+1;
        assert(recoveryTargetPath("mass0:/",dest,sizeof(dest),stage,sizeof(stage)));
        assert(!strcmp(dest,"mass0:/RECOVER-002.mcd"));
    }
    occupied=3;
    assert(!recoveryTargetPath("mass0:/",dest,sizeof(dest),stage,sizeof(stage)));
    occupied=0;
    assert(!recoveryTargetPath("mmce0:/MemoryCards/",dest,sizeof(dest),stage,sizeof(stage)));
    assert(!recoveryTargetPath("mc0:/",dest,sizeof(dest),stage,sizeof(stage)));
    assert(!recoveryTargetPath("udpfs:/",dest,sizeof(dest),stage,sizeof(stage)));
    assert(!recoveryTargetPath("cdfs:/",dest,sizeof(dest),stage,sizeof(stage)));
    assert(!recoveryTargetPath("",dest,sizeof(dest),stage,sizeof(stage)));
    assert(!recoveryTargetPath("mass0:/",dest,8,stage,sizeof(stage)));
    assert(!recoveryTargetPath("mass0:/",dest,sizeof(dest),stage,8));
    puts("PASS: recovery only creates distinct staged names on non-MMCE devices");
    return 0;
}
'''
compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler is required"
with tempfile.TemporaryDirectory(prefix="mmce-recovery-") as temp:
    main = Path(temp) / "recovery.c"
    exe = Path(temp) / "recovery"
    main.write_text(c, encoding="utf-8")
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                    str(main), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
