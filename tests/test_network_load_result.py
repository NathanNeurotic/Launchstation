"""load_ps2ip() and load_smb() from src/init.c, compiled with mocked IOP calls.

PS2IP and SMAP return MODULE_NO_RESIDENT_END (1) when their start fails and
MODLOAD unloads them, so only 0 counts as loaded. Each failure names its stage
in the SMB error. Stage messages come from the diagnostic in #309 (1a9087c).
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / "src/init.c").read_text(encoding="utf-8")


def production_function(signature):
    start = src.index(signature)
    first = src.index("{", start)
    depth = 1
    end = first + 1
    while depth:
        depth += (src[end] == "{") - (src[end] == "}")
        end += 1
    return src[start:end] + "\n"


code = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char u8;
#define ETH 1
#define UDPFS 1
#define NETWORK_STACK_ETH 1
#define DEV9_VARIANT_PS2SDK_V10 2
#define DPRINTF(...) ((void)0)
#define size_ps2ip_irx 1
#define size_ps2ip_v10_irx 1
#define size_ps2smap_irx 1
#define size_smbman_irx 1
#define size_smb2man_irx 1
static unsigned char ps2ip_irx[1], ps2ip_v10_irx[1], ps2smap_irx[1];
static unsigned char smbman_irx[1], smb2man_irx[1];
static unsigned char if_conf[1];
static int if_conf_len = 1;
static u8 have_ps2ip, have_ps2smap, ps2dev9_loaded, have_smbman, have_smb2man;
static int ps2dev9_variant = 1, owner, stack_ok, dev9_ok, connects;
/* per module: 0 loads, 1 start failed (MODULE_NO_RESIDENT_END), -1 load failed */
static int ip_mode, smap_mode;
static const char *dashboard_ip_error;
static const char *smb_load_error[3];
static struct { int legacy_net_drivers; } settings, *setting = &settings;
static int pcCardCfOwnsBus(void) { return owner; }
static int switchNetworkStack(int mode) { assert(mode == NETWORK_STACK_ETH); return stack_ok; }
static int load_ps2dev9(void) { ps2dev9_loaded = (u8)dev9_ok; return dev9_ok; }
static int SifExecModuleBuffer(const void *data, int len, int arglen, const void *arg, int *ret) {
    int mode = 0;
    (void)len; (void)arglen; (void)arg;
    if (data == ps2ip_irx || data == ps2ip_v10_irx) mode = ip_mode;
    else if (data == ps2smap_irx) mode = smap_mode;
    if (mode < 0) { *ret = 0; return -400; }
    *ret = mode;
    return 1;
}
static void showLoadingModulesMsg(const char *device) { (void)device; }
static void ensureCoreIoStackReady(void) {}
static void setupPowerOff(void) {}
static void getIpConfig(void) {}
static void smbSharesConnect(int version, int retry) {
    assert(version == 1 || version == 2);
    assert(retry == 1);
    connects++;
}
"""
code += production_function("static int load_ps2ip(void)\n{")
code += production_function("int load_smb(int version)\n{")
code += production_function("const char *smbLoadError(int version)\n{")
code += r"""
static void reset_state(void) {
    have_ps2ip = have_ps2smap = ps2dev9_loaded = have_smbman = have_smb2man = 0;
    dashboard_ip_error = NULL;
    smb_load_error[1] = smb_load_error[2] = NULL;
    owner = 0; connects = 0;
    stack_ok = dev9_ok = 1;
    ip_mode = smap_mode = 0;
    settings.legacy_net_drivers = 0;
}
static void expect_fail(const char *stage) {
    assert(!load_smb(2));
    assert(strstr(smbLoadError(2), stage) != NULL);
    assert(connects == 0);
}
int main(void) {
    reset_state(); dev9_ok = 0; expect_fail("DEV9");

    /* a start that fails is not a loaded module, and is tried again */
    reset_state(); ip_mode = 1; expect_fail("PS2IP");
    assert(!have_ps2ip);
    reset_state(); ip_mode = -1; expect_fail("PS2IP");
    reset_state(); smap_mode = 1; expect_fail("SMAP");
    assert(have_ps2ip && !have_ps2smap);
    smap_mode = 0;
    assert(load_smb(2) && !strcmp(smbLoadError(2), "") && connects == 1);
    reset_state(); smap_mode = -1; expect_fail("SMAP");

    reset_state(); owner = 1; expect_fail("PC CARD");
    reset_state(); stack_ok = 0; expect_fail("UDPFS");
    reset_state(); stack_ok = 0;
    assert(!load_ps2ip() && strstr(dashboard_ip_error, "UDPFS") != NULL);

    /* success leaves an earlier caller's message alone */
    reset_state(); dashboard_ip_error = "earlier";
    assert(load_ps2ip() && !strcmp(dashboard_ip_error, "earlier"));

    reset_state();
    assert(load_smb(2) && !strcmp(smbLoadError(2), "") && connects == 1);
    reset_state();
    assert(load_smb(1) && !strcmp(smbLoadError(1), "") && connects == 1);

    puts("PASS: PS2IP/SMAP count as loaded only on 0; each network-load failure names its stage");
    return 0;
}
"""
cc = shutil.which("gcc") or shutil.which("clang")
assert cc, "Host compiler required"
with tempfile.TemporaryDirectory(prefix="network-load-result-") as tmp:
    c, exe = Path(tmp) / "test.c", Path(tmp) / "test"
    c.write_text(code, encoding="utf-8")
    subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
