"""Exercise actual network-loader and SMB failure reporting with mocked IOP calls."""
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
static int ps2dev9_variant = 1, owner, stack_ok, dev9_ok, ip_ok, smap_ok, connects;
static const char *dashboard_ip_error;
static const char *smb_load_error[3];
static struct { int legacy_net_drivers; } settings, *setting = &settings;
static int pcCardCfOwnsBus(void) { return owner; }
static int switchNetworkStack(int mode) { assert(mode == NETWORK_STACK_ETH); return stack_ok; }
static int load_ps2dev9(void) { ps2dev9_loaded = (u8)dev9_ok; return dev9_ok; }
static int SifExecModuleBuffer(const void *data, int len, int arglen, const void *arg, int *ret) {
    int good;
    (void)len; (void)arglen; (void)arg;
    if (data == ps2ip_irx || data == ps2ip_v10_irx) good = ip_ok;
    else if (data == ps2smap_irx) good = smap_ok;
    else good = 1;
    *ret = good ? 0 : -1;
    return good ? 1 : -1;
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
    stack_ok = dev9_ok = ip_ok = smap_ok = 1;
    settings.legacy_net_drivers = 0;
}
int main(void) {
    reset_state();
    dev9_ok = 0;
    assert(!load_smb(2) && strstr(smbLoadError(2), "DEV9") != NULL);
    assert(connects == 0);

    reset_state();
    ip_ok = 0;
    assert(!load_smb(2) && strstr(smbLoadError(2), "PS2IP") != NULL);
    assert(connects == 0);

    reset_state();
    smap_ok = 0;
    assert(!load_smb(2) && strstr(smbLoadError(2), "SMAP") != NULL);
    assert(connects == 0);

    reset_state();
    owner = 1;
    assert(!load_smb(2) && strstr(smbLoadError(2), "PC CARD") != NULL);
    assert(connects == 0);

    reset_state();
    stack_ok = 0;
    assert(!load_smb(2) && strstr(smbLoadError(2), "UDPFS") != NULL);
    assert(connects == 0);
    assert(!load_ps2ip() && dashboard_ip_error &&
           strstr(dashboard_ip_error, "UDPFS") != NULL);

    reset_state();
    assert(load_smb(2) && !strcmp(smbLoadError(2), ""));
    assert(connects == 1);

    reset_state();
    assert(load_smb(1) && !strcmp(smbLoadError(1), ""));
    assert(connects == 1);

    puts("PASS: exact production network-load failures identify stage; success unchanged");
    return 0;
}
"""
cc = shutil.which("gcc") or shutil.which("clang")
assert cc, "Host compiler required"
with tempfile.TemporaryDirectory(prefix="smb-network-errors-") as tmp:
    c, exe = Path(tmp) / "test.c", Path(tmp) / "test"
    c.write_text(code, encoding="utf-8")
    subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
