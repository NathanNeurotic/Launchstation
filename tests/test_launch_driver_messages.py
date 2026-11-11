"""A failed launch says what failed: a device whose drivers did not load, a
file that is missing, or a file that is not an executable.

Compiles the real ExecuteMainAction() (src/main_actions.c) and
checkELFheader() (src/elf.c) on the host. Drivers, files and the loader are
fixtures: a device whose drivers did not load opens no file. The dashboard
offers to remove a shortcut whose launch says "is Not Found", so a driver
failure or a file that opened must not say that, and a missing file must.

An optional git ref runs the same checks against that revision's sources.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]


def read(path):
    if len(sys.argv) > 1:
        return subprocess.check_output(['git', 'show', f'{sys.argv[1]}:{path}'], cwd=root, text=True)
    return (root / path).read_text()


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


actions = read('src/main_actions.c')
elf = read('src/elf.c')

program = r'''
#include <assert.h>
#include <elf.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "main_actions.h"
typedef uint8_t u8;
typedef int64_t s64;
#define MAX_PATH 1024
#define TRUE 1
#define FALSE 0
#define ETH 1
#define UDPFS 1
#define SMB 1
#define MX4SIO 1
#define MMCE 1
#define ILINK 1
#define EXFAT 1
#define XFROM 1
#define DVRP 1
#define FIO_O_RDONLY 1
#define SCECdCDDA 0x13
#define SCECdDVDV 0x14
#define SETTING_LK_OSDSYS 0
#define SETTING_LK_ESR 1
#define TEXTEDITOR_RESULT_LAUNCH_ARGS 1
#define TEXTEDITOR_RESULT_BROWSE_DIR 2
#define SMB_PATH_NONE 0
#define DPRINTF(...) ((void)0)
#define stricmp strcasecmp
#define LNG(x) LNG_##x
#define LNG_is_Not_Found "is Not Found"
#define LNG_This_file_isnt_an_ELF "This file isn't an ELF"
#define LNG_Reading_SYSTEMCNF "Reading SYSTEM.CNF"
#define LNG_Failed "Failed"
#define LNG_PS2Disc "PS2Disc"
#define LNG_Powering_Off_Console "Powering off"

/* Every device's drivers either load or fail. A device whose drivers are
 * not running opens no file. A loader can also report a failure although
 * its drivers run, which a launch must survive as it always did. */
enum {D_HDD,D_DVR,D_XFROM,D_MX4SIO,D_ILINK,D_MMCE,D_ATA,D_UDPFS,D_SMB,D_HOST,D_USB,D_COUNT};
static int fail[D_COUNT],running[D_COUNT],misreport[D_COUNT],smb_silent;
static int mx4sio_driver_running,console_is_PSX=1,vmcMounted[2];
static int device(const char *p){
 if(!strncmp(p,"hdd",3)||!strncmp(p,"pfs",3))return D_HDD;
 if(!strncmp(p,"dvr_",4))return D_DVR;
 if(!strncmp(p,"xfrom",5))return D_XFROM;
 if(!strncmp(p,"mx4sio",6))return D_MX4SIO;
 if(!strncmp(p,"ilink",5))return D_ILINK;
 if(!strncmp(p,"mmce",4))return D_MMCE;
 if(!strncmp(p,"ata",3))return D_ATA;
 if(!strncmp(p,"udpfs",5))return D_UDPFS;
 if(!strncmp(p,"smb",3))return D_SMB;
 if(!strncmp(p,"host",4))return D_HOST;
 return D_USB;
}
static int load(int d){if(fail[d])return 0;running[d]=1;return !misreport[d];}
static int loadHddModules(void){return load(D_HDD);}
static int loadDVRPHddModules(void){return load(D_DVR);}
static int loadFlashModules(void){return load(D_XFROM);}
static int loadMx4sioModules(void){int r=load(D_MX4SIO);mx4sio_driver_running=running[D_MX4SIO];return r;}
static int loadIlinkModules(void){return load(D_ILINK);}
static int loadMmceModules(void){return load(D_MMCE);}
static int loadAtaModules(void){return load(D_ATA);}
static int load_udpfs(void){return load(D_UDPFS);}
static void loadCdModules(void){}
static void initHOST(void){load(D_HOST);}
static int isPathDeviceStackLoaded(const char *p){return running[device(p)];}
static const char *smb_error="";
static int smbPathKind(const char *p){return !strncmp(p,"smb1:",5)?1:!strncmp(p,"smb:",4)?2:SMB_PATH_NONE;}
static int isSmbDriverLoaded(int v){(void)v;return running[D_SMB];}
static int load_smb(int v){(void)v;int r=load(D_SMB);smb_error=r||smb_silent?"":"The network adapter is busy (UDPFS).";return r;}
static const char *smbLoadError(int v){(void)v;return smb_error;}

/* Files: a MIPS ELF or some text, on any device. */
enum {F_ELF=1,F_TEXT};
static struct {char path[MAX_PATH];int kind;} files[64];static int nfiles;
static void put(const char *p,int kind){assert(nfiles<64);snprintf(files[nfiles].path,MAX_PATH,"%s",p);files[nfiles++].kind=kind;}
static int open_kind[8],open_pos[8];
static int genOpen(const char *p,int mode){
 (void)mode;if(!running[device(p)])return -ENODEV;
 for(int i=0;i<nfiles;i++)if(!strcmp(files[i].path,p))
  for(int fd=0;fd<8;fd++)if(!open_kind[fd]){open_kind[fd]=files[i].kind;open_pos[fd]=0;return fd;}
 return -ENOENT;
}
static int genClose(int fd){assert(open_kind[fd]);open_kind[fd]=0;return 0;}
static s64 genLseek(int fd,s64 where,int how){(void)how;open_pos[fd]=(int)where;return where;}
static int genRead(int fd,void *buf,int size){
 static Elf32_Ehdr header;static const char text[]="Notes, not an executable.";
 const char *data=text;int n=sizeof(text);
 if(open_kind[fd]==F_ELF){memset(&header,0,sizeof(header));memcpy(header.e_ident,ELFMAG,SELFMAG);
  header.e_type=ET_EXEC;header.e_machine=EM_MIPS;data=(const char *)&header;n=sizeof(header);}
 n-=open_pos[fd];if(n>size)n=size;if(n<0)n=0;memcpy(buf,data+open_pos[fd],n);open_pos[fd]+=n;return n;
}
static int fixpath_fail;
static int genFixPath(const char *in,char *out){if(fixpath_fail)return -1;strcpy(out,in);return 0;}
static int genCmpFileExt(const char *p,const char *e){const char *d=strrchr(p,'.');return d&&!strcasecmp(d+1,e);}

/* The loader, and everything else the launch can reach. */
static char launched[MAX_PATH];
static void RunLoaderElf(char *f,char *party,const char *sel,int kind,int reboot){(void)party;(void)sel;(void)kind;(void)reboot;snprintf(launched,MAX_PATH,"%s",f);}
void (*mainActionLaunchHook)(const char *path);
static struct {int LK_Flag[2];char LK_Path[2][MAX_PATH];int reboot_iop_elf_load,cdrom_disable_gameid,app_gameid;char font_file[MAX_PATH];
 char Misc_OSDSYS[64],Misc_PS2Disc[64],Misc_FileBrowser[64],Misc_PS2Browser[64],Misc_PS2Net[64],Misc_PS2PowerOff[64],Misc_HddManager[64],
  Misc_TextEditor[64],Misc_Configure[64],Misc_ShowFont[64],Misc_Debug_Info[64],Misc_Show_Build_Info[64],Misc_Exploit_Installer[64],Misc_Reboot_IOP[64];
} settings={.Misc_OSDSYS="MISC/OSDSYS",.Misc_PS2Disc="MISC/PS2Disc"},*setting=&settings;
static int BootDiscType,cdmode;static char SystemCnf_BOOT[MAX_PATH],SystemCnf_BOOT2[MAX_PATH],SystemCnf_VER[64],LastDir[MAX_PATH],netConfig[64];
#define uLE_related(out,in) (strcpy((out),(in)),1)
#define LaunchArgsPending() 0
#define LaunchArgsClear() ((void)0)
#define isPcCardCfPath(p) 0
#define IsPopstarterVcdPath(p) 0
#define LaunchPopstarterVcd(p,m,n) ((void)0)
#define IsEmberGamePath(p) 0
#define LaunchEmberGame(p,m,n) ((void)0)
#define IsNeutrinoIsoPath(p) 0
#define LaunchNeutrinoIso(p,m,n) ((void)0)
#define PrepareMbrLaunchPayload(p,a,n) (-1)
#define CleanUpForExec() ((void)0)
#define CleanUpForKeepIopExec(t) 1
#define RunLoaderMemory(a,b,c) ((void)0)
#define DashChooseElfIopMode(x) 1
#define getDVRPPartyMountIndex(p) 0
#define makeHostPath(d,s) ((void)0)
#define LoadExecPS2(a,b,c) ((void)0)
#define drawMsg(s) ((void)0)
#define readSystemCnf() ((void)0)
#define buildLaunchGameID(a,b,c) 0
#define updateOSDHistoryFile(a) ((void)0)
#define applyXPARAM(a) ((void)0)
#define displayRetroGemGameID(a,b) ((void)0)
#define uLE_cdDiscValid() 0
#define Check_ESR_Disc() 0
#define SifLoadModule(a,b,c) (-1)
#define SifLoadModuleEncrypted(a,b,c) (-1)
#define wleExists(p) 0
#define isMiscDashboardPath(p) 0
#define DashIsActive() 1
#define DashRun(c,f) ((void)0)
#define getFilePath(a,b) ((void)0)
#define executeBrowserSelection(a,b) ((void)0)
#define ExecOSD(a,b) ((void)0)
#define PS2BrowserSkipMc 0
#define cardUpdateIsLoadBooter(p) 0
#define loadNetModules() ((void)0)
#define DashFtpServer() 0
#define setupPowerOff() ((void)0)
#define isPowerOffReady() 1
#define closeAllAndPoweroff() ((void)0)
#define dashHddManager() ((void)0)
#define hddManager() ((void)0) /* revisions before the classic screen was removed */
#define TextEditor(p) 0
#define getTextEditorReturnPath(p) ((void)0)
#define Load_External_Language() ((void)0)
#define loadFont(f) ((void)0)
#define config(a,b) ((void)0)
#define ShowFont() ((void)0)
#define ShowDebugInfo(...) ((void)0)
#define isMiscAboutWlePath(p) 0
#define Show_About_uLE() ((void)0)
#define Show_build_info() ((void)0)
#define ShowExploitInstaller(c) ((void)0)
#define rebootIopAndReloadCoreStack() ((void)0)
#define LCDVD_FLUSHCACHE() ((void)0)
#define LCDVD_DISKREADY(x) ((void)0)
#define isLikelyDiscLaunch(p) 0
#define LaunchArgsLoadSidecarForExec(a,b,c) 0
'''
for signature in ('static int readExecHeader(', 'static int classifyExecHeader(',
                  'static int classifyExecByExtension(', 'static int tryCheckExecPath(',
                  'int checkELFheader('):
    program += function(elf, signature)
for signature in ('static int isHddLaunchPath(', 'static int isMbrLaunchPath(',
                  'static const char *getLaunchArgsSidecarBasePath(', 'static int elfLaunchIopMode(',
                  'static void setDriversNotLoadedMsg(', 'void ExecuteMainAction('):
    if signature in actions or signature != 'static void setDriversNotLoadedMsg(':
        program += function(actions, signature)
program += r'''
static char message[MAX_PATH];
static MainExecuteContext ctx={.main_msg=message};
static int failures;
static void reset(void){
 memset(fail,0,sizeof(fail));memset(running,0,sizeof(running));memset(misreport,0,sizeof(misreport));
 running[D_USB]=1;mx4sio_driver_running=smb_silent=fixpath_fail=0;smb_error="";
}
static void launch(const char *p){char in[MAX_PATH];snprintf(in,MAX_PATH,"%s",p);message[0]=launched[0]=0;ExecuteMainAction(in,&ctx);}
static void expect(const char *what,const char *p,const char *want){
 if(strcmp(message,want)||launched[0]){
  fprintf(stderr,"FAIL %s %s: \"%s\"%s; expected \"%s\"\n",what,p,message,launched[0]?" and it launched":"",want);failures++;
 }
}
int main(void){
 static const struct {const char *dir;int dev;const char *no_drivers;} devices[]={
  {"hdd0:/PART/",D_HDD,"HDD drivers could not be loaded."},
  {"dvr_hdd0:/PART/",D_DVR,"DVR HDD drivers could not be loaded."},
  {"xfrom:/",D_XFROM,"XFROM drivers could not be loaded."},
  {"mx4sio:/",D_MX4SIO,"MX4SIO drivers could not be loaded."},
  {"ilink0:/",D_ILINK,"iLink drivers could not be loaded."},
  {"mmce0:/",D_MMCE,"MMCE drivers could not be loaded."},
  {"ata0:/",D_ATA,"ATA drivers could not be loaded."},
  {"udpfs:/",D_UDPFS,"UDPFS drivers could not be loaded."},
  {"smb:/SHARE/",D_SMB,"The network adapter is busy (UDPFS)."},
  {"host:",D_HOST,"Host drivers could not be loaded."},
  {"mass:/",D_USB,NULL},
  {"usb0:/",D_USB,NULL},
 };
 const int count=sizeof(devices)/sizeof(devices[0]);
 char path[MAX_PATH],want[MAX_PATH];
 put("mass:/OTHER.ELF",F_ELF);
 for(int i=0;i<count;i++){
  snprintf(path,MAX_PATH,"%sBOOT.ELF",devices[i].dir);put(path,F_ELF);
  snprintf(path,MAX_PATH,"%sNOTES.TXT",devices[i].dir);put(path,F_TEXT);
 }
 for(int i=0;i<count;i++){
  /* A working launch first, so a stale path from it would show below. */
  reset();launch("mass:/OTHER.ELF");
  if(message[0]||!launched[0]){fprintf(stderr,"FAIL fixture: mass:/OTHER.ELF did not launch: \"%s\"\n",message);return 1;}
  snprintf(path,MAX_PATH,"%sBOOT.ELF",devices[i].dir);
  launch(path);
  if(message[0]||!launched[0]){fprintf(stderr,"FAIL launch %s: \"%s\"\n",path,message);failures++;}
  /* Drivers that do not load are named, and the file is not called missing. */
  if(devices[i].no_drivers){
   reset();fail[devices[i].dev]=1;launch(path);expect("drivers fail",path,devices[i].no_drivers);
  }
  /* A missing file, on a device that works, is not found. */
  reset();launch("mass:/OTHER.ELF");
  snprintf(path,MAX_PATH,"%sMISSING.ELF",devices[i].dir);snprintf(want,MAX_PATH,"%s is Not Found.",path);
  launch(path);expect("missing file",path,want);
  /* A file that opens but is not an executable is named as such. */
  reset();snprintf(path,MAX_PATH,"%sNOTES.TXT",devices[i].dir);snprintf(want,MAX_PATH,"This file isn't an ELF: %s.",path);
  launch(path);expect("not an executable",path,want);
 }
 /* A file that opened as an executable is not called missing when its
  * launch path cannot be prepared afterwards. */
 reset();launch("mass:/OTHER.ELF");fixpath_fail=1;launch("usb0:/BOOT.ELF");
 if(!message[0]||strstr(message,"Not Found")||!strstr(message,"usb0:/BOOT.ELF")||launched[0]){
  fprintf(stderr,"FAIL opened usb0:/BOOT.ELF: \"%s\"%s; expected a message naming it, not \"is Not Found\"\n",message,launched[0]?" and it launched":"");failures++;
 }
 /* SMB without a recorded reason names its drivers. */
 reset();fail[D_SMB]=smb_silent=1;launch("smb:/SHARE/BOOT.ELF");expect("drivers fail","smb:/SHARE/BOOT.ELF","SMB drivers could not be loaded.");
 /* A loader that reports a failure while its drivers run still launches,
  * as the branches that ignored it always did. */
 static const int ignored[]={D_HDD,D_DVR,D_MMCE,D_ATA,D_UDPFS};
 for(int i=0;i<count;i++)for(unsigned j=0;j<sizeof(ignored)/sizeof(ignored[0]);j++)if(devices[i].dev==ignored[j]){
  reset();misreport[devices[i].dev]=1;snprintf(path,MAX_PATH,"%sBOOT.ELF",devices[i].dir);launch(path);
  if(message[0]||!launched[0]){fprintf(stderr,"FAIL misreported load %s: \"%s\"\n",path,message);failures++;}
  /* A file that opened there is not an executable; its drivers are not blamed. */
  reset();misreport[devices[i].dev]=1;snprintf(path,MAX_PATH,"%sNOTES.TXT",devices[i].dir);
  snprintf(want,MAX_PATH,"This file isn't an ELF: %s.",path);launch(path);expect("misreported load, not an executable",path,want);
 }
 /* OSDSYS started from its launch key path: a missing file is named by that
  * path, not by the launch before it. */
 static const char *const osdsys[]={"mass:/OSDSYS.ELF","mc:/BOOT/OSDSYS.ELF"};
 settings.LK_Flag[SETTING_LK_OSDSYS]=1;
 for(unsigned i=0;i<sizeof(osdsys)/sizeof(osdsys[0]);i++){
  reset();launch("mass:/OTHER.ELF");
  snprintf(settings.LK_Path[SETTING_LK_OSDSYS],MAX_PATH,"%s",osdsys[i]);snprintf(want,MAX_PATH,"%s is Not Found.",osdsys[i]);
  launch(settings.Misc_OSDSYS);expect("missing OSDSYS",osdsys[i],want);
 }
 settings.LK_Flag[SETTING_LK_OSDSYS]=0;
 /* mc: tries mc0: then mc1:. A file that opened on one card but is no ELF is not
  * "Not Found" because the other card has none, and the message names the card
  * it opened on; one found on neither is Not Found at mc1:. */
 reset();put("mc0:/NOTES.TXT",F_TEXT);snprintf(want,MAX_PATH,"This file isn't an ELF: mc0:/NOTES.TXT.");
 launch("mc:/NOTES.TXT");expect("mc0: text, mc1: has none","mc:/NOTES.TXT",want);
 reset();put("mc1:/CARD1.TXT",F_TEXT);snprintf(want,MAX_PATH,"This file isn't an ELF: mc1:/CARD1.TXT.");
 launch("mc:/CARD1.TXT");expect("mc1: text, mc0: has none","mc:/CARD1.TXT",want);
 reset();launch("mc:/NOWHERE.ELF");expect("on neither card","mc:/NOWHERE.ELF","mc1:/NOWHERE.ELF is Not Found.");
 /* checkELFheader() itself: missing, device down, not executable, ELF. */
 reset();running[D_UDPFS]=1;fail[D_MMCE]=1;
 char missing[]="udpfs:/MISSING.ELF",notes[]="udpfs:/NOTES.TXT",boot[]="udpfs:/BOOT.ELF",offline[]="mmce0:/BOOT.ELF";
 int got[4]={checkELFheader(missing),checkELFheader(offline),checkELFheader(notes),checkELFheader(boot)};
 if(got[0]!=-2||got[1]!=-2||got[2]!=-1||got[3]!=1){
  fprintf(stderr,"FAIL checkELFheader: missing %d, device down %d, text %d, ELF %d; expected -2, -2, -1, 1\n",got[0],got[1],got[2],got[3]);failures++;
 }
 if(failures)return 1;
 printf("PASS: %d devices: drivers that do not load are named, missing files (OSDSYS's too) are not found, "
        "other files are not executables, and every working launch starts\n",count);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='launch-driver-messages-') as folder:
    p = Path(folder)
    (p / 'test.c').write_text(program)
    exe = p / 'test'
    cc = shutil.which('gcc') or shutil.which('cc')
    if not cc:
        raise SystemExit('Host C compiler required')
    subprocess.run([cc, '-std=gnu99', '-Werror=implicit-function-declaration', '-Wno-format-overflow',
                    '-Wno-format-truncation', '-I', str(root / 'include'),
                    str(p / 'test.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=20)
