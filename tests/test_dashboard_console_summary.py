"""Console labels are passive; exact model lookup stays user-initiated."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
info = (ROOT / "src/dash_info.c").read_text()
home = (ROOT / "src/dash_home.c").read_text()
devices = (ROOT / "src/dash_files.c").read_text()
header = (ROOT / "include/dash_internal.h").read_text()

assert "void dashConsoleSummaryText(char *out, size_t out_size);" in header
summary = info.split("void dashConsoleSummaryText(char *out, size_t out_size)", 1)[1].split("void dashShowSystemInfo(void)", 1)[0]
# Rendering must never open CDVD, run an IOP RPC or re-read hardware identity.
for forbidden in ("GetConsoleModelName(", "GetConsoleDvdVersion(", "getROMVersion(",
                  "Sif", "fileXio", "load", "DelayThread", "uLE_InitializeRegion("):
    assert forbidden not in summary, forbidden
assert "cached_console_model[0]" in summary
assert 'console_is_PSX ? "PSX" : "PS2"' in summary
assert "consoleRegionName(ROMVER_data[4])" in summary
# The region must be derived from ROMVER, never from the memory-card system-folder key.
assert "rough_region" not in summary
assert 'case \'H\': return "Asia (NTSC-J)"' in info
assert 'case \'A\': return "Americas (NTSC-U/C)"' in info

sheet = info.split("void dashShowSystemInfo(void)", 1)[1].split("void dashShowFontViewer(", 1)[0] if "void dashShowFontViewer(" in info else info.split("void dashShowSystemInfo(void)", 1)[1]
assert 'dashWorkerAwaitAll("Finishing up")' in sheet
assert "GetConsoleModelName(ctx->romver_data, job.model, sizeof(job.model));" in sheet
assert "snprintf(cached_console_model, sizeof(cached_console_model)" in sheet
assert "cached_console_model[0] = '\\0';" in sheet

assert "dashConsoleSummaryText(console, sizeof(console));" in home
assert "dashConsoleSummaryText(console, sizeof(console));" in devices
assert "GetConsoleModelName(" not in home
assert "GetConsoleModelName(" not in devices
dev_input = devices.split("static void devInput(const DashInput *in)", 1)[1].split("static void devUpdate(", 1)[0]
assert "in->pressed & DASH_IN_FAV" in dev_input
assert "dashShowSystemInfo();" in dev_input
assert dev_input.index("dashShowSystemInfo();") < dev_input.index("if (ndevs <= 0")
assert 'h[2].label = "Console info"' in devices
print("PASS: passive HOME/DEVICES console summary; explicit model discovery")
