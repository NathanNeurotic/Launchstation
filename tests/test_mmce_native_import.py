"""Host-check the safe MMCE existing-firmware import holding-area workflow.

The adapter's shipped FS supports mkdir, open/write/read/close and rename.
This verifies the distinctive candidate generator and invariants; CI builds
both real PS2 executables separately. This does NOT validate real SD hardware.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_browser.c").read_text(encoding="utf-8")
upstream = (root / "docs/MMCE_SD2PSXTD_FIRMWARE_AUDIT.md").read_text(encoding="utf-8")

def extract(signature):
    at = source.index(signature)
    p = source.index("{", at) + 1
    depth = 1
    while depth:
        if p >= len(source):
            raise AssertionError("Unbalanced function: " + signature)
        if source[p] == "{":
            depth += 1
        if source[p] == "}":
            depth -= 1
        p += 1
    return source[at:p] + "\n"

candidate = extract("static int mmceImportCandidate(")
action = extract("static void opImportCardToMmce(void)")
assert '"mmce%d:/WLE-Recovered"' in candidate
assert '"%s/RECOVER-%03d.mcd"' in candidate
assert '"%s.PART"' in candidate
assert "MemoryCards" not in candidate
assert "mmceImportCandidate" in action
assert "DASH_PICK_FILE" not in action
assert "prepareTransferDeviceStacks(source, folder)" in action
assert "genMkdir(folder, fileMode)" in action
assert "mcCardImportPcsx2(source, stage" in action
assert "mcCardBackupNoEcc(source, stage" in action
assert "mmcePublishInactiveSnapshot(stage, dest)" in action
publisher = extract("static int mmcePublishInactiveSnapshot(")
assert "fileXioRename(stage, dest)" in publisher
assert "mcCardValidateNoEcc(dest)" in publisher
assert action.index("prepareTransferDeviceStacks(") < action.index("genMkdir(") < \
       action.index("mcCardBackupNoEcc(source, stage")
assert "mmce_cmd_set_card" not in action
assert "mmce_cmd_reset" not in action
assert "genRemove(" not in action
assert "MemoryCards/PS2" in action, "explicit separation from selected cards"
assert '"Store NEW snapshot on MMCE..."' in source
assert "opImportCardToMmce();" in source
assert "WLE-Recovered" not in (root / "iop/mmceman/src/mmce_fs.c").read_text()
assert "FS_RENAME" in upstream and "sd_rename" in upstream

c = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int occupied;
static int wleExists(const char *name) {
    if (occupied==3)return 1;
    if (occupied==1 && strstr(name,"RECOVER-001.mcd") &&
        !strstr(name,".PART"))return 1;
    if (occupied==2 && strstr(name,"RECOVER-001.mcd.PART"))return 1;
    return 0;
}
'''
c += candidate
c += r'''
int main(void) {
    char folder[128], dest[256], stage[256];
    occupied=0;
    assert(mmceImportCandidate(0,folder,sizeof(folder),dest,sizeof(dest),
                               stage,sizeof(stage)));
    assert(!strcmp(folder,"mmce0:/WLE-Recovered"));
    assert(!strcmp(dest,"mmce0:/WLE-Recovered/RECOVER-001.mcd"));
    assert(!strcmp(stage,"mmce0:/WLE-Recovered/RECOVER-001.mcd.PART"));
    assert(mmceImportCandidate(1,folder,sizeof(folder),dest,sizeof(dest),
                               stage,sizeof(stage)));
    assert(!strcmp(dest,"mmce1:/WLE-Recovered/RECOVER-001.mcd"));
    for(int i=1;i<=2;i++){
        occupied=i;
        assert(mmceImportCandidate(0,folder,sizeof(folder),dest,sizeof(dest),
                                   stage,sizeof(stage)));
        assert(!strcmp(dest,"mmce0:/WLE-Recovered/RECOVER-002.mcd"));
    }
    occupied=3;
    assert(!mmceImportCandidate(0,folder,sizeof(folder),dest,sizeof(dest),
                                stage,sizeof(stage)));
    occupied=0;
    assert(!mmceImportCandidate(2,folder,sizeof(folder),dest,sizeof(dest),
                                stage,sizeof(stage)));
    assert(!mmceImportCandidate(-1,folder,sizeof(folder),dest,sizeof(dest),
                                stage,sizeof(stage)));
    assert(!mmceImportCandidate(0,folder,8,dest,sizeof(dest),
                                stage,sizeof(stage)));
    assert(!mmceImportCandidate(0,folder,sizeof(folder),dest,8,
                                stage,sizeof(stage)));
    assert(!mmceImportCandidate(0,folder,sizeof(folder),dest,sizeof(dest),
                                stage,8));
    puts("PASS: MMCE import stages into separate holding area, never replacing card files");
    return 0;
}
'''
cc = shutil.which("gcc") or shutil.which("clang")
assert cc, "Host C compiler is required"
with tempfile.TemporaryDirectory(prefix="mmce-native-import-") as td:
    src = Path(td) / "import.c"
    exe = Path(td) / "import"
    src.write_text(c, encoding="utf-8")
    subprocess.run([cc,"-std=c99","-Wall","-Wextra","-Werror",str(src),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
