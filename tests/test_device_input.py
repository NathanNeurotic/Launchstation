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
static int pad_wait_calls;
static void waitAnyPadReady(void) { pad_wait_calls++; }
"""
padcode += pad[pad.index("static char padBuf_t"):pad.index("int simPadKB(void)")]
padcode += r"""
static int simPadKB(void) { new_pad=kb; kb=0; return new_pad!=0; }
static int readpad(void) { int r=readpad_no_KB(); return r&&new_pad?r:simPadKB(); }
static int readpad_noRepeat(void) {
    int r=readpad_noKBnoRepeat(); return r&&new_pad?r:simPadKB();
}
static u64 last_input_ms;
static u32 held_prev, text_repeat_armed;
static int dashboard_menus,dashboard_menu_open,dash_start_menu_blocked;
static void dashDashboardMenu(void) { dashboard_menus++; }
"""
padcode += function(read("src/dash_main.c"), "int dashDashboardMenuIsOpen(")
padcode += function(ui, "void dashInputPollEx(") + function(ui, "void dashInputFlush(")
padcode += r"""
int main(void) {
    DashInput in;
    connected[0]=connected[1]=3;
    dashInputPollEx(&in,0); dashInputFlush();
    assert(!in.any && pad_wait_calls==0 && now_ms==1000);
    connected[0]=1; connected[1]=0;
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
    /* START is consumed globally, even with another action held.
     * Holding it never reopens the popup through the repeat path. */
    kb=0;keys[0]=PAD_START|PAD_CROSS;clearPadPressState();held_prev=0;
    dashInputPollEx(&in,0);assert(dashboard_menus==1 && !in.any && !in.pressed && !in.raw_new);
    dashboard_menu_open=1;
    now_ms+=600;dashInputPollEx(&in,0);assert(dashboard_menus==1 && !in.any);
    /* A timed confirmation consumes START without opening the popup. */
    keys[0]=0;dashInputPollEx(&in,0);dashboard_menu_open=0;
    keys[0]=PAD_START;dash_start_menu_blocked=1;dashInputPollEx(&in,0);
    assert(dashboard_menus==1 && !in.any && !in.pressed);
    dash_start_menu_blocked=0;dashboard_menu_open=1;
    /* Release/repress closes via Back; the chord is consumed so Confirm does not leak. */
    keys[0]=0;dashInputPollEx(&in,0);keys[0]=PAD_START|PAD_CROSS;
    dashInputPollEx(&in,1);
    assert(dashboard_menus==1 && in.any && in.pressed==DASH_IN_BACK && !in.raw_new);
    dashboard_menu_open=0;dashInputFlush();
    keys[0]=0;dashInputPollEx(&in,0);keys[0]=PAD_START;
    dashInputPollEx(&in,1);assert(dashboard_menus==2 && !in.any && !in.raw_new);
    /* Text repeats are separate from one-press actions and disarmed by flush. */
    keys[0]=0;dashInputPollEx(&in,0);keys[0]=PAD_CROSS;swapKeys=1;
    dashInputPollEx(&in,0);assert(in.pressed==DASH_IN_OK && in.text_repeat==DASH_IN_OK);
    now_ms+=600;dashInputPollEx(&in,0);assert(!in.pressed && in.text_repeat==DASH_IN_OK);
    dashInputFlush();now_ms+=600;dashInputPollEx(&in,0);assert(!in.pressed && !in.text_repeat);
    keys[0]=0;dashInputPollEx(&in,0);keys[0]=PAD_CIRCLE;
    dashInputPollEx(&in,0);assert(in.pressed==DASH_IN_BACK && in.text_repeat==DASH_IN_BACK);
    now_ms+=600;dashInputPollEx(&in,0);assert(!in.pressed && in.text_repeat==DASH_IN_BACK);
    keys[0]=0;dashInputPollEx(&in,0);keys[0]=PAD_CROSS;swapKeys=0;
    dashInputPollEx(&in,0);assert(in.pressed==DASH_IN_BACK && in.text_repeat==DASH_IN_BACK);
    swapKeys=1;dashInputFlush();
    /* Held SELECT keeps START as the quick launch key (SELECT+START). */
    keys[0]=0;dashInputPollEx(&in,0);keys[0]=PAD_SELECT;dashInputPollEx(&in,0);
    keys[0]=PAD_SELECT|PAD_START;dashInputPollEx(&in,0);
    assert(dashboard_menus==2 && (in.pressed & DASH_IN_START) && (in.raw_new & PAD_START));
    keys[0]=0;dashInputPollEx(&in,0);clearPadPressState();held_prev=0;
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
symbols = sorted(set(re.findall(r"\b(?:DI_|DASH_SND_|SCECd)\w+", devices)))
devcode = common + device_types + "enum { " + ",".join(symbols) + " };\n"
devcode += "\n".join(re.findall(r"^#define DASH_DEVA_.*", header, re.M)) + "\n"
devcode += r"""
#define MMCE 1
#define MX4SIO 1
#define EXFAT 1
#define UDPFS 1
typedef void (*DashProbeDone)(const char *,int,void *);
static unsigned int reset_generation;
static int cdmode, uLE_cdmode, vmcMounted[2], console_is_PSX;
static unsigned int getIopResetGeneration(void) { return reset_generation; }
static char LaunchElfBootDir[256], LaunchElfDir[256], netConfig[32];
static struct { int dev_auto; int show_unavailable_devices; } dashCfg = {0,1};
static int mc_present[2], usb_mask, usb_loaded, resident, media, loads, opens, closes;
static int failed_load, reset_on_load, reset_on_type, usb_snapshot_valid=1;
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
    if(reset_on_load || reset_on_type==s) {
        reset_generation++; resident=0; usb_loaded=0; usb_snapshot_valid=0;
        reset_on_load=0; reset_on_type=0;
    }
    if(failed_load) return 0;
    resident|=s; return 1;
}
static int loadHddModules(void) { return load(S_HDD); }
static int loadAtaModules(void) { return load(S_ATA); }
static int loadMx4sioModules(void) { usb_snapshot_valid=0; return load(S_MX); }
static int loadMmceModules(void) { return load(S_MMCE0|S_MMCE1); }
static int load_udpfs(void) { return load(S_UDPFS); }
#define MMCE_CMD_PING 1
static int fileXioDevctl(const char *p,int cmd,void *arg,int len,void *out,int size) {
    assert(cmd==MMCE_CMD_PING && !arg && len==0 && !out && size==0);
    return (media & slot(p)) ? 0x100 : -1;
}
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
static int filerUsbDriveMask(void) { return usb_snapshot_valid?usb_mask:0; }
static int dashLoadingActive(void) { return 0; }
static void dashLoadingFrame(const char *s,int d,int n) { (void)s;(void)d;(void)n; }
static u64 usb_media_ready, usb_second_ready;
static int usb_scan_calls;
static int filerUsbRefreshDriveMask(void) {
    usb_scan_calls++;
    usb_snapshot_valid=1;
    if(usb_second_ready && now_ms>=usb_second_ready) usb_mask|=2;
    return now_ms>=usb_media_ready?usb_mask:0;
}
static int isUsbMassStackLoaded(void) { return usb_loaded; }
static void rebootIopAndReloadCoreStackSilent(void) { reset_generation++; resident=0; usb_loaded=0; }
int dashBootDeviceBit(void);
#define DPRINTF(...) ((void)0)
static void loadUsbModules(void) { usb_loaded=1; }
static int prepareUsbRootBrowse(void) { return usb_mask?0:-1; }
static int filerShowHddUnit(int p) { (void)p; return 1; }
static int filerShowAtaUnit(int p) { (void)p; return 1; }
static void dashSoundPlay(int i) { (void)i; }
static void dashAppsRebuildView(void) {}
static void dashStrCopy(char *d,const char *s,int n) { snprintf(d,n,"%s",s); }
static int dashWorkerAwaitAll(const char *t) { (void)t; return 1; }
static int startup_quiet;
static int isStartupDriverStatusQuiet(void) { return startup_quiet; }
static void setStartupDriverStatusQuiet(int quiet) { startup_quiet = quiet; }
static void dashLoadingBegin(const char *t) { (void)t; }
static void dashLoadingStatus(const char *t,int d,int n) { (void)t;(void)d;(void)n; }
static void (*drawMsgOverride)(const char *msg);
static void dashLoadingEnd(void) {}
static void dashDevicesAutoProbe(void);
static int boot_show_all_devices = 0;
static int boot_safe_mode = 0;
static int isPcCardCfPath(const char *p) { return p && !strncmp(p, "cf", 2); }
static int isPcCardCfOnline(void) { return 0; }
static int isPcCardCfEligible(void) { return 0; }
static int isPcCardCfBusBusy(void) { return 0; }
static int isPcCardCfLoaded(void) { return 0; }
static int getPcCardCfCapacitySectors(u32 *s) { (void)s; return -1; }
static int loadPcCardCfModules(void) { return 0; }
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
    /* An absent/late USB disk must never reset unrelated IOP services. */
    strcpy(LaunchElfBootDir,"mc0:/BOOT/"); resident=63; usb_loaded=1;
    unsigned int gen=reset_generation;
    probe("usb:"); probe("usb:");
    assert(!result && reset_generation==gen && resident==63);
    /* With neither port mounted we must not hit a blocking IOP RPC twenty
     * times; the production probe is limited to four scans per request. */
    assert(usb_scan_calls==8);
    usb_scan_calls=0;
    usb_mask=1; usb_media_ready=now_ms+1800; probe("usb:");
    assert(result && reset_generation==gen && resident==63);
    assert(usb_scan_calls==4); /* preserve late-media acceptance */
    usb_scan_calls=0;
    usb_mask=1; usb_media_ready=0; usb_second_ready=now_ms+1000;
    probe("usb:");
    assert(result && usb_mask==3 && now_ms>=usb_second_ready);
    assert(usb_scan_calls==3); /* second device can arrive a second later */
    usb_scan_calls=0;
    usb_mask=1; usb_second_ready=0;
    probe("usb:"); assert(result && usb_scan_calls==3);
    usb_scan_calls=0;
    usb_second_ready=0;
    usb_media_ready=0; usb_mask=0;
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
    reset_generation++; resident=0;
    for(int i=0;i<6;i++) assert(state(mounts[i])==DASH_DEV_PROBE);
    /* Observe a reboot performed by loading the target, before recording it. */
    reset_on_load=1; probe("mmce1:"); assert(result && state("mmce1:")==DASH_DEV_READY);
    /* BDM first-open settles without exhausting IO handles or busy spinning. */
    delayed_ata=now_ms+300; probe("ata0:"); assert(result && now_ms>=delayed_ata);
    failed_load=1; probe("udpfs:"); assert(!result && state("udpfs:")==DASH_DEV_ABSENT);
    failed_load=0;
    reset_generation++; resident=S_MMCE0|S_MMCE1;
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
waitcode = common + r"""
enum { PAD_STATE_STABLE=1, PAD_STATE_FINDCTP1=2, PAD_STATE_DISCONN=0 };
static int states[2]={3,3}, settle;
static int pad_reopen_active=1;
static int legacy_reads;
static int padGetState(int p,int s) {
    (void)s;
    if(!pad_reopen_active && states[p]==3 && ++legacy_reads>=4) return PAD_STATE_STABLE;
    return settle && now_ms>=1500 ? PAD_STATE_STABLE : states[p];
}
static void padStateInt2String(int s,char *out) { (void)s; out[0]=0; }
static void DelayThread(int us) { now_ms+=us/1000; }
"""
waitcode += function(pad, "void waitPadReady(") + function(pad, "void waitAnyPadReady(")
waitcode += r"""
int main(void) {
    waitPadReady(0,0); assert(now_ms==2000);
    states[0]=PAD_STATE_DISCONN; waitAnyPadReady(); assert(now_ms==2000);
    states[0]=PAD_STATE_STABLE; pad_reopen_active=0; waitPadReady(0,0); assert(now_ms==2000);
    pad_reopen_active=1;
    now_ms=1000; states[0]=3; settle=1; waitPadReady(0,0); assert(now_ms==1500);
    now_ms=1000; settle=0; pad_reopen_active=0; legacy_reads=0;
    waitPadReady(0,0); assert(now_ms==1000 && legacy_reads==4);
    puts("PASS: runtime pad wait yields/expires; legacy ready/disconnected handling is preserved");
}
"""
autocode = devcode[:devcode.index("int main(void)")] + r"""
int main(void) {
    strcpy(LaunchElfBootDir,"mc0:/BOOT/");
    dashCfg.dev_auto=DASH_DEVA_USB|DASH_DEVA_HDD|DASH_DEVA_UDPFS;
    usb_mask=1; media=S_HDD|S_UDPFS; reset_on_type=S_UDPFS;
    dashDevicesAutoProbe();
    assert(usb_loaded && filerUsbDriveMask()==1);
    assert((resident&(S_HDD|S_UDPFS))==(S_HDD|S_UDPFS));
    resident=0; usb_loaded=1; usb_snapshot_valid=1;
    dashCfg.dev_auto=DASH_DEVA_USB|DASH_DEVA_MX4SIO; media=S_MX;
    dashDevicesAutoProbe();
    assert((resident&S_MX) && usb_loaded && filerUsbDriveMask()==1);
    puts("PASS: startup restores devices lost to a later reset and refreshes USB after MX4SIO invalidation");
}
"""
art = read("src/dash_art.c")
themecode = common + r"""
#include <stdlib.h>
#define ART_SLOTS 8
#define THEME_SLOTS 8
#define DASH_PATH_MAX 256
#define DASH_JOB_THEME 1
#define DASH_JOB_VISIBLE 2
typedef int GSTEXTURE;
enum { TS_EMPTY,TS_LOADED,TS_MISSING,TS_LOADING };
typedef struct { char path[256]; GSTEXTURE tex; int state,gen,max_w,max_h; } ThemeSlot;
typedef struct { char path[256]; int slot,gen,max_w,max_h; } ThemeTexJob;
static ThemeSlot theme_slots[THEME_SLOTS];
static int loading=1, submitted;
static void *submitted_arg;
static int dashLoadingActive(void) { return loading; }
static int isPathDeviceStackLoaded(const char *p) { (void)p; return 1; }
static void dashStrCopy(char *d,const char *s,int n) { snprintf(d,n,"%s",s); }
static void themeTexRun(void *a) { (void)a; }
static void themeTexDone(void *a) { (void)a; }
static int dashWorkerSubmit(const char *s,int f,void (*r)(void *),void (*d)(void *),void *a) {
    (void)s;(void)f;(void)r;(void)d; submitted++; submitted_arg=a; return 1;
}
"""
themecode += function(art, "GSTEXTURE *dashThemeTextureFile(")
themecode += r"""
int main(void) {
    assert(!dashThemeTextureFile("mc0:/THEME/new.png",64,64) && !submitted);
    strcpy(theme_slots[0].path,"mc0:/THEME/cached.png"); theme_slots[0].state=TS_LOADED; theme_slots[0].max_w=theme_slots[0].max_h=64;
    assert(dashThemeTextureFile("mc0:/THEME/cached.png",64,64)==&theme_slots[0].tex);
    loading=0; assert(!dashThemeTextureFile("mc0:/THEME/new.png",64,64) && submitted==1);
    free(submitted_arg);
    puts("PASS: loading overlays use cached theme textures and defer new IO until loading ends");
}
"""
with tempfile.TemporaryDirectory(prefix="wle-device-input-") as tmp:
    for name, code in (("input", padcode), ("devices", devcode), ("pad-waits", waitcode), ("startup", autocode), ("theme-io", themecode)):
        source = Path(tmp) / f"{name}.c"
        exe = Path(tmp) / f"{name}.exe"
        source.write_text("#define localizeDashboardText(s) (s)\n" + code)
        try:
            subprocess.run([compiler, "-std=gnu11", "-O1", str(source), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=5)
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
            failures.append(name)
if failures:
    raise SystemExit("FAILED: " + ", ".join(failures))
