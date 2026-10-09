"""Exercise packaged payloads and the actual updater parser with hostile archives."""
import importlib.util
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("packages", root / "scripts/package_apps.py")
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)
compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="app-packages-") as tmp:
    tmp = Path(tmp)
    elf = bytearray(6001)
    elf[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<H", elf, 18, 8)
    elf[52:] = bytes(i % 251 for i in range(len(elf) - 52))
    (tmp / "elf").write_bytes(elf)
    p.package(tmp / "elf", tmp / "elf", tmp / "out", "a" * 40, 1791500000)
    assert len(list((tmp / "out").iterdir())) == 4
    (tmp / "launchelf.h").write_text('''
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
#define MAX_PATH 1024
#define FIO_O_RDONLY O_RDONLY
#define FIO_O_WRONLY O_WRONLY
#define FIO_O_CREAT O_CREAT
#define FIO_O_TRUNC O_TRUNC
#define genOpen(p,f) open(p,(f)|O_BINARY,0600)
#define genClose close
#define genLseek lseek
#define genRemove unlink
/* Deliberately short transfers exercise both retry loops. */
#define genRead(fd,p,n) read(fd,p,(n)>17?17:(n))
#define genWrite(fd,p,n) write(fd,p,(n)>13?13:(n))
''')
    (tmp / "main.c").write_text('''
#include "launchelf.h"
#include "self_update.h"
int main(int argc,char **argv) {
    int fd=open(argv[1],O_RDONLY), size=lseek(fd,0,SEEK_END); close(fd);
    printf("%d",selfUpdateExtractPsu(argv[1],argv[2],size)); return 0;
}
''')
    for index, (folder, name, _) in enumerate(p.VARIANTS):
        archive = (tmp / "out" / (folder + ".psu")).read_bytes()
        with zipfile.ZipFile(tmp / "out" / (folder + ".zip")) as z:
            assert set(z.namelist()) == {folder + "/" + name, folder + "/title.cfg"}
            assert z.read(folder + "/" + name) == elf
            assert ("boot=" + name + "\n").encode() in z.read(folder + "/title.cfg")
        binary = tmp / ("parser" + str(index) + ".exe")
        subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-I" + str(tmp), "-I" + str(root / "include"),
                        *(["-DDS34"] if index else []), str(root / "src/update_psu.c"),
                        str(tmp / "main.c"), "-o", str(binary)], check=True)
        def check(data, good=False):
            path, stage = tmp / "candidate.psu", tmp / "stage.elf"
            path.write_bytes(data)
            result = int(subprocess.check_output([str(binary), str(path), str(stage)]))
            if good:
                assert result == len(elf) and stage.read_bytes() == elf
            else:
                assert result == -1 and not stage.exists()
        check(archive, True)
        check(archive[:-1])
        check(archive + b"trailing")
        bad = bytearray(archive); struct.pack_into("<I", bad, 4, 5); check(bad)
        bad = bytearray(archive); bad[64:64+448] = b"x" * 448; check(bad)
        bad = bytearray(archive); struct.pack_into("<I", bad, 1540, 0xffffffff); check(bad)
        bad = bytearray(archive); bad[1600:1600+448] = b"x" * 448; check(bad)
        check((tmp / "out" / (p.VARIANTS[1-index][0] + ".psu")).read_bytes())
        date = bytes(8)
        check(p.make_psu(folder, {"title.cfg": b"boot=x\n", name: bytes(elf)}, date), True)
        check(p.make_psu(folder, {name: bytes(elf), "wrong.cfg": b"x"}, date))
        # Duplicate ELF records, which a dictionary-based producer cannot represent.
        check(archive[:512] + archive[512:1536] + archive[1536:8192] * 2)
print("App package payloads and both native PSU parsers passed")
