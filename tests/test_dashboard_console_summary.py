"""Console labels are passive; identity is read once at startup (R3Z 0aeaaea)."""
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
assert "cached_console_info.model[0]" in summary
assert 'console_is_PSX ? "PSX" : "PS2"' in summary
assert "consoleRegionName(ROMVER_data[4])" in summary
assert '"ROM: %c.%c%c%c"' in summary and '"DVD: unavailable"' in summary
# The region must be derived from ROMVER, never from the memory-card system-folder key.
assert "rough_region" not in summary
assert 'case \'H\': return "Asia (NTSC-J)"' in info
assert 'case \'A\': return "Americas (NTSC-U/C)"' in info

sheet = info.split("void dashShowSystemInfo(void)", 1)[1].split("void dashShowFontViewer(", 1)[0] if "void dashShowFontViewer(" in info else info.split("void dashShowSystemInfo(void)", 1)[1]
assert 'dashWorkerAwaitAll("Finishing up")' in sheet
assert "dashConsoleIdentityInit();" in sheet and "job = cached_console_info;" in sheet
assert "GetConsoleModelName(" not in sheet
init = info.split("void dashConsoleIdentityInit(void)", 1)[1].split("void dashConsoleSummaryText(", 1)[0]
assert "if (console_identity_initialized)" in init
assert "GetConsoleModelName(ROMVER_data, cached_console_info.model" in init
assert "GetConsoleDvdVersion(cached_console_info.dvd" in init
main = (ROOT / "src/dash_main.c").read_text()
first = main.split("static void firstInit(void)", 1)[1]
# Identity is cached before the dashboard worker can run IOP-side file work.
assert first.index("dashConsoleIdentityInit();") < first.index("dashWorkerInit();")

assert "dashConsoleSummaryText(console, sizeof(console));" in home
assert "dashConsoleSummaryText(console, sizeof(console));" in devices
assert "GetConsoleModelName(" not in home
assert "GetConsoleModelName(" not in devices
assert "char console[160];" in home and "char console[160];" in devices
dev_input = devices.split("static void devInput(const DashInput *in)", 1)[1].split("static void devUpdate(", 1)[0]
assert 'items[n].label = "Console information"' in dev_input
assert "ids[n++] = 4;" in dev_input
assert "case 4:" in dev_input and "dashShowSystemInfo();" in dev_input
assert "DashHint h[4];" in devices
assert "dashDrawHints(h, 4);" in devices
print("PASS: passive HOME/DEVICES console summary; identity cached once at startup")
