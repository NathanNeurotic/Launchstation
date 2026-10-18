"""Behavioral R3Z #61 ISO/SYSTEM.CNF and MMCE safety regression.

Compiles the actual neutrino_gameid.c against POSIX file/Devctl stubs, then
exercises valid/malformed ISO9660 images and MMCE slot exclusion policies.
"""
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
GAMEID = (ROOT / "src/neutrino_gameid.c").read_text(encoding="utf-8")
NEUTRINO = (ROOT / "src/neutrino_iso.c").read_text(encoding="utf-8")
BUILD = (ROOT / "ps2.yaml").read_text(encoding="utf-8")
MAKE = (ROOT / "Makefile").read_text(encoding="utf-8")

assert "neutrino_gameid.o" in MAKE
assert "src/neutrino_gameid.c" in BUILD
assert "loadMmceModules(" not in GAMEID, "ID lookup must never reboot/replace storage"
assert "load_udpfs(" not in GAMEID
assert "showNeutrinoLaunchOptions(&options)" in NEUTRINO
assert "prepareNeutrinoGameID(opened_iso" in NEUTRINO
assert "displayRetroGemGameID(gameID, 2);" in NEUTRINO
assert "setNeutrinoMmceGameID(gameID, opened_iso, selected);" in NEUTRINO
assert NEUTRINO.index("prepareNeutrinoGameID(opened_iso") < NEUTRINO.index("preparePopstarterCandidateLaunch(&candidates")
assert NEUTRINO.index("setNeutrinoMmceGameID(gameID") < NEUTRINO.index("CleanUpForKeepIopExec(")
assert NEUTRINO.index("displayRetroGemGameID(gameID, 2);") > NEUTRINO.index("CleanUpForKeepIopExec(")
assert '"mmce:"' in GAMEID and '"mc:"' in GAMEID

def iso_fixture(path, cnf: bytes, valid=True):
    sectors = bytearray(23 * 2048)
    pvd = 16 * 2048
    sectors[pvd] = 1
    sectors[pvd+1:pvd+6] = b"CD001" if valid else b"BROKN"
    sectors[pvd+6] = 1
    sectors[pvd+156] = 34
    sectors[pvd+158:pvd+162] = struct.pack("<I", 20)
    sectors[pvd+166:pvd+170] = struct.pack("<I", 2048)
    sectors[pvd+181] = 2

    # Root directory has a real ISO9660 SYSTEM.CNF;1 extent at LBA 21.
    root = 20 * 2048
    sectors[root] = 34
    sectors[root+25] = 2
    sectors[root+32] = 1
    sectors[root+33] = 0
    file = root + 34
    sectors[file] = 46
    sectors[file+2:file+6] = struct.pack("<I", 21)
    sectors[file+10:file+14] = struct.pack("<I", len(cnf))
    sectors[file+32] = 12
    sectors[file+33:file+45] = b"SYSTEM.CNF;1"
    sectors[21*2048:21*2048+len(cnf)] = cnf
    path.write_bytes(sectors)

prefix = r"""
#define MMCE 1
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
typedef unsigned int u32;
typedef long long s64;
static FILE *iso;
static int stack_present = 0;
static int calls = 0, pings = 0, delays = 0;
static int wle_ascii_tolower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a'-'A') : c; }
int openPathForRead(const char *path, char *out, size_t out_size) {
    (void)out; (void)out_size;
    iso = fopen(path, "rb");
    return iso ? 1 : -1;
}
s64 genLseek(int fd, s64 where, int how) {
    (void)fd;
    if (fseek(iso, (long)where, how)) return -1;
    return ftell(iso);
}
int genRead(int fd, void *buffer, int count) {
    (void)fd;
    return (int)fread(buffer, 1, count, iso);
}
void genClose(int fd) { (void)fd; fclose(iso); iso = NULL; }
int isPathDeviceStackLoaded(const char *path) {
    assert(!strcmp(path, "mmce0:/"));
    return stack_present;
}
int fileXioDevctl(const char *device, int cmd, void *arg, int arglen, void *out, int outlen) {
    (void)device; (void)arg; (void)arglen; (void)out; (void)outlen;
    if (cmd == 1) pings++;
    if (cmd == 8) calls++;
    return 0;
}
void DelayThread(int usec) { (void)usec; delays++; }
#define DPRINTF(...) ((void)0)
"""
code = (
    GAMEID.replace('#include "launchelf.h"', "")
          .replace('#include "popstarter_internal.h"', "")
          .replace('#include "neutrino_gameid.h"', "")
)
main = r"""
int main(int argc, char **argv) {
    char id[12];
    assert(argc == 4);
    assert(prepareNeutrinoGameID(argv[1], id, sizeof(id)) == 1);
    assert(strcmp(id, "SLUS_123.45") == 0);
    assert(prepareNeutrinoGameID(argv[2], id, sizeof(id)) == 0);
    assert(id[0] == '\0');
    assert(prepareNeutrinoGameID(argv[3], id, sizeof(id)) == 0);
    assert(id[0] == '\0');
    assert(prepareNeutrinoGameID(argv[1], id, sizeof(id)-1) == 0);
    assert(prepareNeutrinoGameID(argv[1], NULL, 0) == 0);

    setNeutrinoMmceGameID("SLUS_123.45", "usb0:/GAME.iso", "mass0:/NEUTRINO.ELF");
    assert(calls == 0 && pings == 0);
    stack_present = 1;
    setNeutrinoMmceGameID("SLUS_123.45", "mx4sio0:/GAME.iso", "mc0:/N.ELF");
    assert(calls == 0);
    setNeutrinoMmceGameID("SLUS_123.45", "usb0:/GAME.iso", "mc:/N.ELF");
    assert(calls == 0);
    setNeutrinoMmceGameID("SLUS_123.45", "mmce0:/GAME.iso", "mass0:/NEUTRINO.ELF");
    assert(calls == 1);
    setNeutrinoMmceGameID("SLUS_123.45", "usb0:/GAME.iso", "mc1:/NEUTRINO.ELF");
    assert(calls == 2);
    setNeutrinoMmceGameID("bad", "usb0:/GAME.iso", "mass0:/NEUTRINO.ELF");
    assert(calls == 2);
    assert(delays == 0);
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="launchstation-r3z-gameid-") as td:
    temp = Path(td)
    valid = temp / "game.iso"
    malformed = temp / "malformed.iso"
    missing = temp / "missing.iso"
    iso_fixture(valid, b"BOOT2 = cdrom0:\\SLUS_123.45;1\r\nVER = 1.00\r\n")
    iso_fixture(malformed, b"BOOT2 = cdrom0:\\SLUS_123.45;1\r\n", valid=False)
    iso_fixture(missing, b"BOOT2 = cdrom0:\\NOT_A_GAME;1\r\n")
    (temp / "test.c").write_text(prefix + code + main, encoding="utf-8")
    binary = temp / "neutrino_gameid_test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "include"), str(temp / "test.c"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary), str(valid), str(malformed), str(missing)], check=True)

print("R3Z Neutrino GameID: valid/malformed ISO, MMCE protections passed")
