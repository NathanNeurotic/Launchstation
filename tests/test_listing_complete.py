"""Run the real folder listing (src/filer.c) on fake devices.

getDirComplete() is the listing copy, delete and Get Size act on: it must
return an error unless it read the whole folder. getDir() is the browser's
listing and keeps showing what it could read. Both go through the real
listDir() dispatcher and the real memory card, generic (BDM, MMCE, PC card),
CD, VMC, HDD, PSX flash, DVR and host readers; only the drivers below them
are fakes, with the end of folder and error results each driver really
returns.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
filer = (root / 'src/filer.c').read_text()
header = (root / 'include/launchelf.h').read_text()
limits = {name: int(re.search(r'\b%s = (\d+),' % name, header).group(1))
          for name in ('MAX_NAME', 'MAX_PATH', 'MAX_ENTRY', 'MAX_PART_NAME')}


def function(signature):
    start = filer.index(signature)
    end = filer.index('{', start) + 1
    depth = 1
    while depth:
        depth += (filer[end] == '{') - (filer[end] == '}')
        end += 1
    return filer[start:end] + '\n'


code = r'''
#include <assert.h>
#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8;typedef unsigned short u16;typedef unsigned int u32;typedef unsigned long long u64;
enum {MAX_NAME=%(MAX_NAME)d,MAX_PATH=%(MAX_PATH)d,MAX_ENTRY=%(MAX_ENTRY)d,MAX_PART_NAME=%(MAX_PART_NAME)d};
#define ETH 1
#define MMCE 1
#define MX4SIO 1
#define XFROM 1
#define DVRP 1
#define FALSE 0
#define DPRINTF(...) ((void)0)
#define FIO_S_IFMT 0xF000
#define FIO_S_IFREG 0x2000
#define FIO_S_IFDIR 0x1000
#define FIO_S_ISREG(m) (((m)&FIO_S_IFMT)==FIO_S_IFREG)
#define FIO_S_ISDIR(m) (((m)&FIO_S_IFMT)==FIO_S_IFDIR)
#define FIO_O_RDONLY 1
#define MC_ATTR_norm_folder 0x8427
#define MC_ATTR_norm_file 0x8497
#define sceMcFileAttrFile 0x0010
#define sceMcFileAttrSubdir 0x0020
#define sceMcFileAttrExists 0x8000
#define sceMcResSucceed 0
#define sceMcResChangedCard -1
#define sceMcResNoFormat -2
#define sceMcResNoEntry -4
#define MC_BROWSER_RETRY_COUNT 3
#define MC_BROWSER_RETRY_DELAY_US 1000
#define SCECdNODISC 0x00
#define SCECdUNKNOWN 0x05
#define SCECdPSCD 0x10
#define SCECdPS2DVD 0x14
typedef struct {u8 Resv2,Sec,Min,Hour,Day,Month;u16 Year;} sceMcStDateTime;
typedef struct {sceMcStDateTime _Create,_Modify;u32 FileSizeByte;u16 AttrFile,Reserve1;u32 Reserve2,PdaAplNo;u8 EntryName[32];}
 __attribute__((aligned(64))) sceMcTblGetDir; /* as in libmc.h */
typedef struct {char name[MAX_NAME];unsigned char title[32*2+2];sceMcTblGetDir stats;} FILEINFO;
typedef struct {unsigned mode,attr,size;u8 ctime[8],atime[8],mtime[8];unsigned hisize,private_0,private_1,private_2;} iox_stat_t;
typedef struct {iox_stat_t stat;char name[256];} iox_dirent_t;
static int size_valid,time_valid,mctype_PSx=2,host_elflist,USB_mass_scanned=1,cdmode=SCECdPS2DVD;
static u64 now;static u64 Timer(void){return now+=10;}
static void clearMcTable(sceMcTblGetDir *t){memset(t,0,sizeof(*t));}

/* A fake device folder. Entries are E00000, E00001, ...; every fifth is a folder. */
typedef struct {
 char path[64];
 int count;       /* entries besides "." and ".." */
 int dots;        /* the folder lists "." and ".." first */
 int open_error;  /* every open fails with this */
 int fail_opens;  /* the first opens fail with -ENOENT */
 int fail_at;     /* from this entry on, a read fails with fail_code (-1: never) */
 int fail_code;
 int end_code;    /* what a read returns after the last entry */
} Folder;
static Folder folders[32];static int nfolders;
static Folder *add(const char *path,int count){
 Folder *f=&folders[nfolders++];assert(nfolders<=32);memset(f,0,sizeof(*f));
 snprintf(f->path,sizeof(f->path),"%%s",path);f->count=count;f->fail_at=-1;return f;
}
static Folder *find(const char *path){for(int i=0;i<nfolders;i++)if(!strcmp(folders[i].path,path))return &folders[i];return NULL;}
static void entry(int i,char *name,unsigned *mode,unsigned *size){
 sprintf(name,"E%%05d",i);*mode=i%%5==4?FIO_S_IFDIR:FIO_S_IFREG;*size=i%%5==4?0:100+i;
}
static struct {Folder *folder;int position;} handles[8];static int open_handles,opens;
static int fileXioDopen(const char *path){
 Folder *f=find(path);opens++;
 if(!f)return -ENOENT;
 if(f->fail_opens){f->fail_opens--;return -ENOENT;}
 if(f->open_error)return f->open_error;
 for(int h=0;h<8;h++)if(!handles[h].folder){handles[h].folder=f;handles[h].position=0;open_handles++;return h+3;}
 assert(!"too many open folders");return -EMFILE;
}
static int fileXioDread(int fd,iox_dirent_t *out){
 assert(fd>=3&&fd<11&&handles[fd-3].folder);
 Folder *f=handles[fd-3].folder;int i=handles[fd-3].position-(f->dots?2:0);
 if(f->fail_at>=0&&i>=f->fail_at)return f->fail_code; /* a read error leaves *out as it was */
 if(i>=f->count)return f->end_code;
 memset(out,0,sizeof(*out));
 if(i<0){strcpy(out->name,i==-2?".":"..");out->stat.mode=FIO_S_IFDIR;}
 else entry(i,out->name,&out->stat.mode,&out->stat.size);
 handles[fd-3].position++;return 1;
}
static int fileXioDclose(int fd){assert(fd>=3&&fd<11&&handles[fd-3].folder);handles[fd-3].folder=NULL;open_handles--;return 0;}
static int fileXioOpen(const char *p,int f,int m){(void)p;(void)f;(void)m;assert(!"elflist.txt is not used here");return -ENOENT;}
static int fileXioClose(int fd){(void)fd;return 0;}
static int fileXioLseek(int fd,int o,int w){(void)fd;(void)o;(void)w;return -EIO;}
static int fileXioRead(int fd,void *p,int n){(void)fd;(void)p;(void)n;return -EIO;}

/* Memory card: mcGetDir lists "." and ".." first in a subfolder and returns
 * through mcSync the count it put in the table, or a libmc error. */
static int mc_ready=sceMcResSucceed,mc_result,mc_maxent;
static int filerPrepareMemoryCardRead(int port){assert(port==0||port==1);return mc_ready;}
static int waitMemoryCardReadyForBrowse(int port){(void)port;return sceMcResSucceed;}
static void DelayThread(int us){(void)us;}
static int mcGetDir(int port,int slot,const char *name,unsigned mode,int maxent,sceMcTblGetDir *table){
 char path[MAX_PATH];size_t n=strlen(name);(void)slot;(void)mode;
 assert(n&&name[n-1]=='*');snprintf(path,sizeof(path),"mc%%d:%%.*s",port,(int)(n-1),name);
 assert(maxent>=1&&maxent<=MAX_ENTRY+1); /* readMC's table holds MAX_ENTRY + 1 */
 mc_maxent=maxent;
 Folder *f=find(path);
 if(!f){mc_result=sceMcResNoEntry;return 0;}
 if(f->open_error){mc_result=f->open_error;return 0;}
 int total=f->count+(f->dots?2:0),k;
 for(k=0;k<total&&k<maxent;k++){
  unsigned fmode,size;char fname[32];int i=k-(f->dots?2:0);memset(&table[k],0,sizeof(table[k]));
  if(i<0){strcpy((char *)table[k].EntryName,i==-2?".":"..");table[k].AttrFile=MC_ATTR_norm_folder;continue;}
  entry(i,fname,&fmode,&size);strcpy((char *)table[k].EntryName,fname);
  table[k].AttrFile=fmode==FIO_S_IFDIR?MC_ATTR_norm_folder:MC_ATTR_norm_file;table[k].FileSizeByte=size;
 }
 mc_result=k;return 0;
}
static int mcSync(int mode,int *cmd,int *result){(void)mode;(void)cmd;if(result)*result=mc_result;return 1;}

/* HDD: "hdd0:/PART/DIR/" is read as "pfs0:/DIR/" once PART is mounted. */
static int hddNparties[2],hdd_partitions;static char hddParties[2][4][MAX_PART_NAME+1];
static int (*iopResetBlocked)(void);
static void loadHddModules(void){}
static void setPartyListForDevice(const char *device){
 int unit=device[3]-'0';hddNparties[unit]=hdd_partitions;
 for(int i=0;i<hdd_partitions;i++)sprintf(hddParties[unit][i],"+PART%%d",i);
}
static void unmountHddPartiesNotNeededByClipboard(void){}
static int getHddParty(const char *path,const FILEINFO *file,char *party,char *dir){
 const char *p=path+6,*slash=strchr(p,'/');(void)file;assert(slash);
 snprintf(party,MAX_NAME,"%%.*s",(int)(slash-p),p);snprintf(dir,MAX_PATH,"pfs0:%%s",slash);return 0;
}
static int mountParty(const char *party){(void)party;return 0;}

/* PSX: the flash (xfrom:) lists like a memory card, through its own getdir.
 * The DVR drive: "dvr_hdd0:/PART/DIR/" is read as "dvr_pfs0:/DIR/". */
static u8 console_is_PSX=1;static int xfrom_result;
static int loadFlashModules(void){return 1;}
static int xfromGetInfo(int port,int slot,int *type,int *free,int *format){(void)port;(void)slot;(void)free;(void)format;*type=2;xfrom_result=0;return 0;}
static int xfromGetDir(int port,int slot,const char *name,unsigned mode,int maxent,sceMcTblGetDir *table){
 char path[MAX_PATH];size_t n=strlen(name);(void)port;(void)slot;(void)mode;
 assert(n&&name[n-1]=='*');snprintf(path,sizeof(path),"xfrom:/%%.*s",(int)(n-1),name);
 assert(maxent>=1&&maxent<=MAX_ENTRY+1); /* readXFROM's table holds MAX_ENTRY + 1 */
 Folder *f=find(path);
 if(!f){xfrom_result=sceMcResNoEntry;return 0;}
 if(f->open_error){xfrom_result=f->open_error;return 0;}
 int total=f->count+(f->dots?2:0),k;
 for(k=0;k<total&&k<maxent;k++){
  unsigned fmode,size;char fname[32];int i=k-(f->dots?2:0);memset(&table[k],0,sizeof(table[k]));
  if(i<0){strcpy((char *)table[k].EntryName,i==-2?".":"..");table[k].AttrFile=MC_ATTR_norm_folder;continue;}
  entry(i,fname,&fmode,&size);strcpy((char *)table[k].EntryName,fname);
  table[k].AttrFile=fmode==FIO_S_IFDIR?MC_ATTR_norm_folder:MC_ATTR_norm_file;table[k].FileSizeByte=size;
 }
 xfrom_result=k;return 0;
}
static int xfromSync(int mode,int *cmd,int *result){(void)mode;(void)cmd;if(result)*result=xfrom_result;return 1;}
static int ndvrpparties,dvr_partitions;static char dvrParties[4][MAX_PART_NAME+1];
static int loadDVRPHddModules(void){return 1;}
static void setDVRPPartyList(void){
 ndvrpparties=dvr_partitions;for(int i=0;i<dvr_partitions;i++)sprintf(dvrParties[i],"+DVR%%d",i);
}
static int getHddDVRPParty(const char *path,const FILEINFO *file,char *party,char *dir){
 const char *p=path+10,*slash=strchr(p,'/');(void)file;assert(slash);
 snprintf(party,MAX_NAME,"%%.*s",(int)(slash-p),p);snprintf(dir,MAX_PATH,"dvr_pfs0:%%s",slash);return 0;
}
static int mountDVRPParty(const char *party){(void)party;return 0;}

/* host: (ps2client) */
static void initHOST(void){}
static char *makeHostPath(char *out,const char *in){if(out!=in)strcpy(out,in);return out;}
static void makeFslPath(char *out,const char *in){strcpy(out,in);}

/* What the dispatcher needs for devices this test does not list. */
static void loadCdModules(void){}
static void loadMmceModules(void){}
static int sceCdGetDiskType(void){return SCECdPS2DVD;}
static int uLE_cdDiscValid(void){return 1;}
static void uLE_cdStop(void){}
static int mapUsbPathToFatFsPath(const char *p,char *out){(void)p;(void)out;return -1;}
static void scanUsbMassDevices(int force){(void)force;}
static void scan_USB_mass(void){}
static int isPcCardCfPath(const char *p){return !strncmp(p,"cf",2)&&p[2]>='0'&&p[2]<='9'&&p[3]==':';}
static int isPathDeviceStackLoaded(const char *p){(void)p;return 1;}
static int loadPcCardCfModules(void){return 1;}
/* The PC card answers this many more checks online, then is offline (-1: stays online). */
static int cf_online=-1;
static int isPcCardCfOnline(void){if(cf_online<0)return 1;if(!cf_online)return 0;cf_online--;return 1;}
static u8 mx4sio_driver_running=1;static int discard_next_mx4sio_root_listing;
static int loadMx4sioModules(void){return 1;}
'''
code %= limits
start = filer.index('static int dir_opened, dir_open_error, dir_read_error;')
code += filer[start:filer.index('\n', start) + 1] + 'static int dir_complete;\n'
for signature in ('static void dirOpenResult(', 'static void dirReadFailed(',
                  'static int getHddDeviceFromPath(', 'static int isHddRootPath(', 'static int isAtaPath(',
                  'static const char *canonicalizeAtaPath(', 'static const char *canonicalizePcCardCfPath(',
                  'static int getHddUnitFromDevice(', 'static int isSimpleDeviceRootPath(',
                  'static int isMx4sioRootPath(', 'static void drainGENERIC(',
                  'static const char *getXfromRelativePath('):
    code += function(signature)
code += 'int readGENERIC(const char *path, FILEINFO *info, int max);\n'
for signature in ('static int readGENERICWithFirstOpenRetry(',
                  'int readMC(const char *path, FILEINFO *info, int max)\n{',
                  'int readCD(const char *path, FILEINFO *info, int max)\n{',
                  'int readVMC(const char *path, FILEINFO *info, int max)\n{',
                  'int readHDD(const char *path, FILEINFO *info, int max)\n{',
                  'int readXFROM(const char *path, FILEINFO *info, int max)\n{',
                  'int readHDDDVRP(const char *path, FILEINFO *info, int max)\n{',
                  'int readGENERIC(const char *path, FILEINFO *info, int max)\n{',
                  'int readHOST(const char *path, FILEINFO *info, int max)\n{',
                  'static int listDir(const char *path, FILEINFO *info, int max)\n{',
                  'int getDir(const char *path, FILEINFO *info)\n{',
                  'int getDirComplete(const char *path, FILEINFO *info, int *more)\n{'):
    code += function(signature)
code += r'''
static FILEINFO info[MAX_ENTRY+1]; /* the last is a guard no listing may reach */
static int more;
static int complete(const char *path){more=-1;memset(&info[MAX_ENTRY],0x5A,sizeof(info[0]));return getDirComplete(path,info,NULL);}
static int complete_more(const char *path){more=-1;return getDirComplete(path,info,&more);}
static int browse(const char *path){return getDir(path,info);}
static int listed(int n){
 for(int i=0;i<n;i++){char name[16];sprintf(name,"E%05d",i);if(strcmp(info[i].name,name))return 0;}
 for(int i=0;i<(int)sizeof(info[0]);i++)if(((unsigned char *)&info[MAX_ENTRY])[i]!=0x5A)return 0;
 return !open_handles;
}
int main(void){
 const int CAP=MAX_ENTRY-2; /* the most a listing can hold: 2046 */
 /* BDM (FAT/exFAT): a read error ends the folder with 0 like its end does, so
  * only an open failure can be seen. */
 add("mass0:/FULL/",CAP);add("mass0:/OVER/",CAP+1);add("mass0:/HUGE/",5000);add("mass0:/EMPTY/",0);
 add("mass0:/",3)->fail_opens=1;add("mass0:/LATE/",3)->fail_opens=1;
 assert(complete("mass0:/FULL/")==CAP&&listed(CAP)&&!dir_complete);
 assert(complete_more("mass0:/FULL/")==CAP&&more==0);
 assert(browse("mass0:/FULL/")==CAP&&listed(CAP));
 /* one entry over the cap: an error, or with 'more' MAX_ENTRY - 1 entries to act on first */
 assert(complete("mass0:/OVER/")==-E2BIG&&listed(0));
 assert(complete_more("mass0:/OVER/")==CAP+1&&more==1&&listed(CAP+1));
 assert(complete("mass0:/HUGE/")==-E2BIG);
 assert(complete_more("mass0:/HUGE/")==CAP+1&&more==1&&listed(CAP+1));
 assert(browse("mass0:/OVER/")==CAP&&browse("mass0:/HUGE/")==CAP&&listed(CAP)); /* the browser shows the first 2046 */
 /* an empty folder is complete; a missing one is an error, shown empty in the browser */
 assert(complete("mass0:/EMPTY/")==0&&complete_more("mass0:/EMPTY/")==0&&more==0);
 assert(complete("mass0:/GONE/")==-ENOENT&&!open_handles);
 assert(browse("mass0:/GONE/")==0);
 /* the root's first open may fail while the drive settles: the retry that opens it counts */
 assert(complete("mass0:/")==3&&listed(3));
 /* a folder whose first open failed is not listed by a later probe, so it is not complete */
 assert(complete("mass0:/LATE/")==-ENOENT&&!open_handles);
 /* MX4SIO tries other spellings of a path: a folder one of them opens was read,
  * even when another fails after it (here the empty root, as "mx4sio:/" only) */
 add("mx4sio:/",0);add("mx4sio:/DIR/",3);
 assert(complete("mx4sio:/")==0&&complete("mx4sio:/DIR/")==3&&listed(3));
 assert(complete("mx4sio:/GONE/")==-ENOENT&&!open_handles);
 /* MMCE ends every folder with -1, and cdfs with -EPERM: both are the end, not an error */
 add("mmce0:/DIR/",7)->end_code=-1;add("cdfs:/DIR/",7)->end_code=-EPERM;
 assert(complete("mmce0:/DIR/")==7&&listed(7)&&!dir_complete);
 assert(complete("cdfs:/DIR/")==7&&listed(7));
 /* PC card CF: an ATA error latches the card offline, and FAT then ends the read
  * early with no error, so a card offline after the read was cut short */
 add("cf0:/DIR/",5);assert(complete("cf0:/DIR/")==5&&listed(5));
 cf_online=1;assert(complete("cf0:/DIR/")==-ENODEV&&!open_handles);
 cf_online=1;assert(browse("cf0:/DIR/")==0); /* the browser still shows it empty */
 cf_online=0;assert(complete("cf0:/DIR/")==-ENODEV&&browse("cf0:/DIR/")==0); /* offline before it */
 cf_online=-1;assert(complete("cf0:/DIR/")==5&&listed(5));
 /* VMC, PFS and host end a folder with 0, so their negative result is a failed read */
 Folder *f=add("vmc0:/DIR/",9);f->dots=1;f->fail_at=4;f->fail_code=-EIO;
 assert(complete("vmc0:/DIR/")==-EIO&&!open_handles);
 assert(browse("vmc0:/DIR/")==4&&listed(4)); /* the browser shows what was read */
 add("vmc0:/OK/",9)->dots=1;assert(complete("vmc0:/OK/")==9&&listed(9)); /* nothing carries over */
 f=add("pfs0:/DIR/",9);f->dots=1;f->fail_at=6;f->fail_code=-EIO;
 assert(complete("hdd0:/PART/DIR/")==-EIO&&!open_handles);
 assert(browse("hdd0:/PART/DIR/")==6&&listed(6));
 f->fail_at=-1;assert(complete("hdd0:/PART/DIR/")==9&&listed(9));
 assert(complete("hdd0:/PART/GONE/")==-ENOENT&&browse("hdd0:/PART/GONE/")==0);
 hdd_partitions=0;assert(complete("hdd0:/")==-ENODEV); /* a drive with no partition list was not read */
 hdd_partitions=3;hddNparties[0]=0;assert(complete("hdd0:/")==3&&!strcmp(info[2].name,"+PART2"));
 /* ps2client returns -1 when the network fails: the listing stops with no stale entry */
 f=add("host:DIR/",9);f->fail_at=2;f->fail_code=-1;
 assert(complete("host:/DIR/")==-1&&!open_handles);
 assert(browse("host:/DIR/")==2&&listed(2)&&!info[2].name[0]);
 f->fail_at=-1;assert(complete("host:/DIR/")==9&&listed(9));
 add("host:OVER/",CAP+1);assert(complete("host:/OVER/")==-E2BIG);
 assert(complete_more("host:/OVER/")==CAP+1&&more==1&&listed(CAP+1));
 assert(complete("host:/GONE/")==-ENOENT&&browse("host:/GONE/")==0);
 /* Memory card: a subfolder lists "." and ".." too, so the table must hold two more */
 add("mc0:/FULL/",CAP)->dots=1;add("mc0:/OVER/",CAP+1)->dots=1;add("mc1:/",CAP+1);
 assert(complete("mc0:/FULL/")==CAP&&listed(CAP)&&mc_maxent==MAX_ENTRY+1&&dir_complete);
 assert(browse("mc0:/FULL/")==CAP&&listed(CAP)&&mc_maxent==CAP+2);
 assert(complete("mc0:/OVER/")==-E2BIG);
 assert(complete_more("mc0:/OVER/")==CAP+1&&more==1&&listed(CAP+1));
 assert(complete("mc1:/")==-E2BIG&&browse("mc1:/")==CAP&&listed(CAP));
 assert(complete("mc0:/GONE/")==-ENOENT&&browse("mc0:/GONE/")==0);
 add("mc0:/BAD/",3)->open_error=sceMcResNoFormat;assert(complete("mc0:/BAD/")==-EIO&&browse("mc0:/BAD/")==0);
 mc_ready=sceMcResChangedCard;assert(complete("mc0:/FULL/")==-ENODEV&&browse("mc0:/FULL/")==0);
 mc_ready=sceMcResSucceed;assert(complete("mc0:/FULL/")==CAP);
 /* PSX flash: a folder lists "." and ".." like a memory card one; its getdir
  * error is a folder that could not be read, shown empty in the browser */
 add("xfrom:/FULL/",CAP)->dots=1;add("xfrom:/OVER/",CAP+1)->dots=1;
 assert(complete("xfrom:/FULL/")==CAP&&listed(CAP)&&browse("xfrom:/FULL/")==CAP&&listed(CAP));
 assert(complete("xfrom:/OVER/")==-E2BIG);
 assert(complete_more("xfrom:/OVER/")==CAP+1&&more==1&&listed(CAP+1));
 assert(complete("xfrom:/GONE/")==-ENOENT&&browse("xfrom:/GONE/")==0);
 add("xfrom:/BAD/",3)->open_error=sceMcResNoFormat;
 assert(complete("xfrom:/BAD/")==-EIO&&browse("xfrom:/BAD/")==0);
 console_is_PSX=0;assert(complete("xfrom:/FULL/")==-ENODEV&&complete("dvr_hdd0:/")==-ENODEV);
 console_is_PSX=1;
 /* DVR drive: its partition list stands in for the root, and a folder read
  * that fails partway is an error, as on the HDD */
 assert(complete("dvr_hdd0:/")==-ENODEV&&browse("dvr_hdd0:/")==0); /* no partition list */
 dvr_partitions=2;ndvrpparties=0;assert(complete("dvr_hdd0:/")==2&&!strcmp(info[1].name,"+DVR1"));
 f=add("dvr_pfs0:/DIR/",9);f->dots=1;f->fail_at=5;f->fail_code=-EIO;
 assert(complete("dvr_hdd0:/+DVR0/DIR/")==-EIO&&!open_handles);
 assert(browse("dvr_hdd0:/+DVR0/DIR/")==5&&listed(5));
 f->fail_at=-1;assert(complete("dvr_hdd0:/+DVR0/DIR/")==9&&listed(9));
 assert(complete("dvr_hdd0:/+DVR0/GONE/")==-ENOENT&&browse("dvr_hdd0:/+DVR0/GONE/")==0);
 /* a device nothing reads */
 assert(complete("xyz0:/")==-ENODEV&&browse("xyz0:/")==0);
 assert(!open_handles);
 puts("PASS: complete listings fail on an open or read error and over 2046 entries (or list 2047 to remove), "
      "keep each driver's end of folder, and the browser listing still shows what it read");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='listing-complete-') as folder:
    path = Path(folder)
    (path / 'test.c').write_text(code)
    exe = path / 'test'
    cc = shutil.which('gcc') or shutil.which('cc')
    if not cc:
        raise SystemExit('Host C compiler required')
    subprocess.run([cc, '-std=gnu99', '-Wall', '-Wno-unused-function', '-Wno-format-truncation',
                    '-Werror', str(path / 'test.c'),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=60)
