"""Run the production USB loader against deterministic IOP/PAD mocks.

Checks resident-core reuse and separation from wired-pad teardown. It does not
emulate disk formats, IOP RPC latency or console USB compatibility.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/init.c').read_text()
start = source.index('void loadUsbModules(void)\n{')
end = source.index('//endfunc loadUsbModules', start)
loader = source[start:end]
def function(text, signature):
    start=text.index(signature); pos=text.index('{',start)+1; depth=1
    while depth:
        depth+=(text[pos]=='{')-(text[pos]=='}'); pos+=1
    return text[start:pos]+'\n'
helpers = function((root/'src/filer.c').read_text(), 'static int mapMassPathToFatFsPath(')
helpers += function(source, 'int isPathDeviceStackLoaded(const char *path)\n{')
helpers += function(source, 'static int pathUsesUsbMass(const char *path)\n{')
helpers += function((root/'src/filer_device.c').read_text(), 'int ensurePathDeviceStackReady(')
stub = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define EXFAT 1
#define MAX_PATH 256
#define TRUE 1
#define FALSE 0
#define BLOCK_STACK_HDD 1
#define BLOCK_STACK_ATA 2
#define USB_MASS_MAX_DRIVES 10
#define DPRINTF(...) ((void)0)
static int have_usbd,have_usb_mass,have_bdm,have_bdmfs,USB_mass_loaded,USB_mass_max_drives;
static int is_early_init,loads,pad_restarts,failed_mass,failed_bdm,failed_usbd,worker;
static char load_order[16];
static int load_order_count;
static void trackLoad(char stage) {
    assert(load_order_count < (int)sizeof(load_order)-1);
    load_order[load_order_count++] = stage;
    load_order[load_order_count] = 0;
}
static int have_HDD_modules,block_storage_stack_mode,have_ata_bd,have_cdvd,pccf_loaded;
static int isPcCardCfPath(const char *path) { (void)path; return 0; }
static int loadPcCardCfModules(void) { return 0; }
static int isPcCardCfOnline(void) { return 0; }
static char LaunchElfDir[256];
static char bdm_irx[1],bdmfs_fatfs_irx[1],usbmass_bd_irx[1];
static int size_bdm_irx=1,size_bdmfs_fatfs_irx=1,size_usbmass_bd_irx=1;
static int onWorker(void) { return worker; }
static int (*iopResetBlocked)(void)=onWorker;
static void showLoadingModulesMsg(const char *s) { (void)s; }
static void ensureCoreIoStackReady(void) {}
static void loadUsbDModule(void) {
    if (!have_usbd) trackLoad('U');
    have_usbd = !failed_usbd;
}
static void waitForUsbMassBdmfsSettle(void) {}
static void delay(int n) { (void)n; }
static int reopenPadPorts(void) { pad_restarts++; return 1; }
static int loadAtaModules(void) { return 1; }
static int loadHddModules(void) { return 1; }
static void loadCdModules(void) {}
static int SifExecModuleBuffer(void *p,int n,int ac,void *av,int *ret) {
    (void)n;(void)ac;(void)av; loads++; *ret=0;
    if(p==bdm_irx) { trackLoad('B'); if(failed_bdm) return -1; }
    if(p==bdmfs_fatfs_irx) trackLoad('F');
    if(p==usbmass_bd_irx) { trackLoad('M'); if(failed_mass) return -1; }
    return 1;
}
'''
checks = r'''
int main(void) {
    char path[256],mapped[256];
    have_bdm=have_bdmfs=1;
    for(int slot=0;slot<10;slot++) {
        snprintf(path,sizeof(path),"mass%d:/APPS/test.ELF",slot);
        assert(mapMassPathToFatFsPath(path,mapped)==0 && !strcmp(path,mapped));
        assert(isPathDeviceStackLoaded(path) && ensurePathDeviceStackReady(path) && loads==0);
        assert(!pathUsesUsbMass(path));
    }
    assert(!isPathDeviceStackLoaded("usb0:/"));
    assert(pathUsesUsbMass("usb1:/") && pathUsesUsbMass("mass:/"));
    assert(mapMassPathToFatFsPath("mass:/",mapped)==0 && !strcmp(mapped,"usb0:/"));
    assert(mapMassPathToFatFsPath("mass10:/",mapped)<0);
    have_bdm=0; assert(!ensurePathDeviceStackReady("mass3:/") && loads==0);
    /* USB adds its block driver to a BDM core already used by ATA/MX4SIO. */
    have_bdm=have_bdmfs=have_usbd=1;
    loadUsbModules();
    assert(have_usb_mass && USB_mass_loaded && loads==1 && pad_restarts==0);
    loadUsbModules();
    assert(loads==1 && pad_restarts==0);
    /* Worker directory discovery cannot start modules or change the PAD session. */
    have_usb_mass=USB_mass_loaded=0; worker=1;
    loadUsbModules(); assert(loads==1 && !have_usb_mass && !pad_restarts);
    worker=0;
    failed_mass=1; loadUsbModules();
    assert(!have_usb_mass && !USB_mass_loaded && have_bdm && have_bdmfs && !pad_restarts);
    failed_mass=0; have_bdm=have_bdmfs=0;
    int before=loads; loadUsbModules();
    assert(have_bdm && have_bdmfs && have_usb_mass && loads==before+3 && !pad_restarts);
    /* Work against the working wLaunchELF_R3Z USB host-first sequence.
     * Cold storage startup: USBD -> BDM -> BDMFS -> USBMASS_BD. */
    have_usbd=have_usb_mass=have_bdm=have_bdmfs=USB_mass_loaded=0;
    load_order_count=0; load_order[0]=0;
    loadUsbModules();
    assert(!strcmp(load_order,"UBFM") && have_usb_mass && !pad_restarts);

    /* A failed BDM core cannot start BDMFS or USBMASS_BD, but the shared
     * USBD host legitimately stays resident (as in the R3Z baseline). */
    have_usbd=have_usb_mass=have_bdm=have_bdmfs=USB_mass_loaded=0;
    load_order_count=0; load_order[0]=0; failed_bdm=1;
    loadUsbModules();
    assert(!strcmp(load_order,"UB") && have_usbd && !have_usb_mass);
    assert(!have_bdm && !have_bdmfs);
    failed_bdm=0;

    /* Failed USB host init must NOT start BDM, BDMFS or USBMASS. */
    have_usbd=have_usb_mass=have_bdm=have_bdmfs=USB_mass_loaded=0;
    load_order_count=0; load_order[0]=0; failed_usbd=1;
    loadUsbModules();
    assert(!strcmp(load_order,"U") && !have_usbd && !have_usb_mass);
    assert(!have_bdm && !have_bdmfs);
    failed_usbd=0;

    /* Reuse USBD if controllers, keyboard or another client loaded it. */
    have_usbd=1;have_usb_mass=have_bdm=have_bdmfs=USB_mass_loaded=0;
    load_order_count=0; load_order[0]=0;
    loadUsbModules();
    assert(!strcmp(load_order,"BFM") && have_usb_mass && have_usbd);

    /* Already-resident shared BDM/BDMFS core remains untouched. */
    have_usbd=have_bdm=have_bdmfs=1;
    have_usb_mass=USB_mass_loaded=0;
    load_order_count=0; load_order[0]=0;
    loadUsbModules();
    assert(!strcmp(load_order,"M") && have_usb_mass && !pad_restarts);

    /* Reinvoking the loader must not unload/reinit the host or PADMAN. */
    load_order_count=0; load_order[0]=0;
    loadUsbModules();
    assert(!strcmp(load_order,"") && pad_restarts==0);
    puts("PASS: wLE_R3Z USBD->BDM->BDMFS->USBMASS_BD parity, failures, shared reuse and PAD preservation");
}
'''
compiler = shutil.which('gcc') or shutil.which('clang')
if not compiler:
    raise SystemExit('Host gcc or clang required')
with tempfile.TemporaryDirectory(prefix='wle-usb-lifecycle-') as tmp:
    c=Path(tmp)/'usb.c'; exe=Path(tmp)/'usb.exe'
    c.write_text(stub+loader+helpers+checks)
    subprocess.run([compiler,'-std=gnu11',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=5)
