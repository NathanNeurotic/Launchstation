"""Host-compile the production new-card allocation policy, with fault injection.

No existing CardN folder may be reused: even an apparently empty directory
could belong to a selected card, another channel, or an interrupted import.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_browser.c").read_text(encoding="utf-8")
workflow = (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")


def extract(signature, text=None):
    source_text = source if text is None else text
    at = source_text.index(signature)
    p = source_text.index("{", at) + 1
    level = 1
    while level:
        if p >= len(source_text):
            raise AssertionError("Unclosed C function " + signature)
        level += (source_text[p] == "{") - (source_text[p] == "}")
        p += 1
    return source_text[at:p] + "\n"


allocator = extract("static int mmceSelectableCandidate(")
installer = extract("static void opInstallMmceCard(int game_card)")
namer = extract("static int gameIdFromImageName(")
assert '"mmce%d:/MemoryCards/PS2/Card%d"' in allocator
assert '"%s/Card%d-1.mcd"' in allocator
assert '"%s.PART"' in allocator
assert "genDopen(folder)" in allocator
assert "genDclose(fd);" in allocator
assert "number <= 255" in allocator
assert "mmceSelectableCandidate(unit, folder" in installer
assert "genMkdir(folder, fileMode)" in installer
assert "genDopen(parent)" in installer
assert "mcCardImportPcsx2NewFile(source, stage" in installer
assert "mcCardBackupNoEccNewFile(source, stage" in installer
assert "mmcePublishInactiveSnapshot(stage, dest)" in installer
assert "mcCardValidatePcsx2(source)" in installer
assert "mcCardValidateNoEcc(source)" in installer
assert "prepareTransferDeviceStacks(source, parent)" in installer
assert "mmce0:/WLE-Recovered/" in installer
assert "mmce1:/WLE-Recovered/" in installer
# Resized copies and new blank cards are staged outside MemoryCards for this action.
for held in ("WLE-Resized/", "WLE-New/", "WLE-GameID/"):
    for unit in (0, 1):
        assert f'strncmp(source, "mmce{unit}:/{held}", {len(f"mmce{unit}:/{held}")})' in installer, held
assert '"mmce%d:/WLE-Resized/"' in (root / "src/dash_card_tool.c").read_text(encoding="utf-8")
assert '"mmce%d:/WLE-New/"' in (root / "src/dash_card_tool.c").read_text(encoding="utf-8")
assert '"mmce%d:/WLE-GameID/"' in (root / "src/dash_card_tool.c").read_text(encoding="utf-8")
# Game ID cards: the whole <ID> folder must be absent, then the same verified publish.
assert 'snprintf(folder, sizeof(folder), "%s/%s", parent, id)' in installer
assert '"%s/%s-1.mcd", folder, id' in installer
at = installer.index('snprintf(folder, sizeof(folder), "%s/%s", parent, id)')
assert installer.index("genDopen(folder)", at) < installer.index("genMkdir(folder, fileMode)", at)
assert "This game already has a card" in installer
assert '"Install as MMCE Game ID card..."' in source and "opInstallMmceCard(1);" in source
assert "mmce_cmd_set_card" not in installer
assert "mmce_cmd_set_channel" not in installer
assert "mmce_cmd_reset" not in installer
assert "genRemove(" not in installer
assert "F_INSTALL_NEW_MMCE_CARD" in source
assert '"Install as NEW selectable MMCE card..."' in source
assert "opInstallMmceCard(0);" in source
assert "python3 tests/test_mmce_selectable_install.py" in workflow

harness = r"""
#include <stdio.h>
#include <string.h>
#include <assert.h>
static int first_free = 1, all_exists, exists_probe, opened, closed;
static int genDopen(const char *name) {
    int n = -1;
    if (sscanf(name, "mmce%*d:/MemoryCards/PS2/Card%d", &n) != 1)
        return -1;
    opened++;
    return all_exists || n < first_free ? 1 : -1;
}
static void genDclose(int fd) { assert(fd == 1); closed++; }
static int wleExists(const char *name) {
    assert(strstr(name, "Card") != NULL);
    return exists_probe && strstr(name, "Card1") != NULL;
}
"""
harness += allocator + r"""
int main(void) {
    char folder[128], dest[256], stage[256];
    int r;
    r = mmceSelectableCandidate(0, folder, sizeof(folder), dest, sizeof(dest),
                                stage, sizeof(stage));
    assert(r == 1);
    assert(!strcmp(folder, "mmce0:/MemoryCards/PS2/Card1"));
    assert(!strcmp(dest, "mmce0:/MemoryCards/PS2/Card1/Card1-1.mcd"));
    assert(!strcmp(stage, "mmce0:/MemoryCards/PS2/Card1/Card1-1.mcd.PART"));
    first_free = 2;
    r = mmceSelectableCandidate(1, folder, sizeof(folder), dest, sizeof(dest),
                                stage, sizeof(stage));
    assert(r == 2);
    assert(opened > 0 && closed > 0);
    first_free = 1;
    exists_probe = 1;
    assert(mmceSelectableCandidate(0, folder, sizeof(folder), dest, sizeof(dest),
                                   stage, sizeof(stage)) == 2);
    exists_probe = 0;
    all_exists = 1;
    assert(mmceSelectableCandidate(0, folder, sizeof(folder), dest, sizeof(dest),
                                   stage, sizeof(stage)) == 0);
    all_exists = 0;
    first_free = 255;
    assert(mmceSelectableCandidate(0, folder, sizeof(folder), dest, sizeof(dest),
                                   stage, sizeof(stage)) == 255);
    first_free = 256;
    assert(mmceSelectableCandidate(0, folder, sizeof(folder), dest, sizeof(dest),
                                   stage, sizeof(stage)) == 0);
    assert(!mmceSelectableCandidate(-1, folder, sizeof(folder), dest,
                                     sizeof(dest), stage, sizeof(stage)));
    assert(!mmceSelectableCandidate(2, folder, sizeof(folder), dest,
                                     sizeof(dest), stage, sizeof(stage)));
    assert(!mmceSelectableCandidate(0, folder, 5, dest,
                                     sizeof(dest), stage, sizeof(stage)));
    assert(!mmceSelectableCandidate(0, folder, sizeof(folder), dest,
                                     5, stage, sizeof(stage)));
    assert(!mmceSelectableCandidate(0, folder, sizeof(folder), dest,
                                     sizeof(dest), stage, 5));
    assert(!mmceSelectableCandidate(0, NULL, sizeof(folder), dest,
                                     sizeof(dest), stage, sizeof(stage)));
    puts("PASS: new MMCE selectable cards never reuse an existing numbered directory");
}
"""
namer_test = r"""
#include <stdio.h>
#include <string.h>
#include <assert.h>
""" + extract("int mcGameIdValid(const char *id)", (root / "src/mc_card_write.c").read_text(encoding="utf-8")) + namer + r"""
int main(void) {
    char id[11];
    assert(gameIdFromImageName("mmce0:/WLE-GameID/SLUS-21050/SLUS-21050-1.mcd", id) && !strcmp(id, "SLUS-21050"));
    assert(gameIdFromImageName("mass:/SCES-50000.ps2", id) && !strcmp(id, "SCES-50000"));
    assert(!gameIdFromImageName("mass:/Card1-1.mcd", id));
    assert(!gameIdFromImageName("mass:/SLUS-2105-1.mcd", id));
    assert(!gameIdFromImageName("mass:/slus-21050-1.mcd", id));
    assert(!gameIdFromImageName("mass:/SLUS_21050-1.mcd", id));
    assert(!gameIdFromImageName("mass:/SLUS-21050X.mcd", id));
    assert(!gameIdFromImageName("SLUS-2105", id));
    assert(!gameIdFromImageName("mass:/SLUS-21050-2.mcd", id));      /* channel 2 is not the -1 card */
    assert(!gameIdFromImageName("mass:/SLUS-21050-backup.mcd", id));
    assert(!gameIdFromImageName("mass:/SLUS-21050-1", id));
    puts("PASS: Game ID card names");
}
"""
cc = shutil.which("gcc") or shutil.which("clang")
assert cc, "C host compiler required"
with tempfile.TemporaryDirectory(prefix="mmce-selectable-") as tmp:
    c = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    c.write_text(harness, encoding="utf-8")
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror",
                    str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    c.write_text(namer_test, encoding="utf-8")
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
