"""Run production EE CF ownership/probe code with mocked module/RPC boundaries."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/init.c").read_text()

def function(signature):
    start = source.index(signature + "\n{")
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos] + "\n"

code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pccf_control.h"
static char ROMVER_data[16];
static int console_is_PSX, boot_safe_mode, have_ps2dev9, ps2dev9_loaded;
static int pccf_bus_owned, pccf_loaded, done_setupPowerOff, have_poweroff;
static int core_calls, bdm_ok=1, power_calls, dev9_calls, module_calls, module_ret, rpc_calls, rpc_ret;
static unsigned char pccf_irx[1], poweroff_irx[1];
static int size_pccf_irx=1, size_poweroff_irx=1;
#define DPRINTF(...) ((void)0)
static int pcCardCfOwnsBus(void);
static void poweroffHandler(int i) { (void)i; }
static void ensureCoreIoStackReady(void) { assert(pccf_bus_owned); ++core_calls; }
static int loadBdmCoreModules(void) { assert(pccf_bus_owned); return bdm_ok; }
static int poweroffInit(void) { ++power_calls; return 0; }
static void poweroffSetCallback(void *fn, void *data) { (void)fn; (void)data; }
static int SifExecModuleBuffer(const void *module, int size, int len, void *args, int *ret) {
    (void)size; (void)len; (void)args;
    assert(pccf_bus_owned);
    *ret = module == pccf_irx ? module_ret : 0;
    if (module == pccf_irx) ++module_calls;
    return 3;
}
static int fileXioDevctl(const char *dev, int cmd, void *a, int al, void *b, int bl) {
    assert(!strcmp(dev, PCCF_CONTROL_DEVICE) && cmd == PCCF_CONTROL_OFF);
    assert(!a && !al && !b && !bl); ++rpc_calls; return rpc_ret;
}
'''
# Single-line getter has no braces to balance with the helper's line anchor.
code += "static int pcCardCfOwnsBus(void) { return pccf_bus_owned; }\n"
for signature in ("int isPcCardCfPath(const char *path)", "int isPcCardCfEligible(void)", "int shutdownPcCardCf(void)",
                  "void setupPowerOff(void)", "int loadPcCardCfModules(void)"):
    code += function(signature)
code += r'''
int main(void) {
    assert(isPcCardCfPath("cf:/BOOT.ELF") && isPcCardCfPath("cf0:/BOOT.ELF") && isPcCardCfPath("cf12:"));
    assert(!isPcCardCfPath(NULL) && !isPcCardCfPath("") && !isPcCardCfPath("c") && !isPcCardCfPath("cfg:/") && !isPcCardCfPath("mass0:/cf.elf"));
    const char *accepted[] = {"0100JC20000117", "0101JC20000217", "0110JC20000727", "0120JC20001027"};
    const char *rejected[] = {"", "010", "0100", "0100J", "0100AC", "0120EC", "0200JC", "0220JC", "0100JD", "0199JC"};
    unsigned int i;
    for (i=0; i<sizeof(accepted)/sizeof(*accepted); ++i) {
        strcpy(ROMVER_data, accepted[i]); assert(isPcCardCfEligible());
    }
    for (i=0; i<sizeof(rejected)/sizeof(*rejected); ++i) {
        strcpy(ROMVER_data, rejected[i]); assert(!isPcCardCfEligible());
        assert(!loadPcCardCfModules());
    }
    assert(!core_calls && !module_calls);
    strcpy(ROMVER_data, accepted[0]); console_is_PSX=1;
    assert(!loadPcCardCfModules()); console_is_PSX=0;
    boot_safe_mode=1; assert(!loadPcCardCfModules()); boot_safe_mode=0;
    have_ps2dev9=1; assert(!loadPcCardCfModules()); have_ps2dev9=0;
    ps2dev9_loaded=1; assert(!loadPcCardCfModules()); ps2dev9_loaded=0;
    assert(!core_calls && !module_calls);
    bdm_ok=0; assert(!loadPcCardCfModules() && !pcCardCfOwnsBus());
    assert(!power_calls && !module_calls); bdm_ok=1;
    module_ret=1; assert(!loadPcCardCfModules() && !pcCardCfOwnsBus());
    assert(power_calls==1 && !dev9_calls && module_calls==1);
    module_ret=0; assert(loadPcCardCfModules() && pcCardCfOwnsBus());
    assert(!dev9_calls && module_calls==2);
    assert(loadPcCardCfModules() && module_calls==2);
    rpc_ret=-1; assert(shutdownPcCardCf()==-1 && pcCardCfOwnsBus());
    rpc_ret=0; assert(!shutdownPcCardCf() && pcCardCfOwnsBus() && pccf_loaded);
    assert(rpc_calls==2);
    puts("PASS: EE ROM hints, DESR/debug/safe-mode rejection, bus reservation, probe rollback and retained shutdown ownership");
}
'''

# Prevent a network stack switch from resetting away an established CF owner.
for signature in ("static int load_ps2dev9(void)", "static int load_ps2ip(void)",
                  "void load_ps2host(void)", "static void load_udpfs_stack(void)",
                  "int loadAtaModules(void)", "int loadHddModules(void)",
                  "void loadNetModules(void)", "int ensureDashboardIpNetworkReady(void)"):
    body = function(signature)
    assert "if (pcCardCfOwnsBus()) return" in body, signature
    guard = body.index("if (pcCardCfOwnsBus()) return")
    for boundary in ("switchNetworkStack(", "switchBlockStorageStack(", "switchPsxHddDriverStack(", "SifExecModuleBuffer("):
        if boundary in body:
            assert guard < body.index(boundary), signature
assert "pccf_bus_owned" not in function("static void clearIopModuleState(void)")
for signature in ("int prepareExploitSignerIop(void)", "void Reset()"):
    body = function(signature)
    assert body.index("SifIopSync") < body.index("pccf_bus_owned = pccf_loaded = 0")
assert "shutdownPcCardCf()" in function("static void idleDev9StorageBeforeReset(void)")
assert "shutdownPcCardCf()" in function("void closeAllAndPoweroff(void)")

cc = shutil.which("gcc") or shutil.which("clang")
assert cc, "host C compiler required"
with tempfile.TemporaryDirectory(prefix="pccf-owner-") as temp:
    cfile, exe = Path(temp) / "owner.c", Path(temp) / "owner.exe"
    cfile.write_text(code)
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-I" + str(root / "include"),
                    str(cfile), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: CF ownership guards precede competing stack switches; only successful IOP sync releases owner")
