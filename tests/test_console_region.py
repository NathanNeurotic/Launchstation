"""Console information must name the real region from rom0:ROMVER.

Regression: an SCPH-70005 (Korean, ROMVER 0200HC20040614) showed "Region: A"
because the sheet printed rough_region, the memory-card system-folder letter
that USA and Asia consoles share (BAEXEC-SYSTEM).
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
info = (root / "src/dash_info.c").read_text(encoding="utf-8")

start = info.index("static const char *consoleRegionName(")
end = info.index("}\n", info.index("default:", start)) + 2
end = info.index("}\n", end) + 2
func = info[start:end]

harness = "#include <assert.h>\n#include <stdio.h>\n#include <string.h>\n" + func + r'''
int main(void) {
    const char *romver = "0200HC20040614"; /* SCPH-70005 from the report */
    assert(!strcmp(consoleRegionName(romver[4]), "Asia (NTSC-J)"));
    assert(!strcmp(consoleRegionName('J'), "Japan (NTSC-J)"));
    assert(!strcmp(consoleRegionName('A'), "Americas (NTSC-U/C)"));
    assert(!strcmp(consoleRegionName('E'), "Europe / Oceania (PAL)"));
    assert(!strcmp(consoleRegionName('C'), "China"));
    assert(!strcmp(consoleRegionName('\0'), "unknown"));
    puts("PASS: console region comes from the ROMVER letter (H = Asia)");
    return 0;
}
'''
compiler = shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("Host gcc or clang required")
with tempfile.TemporaryDirectory(prefix="wle-region-") as tmp:
    c, exe = Path(tmp) / "r.c", Path(tmp) / "r.exe"
    c.write_text(harness, encoding="utf-8")
    subprocess.run([compiler, "-std=c99", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# The sheet must not print the system-folder letter as the region again.
assert '"Region: %c", ctx->rough_region' not in info
assert "consoleRegionName(ctx->romver_data[0] ? ctx->romver_data[4] : '\\0')" in info
print("PASS: Console information no longer labels the BxEXEC letter as region")
