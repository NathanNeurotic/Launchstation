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
static int is_early_init,loads,pad_restarts,failed_mass,worker;
static int have_HDD_modules,block_storage_stack_mode,have_ata_bd,have_cdvd;
static char bdm_irx[1],bdmfs_fatfs_irx[1],usbmass_bd_irx[1];
static int size_bdm_irx=1,size_bdmfs_fatfs_irx=1,size_usbmass_bd_irx=1;
static int onWorker(void) { return worker; }
static int (*iopResetBlocked)(void)=onWorker;
static void showLoadingModulesMsg(const char *s) { (void)s; }
static void ensureCoreIoStackReady(void) {}
static void loadUsbDModule(void) { have_usbd=1; }
static void waitForUsbMassBdmfsSettle(void) {}
static void delay(int n) { (void)n; }
static int reopenPadPorts(void) { pad_restarts++; return 1; }
static int loadAtaModules(void) { return 1; }
static int loadHddModules(void) { return 1; }
static void loadCdModules(void) {}
static int SifExecModuleBuffer(void *p,int n,int ac,void *av,int *ret) {
    (void)n;(void)ac;(void)av; loads++; *ret=0;
    if(p==usbmass_bd_irx && failed_mass) return -1;
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
    }
    assert(!isPathDeviceStackLoaded("usb0:/"));
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
    puts("PASS: USB reuses shared BDM, preserves PAD session, blocks worker startup and reports module failure");
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
