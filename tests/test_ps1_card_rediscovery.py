#!/usr/bin/env python3
"""Physical PS1 card rediscovery after an IOP reset, and live memory-card
rows in the views that keep their device list open.

From saildot4k/Launchstation 9372a47: the physical mcman is built from the
vendored vmcman sources, so a PS1 card that stays powered across an IOP reset
(and so reports no new insertion) has its header and directory read again.
"""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def function(src, signature):
    start = src.index(signature)
    brace = src.index("{", start)
    depth = 0
    for i in range(brace, len(src)):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[start:i + 1]
    raise AssertionError(signature)


def check_driver_target():
    yaml = read("ps2.yaml")
    target = yaml[yaml.index("- name: mcman_physical"):yaml.index("- name: vmcman")]
    for src in ("main.c", "mcdev.c", "mcsio2.c", "ps2mc_fio.c", "ps1mc_fio.c"):
        assert "iop/vmcman/src/" + src in target, src
    assert "defines: [BUILDING_XMCMAN]" in target
    assert "BUILDING_VMCMAN" not in target and "SIO2MAN_V2" not in target
    main = yaml[yaml.index("- name: wLaunchELF\n"):]
    assert "- mcman_physical:mcman" in main
    assert re.search(r"^\s+- mcman\s*$", main, re.M) is None, "main ELF still embeds the SDK mcman"
    # The exported library keeps the SDK name and version MCSERV binds to.
    assert "DECLARE_EXPORT_TABLE(mcman, 2, 3)" in read("iop/vmcman/src/exports.tab")


def check_redetect():
    src = read("iop/vmcman/src/mcsio2.c")
    fast = function(src, "int mcman_probePS1Card2(int port, int slot)")
    full = function(src, "int mcman_probePS1Card(int port, int slot)")
    known = "mcdi->cardtype == sceMcTypePS1 || mcdi->cardtype == sceMcTypePDA"
    for body in (fast, full):
        block = body[body.index("if (mcman_sio2outbufs_PS1PDA[1] == 0) {"):]
        block = block[:block.index("else if (mcman_sio2outbufs_PS1PDA[1] != ")]
        # Cached results are trusted only for a card already known as PS1/PDA.
        assert known in block, block
        assert block.index(known) < block.index("return sceMcResSucceed")
    assert "return mcman_probePS1Card(port, slot);" in fast
    tail = full[full.index(known):]
    for call in ("mcman_invhandles(port, slot);", "mcman_clearcache(port, slot);",
                 "r = mcman_setPS1devinfos(port, slot);"):
        assert call in tail, call
    assert "r == sceMcResSucceed ? sceMcResChangedCard : r" in tail
    # The physical build has the plain ioman stat, without private_* fields.
    fio = read("iop/vmcman/src/ps2mc_fio.c")
    assert fio.count("#if MCMAN_ENABLE_EXTENDED_DEV_OPS\n\tdirent->stat.private_0") == 1
    assert fio.count("#if MCMAN_ENABLE_EXTENDED_DEV_OPS\n\tstat->private_0") == 1


HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include <assert.h>
enum { DI_MC = 1, DI_USB = 2 };
enum { DASH_DEV_READY, DASH_DEV_ABSENT, DASH_DEV_BUSY };
typedef struct { int icon, state; float fill; char name[32], mount[16], browse[16], status[48], detail[48]; } DashDevice;
static int present[2], types[2], frees[2], mc1_loaded = 1;
static int filerRootMcPresent(int port, int *type, int *free_kb) { *type = types[port]; *free_kb = frees[port]; return present[port]; }
static int isPathDeviceStackLoaded(const char *p) { (void)p; return mc1_loaded; }
static const char *localizeDashboardText(const char *s) { return s; }
static void dashStrCopy(char *d, const char *s, int n) { snprintf(d, n, "%s", s); }
%FUNC%
int main(void)
{
	DashDevice d[3];
	memset(d, 0, sizeof(d));
	d[0].icon = DI_MC; strcpy(d[0].mount, "mc0:");
	d[1].icon = DI_MC; strcpy(d[1].mount, "mc1:");
	d[2].icon = DI_MC; strcpy(d[2].status, "VMC"); /* a mounted image: no mcN: mount */
	present[0] = 0; present[1] = -1;
	dashDevicesRefreshMemoryCards(d, 3);
	assert(d[0].state == DASH_DEV_ABSENT && !strcmp(d[0].status, "Slot empty"));
	assert(d[1].state == DASH_DEV_BUSY && !strcmp(d[1].status, "Checking..."));
	assert(!strcmp(d[2].status, "VMC"));
	/* Card inserted while the list is open: the row follows the poll. */
	present[0] = 1; types[0] = 1; frees[0] = 56;
	present[1] = 1; types[1] = 2; frees[1] = 4096;
	dashDevicesRefreshMemoryCards(d, 3);
	assert(d[0].state == DASH_DEV_READY && !strcmp(d[0].status, "PS1 memory card"));
	assert(d[1].state == DASH_DEV_READY && !strcmp(d[1].detail, "4.00 MB free of 8 MB") && d[1].fill == 0.5f);
	present[1] = -2; mc1_loaded = 0;
	dashDevicesRefreshMemoryCards(d, 3);
	assert(d[1].state == DASH_DEV_BUSY && !strcmp(d[1].status, "Used by MX4SIO - open to switch") && !d[1].detail[0]);
	mc1_loaded = 1;
	dashDevicesRefreshMemoryCards(d, 3);
	assert(!strcmp(d[1].status, "Not responding - open to retry"));
	return 0;
}
"""


def check_live_rows():
    devices = read("src/dash_devices.c")
    func = function(devices, "void dashDevicesRefreshMemoryCards(DashDevice *devs, int count)")
    listing = function(devices, "int dashDevicesList(DashDevice *out, int max)")
    assert "dashDevicesRefreshMemoryCards(d, 1);" in listing
    assert "filerRootMcPresent" not in listing
    for rel, guard in (("src/dash_browser.c", "!fb.dir[0] && !fb.loading && !dashWorkerIoBusy() && !dashIopParked()"),
                       ("src/dash_widgets.c", "!pk.loading && !pk.starting && !dashWorkerIoBusy() && !dashIopParked()")):
        src = read(rel)
        block = src[src.index(guard):]
        block = block[:block.index("}")]
        assert "pollRootMemoryCardDevices();" in block and "dashDevicesRefreshMemoryCards(" in block, rel
    with tempfile.TemporaryDirectory() as tmp:
        c = pathlib.Path(tmp) / "h.c"
        c.write_text(HARNESS.replace("%FUNC%", func), encoding="utf-8")
        exe = pathlib.Path(tmp) / "h"
        subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Werror", "-fsanitize=address,undefined",
                        str(c), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


check_driver_target()
check_redetect()
check_live_rows()
print("ok: PS1 card rediscovery and live memory-card rows")
