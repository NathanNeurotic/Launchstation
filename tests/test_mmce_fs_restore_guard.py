"""Execute the production MMCE firmware-path guards on a host compiler.

Only defensive checks are introduced. Direct MMCE restore stays unavailable:
GET_STATUS / GET_CARD alone do not prove an arbitrary image is inactive.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
header = root / "iop/mmceman/include/mmce_restore_guard.h"
source = (root / "iop/mmceman/src/mmce_fs.c").read_text(encoding="utf-8")
workflow = (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")
assert header.exists()
assert '#include "mmce_restore_guard.h"' in source
assert "MMCE_FS_ACTIVE_NAME" not in source.split("#define MMCE_FS_ACTIVE_HANDLE")[1].split("#define NOT_SUPPORTED_OP")[0], \
    "do not define the active virtual name separately from the guard"

mutations = {
    "int mmce_fs_open(": "if (!mmce_fs_wire_name_ok(name))",
    "int mmce_fs_remove(": "if (mmce_fs_reserved_component(name))",
    "int mmce_fs_mkdir(": "if (mmce_fs_reserved_component(name))",
    "int mmce_fs_rmdir(": "if (mmce_fs_reserved_component(name))",
    "int\tmmce_fs_rename(": "if (mmce_fs_reserved_component(old_name) ||",
}
for start, expected in mutations.items():
    body = source[source.index(start):]
    assert expected in body[:1700], start
    assert body.index(expected) < body.index("mmce_fs_update_unit(file->unit)"), \
        "guard must run before firmware transactions: " + start
assert "if ((flags & 3) != FIO_O_RDONLY || (flags & 0xF00) != 0)" in source
assert "rdbuf[0x1] < MMCE_FS_MAX_FD" in source
assert "mmce_fs_wire_name_ok(old_name) || !mmce_fs_wire_name_ok(new_name)" in source
assert "mmce_cmd_set_card" in source, "must not remove normal card selection"
assert "python3 tests/test_mmce_fs_restore_guard.py" in workflow

harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mmce_restore_guard.h"

static void length_cases(void) {
    char name[258];
    size_t i;
    memset(name,'a',sizeof(name));
    name[254]=0;
    assert(mmce_fs_wire_name_ok(name));
    name[254]='a';name[255]=0;
    assert(!mmce_fs_wire_name_ok(name));
    name[0]=0;
    assert(!mmce_fs_wire_name_ok(name));
    assert(!mmce_fs_wire_name_ok(NULL));
    for (i=0;i<254;i++)name[i]='a';
    name[254]=0;
    assert(mmce_fs_wire_name_ok(name));
}
int main(void) {
    length_cases();
    assert(mmce_fs_reserved_component("__active_card__.mcd"));
    assert(mmce_fs_reserved_component("/__active_card__.mcd"));
    assert(mmce_fs_reserved_component("//__active_card__.mcd"));
    assert(mmce_fs_reserved_component("./__active_card__.mcd"));
    assert(mmce_fs_reserved_component("/MemoryCards/__active_card__.mcd"));
    assert(mmce_fs_reserved_component("foo\\__active_card__.mcd"));
    assert(!mmce_fs_reserved_component(NULL));
    assert(!mmce_fs_reserved_component("/MemoryCards"));
    assert(!mmce_fs_reserved_component("__active_card__.mcd.bak"));
    assert(!mmce_fs_reserved_component("my__active_card__.mcd"));
    assert(!mmce_fs_reserved_component("/MemoryCards/Card-01.mcd"));
    assert(!mmce_fs_reserved_component("/MemoryCards/__active_card__.mcd2"));
    puts("PASS: firmware path lengths and virtual active-card aliases fail closed");
    return 0;
}
'''
cc = shutil.which("gcc") or shutil.which("clang")
assert cc, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="mmce-restore-guard-") as td:
    src = Path(td) / "guard.c"
    exe = Path(td) / "guard"
    src.write_text(harness, encoding="utf-8")
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror",
                    "-I", str(header.parent), str(src), "-o", str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)
