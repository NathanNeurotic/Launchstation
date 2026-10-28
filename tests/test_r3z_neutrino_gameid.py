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
MAKE = (ROOT / "ps2.yaml").read_text(encoding="utf-8")

assert "- src/neutrino_gameid.c" in MAKE
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
#include "neutrino_gameid.h"
typedef unsigned int u32;
typedef long long s64;
static FILE *iso;
static int stack_present = 0;
static int control_success = 0;
static int calls = 0, pings = 0, delays = 0, polls = 0;
static int settle_pending;
static int ping_error, set_error, status_result, read_limit, read_error, seek_error;
static int wle_ascii_tolower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a'-'A') : c; }
int openPathForRead(const char *path, char *out, size_t out_size) {
    (void)out; (void)out_size;
    iso = fopen(path, "rb");
    return iso ? 1 : -1;
}
s64 genLseek(int fd, s64 where, int how) {
    (void)fd;
    if (seek_error) return -1;
    if (fseek(iso, (long)where, how)) return -1;
    return ftell(iso);
}
int genRead(int fd, void *buffer, int count) {
    (void)fd;
    if (read_error) return 0;
    if (read_limit && count > read_limit) count = read_limit;
    return (int)fread(buffer, 1, count, iso);
}
void genClose(int fd) { (void)fd; fclose(iso); iso = NULL; }
int ensureMmceGameIdControl(void) { return control_success; }
/* Retain this legacy predicate so reintroducing the old caller guard is tested. */
int isPathDeviceStackLoaded(const char *path) { (void)path; return stack_present; }
int fileXioDevctl(const char *device, int cmd, void *arg, int arglen, void *out, int outlen) {
    (void)device; (void)arg; (void)arglen; (void)out; (void)outlen;
    if (cmd == 1) { pings++; return ping_error ? -1 : 0; }
    if (cmd == 8) {
        assert(arglen == NEUTRINO_GAME_ID_SIZE && !strcmp(arg, "SLUS_123.45"));
        calls++; settle_pending = !set_error; return set_error ? -1 : 0;
    }
    assert(cmd == 2); assert(!settle_pending); polls++; return status_result;
}
void DelayThread(int usec) { assert(usec == 1000000); settle_pending = 0; delays++; }
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
    assert(argc >= 4);
    assert(prepareNeutrinoGameID(argv[1], id, sizeof(id)) == 1);
    assert(strcmp(id, "SLUS_123.45") == 0);
    assert(iso == NULL);
    for (int i=2; i<argc; i++) {
        strcpy(id, "SLUS_123.45");
        assert(prepareNeutrinoGameID(argv[i], id, sizeof(id)) == 0);
        assert(id[0] == '\0' && iso == NULL);
    }
    read_limit = 11; /* Complete fragmented reads, not just sector-sized reads. */
    assert(prepareNeutrinoGameID(argv[1], id, sizeof(id)) == 1 && iso == NULL);
    read_error = 1;
    assert(!prepareNeutrinoGameID(argv[1], id, sizeof(id)) && !id[0] && iso == NULL);
    read_error = 0; seek_error = 1;
    assert(!prepareNeutrinoGameID(argv[1], id, sizeof(id)) && !id[0] && iso == NULL);
    seek_error = read_limit = 0;
    assert(prepareNeutrinoGameID(argv[1], id, sizeof(id)-1) == 0);
    assert(prepareNeutrinoGameID(argv[1], NULL, 0) == 0);

    setNeutrinoMmceGameID("SLUS_123.45", "usb0:/GAME.iso", "mass0:/NEUTRINO.ELF");
    assert(calls == 0 && pings == 0);
    control_success = 1;
    assert(!stack_present);
    setNeutrinoMmceGameID("SLUS_123.45", "usb0:/GAME.iso", "mass0:/NEUTRINO.ELF");
    assert(calls == 2 && pings == 2 && polls == 2 && !stack_present);
    calls = pings = polls = 0;
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
    /* One settle delay before each of the four accepted cards' first poll. */
    assert(delays == 4);
    /* Both source cards are protected, including Neutrino on a real MC alias. */
    pings = calls = polls = delays = 0;
    setNeutrinoMmceGameID("SLUS_123.45", "mmce0:/GAME.iso", "mc1:/N.ELF");
    assert(!pings && !calls && !polls);
    setNeutrinoMmceGameID("SLUS_123.45", "mmce1:/GAME.iso", "mmce0:/N.ELF");
    assert(!pings && !calls && !polls);
    /* No status waits when absent or when SET_GAMEID was rejected. */
    ping_error = 1;
    setNeutrinoMmceGameID("SLUS_123.45", "mmce0:/GAME.iso", "mass0:/N.ELF");
    assert(pings == 1 && !calls && !polls && !delays);
    ping_error = 0; set_error = 1;
    setNeutrinoMmceGameID("SLUS_123.45", "mmce0:/GAME.iso", "mass0:/N.ELF");
    assert(calls == 1 && !polls && !delays);
    set_error = 0; status_result = -1;
    setNeutrinoMmceGameID("SLUS_123.45", "mmce0:/GAME.iso", "mass0:/N.ELF");
    assert(polls == 1 && delays == 1);
    status_result = 1; polls = delays = 0;
    setNeutrinoMmceGameID("SLUS_123.45", "mmce0:/GAME.iso", "mass0:/N.ELF");
    assert(polls == 15 && delays == 15); /* Genuine busy card has a finite bound. */
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
    invalid = [malformed, missing]
    original = valid.read_bytes()
    # Mutate on-disk lengths/extents, not parser implementation details.
    pvd, record = 16 * 2048, 20 * 2048 + 34
    mutations = [
        (pvd + 158, struct.pack("<I", 0xffffffff)),  # root outside image
        (pvd + 166, struct.pack("<I", 0xffffffff)),  # root size exceeds image
        (record, b"\x21"),                         # short directory record
        (record + 32, b"\xff"),                    # name exceeds record
        (record + 2, struct.pack("<I", 0xffffffff)), # CNF outside image
        (record + 10, struct.pack("<I", 4097)),      # oversized CNF
        (record + 10, struct.pack("<I", 0)),         # empty CNF
        (record + 25, b"\x80"),                    # unsupported multi-extent CNF
        (record + 26, b"\x01"),                    # unsupported interleave
    ]
    for i, (offset, value) in enumerate(mutations):
        image = bytearray(original)
        image[offset:offset + len(value)] = value
        path = temp / f"invalid-{i}.iso"
        path.write_bytes(image)
        invalid.append(path)
    truncated = temp / "truncated.iso"
    truncated.write_bytes(original[:pvd + 100])
    invalid.extend([truncated, temp / "nonexistent.iso"])
    (temp / "test.c").write_text(prefix + code + main, encoding="utf-8")
    binary = temp / "neutrino_gameid_test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "include"), str(temp / "test.c"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary), str(valid), *map(str, invalid)], check=True, timeout=10)

print("R3Z Neutrino GameID: malformed extents, fragmented/failed IO, source protection and bounded MMCE failures passed")
