"""Execute production optional BGM loading/lifetime with mocked IO/audsrv."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_sound.c").read_text()


def function(signature, text=source):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


# Both build paths must exclude the track; CI still offers its loose asset.
assert "EMBED_FILE(music_adpcm" not in (root / "src/embed_blobs.S").read_text()
assert "music_adpcm.o" not in (root / "Makefile").read_text()
assert "music_adpcm.s:" not in (root / "embed.make").read_text()
assert (root / "res/music.sad").is_file()
track = (root / "res/music.sad").read_bytes()
assert track[:8] == b"APCM\x01\x01\x01\x00" and len(track) % 16 == 0

code = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef struct { void *buffer; } audsrv_adpcm_t;
#define MAX_PATH 256
#define BGM_MAX_BYTES (1024*1024)
#define DASH_BGM_CHANNEL 23
#define SND_CURSOR_CH_COUNT 6
#define FIO_O_RDONLY 1
#define DASH_TOAST_WARN 1
static struct { int sound,bgm,bgm_vol,sound_vol; } dashCfg={1,1,80,60};
static audsrv_adpcm_t bgm_sample;
static int bgm_loaded,bgm_tried,bgm_channel=-1,sound_ready=1;
static void *bgm_data;
static unsigned int reset_generation,sound_reboot_generation,sound_tried_generation=(unsigned int)-1;
static int sound_retry;
static char LaunchElfBootDir[MAX_PATH]="usb0:/BOOT/",LaunchElfDir[MAX_PATH]="mc0:/SYS-CONF/";
static int exists=1,loaded=1,busy,parked,opens,uploads,plays,volumes,quit_calls,hard_stops,allocs;
static int size=64,bytes=64,pos,close_error,upload_error;
static u8 file[64];
static char opened[MAX_PATH];
static unsigned int getIopResetGeneration(void) { return reset_generation; }
static int soundAlive(void) { return sound_ready && sound_reboot_generation==reset_generation; }
static int dashWorkerIoBusy(void) { return busy; }
static int dashIopParked(void) { return parked; }
static int isPathDeviceStackLoaded(const char *p) {
    assert(!strcmp(p,LaunchElfBootDir[0]?LaunchElfBootDir:LaunchElfDir)); return loaded;
}
static void filerRootMcPollIdle(void) {}
static int genFixPath(const char *p,char *out) { strcpy(out,p); return 0; }
static int genOpen(const char *p,int mode) {
    assert(mode==FIO_O_RDONLY); opens++; strcpy(opened,p); pos=0; return exists?0:-1;
}
static int genLseek(int fd,int offset,int mode) {
    assert(fd==0 && offset==0); if(mode==SEEK_END) return size; pos=0; return 0;
}
static int genRead(int fd,void *dst,int n) {
    assert(fd==0); if(pos>=bytes)return 0;
    if(n>16)n=16; if(n>bytes-pos)n=bytes-pos; memcpy(dst,file+pos,n); pos+=n; return n;
}
static int genClose(int fd) { assert(fd==0); return close_error?-1:0; }
static void *allocate(int a,int n) { assert(a==64); allocs++; return calloc(1,n); }
static void release(void *p) { if(p)allocs--; free(p); }
#define memalign allocate
#define free release
static int audsrv_load_adpcm(audsrv_adpcm_t *s,void *p,int n) {
    assert(n==64 && !memcmp(p,"APCM",4)); uploads++;
    /* A real BGM voice MUST have SPU2 start, repeat and repeat-end flags.
       The original fixture deliberately has no frame flags to catch
       accidental dependence on whichever encoder produced the sidecar. */
    const unsigned char *b=(const unsigned char *)p;
    assert(b[6]==1 && b[17]==6 && b[33]==2 && b[49]==3);
    if(upload_error)return -1; s->buffer=p; return 0;
}
static int audsrv_ch_play_adpcm(int ch,audsrv_adpcm_t *s) {
    assert(ch==23 && s->buffer==bgm_data && bgm_data); plays++; return ch;
}
static void audsrv_adpcm_set_volume_and_pan(int ch,int v,int p) {
    assert(ch>=0 && ch<=23 && v>=0 && v<=100 && p==0); volumes++;
}
static int audsrv_adpcm_init(void) { hard_stops++; return 0; }
static int audsrv_stop_audio(void) { hard_stops++; return 0; }
static void audsrv_quit(void) { quit_calls++; }
static void dashToast(int k,const char *s) { (void)k; (void)s; }
static void dashBgmStart(void);
static void dashBgmStop(void);
static void dashSoundShutdown(void);
static int dashSoundInit(void) {
    if(!soundAlive())dashSoundShutdown(); sound_ready=1;
    sound_reboot_generation=reset_generation; return 1;
}
"""
for signature in ("static int prepareBgmLoop(", "static int loadBgmSidecar(", "void dashBgmSetVolume(",
                  "void dashBgmStop(", "void dashBgmStart(",
                  "void dashSoundShutdown(", "void dashSoundMaintain(",
                  "void dashSoundSuspend(", "void dashSoundApplyVolume("):
    code += function(signature)
code += r"""
static void reset(void) {
    dashSoundShutdown(); assert(!allocs);
    sound_ready=1; sound_reboot_generation=reset_generation;
    exists=loaded=1; busy=parked=close_error=upload_error=0;
    size=bytes=64; dashCfg.sound=dashCfg.bgm=1;
    memset(file,0,sizeof(file)); memcpy(file,"APCM",4);
    file[4]=file[5]=file[6]=1; file[8]=0xac; file[9]=3;
    opens=uploads=plays=volumes=hard_stops=0;
}
int main(void) {
    reset(); dashBgmStart();
    assert(bgm_loaded && bgm_channel==23 && allocs==1 && opens==1 && uploads==1 && plays==1);
    assert(file[17]==0 && file[33]==0 && file[49]==0); /* never mutate the sidecar */
    assert(!strcmp(opened,"usb0:/BOOT/BGM.ADP")); /* not the imported config's folder */
    for(int i=0;i<20;i++)dashSoundMaintain(); assert(opens==1 && plays==1);
    /* Returning from a failed launch must not quit/reload a resident server. */
    int quits=quit_calls;
    dashSoundSuspend(); dashSoundMaintain(); dashSoundApplyVolume(); dashBgmStart();
    assert(quit_calls==quits && !hard_stops && opens==1 && uploads==1 && plays==1);
    assert(soundAlive() && allocs==1 && bgm_channel==23);
    dashCfg.bgm=0; dashBgmStop(); dashCfg.bgm=1; dashBgmStart();
    assert(opens==1 && plays==1 && volumes>1);
    /* Missing file is silent, attempted once, and explicit off/on retries. */
    reset(); exists=0; dashBgmStart();
    for(int i=0;i<20;i++)dashSoundMaintain(); assert(opens==1 && !uploads && sound_ready);
    exists=1; dashBgmStop(); dashBgmStart(); assert(opens==2 && plays==1);
    /* Startup/current worker IO defers the optional read, without loading drivers. */
    reset(); busy=1; dashBgmStart(); assert(!opens && !bgm_tried);
    busy=0; loaded=0; dashSoundMaintain(); assert(!opens && !bgm_tried);
    loaded=1; dashSoundMaintain(); assert(opens==1 && plays==1);
    reset(); parked=1; dashBgmStart(); assert(!opens); parked=0;
    /* Incomplete or malformed files never reach the audio server. */
    for(int i=0;i<9;i++) {
        reset();
        switch(i) {
            case 0: bytes=32; break;
            case 1: file[0]='X'; break;
            case 2: file[4]=0; break;
            case 3: file[5]=2; break;
            case 4: file[8]=file[9]=0; break;
            case 5: size=31; break;
            case 6: size=BGM_MAX_BYTES+16; break;
            case 7: close_error=1; break;
            case 8: file[9]=0xff; break;
        }
        dashBgmStart(); assert(!uploads && !plays && !allocs && bgm_tried);
        dashSoundMaintain(); assert(opens==1);
    }
    reset(); upload_error=1; dashBgmStart(); assert(uploads==1 && !plays && !allocs);
    /* Service reset frees the old EE buffer and reloads on the new IOP. */
    reset(); dashBgmStart(); reset_generation++; dashSoundMaintain();
    assert(allocs==1 && opens==2 && uploads==2 && plays==2 && soundAlive());
    reset(); dashCfg.sound=0; dashBgmStart(); assert(plays==1);
    dashSoundShutdown(); assert(!allocs && !bgm_sample.buffer && !bgm_loaded);
    assert(hard_stops==2 && quit_calls>0);
    puts("PASS: optional boot sidecar, once-only miss, toggle/reboot reload, IO deferral, invalid/short files, cleanup");
}
"""
compiler = shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("Host gcc or clang required")

# Exercise real boot-path preparation and residency guards as well as the
# mocked music IO above. Disc boot reads must not use the writable CNF path.
startup = (root / "src/main_startup.c").read_text()
init = (root / "src/init.c").read_text()
boot_header = (root / "include/main_startup.h").read_text()
bootcode = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MAX_PATH 1024
#define MAX_NAME 256
#define EXFAT 1
#define MMCE 1
#define MX4SIO 1
#define ILINK 1
#define UDPFS 1
#define ETH 1
#define XFROM 1
#define DVRP 1
#define DPRINTF(...) ((void)0)
#define BLOCK_STACK_HDD 1
#define BLOCK_STACK_ATA 2
#define STORAGE_STACK_MMCE 1
#define STORAGE_STACK_MX4SIO 2
static char LaunchElfDir[MAX_PATH], LaunchElfBootDir[MAX_PATH], LastDir[MAX_PATH];
static int console_is_PSX;
static int have_usbd, have_usb_mass, have_HDD_modules, block_storage_stack_mode;
static int have_ps2host, have_ps2ip, have_ps2smap, ps2dev9_loaded;
static int have_Flash_modules, have_DVRP_HDD_modules, have_ata_bd, have_bdmfs;
static int have_mmce, have_mx4sio, storage_driver_stack_mode, have_udpfs_ioman, have_cdvd;
static int have_ilinkman, have_ieee1394_bd, have_bdm;
"""
bootcode += boot_header[boot_header.index("enum BOOT_DEVICE {"):boot_header.index("enum BOOT_DEVICE prepare")]
signatures = ("static void normalizeBootPath(", "static int isSlashRootedBootPathDevice(",
              "static int isDevicePathTerminator(", "static int isBootUsbMassPath(",
              "static int isBootAtaPath(", "static int isBootIlinkPath(", "static int isBootHddPath(",
              "static int isBootXfromPath(", "static void buildBootBlockPath(")
# Start after the forward declarations, so extraction takes the definitions.
definitions = startup[startup.index("static void normalizeBootPath(char *path)\n{"):]
for sig in signatures:
    bootcode += function(sig, definitions).split("{", 1)[0] + ";\n"
for sig in signatures:
    bootcode += function(sig, definitions)
bootcode += function("enum BOOT_DEVICE prepareBootDeviceAndPath(", startup)
bootcode += function("int isPathDeviceStackLoaded(", init)
bootcode += r"""
int main(void) {
    char path[MAX_PATH];
    assert(prepareBootDeviceAndPath("cdrom0:\\APPS\\BOOT.ELF;1",path,sizeof(path))==BOOT_DEVICE_CDVD);
    assert(!strcmp(LaunchElfBootDir,"cdfs:/APPS/"));
    assert(!strcmp(LaunchElfDir,"mc0:/SYS-CONF/"));
    assert(prepareBootDeviceAndPath("cdfs:/BOOT.ELF",path,sizeof(path))==BOOT_DEVICE_CDVD);
    assert(!strcmp(LaunchElfBootDir,"cdfs:/"));
    assert(prepareBootDeviceAndPath("usb0:/APPS/BOOT.ELF",path,sizeof(path))==BOOT_DEVICE_MASS);
    assert(!strcmp(LaunchElfBootDir,"usb0:/APPS/") && !strcmp(LaunchElfDir,LaunchElfBootDir));
    assert(prepareBootDeviceAndPath("ilink0:/APPS/BOOT.ELF",path,sizeof(path))==BOOT_DEVICE_ILINK);
    assert(!strcmp(LaunchElfBootDir,"ilink0:/APPS/") && !strcmp(LaunchElfDir,LaunchElfBootDir));
    assert(prepareBootDeviceAndPath("hdd0:__common:pfs:/BOOT/BOOT.ELF",path,sizeof(path))==BOOT_DEVICE_HDD);
    assert(!strcmp(LaunchElfBootDir,"hdd0:/__common/BOOT/"));
    assert(!isPathDeviceStackLoaded("cdfs:/") && !isPathDeviceStackLoaded("pfs0:/"));
    have_cdvd=1; assert(isPathDeviceStackLoaded("cdfs:/") && isPathDeviceStackLoaded("cdrom0:/"));
    have_HDD_modules=1; block_storage_stack_mode=BLOCK_STACK_HDD;
    assert(isPathDeviceStackLoaded("hdd0:/") && isPathDeviceStackLoaded("pfs0:/"));
    have_bdm=have_bdmfs=have_ilinkman=have_ieee1394_bd=1;
    assert(isPathDeviceStackLoaded("ilink0:/"));
    have_ps2host=have_ps2ip=have_ps2smap=ps2dev9_loaded=1;
    assert(isPathDeviceStackLoaded("host:/"));
    have_Flash_modules=have_DVRP_HDD_modules=1;
    assert(isPathDeviceStackLoaded("xfrom0:/") && isPathDeviceStackLoaded("dvr_pfs0:/"));
    assert(!isPathDeviceStackLoaded("unknown:/"));
    puts("PASS: actual USB/HDD/disc boot folders, writable disc config fallback, active-stack aliases");
}
"""
with tempfile.TemporaryDirectory(prefix="wle-bgm-") as tmp:
    for name, harness_code in (("bgm", code), ("boot", bootcode)):
        harness = Path(tmp) / f"{name}.c"
        exe = Path(tmp) / f"{name}.exe"
        harness.write_text(harness_code)
        subprocess.run([compiler, "-std=gnu11", str(harness), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
