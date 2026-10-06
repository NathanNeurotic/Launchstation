"""Validate PCSX2 memory-card page conversion against the repository's AFL MCMAN ECC."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / "src/mc_card_export.c").read_text()
mcman = (root / "iop/vmcman/src/main.c").read_text()

def table(text, name):
    m = re.search(r"static const u8 " + re.escape(name) + r"\[256\]\s*=\s*\{(.*?)\};", text, re.S)
    assert m, name
    values = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1))]
    assert len(values) == 256
    return values

export_table = table(src, "mc_ecc_xor")
mcman_table = table(mcman, "mcman_xortable")
assert export_table == mcman_table, "EE export ECC table diverged from AFL MCMAN"

def function(signature, text):
    start = text.index(signature)
    body = text.index("{", start)
    end, depth = body + 1, 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"

active_export = function("int mcCardExportActiveMmcePcsx2(", src)
assert active_export.count("genOpen(source, FIO_O_RDONLY)") == 1, "active export must discover/open the synthetic source once"
assert "source_info_fd(src_fd" in active_export, "active export must validate the already-open active source"
assert active_export.count("read_exact_fd(src_fd") >= 2, "write and verify passes must read through the stable active handle"
assert "convert_block(export_in" in active_export, "active export must use the existing PCSX2 page converter"
assert "read_exact_at(dest" in active_export and "memcmp(export_out, verify_out" in active_export, \
    "active export must verify the completed PCSX2 image against regenerated output"
assert "genRemove(remove_path)" in active_export, "failed active exports must remove partial output"

page = bytes((i & 0xFF) for i in range(512))

def ecc128(data):
    c1 = c2 = c3 = 0
    for i, byte in enumerate(data):
        v = mcman_table[byte]
        c1 ^= v
        if v & 0x80:
            c2 ^= ~i
            c3 ^= i
    return bytes(((~c1) & 0x77, (~c2) & 0x7F, (~c3) & 0x7F))

expected = b"".join(ecc128(page[i:i+128]) for i in range(0, 512, 128)) + b"\0\0\0\0"
assert len(expected) == 16

compiler = shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("Host gcc or clang required")

c = """
#include <stdio.h>
#include <string.h>
typedef unsigned char u8;
#define MC_PAGE_DATA 512
#define MC_PAGE_SPARE 16
#define MC_PAGE_PCSX2 528
"""
m = re.search(r"static const u8 mc_ecc_xor\[256\]\s*=\s*\{.*?\};", src, re.S)
assert m
c += m.group(0) + "\n"
c += function("static void page_ecc_128(", src)
c += function("void mcCardPcsx2Page(", src)
c += r"""
int main(void) {
    u8 in[512], out[528];
    int i;
    for (i = 0; i < 512; i++) in[i] = (u8)i;
    memset(out, 0xA5, sizeof(out));
    mcCardPcsx2Page(in, out);
    if (memcmp(in, out, 512)) return 2;
    for (i = 512; i < 528; i++) printf("%02x", out[i]);
    putchar('\n');
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="wle-card-export-") as tmp:
    harness = Path(tmp) / "card.c"
    exe = Path(tmp) / "card.exe"
    harness.write_text(c)
    subprocess.run([compiler, "-std=gnu11", "-Wall", "-Werror", str(harness), "-o", str(exe)], check=True)
    got = bytes.fromhex(subprocess.check_output([str(exe)], text=True).strip())

assert got == expected, f"ECC mismatch: {got.hex()} != {expected.hex()}"
assert got[-4:] == b"\0\0\0\0"

# Exercise the public image validators against exact-size and truncated files.
validator_c = r"""
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
typedef unsigned char u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
#define FIO_O_RDONLY O_RDONLY
#define MC_PAGE_DATA 512
#define MC_PAGE_PCSX2 528
enum {
    MC_CARD_EXPORT_OK = 0,
    MC_CARD_EXPORT_OPEN = -1,
    MC_CARD_EXPORT_FORMAT = -2,
    MC_CARD_EXPORT_IO = -3
};
static int genOpen(const char *p, int f) { return open(p, f); }
static int genRead(int fd, void *p, int n) { return (int)read(fd, p, (size_t)n); }
static s64 genLseek(int fd, s64 off, int whence) { return (s64)lseek(fd, (off_t)off, whence); }
static int genClose(int fd) { return close(fd); }
"""
for sig in (
    "static u16 read_le16(",
    "static u32 read_le32(",
    "static int source_info_fd(",
    "static int source_info(",
    "int mcCardValidateNoEcc(",
    "static int pcsx2_source_info(",
    "int mcCardValidatePcsx2(",
):
    validator_c += function(sig, src)

validator_c += r"""
int main(int argc, char **argv) {
    if (argc != 3) return 9;
    if (!strcmp(argv[1], "mcd"))
        return mcCardValidateNoEcc(argv[2]) == MC_CARD_EXPORT_OK ? 0 : 1;
    if (!strcmp(argv[1], "ps2"))
        return mcCardValidatePcsx2(argv[2]) == MC_CARD_EXPORT_OK ? 0 : 1;
    return 8;
}
"""

with tempfile.TemporaryDirectory(prefix="wle-card-validate-") as tmp:
    tmp = Path(tmp)
    harness = tmp / "validate.c"
    exe = tmp / "validate"
    harness.write_text(validator_c)
    subprocess.run([compiler, "-std=gnu11", "-Wall", "-Werror", str(harness), "-o", str(exe)], check=True)

    raw_size = 512 * 2 * 4
    encoded_size = 528 * 2 * 4
    header = bytearray(64)
    header[:28] = b"Sony PS2 Memory Card Format "
    header[40:42] = (512).to_bytes(2, "little")
    header[42:44] = (2).to_bytes(2, "little")
    header[48:52] = (4).to_bytes(4, "little")

    valid_mcd = tmp / "valid.mcd"
    valid_mcd.write_bytes(bytes(header) + bytes(raw_size - len(header)))
    trunc_mcd = tmp / "trunc.mcd"
    trunc_mcd.write_bytes(valid_mcd.read_bytes()[:-512])

    valid_ps2 = tmp / "valid.ps2"
    valid_ps2.write_bytes(bytes(header) + bytes(encoded_size - len(header)))
    trunc_ps2 = tmp / "trunc.ps2"
    trunc_ps2.write_bytes(valid_ps2.read_bytes()[:-528])

    subprocess.run([str(exe), "mcd", str(valid_mcd)], check=True)
    subprocess.run([str(exe), "ps2", str(valid_ps2)], check=True)
    assert subprocess.run([str(exe), "mcd", str(trunc_mcd)]).returncode != 0
    assert subprocess.run([str(exe), "ps2", str(trunc_ps2)]).returncode != 0
print("PASS: PCSX2 ECC and active export verified; image validators accept exact geometry and reject truncated backups")
