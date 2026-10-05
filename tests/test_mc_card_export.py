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
print("PASS: PCSX2 page conversion matches AFL MCMAN ECC; active-MMCE export keeps one stable source handle and verifies output")
