"""Production transfer preflight must retire MX4SIO before opening a source,
and must refuse a pair whose drivers cannot stay loaded together.

This models the exact invalidation boundaries, not a console/network transfer:
retiring MX4SIO for mc1:, and switching between the Ethernet network stack
(host:, smb:, smb1:) and the UDPFS one, each reboots the IOP and unloads every
driver. A pair that needs both network stacks is refused before any driver is
loaded; any other pair the preflight accepts has both drivers loaded at the end,
whichever network stack ran before it, even when loading the destination's
stack rebooted the IOP after the source's was loaded.
Optional git ref demonstrates that an old preflight fails these checks.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parents[1]
def read(path):
    if len(sys.argv)>1:
        return subprocess.check_output(['git','show',f'{sys.argv[1]}:{path}'],cwd=root,text=True)
    return (root/path).read_text()
def function(source, signature):
    a=source.index(signature);b=source.index('{',a)+1;depth=1
    while depth:
        depth+=(source[b]=='{')-(source[b]=='}');b+=1
    return source[a:b]+'\n'
source=read('src/filer_device.c')
# Compile the actual complete path/transfer policy with narrow driver fixtures.
source=source[source.index('enum {'):]
code=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MX4SIO 1
#define MMCE 1
#define UDPFS 1
#define ETH 1
#define SMB 1
#define TRUE 1
#define FALSE 0
#define TRANSFER_STACK_READY 0
#define TRANSFER_STACK_LOAD_FAILED -1
#define TRANSFER_STACK_INCOMPATIBLE -2
#define SMB_DEVICE_V2 "smb:"
#define SMB_DEVICE_V1 "smb1:"
#define SMB_DEVICE_NFS "nfs:"
enum {SMB_PATH_NONE=0,SMB_PATH_V1=1,SMB_PATH_V2=2,SMB_PATH_NFS=3};
enum {NET_NONE,NET_ETH,NET_UDPFS};
static int mx4sio_driver_running, reset_allowed=1, resets, loads, open_handles;
static int network,usb,USB_mass_loaded,mmce,host_ready,smb_ready,net_stack;
/* A made-up pair of stacks that reboot the IOP to load beside each other. */
static int mmce_displaces_udpfs;
int host_error;
/* Each IOP reboot advances the generation, as Reset() does. (Not static, so
 * a revision that does not read it still compiles cleanly.) */
unsigned int getIopResetGeneration(void){return (unsigned int)resets;}
/* An IOP reboot unloads every driver, network stacks included. */
static void iop_reset(void){
 assert(!open_handles);resets++;
 mx4sio_driver_running=network=usb=USB_mass_loaded=mmce=host_ready=smb_ready=0;net_stack=NET_NONE;
}
/* switchNetworkStack(): the other network stack only loads after a reboot. */
static void network_stack(int want){if(net_stack!=NET_NONE&&net_stack!=want)iop_reset();net_stack=want;}
static int isPcCardCfPath(const char *p){return p&&!strncmp(p,"cf",2);}
static int loadPcCardCfModules(void){return 1;}
static int isPcCardCfOnline(void){return 1;}
static int loadHddModules(void){loads++;return 1;}
static int loadMmceModules(void){loads++;if(mmce_displaces_udpfs&&network)iop_reset();mmce=1;return 1;}
static int loadMx4sioModules(void){loads++;mx4sio_driver_running=1;return 1;}
static int load_udpfs(void){loads++;if(mmce_displaces_udpfs&&mmce)iop_reset();network_stack(NET_UDPFS);network=1;return 1;}
static void initHOST(void){loads++;network_stack(NET_ETH);host_ready=1;host_error=0;}
static int isSmbDriverLoaded(int v){(void)v;return smb_ready;}
static int load_smb(int v){(void)v;loads++;network_stack(NET_ETH);smb_ready=1;return 1;}
static void loadUsbModules(void){loads++;USB_mass_loaded=usb=1;}
static void loadCdModules(void){loads++;}
static int isPathDeviceStackLoaded(const char *p){
 if(!strncmp(p,"mx4sio",6))return mx4sio_driver_running;
 return 1;
}
static void ensureMemoryCardPortAccessible(int p){
 assert(p==1);
 if(mx4sio_driver_running&&reset_allowed)iop_reset();
}
'''+function(read('src/smb_cnf.c'),'int smbPathKind(')+source+r'''
static void reset(void){mx4sio_driver_running=1;network=usb=1;mmce=host_ready=smb_ready=0;net_stack=NET_NONE;loads=resets=open_handles=0;reset_allowed=1;}
static int is_udpfs(const char *p){return !strncmp(p,"udpfs",5);}
static int is_ethernet(const char *p){return !strncmp(p,"host",4)||smbPathKind(p)!=SMB_PATH_NONE;}
/* Whether a device's driver is loaded now, in this fixture. */
static int loaded(const char *p){
 if(!strncmp(p,"host",4))return host_ready;
 if(smbPathKind(p)!=SMB_PATH_NONE)return smb_ready;
 if(is_udpfs(p))return network;
 if(!strncmp(p,"usb",3))return usb;
 if(!strncmp(p,"mmce",4))return mmce;
 return 1; /* mc0: needs no driver here */
}
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

 /* host:, smb: and smb1: run on the Ethernet network stack and udpfs: on
  * its own. Whichever network stack is already running, a pair needing both
  * is refused with no driver load and no IOP reset. Every other pair, both
  * ways, ends with both of its drivers loaded, so its files can be opened:
  * with no reset when no network stack ran, else with at most the one that
  * switches it, even when that comes with the destination (usb0: -> host:
  * after udpfs: ran) and unloads the source's drivers. */
 const char *devices[]={"host:/","smb:/SHARE/","smb1:/","udpfs:/","mc0:/","usb0:/","mmce0:/"};
 const int count=sizeof(devices)/sizeof(devices[0]);
 int refused=0,accepted=0;
 for(int stack=NET_NONE;stack<=NET_UDPFS;stack++)for(int a=0;a<count;a++)for(int b=0;b<count;b++){
  const char *src=devices[a],*dst=devices[b];
  int conflict=(is_udpfs(src)&&is_ethernet(dst))||(is_ethernet(src)&&is_udpfs(dst));
  if(a==b)continue;
  reset();mx4sio_driver_running=network=usb=0;net_stack=stack;
  int r=prepareTransferDeviceStacks(src,dst);
  if(conflict){
   if(r!=TRANSFER_STACK_INCOMPATIBLE||loads||resets||net_stack!=stack){
    fprintf(stderr,"FAIL %s -> %s (network stack %d): result %d, %d loads, %d IOP resets; "
            "expected INCOMPATIBLE with no load or reset\n",src,dst,stack,r,loads,resets);
    return 1;
   }
   refused++;
  }else{
   if(r!=TRANSFER_STACK_READY||resets>(stack!=NET_NONE)||!loaded(src)||!loaded(dst)){
    fprintf(stderr,"FAIL %s -> %s (network stack %d): result %d, %d IOP resets, source driver %s, destination driver %s\n",
            src,dst,stack,r,resets,loaded(src)?"loaded":"unloaded",loaded(dst)?"loaded":"unloaded");
    return 1;
   }
   accepted++;
  }
 }
 assert(refused==3*6&&accepted==3*(count*(count-1)-6));
 /* Two stacks that each reboot the IOP to load beside the other cannot both
  * stay loaded: the source's second load unloads the destination again. */
 reset();mx4sio_driver_running=network=usb=0;mmce_displaces_udpfs=1;
 int displaced=prepareTransferDeviceStacks("mmce0:/","udpfs:/");mmce_displaces_udpfs=0;
 if(displaced!=TRANSFER_STACK_INCOMPATIBLE){
  fprintf(stderr,"FAIL mmce0:/ -> udpfs:/ whose stacks displace each other: result %d, %d IOP resets, mmce %s, udpfs %s; "
          "expected INCOMPATIBLE\n",displaced,resets,mmce?"loaded":"unloaded",network?"loaded":"unloaded");
  return 1;
 }
 /* host: and smb: share the Ethernet stack: still allowed after udpfs: ran,
  * where the first load's one reset comes before either driver is loaded. */
 for(int reverse=0;reverse<2;reverse++){
  reset();mx4sio_driver_running=network=usb=0;net_stack=NET_UDPFS;
  assert(prepareTransferDeviceStacks(reverse?"smb:/SHARE/":"host:/",reverse?"host:/":"smb:/SHARE/")==TRANSFER_STACK_READY);
  assert(resets==1&&host_ready&&smb_ready&&net_stack==NET_ETH);
 }
 printf("PASS: normal slot-2 preflight precedes source loading; refused/incompatible stacks fail closed; "
        "%d network pairs refused before loading, %d pairs ready with both drivers loaded\n",refused,accepted);
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
