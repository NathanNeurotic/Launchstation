"""Regression tests for Zack's October 2026 feedback:
- Issue #132: MASS storage label without '(USB)' suffix
- Issues #142 / #157: Theme background / debug color eliminated on ELF execution and boot
- Issue #129: Consistent file size formatting (2 decimal places for MB/GB) between file browser and details modal
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (root / rel_path).read_text(encoding="utf-8")

def body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    pos = brace + 1
    while depth:
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
        pos += 1
    return source[start:pos]

# 1. Issue #132: MASS labeling in dash_devices.c
devices_src = read("src/dash_devices.c")
assert '"USB Mass Storage"' in devices_src
assert 'snprintf(name, sizeof(name), "USB Mass Storage");' in devices_src
assert '"MASS (USB)"' not in devices_src
assert '"MASS (USB) %d"' not in devices_src

# 2. Issues #142 & #157: Black screen on launch handoff and boot
actions_src = read("src/main_actions.c")
draw_src = read("src/draw.c")
header_src = read("include/launchelf.h")
main_src = read("src/dash_main.c")

cleanup_fn = body(actions_src, "static void cleanUpForExecInternal(")
clear_black_fn = body(draw_src, "void clearScreenToBlack(void)")
first_init_fn = body(main_src, "static void firstInit(void)")
dash_run_fn = body(main_src, "void DashRun(")

# cleanUpForExecInternal must disable legacy theme and clear to black
assert "DashThemeSetLegacyEnabled(0);" in cleanup_fn
assert "clearScreenToBlack();" in cleanup_fn
assert "clrScr(GS_SETREG_RGBA(0x00, 0x00, 0x00, 0));" not in cleanup_fn
assert cleanup_fn.index("DashThemeSetLegacyEnabled(0);") < cleanup_fn.index("clearScreenToBlack();")
assert cleanup_fn.index("clearScreenToBlack();") < cleanup_fn.index("free(setting);")

# clearScreenToBlack must zero BGColor, set GS_SET_BGCOLOR(0, 0, 0), and clear both buffers
assert "GS_SET_BGCOLOR(0, 0, 0);" in clear_black_fn
assert "gsKit_clear(gsGlobal, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x00, 0x00));" in clear_black_fn
assert "gsKit_sync_flip(gsGlobal);" in clear_black_fn
assert "void clearScreenToBlack(void);" in header_src

# Startup ensures both buffers are black before UI presentation
assert "clearScreenToBlack();" in first_init_fn
assert "clearScreenToBlack();" in dash_run_fn

# 3. Issue #129: Sizing consistency (2 decimal places for MB and GB)
browser_src = read("src/dash_browser.c")
fmt_size_fn = body(browser_src, "static void fmtSize(")
fmt_bytes_fn = body(browser_src, "static void fmtBytes(")
measure_fn = body(browser_src, "static void opSize(void)")

assert 'snprintf(out, size, "%.2f MB"' in fmt_size_fn
assert 'snprintf(out, size, "%.2f GB"' in fmt_size_fn
assert 'snprintf(out, size, "%.1f MB"' not in fmt_size_fn
assert 'snprintf(out, size, "%.2f MB"' in fmt_bytes_fn
assert 'snprintf(out, size, "%.2f GB"' in fmt_bytes_fn
assert '%.2f MB' in measure_fn
assert '%.2f GB' in measure_fn

print("PASS: zack launch and sizing feedback contracts")
