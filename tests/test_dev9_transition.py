"""Execute DEV9 and ATA loaders across refused/successful variant resets."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/init.c").read_text()

def function(name):
    match = re.search(r"(?:static )?(?:int|void) " + name + r"\(void\)\n\{", source)
    assert match, name
    pos = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[match.start():pos] + "\n"

code = r'''
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#define ETH 1
#define UDPFS 1
#define NETWORK_STACK_ETH 1
#define TRUE 1
#define DEV9_VARIANT_NONE 0
#define DEV9_VARIANT_CURRENT 1
#define DEV9_VARIANT_PS2SDK_V10 2
#define DPRINTF(...) ((void)0)
typedef unsigned char u8;
static u8 ps2dev9_irx[1], ps2dev9_v10_irx[1], ata_bd_irx[1];
static int size_ps2dev9_irx=1, size_ps2dev9_v10_irx=1, size_ata_bd_irx=1;
static u8 ps2ip_irx[1], ps2ip_v10_irx[1], ps2smap_irx[1], ps2host_irx[1], ps2ftpd_irx[1], ps2netfs_irx[1];
static int size_ps2ip_irx=1, size_ps2ip_v10_irx=1, size_ps2smap_irx=1, size_ps2host_irx=1, size_ps2ftpd_irx=1, size_ps2netfs_irx=1;
static int have_ps2ip, have_ps2smap, have_ps2host, have_ps2ftpd, have_ps2netfs, network_calls;
static int network_allowed=1, client_calls, if_conf_len;
static int console_is_PSX, have_Flash_modules, xfromserv_loaded, power_calls, flash_calls;
static char if_conf[1];
static struct Config { int legacy_net_drivers; } config;
static struct Config *setting = &config;
static int ps2dev9_loaded, have_ps2dev9, ps2dev9_variant, have_ata_bd;
static int cf_owned, allow_reset, resets, bdm_live, bdm_ok, bdm_calls, ata_calls, dev9_calls, settle_calls;
static int module_id=1, module_result;
static int pcCardCfOwnsBus(void) { return cf_owned; }
static int switchNetworkStack(int mode) { assert(mode==NETWORK_STACK_ETH); return network_allowed; }
static void showLoadingModulesMsg(const char *name) { (void)name; }
static void ensureCoreIoStackReady(void) {}
static void setupPowerOff(void) { power_calls++; }
static void load_pflash(void) { flash_calls++; xfromserv_loaded=1; }
static void getIpConfig(void) {}
static int ps2ip_init(void) { client_calls++; return 0; }
static void resetRuntimeDeviceState(int show) {
    assert(show == TRUE); resets++;
    if (allow_reset) {
        have_ps2dev9=ps2dev9_loaded=have_ata_bd=bdm_live=0;
        have_ps2ip=have_ps2smap=have_ps2host=have_ps2ftpd=have_ps2netfs=0;
        ps2dev9_variant=DEV9_VARIANT_NONE;
    }
}
static int loadBdmCoreModules(void) { bdm_calls++; bdm_live=bdm_ok; return bdm_live; }
static int SifExecModuleBuffer(const void *module, int size, int len, const void *args, int *ret) {
    assert(size==1); (void)len; (void)args;
    if (module==ata_bd_irx) { ata_calls++; if (!bdm_live) return -1; }
    else if (module==ps2dev9_irx || module==ps2dev9_v10_irx) dev9_calls++;
    else network_calls++;
    *ret=module_result; return module_id;
}
static void pauseAfterAtaBlockDriverLoad(void) { settle_calls++; }
'''
for name in ("load_ps2dev9", "loadAtaBlockDriver", "load_ps2ip", "load_ps2host",
             "load_ps2ftpd", "load_ps2netfs", "ensureDashboardIpNetworkReady", "loadFlashModules"):
    code += function(name)
code += r'''
static void reset(void) {
    config.legacy_net_drivers=0;
    ps2dev9_loaded=have_ps2dev9=have_ata_bd=cf_owned=resets=bdm_live=0;
    bdm_calls=ata_calls=dev9_calls=settle_calls=module_result=0;
    ps2dev9_variant=DEV9_VARIANT_NONE; allow_reset=bdm_ok=module_id=1;
    have_ps2ip=have_ps2smap=have_ps2host=have_ps2ftpd=have_ps2netfs=network_calls=client_calls=0;
    network_allowed=1;
    console_is_PSX=have_Flash_modules=xfromserv_loaded=power_calls=flash_calls=0;
}
int main(void) {
    /* Refused reset must preserve ownership and perform no dependent load. */
    reset(); have_ps2dev9=ps2dev9_loaded=1;
    ps2dev9_variant=DEV9_VARIANT_CURRENT; config.legacy_net_drivers=1;
    allow_reset=0;
    assert(!loadAtaBlockDriver());
    assert(resets==1 && !ata_calls && !dev9_calls && !bdm_calls);
    assert(ps2dev9_loaded && ps2dev9_variant==DEV9_VARIANT_CURRENT);
    reset(); have_ps2dev9=ps2dev9_loaded=1;
    ps2dev9_variant=DEV9_VARIANT_PS2SDK_V10; allow_reset=0;
    assert(!loadAtaBlockDriver() && resets==1 && !ata_calls && !dev9_calls && !bdm_calls);
    assert(ps2dev9_variant==DEV9_VARIANT_PS2SDK_V10);
    /* Successful reset clears BDM; restore it before the ATA module runs. */
    reset(); have_ps2dev9=ps2dev9_loaded=bdm_live=1;
    ps2dev9_variant=DEV9_VARIANT_CURRENT; config.legacy_net_drivers=1;
    assert(loadAtaBlockDriver());
    assert(resets==1 && dev9_calls==1 && bdm_calls==1 && ata_calls==1 && settle_calls==1);
    assert(ps2dev9_variant==DEV9_VARIANT_PS2SDK_V10 && bdm_live);
    assert(loadAtaBlockDriver());
    assert(resets==1 && dev9_calls==1 && ata_calls==1 && settle_calls==1);
    /* CF ownership, module rejection and BDM failure stop dependent loads. */
    reset(); cf_owned=1; assert(!loadAtaBlockDriver());
    assert(!dev9_calls && !bdm_calls && !ata_calls);
    reset(); module_id=-1; assert(!loadAtaBlockDriver() && !bdm_calls && !ata_calls);
    reset(); module_result=-1; assert(!loadAtaBlockDriver() && !bdm_calls && !ata_calls);
    reset(); bdm_ok=0; assert(!loadAtaBlockDriver() && !ata_calls);
    /* Existing IP flags do not override a refused DEV9 transition. */
    reset(); have_ps2dev9=ps2dev9_loaded=have_ps2ip=have_ps2smap=1;
    ps2dev9_variant=DEV9_VARIANT_CURRENT; config.legacy_net_drivers=1; allow_reset=0;
    load_ps2host(); load_ps2ftpd(); load_ps2netfs();
    assert(!ensureDashboardIpNetworkReady());
    assert(resets==4 && have_ps2ip && have_ps2smap);
    assert(!network_calls && !client_calls && !dev9_calls);
    assert(!have_ps2host && !have_ps2ftpd && !have_ps2netfs);
    reset(); network_allowed=0;
    load_ps2host(); load_ps2ftpd(); load_ps2netfs();
    assert(!ensureDashboardIpNetworkReady() && !network_calls && !dev9_calls && !client_calls);
    reset(); assert(ensureDashboardIpNetworkReady());
    assert(dev9_calls==1 && network_calls==2 && client_calls==1);
    load_ps2host(); load_ps2ftpd(); load_ps2netfs();
    assert(network_calls==5 && have_ps2host && have_ps2ftpd && have_ps2netfs);
    /* XFROM boot registers power-off even if DEV9 rejects initialization. */
    reset(); console_is_PSX=1; module_id=-1;
    assert(!loadFlashModules() && power_calls==1 && !flash_calls);
    reset(); console_is_PSX=1;
    assert(loadFlashModules() && power_calls==1 && flash_calls==1);
    puts("PASS: refused DEV9/IP transitions, BDM ordering, dependent services, XFROM power-off and CF ownership");
}
'''
with tempfile.TemporaryDirectory(prefix="dev9-transition-") as folder:
    path = Path(folder)
    (path / "test.c").write_text(code)
    exe = path / "test.exe"
    cc = shutil.which("gcc") or shutil.which("clang")
    assert cc, "Host C compiler required"
    subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", str(path / "test.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
