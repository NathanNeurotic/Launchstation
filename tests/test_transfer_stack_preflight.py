"""Production transfer preflight must retire MX4SIO before opening a source.

This models the exact invalidation boundary, not a console/network transfer.
Optional git ref demonstrates that the old preflight leaves a live MX4SIO stack.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parents[1]
source=(subprocess.check_output(['git','show',f'{sys.argv[1]}:src/filer_device.c'],cwd=root,text=True)
        if len(sys.argv)>1 else (root/'src/filer_device.c').read_text())
# Compile the actual complete path/transfer policy with narrow driver fixtures.
source=source[source.index('enum {'):]
code=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MX4SIO 1
#define MMCE 1
#define UDPFS 1
#define TRUE 1
#define FALSE 0
#define TRANSFER_STACK_READY 0
#define TRANSFER_STACK_LOAD_FAILED -1
#define TRANSFER_STACK_INCOMPATIBLE -2
static int mx4sio_driver_running, reset_allowed=1, resets, loads, open_handles;
static int network,usb,USB_mass_loaded;
static int isPcCardCfPath(const char *p){return p&&!strncmp(p,"cf",2);}
static int loadPcCardCfModules(void){return 1;}
static int isPcCardCfOnline(void){return 1;}
static int loadHddModules(void){loads++;return 1;}
static int loadMmceModules(void){loads++;return 1;}
static int loadMx4sioModules(void){loads++;mx4sio_driver_running=1;return 1;}
static int load_udpfs(void){loads++;network=1;return 1;}
static void loadUsbModules(void){loads++;USB_mass_loaded=usb=1;}
static void loadCdModules(void){loads++;}
static int isPathDeviceStackLoaded(const char *p){
 if(!strncmp(p,"mx4sio",6))return mx4sio_driver_running;
 return 1;
}
static void ensureMemoryCardPortAccessible(int p){
 assert(p==1);
 if(mx4sio_driver_running&&reset_allowed){
  assert(!open_handles);resets++;mx4sio_driver_running=network=usb=USB_mass_loaded=0;
 }
}
'''+source+r'''
static void reset(void){mx4sio_driver_running=1;network=usb=1;loads=resets=open_handles=0;reset_allowed=1;}
int main(void){
 const char *sources[]={"udpfs:/folder/","usb0:/folder/","mc0:/folder/"};
 for(int i=0;i<3;i++)for(int reverse=0;reverse<2;reverse++){
  reset();const char *src=reverse?"mc1:/folder/":sources[i];
  const char *dst=reverse?sources[i]:"mc1:/folder/";
  assert(prepareTransferDeviceStacks(src,dst)==TRANSFER_STACK_READY);
  assert(resets==1&&!mx4sio_driver_running);
  if(i==0)assert(network);
  if(i==1)assert(usb);
  /* Once handles exist, normal-card access must not cause another reset. */
  open_handles=2;ensureMemoryCardPortAccessible(1);assert(resets==1);open_handles=0;
 }
 reset();assert(prepareTransferDeviceStacks("mx4sio:/","mc1:/")==TRANSFER_STACK_INCOMPATIBLE);
 assert(!resets&&!loads&&mx4sio_driver_running);
 reset();assert(prepareTransferDeviceStacks("mc1:/","mx4sio:/")==TRANSFER_STACK_INCOMPATIBLE);
 assert(!resets&&!loads&&mx4sio_driver_running);
 reset();reset_allowed=0;assert(prepareTransferDeviceStacks("udpfs:/","mc1:/")==TRANSFER_STACK_LOAD_FAILED);
 assert(!resets&&!loads&&mx4sio_driver_running);
 reset();assert(prepareTransferDeviceStacks("udpfs:/","mc0:/")==TRANSFER_STACK_READY);
 assert(!resets&&mx4sio_driver_running&&network);
 mx4sio_driver_running=0;resets=0;
 assert(prepareTransferDeviceStacks("udpfs:/","mc1:/")==TRANSFER_STACK_READY);assert(!resets);
 puts("PASS: normal slot-2 preflight precedes source loading; refused/incompatible stacks fail closed");
}
'''
if len(sys.argv)==1:
    copy=(root/'src/filer_copy.c').read_text()
    assert 'prepareTransferDeviceStacks(inPath, outPath) != TRANSFER_STACK_READY' in copy
    assert copy.index('prepareTransferDeviceStacks(inPath, outPath)')<copy.index('in_fd = genOpen(in, FIO_O_RDONLY);')
with tempfile.TemporaryDirectory(prefix='transfer-stack-') as folder:
    p=Path(folder);(p/'test.c').write_text(code);exe=p/'test.exe'
    subprocess.run([shutil.which('gcc'),'-std=c99','-Wall','-Wextra',str(p/'test.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=10)
