"""Execute production controller and storage probes with mocked hardware.

Optional git ref runs the same assertions against a baseline. This verifies
event/state handling, not PS2 controller latency or a console storage crash.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]


def read(path):
    if len(sys.argv) > 1:
        return subprocess.check_output(["git", "show", f"{sys.argv[1]}:{path}"],
                                       cwd=root, text=True)
    return (root / path).read_text()


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


common = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
static u64 now_ms = 1000;
static u64 Timer(void) { return now_ms; }
"""
header = read("include/dash_internal.h")
pad = read("src/pad.c")
ui = read("src/dash_ui.c")
inputs = header[header.rfind("enum {", 0, header.index("DASH_IN_UP")):
                header.index("void dashInputPoll(DashInput")]
padcode = common + inputs + r"""
enum { PAD_STATE_STABLE=1, PAD_STATE_FINDCTP1=2, PAD_STATE_DISCONN=0 };
enum { PAD_SELECT=1, PAD_L3=2, PAD_R3=4, PAD_START=8,
       PAD_UP=16, PAD_RIGHT=32, PAD_DOWN=64, PAD_LEFT=128,
       PAD_L2=256, PAD_R2=512, PAD_L1=1024, PAD_R1=2048,
       PAD_TRIANGLE=4096, PAD_CIRCLE=8192, PAD_CROSS=16384, PAD_SQUARE=32768 };
#define PAD_R3_V0 0x010000
#define PAD_R3_V1 0x020000
#define PAD_R3_H0 0x040000
#define PAD_R3_H1 0x080000
#define PAD_L3_V0 0x100000
#define PAD_L3_V1 0x200000
#define PAD_L3_H0 0x400000
#define PAD_L3_H1 0x800000
struct padButtonStatus { u32 btns; int rjoy_h,rjoy_v,ljoy_h,ljoy_v; };
static int connected[2]={1,0}, good[2]={1,1}, keys[2], kb, busy, swapKeys;
static int left_v=0x80;
static int padGetState(int p,int s) { (void)s; return connected[p]; }
static int padRead(int p,int s,struct padButtonStatus *b) {
    (void)s; b->btns=0xffff ^ keys[p];
    b->rjoy_h=b->rjoy_v=b->ljoy_h=0x80; b->ljoy_v=left_v; return good[p];
}
static void dashWorkerParkPoint(void) {}
static int dashIopParked(void) { return 0; }
static int dashWorkerIoBusy(void) { return busy; }
static void waitAnyPadReady(void) {}
"""
padcode += pad[pad.index("static char padBuf_t"):pad.index("int simPadKB(void)")]
padcode += r"""
static int simPadKB(void) { new_pad=kb; kb=0; return new_pad!=0; }
static int readpad(void) { int r=readpad_no_KB(); return r&&new_pad?r:simPadKB(); }
static int readpad_noRepeat(void) {
    int r=readpad_noKBnoRepeat(); return r&&new_pad?r:simPadKB();
}
static u64 last_input_ms;
static u32 held_prev;
"""
padcode += function(ui, "void dashInputPollEx(") + function(ui, "void dashInputFlush(")
padcode += r"""
int main(void) {
    DashInput in;
    /* A successful press followed by failed reads must not replay it. */
    keys[0]=PAD_TRIANGLE;
    assert(readpad_no_KB() && new_pad==PAD_TRIANGLE);
    good[0]=0; assert(!readpad_no_KB() && new_pad==0 && paddata==0);
    good[0]=1; clearPadPressState();
    assert(readpad_noKBnoRepeat() && new_pad==PAD_TRIANGLE);
    good[0]=0; assert(!readpad_noKBnoRepeat() && new_pad==0 && paddata==0);
    connected[0]=0; assert(!readpad_no_KB() && !readpad_noKBnoRepeat());
    /* Port 2 works on its own; no stale held bits survive port 1. */
    connected[1]=1; keys[1]=PAD_DOWN;
    assert(readpad_no_KB() && new_pad==PAD_DOWN && paddata==PAD_DOWN);
    connected[1]=0; connected[0]=1; good[0]=1;
    /* no-repeat/flush must give a new held key its full repeat delay. */
    keys[0]=0; now_ms=2000; readpad_no_KB();
    now_ms=3000; keys[0]=PAD_UP; readpad_noKBnoRepeat();
    now_ms=3016; assert(readpad_no_KB() && new_pad==0);
    now_ms=3401; assert(readpad_no_KB() && new_pad==PAD_UP);
    clearPadPressState(); held_prev=0;
    keys[0]=PAD_TRIANGLE; now_ms=4000; dashInputPollEx(&in,1);
    assert(in.pressed==DASH_IN_MENU);
    now_ms=4600; dashInputPollEx(&in,1); assert(!in.any);
    keys[0]=0; dashInputPollEx(&in,1);
    keys[0]=PAD_TRIANGLE; dashInputPollEx(&in,1); assert(in.pressed==DASH_IN_MENU);
    /* A menu flush suppresses held actions, while directions still repeat. */
    dashInputFlush(); now_ms=5200; dashInputPollEx(&in,1); assert(!in.any);
    keys[0]=PAD_DOWN; dashInputPollEx(&in,1); assert(in.pressed==DASH_IN_DOWN);
    now_ms+=401; dashInputPollEx(&in,1); assert(in.pressed==DASH_IN_DOWN);
    keys[0]=PAD_CROSS; swapKeys=1; dashInputPollEx(&in,1); assert(in.pressed==DASH_IN_OK);
    now_ms+=600; dashInputPollEx(&in,1); assert(!in.any);
    keys[0]=0; dashInputPollEx(&in,1);
    kb=PAD_CROSS; dashInputPollEx(&in,1); assert(in.pressed==DASH_IN_OK);
    kb=PAD_CROSS; dashInputPollEx(&in,1); assert(in.pressed==DASH_IN_OK);
    busy=1; kb=PAD_CROSS; dashInputPollEx(&in,1); assert(!in.any && kb);
    busy=0; dashInputPollEx(&in,0); assert(!in.any && kb);
    /* A tilted stick plus Triangle must not alternate held button masks. */
    kb=0; clearPadPressState(); held_prev=0; padtype_t[0]=2;
    left_v=0xff; keys[0]=PAD_TRIANGLE; now_ms=6000;
    dashInputPollEx(&in,0); assert(in.pressed==(DASH_IN_DOWN|DASH_IN_MENU));
    for(int i=0;i<20;i++) {
        now_ms+=16; dashInputPollEx(&in,0); assert(!in.any);
    }
    now_ms=6401; dashInputPollEx(&in,0); assert(in.pressed==DASH_IN_DOWN);
    puts("PASS: failed/disconnected reads, both ports, flush timing, held actions, navigation, keyboard");
}
"""

devices = read("src/dash_devices.c")
device_types = header[header.index("enum DashDevState"):
                      header.index("void dashDevicesInit(void)")]
symbols = sorted(set(re.findall(r"\b(?:DI_|DASH_SND_|DASH_DEVA_|SCECd)\w+", devices)))
devcode = common + device_types + "enum { " + ",".join(symbols) + " };\n"
devcode += r"""
#define MMCE 1
#define MX4SIO 1
#define EXFAT 1
#define UDPFS 1
typedef void (*DashProbeDone)(const char *,int,void *);
static int _iop_reboot_count, cdmode, uLE_cdmode, vmcMounted[2], console_is_PSX;
static char LaunchElfBootDir[256], LaunchElfDir[256], netConfig[32];
static struct { int dev_auto; int show_unavailable_devices; } dashCfg = {0,1};
static int mc_present[2], usb_mask, usb_loaded, resident, media, loads, opens, closes;
static int failed_load, reset_on_load;
static u64 delayed_ata;
enum { S_HDD=1, S_ATA=2, S_MX=4, S_MMCE0=8, S_MMCE1=16, S_UDPFS=32 };
static int slot(const char *p) {
    if(!strncmp(p,"hdd",3)) return S_HDD;
    if(!strncmp(p,"ata",3)) return S_ATA;
    if(!strncmp(p,"mx4sio",6)) return S_MX;
    if(!strncmp(p,"mmce",4)) return p[4]=='1'?S_MMCE1:S_MMCE0;
    if(!strncmp(p,"udpfs",5)) return S_UDPFS;
    return 0;
}
static int isPathDeviceStackLoaded(const char *p) {
    int s=slot(p);
    return (s&(S_MMCE0|S_MMCE1))?(resident&(S_MMCE0|S_MMCE1))!=0:(resident&s)!=0;
}
static int load(int s) {
    loads++;
    if(reset_on_load) { _iop_reboot_count++; resident=0; reset_on_load=0; }
    if(failed_load) return 0;
    resident|=s; return 1;
}
static int loadHddModules(void) { return load(S_HDD); }
static int loadAtaModules(void) { return load(S_ATA); }
static int loadMx4sioModules(void) { return load(S_MX); }
static int loadMmceModules(void) { return load(S_MMCE0|S_MMCE1); }
static int load_udpfs(void) { return load(S_UDPFS); }
static int fileXioDopen(const char *p) {
    opens++; int s=slot(p);
    return (media&s) && (s!=S_ATA || now_ms>=delayed_ata)?0:-1;
}
static void fileXioDclose(int dd) { assert(dd==0); closes++; }
static void DelayThread(int us) { now_ms+=us/1000; }
static void pollRootMemoryCardDevices(void) {}
static void filerRootMcPollIdle(void) {}
static int filerRootMcPresent(int p,int *t,int *f) {
    if(t)*t=2; if(f)*f=1024; return mc_present[p];
}
static int filerUsbDriversLoaded(void) { return usb_loaded; }
static int filerUsbDriveMask(void) { return usb_mask; }
static int isUsbMassStackLoaded(void) { return usb_loaded; }
static void loadUsbModules(void) { usb_loaded=1; }
static int prepareUsbRootBrowse(void) { return usb_mask?0:-1; }
static int filerShowHddUnit(int p) { (void)p; return 1; }
static int filerShowAtaUnit(int p) { (void)p; return 1; }
static void dashSoundPlay(int i) { (void)i; }
static void dashAppsRebuildView(void) {}
static void dashStrCopy(char *d,const char *s,int n) { snprintf(d,n,"%s",s); }
static int dashWorkerAwaitAll(const char *t) { (void)t; return 1; }
static void dashLoadingBegin(const char *t) { (void)t; }
static void dashLoadingStatus(const char *t,int d,int n) { (void)t;(void)d;(void)n; }
static void dashLoadingEnd(void) {}
static void dashDevicesAutoProbe(void);
"""
devcode += re.sub(r"^#include[^\n]*\n", "", devices, flags=re.M)
devcode += r"""
static int state(const char *mount) {
    DashDevice ds[DASH_MAX_DEVICES]; int n=dashDevicesList(ds,DASH_MAX_DEVICES);
    for(int i=0;i<n;i++) if(!strcmp(ds[i].mount,mount)) return ds[i].state;
    return -1;
}
static int result;
static void done(const char *m,int ok,void *u) { (void)m;(void)u; result=ok; }
static void probe(const char *m) {
    DashDevice d={0}; strcpy(d.mount,m); strcpy(d.name,"test");
    assert(dashDevicesStart(&d,done,NULL)==0);
}
int main(void) {
    dashDevicesInit(); resident=63;
    const char *mounts[]={"hdd0:","ata0:","mx4sio:","mmce0:","mmce1:","udpfs:"};
    /* Resident drivers on an empty setup must not claim that storage exists. */
    for(int i=0;i<6;i++) assert(state(mounts[i])==DASH_DEV_PROBE);
    assert(loads==0 && opens==0);
    for(int i=0;i<6;i++) {
        probe(mounts[i]); assert(!result && state(mounts[i])==DASH_DEV_ABSENT);
        assert(isPathDeviceStackLoaded(mounts[i]));
        assert(!dashDevicePathReady(mounts[i]));
    }
    media=S_MMCE1; probe("mmce1:");
    assert(result && state("mmce1:")==DASH_DEV_READY && state("mmce0:")==DASH_DEV_ABSENT);
    media=63;
    for(int i=0;i<6;i++) { probe(mounts[i]); assert(result && state(mounts[i])==DASH_DEV_READY); }
    assert(closes>0); /* fd zero and empty roots are valid */
    _iop_reboot_count++; resident=0;
    for(int i=0;i<6;i++) assert(state(mounts[i])==DASH_DEV_PROBE);
    /* Observe a reboot performed by loading the target, before recording it. */
    reset_on_load=1; probe("mmce1:"); assert(result && state("mmce1:")==DASH_DEV_READY);
    /* BDM first-open settles without exhausting IO handles or busy spinning. */
    delayed_ata=now_ms+300; probe("ata0:"); assert(result && now_ms>=delayed_ata);
    failed_load=1; probe("udpfs:"); assert(!result && state("udpfs:")==DASH_DEV_ABSENT);
    failed_load=0;
    _iop_reboot_count++; resident=S_MMCE0|S_MMCE1;
    strcpy(LaunchElfBootDir,"mmce1:/BOOT/"); dashDevicesInit();
    assert(state("mmce1:")==DASH_DEV_READY && state("mmce0:")==DASH_DEV_PROBE);
    strcpy(LaunchElfBootDir,"usb0:/BOOT/"); strcpy(LaunchElfDir,"mc0:/SYS-CONF/");
    assert(dashBootDeviceBit()==DASH_DEVA_USB);
    mc_present[0]=-1; assert(!dashMcPresent(0));
    mc_present[0]=1; assert(dashMcPresent(0));
    DashDevice ds[DASH_MAX_DEVICES]; dashDevicesList(ds,DASH_MAX_DEVICES);
    assert(strstr(ds[0].detail,"1.00 MB free"));
    int before=opens; dashDevicesList(ds,DASH_MAX_DEVICES); assert(opens==before);
    puts("PASS: driver-only/failed/empty/late roots, MMCE units, reboot invalidation, boot source, no draw IO");
}
"""

compiler = shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("Host gcc or clang required")
failures = []
with tempfile.TemporaryDirectory(prefix="wle-device-input-") as tmp:
    for name, code in (("input", padcode), ("devices", devcode)):
        source = Path(tmp) / f"{name}.c"
        exe = Path(tmp) / f"{name}.exe"
        source.write_text(code)
        try:
            subprocess.run([compiler, "-std=gnu11", "-O1", str(source), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
        except subprocess.CalledProcessError:
            failures.append(name)
if failures:
    raise SystemExit("FAILED: " + ", ".join(failures))
