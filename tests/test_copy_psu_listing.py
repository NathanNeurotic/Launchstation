"""Run copy() and subfunc_Paste() through PSU backup, PSU restore and folder
pastes on POSIX fixtures, with the real psu_functions.c and readGENERIC().

Each folder level allocates its listing before it removes or creates anything
at the destination. When that allocation fails, an older PSU backup or save
must be left exactly as it was, and that level creates no folder. Every early
exit frees the listing, and a PSU restore that stops early closes its PSU. A
file whose first entry is not a folder is not a PSU and is refused and closed.

Drivers are replaced by POSIX files; libmc and the IOP are not emulated.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
copy = (root / 'src/filer_copy.c').read_text()
browser = (root / 'src/filer_browser.c').read_text()
filer = (root / 'src/filer.c').read_text()
psu_source = (root / 'src/psu_functions.c').read_text()
psu_types = (root / 'include/psu_types.h').read_text()


def function(source, signature):
    a = source.index(signature)
    b = source.index('{', a) + 1
    depth = 1
    while depth:
        depth += (source[b] == '{') - (source[b] == '}')
        b += 1
    return source[a:b] + '\n'


code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <malloc.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;typedef int64_t s64;
#define MAX_PATH 1024
#define MAX_NAME 256
#define MAX_ENTRY 16
#define MAX_RECURSE 16
#define FILEOP_TRACE 0
#define UDPFS 1
#define ETH 1
#define DVRP 1
#define GIT_HASH "fixture-head"
#define TRUE 1
#define FALSE 0
#define PM_NORMAL 0
#define PM_PSU_BACKUP 1
#define PM_PSU_RESTORE 2
#define PM_RENAME 3
#define TRANSFER_STACK_READY 0
#define TRANSFER_STACK_INCOMPATIBLE -2
#define COPY_BUFFER_FAST_DEFAULT 0x100000
#define TRANSFER_ETA_UPDATE_MS 1000
#define SOURCE_DEVICE_WAIT_TIMEOUT_MS 6000
#define SOURCE_DEVICE_WAIT_INTERVAL_MS 1000
#define MC_SFI 0xFEED
#define MC_ATTR_norm_folder 0x8427
#define MC_ATTR_norm_file 0x8497
#define HDD_ATTR_save_prot_folder 0xC4AF
#define HDD_ATTR_save_folder 0xC4A7
#define sceMcFileAttrSubdir 0x20
#define sceMcFileAttrPDAExec 0x100
#define sceMcFileAttrDupProhibit 0x80
#define FIO_O_RDONLY O_RDONLY
#define FIO_O_WRONLY O_WRONLY
#define FIO_O_CREAT O_CREAT
#define FIO_O_EXCL O_EXCL
#define FIO_O_TRUNC O_TRUNC
#define FIO_S_IFDIR 0x1000
#define FIO_S_IFREG 0x2000
#define FIO_S_ISDIR(m) (((m)&FIO_S_IFDIR)!=0)
#define FIO_S_ISREG(m) (((m)&FIO_S_IFREG)!=0)
#define FIO_CST_ATTR 1
#define FIO_CST_CT 2
#define FIO_CST_AT 4
#define FIO_CST_MT 8
#define LNG(x) #x
#define DPRINTF(...) ((void)0)
/* libmc.h layout: the 64-byte alignment is what made copy() realign $sp */
typedef struct {u8 Resv2,Sec,Min,Hour,Day,Month;u16 Year;} sceMcStDateTime;
typedef struct {sceMcStDateTime _Create,_Modify;u32 FileSizeByte;u16 AttrFile,Reserve1;u32 Reserve2,PdaAplNo;unsigned char EntryName[32];} __attribute__((aligned(64))) sceMcTblGetDir;
typedef struct {char name[MAX_NAME];unsigned char title[32*2+2];sceMcTblGetDir stats;} FILEINFO;
typedef struct {unsigned mode,attr,size;char ctime[8],atime[8],mtime[8];unsigned hisize;} iox_stat_t;
typedef struct {iox_stat_t stat;char name[256];} iox_dirent_t;
static struct {int PSU_HugeNames,PSU_DateNames,PSU_NoOverwrite;} config,*setting=&config;
static int PSU_content;
static int PM_flag[MAX_RECURSE],PM_file[MAX_RECURSE],PasteMode,PasteProgress_f,mctype_PSx;
static int nclipFiles,browser_cut,browser_pushed,browser_cd;
static FILEINFO clipFiles[MAX_ENTRY];static char clipPath[MAX_PATH];
static unsigned clipIopResetGeneration,generation;
static u64 written_size,PasteTime,clock_ms;
static int size_valid,time_valid;
static int yn_answer=1,yn_calls,cancel_copy;
/* Every file and directory handle and every heap block the copy code takes is tracked. */
static int fd_open[1024],handles,double_closes;
/* The Nth write fails as on a full device; allocations fail while copy() is at one stage. */
static int writes,write_fail_at;static const char *alloc_fail_stage,*const *diag_stage;
static size_t fail_alloc_bytes;static int allocs_live,bad_frees;static void *live_ptr[256];
static void *test_memalign(size_t a,size_t n){
 if(fail_alloc_bytes&&n==fail_alloc_bytes)return NULL;
 if(alloc_fail_stage&&*diag_stage&&!strcmp(*diag_stage,alloc_fail_stage))return NULL;
 void *p=memalign(a,n);assert(p&&((uintptr_t)p%a)==0);assert(allocs_live<256);live_ptr[allocs_live++]=p;return p;
}
static void test_free(void *p){
 if(!p)return;
 for(int i=0;i<allocs_live;i++)if(live_ptr[i]==p){live_ptr[i]=live_ptr[--allocs_live];free(p);return;}
 bad_frees++; /* a double free or a foreign pointer: never hand it to libc */
}
#define memalign test_memalign
#define free test_free
static u64 Timer(void){return ++clock_ms;}
static unsigned getIopResetGeneration(void){return generation;}
static int prepareTransferDeviceStacks(const char *a,const char *b){(void)a;(void)b;return TRANSFER_STACK_READY;}
static int clipboardSourceDeviceReady(void){return 1;}
static void waitUntilTimer(u64 end){clock_ms=end;}
static void drawMsg(const char *s){(void)s;}
static int ynDialog(const char *s){(void)s;yn_calls++;return yn_answer;}
static int readpad(void){return cancel_copy;}
static int DashIsActive(void){return 1;}
static int DashFileProgress(const char *a,const char *b,u64 r,u64 t){(void)a;(void)b;(void)r;(void)t;return 1;}
static int DashFileTransferChunk(void (*run)(void *),void *arg){run(arg);return 1;}
static void nonDialog(char *s){(void)s;}
static int keyboard(char *s,int n){(void)s;(void)n;return 0;}
static int isPcCardCfPath(const char *s){(void)s;return 0;}
static int isPcCardCfOnline(void){return 1;}
static void ensureMemoryCardPortAccessible(int p){assert(p==0||p==1);}
static char *makeHostPath(char *d,char *s){if(d!=s)strcpy(d,s);return d;}
static void genLimObjName(char *p,int reserve){(void)p;(void)reserve;}
static int genOpen(const char *p,int flags){int fd=open(p,flags,0600);if(fd<0)return -errno;assert(fd<1024);fd_open[fd]=1;handles++;return fd;}
static int genClose(int fd){if(fd<0||fd>=1024||!fd_open[fd]){double_closes++;return -EBADF;}fd_open[fd]=0;handles--;return close(fd)?-errno:0;}
static s64 genLseek(int fd,s64 n,int whence){assert(fd_open[fd]);return lseek(fd,n,whence);}
static int genRead(int fd,void *p,int n){assert(fd_open[fd]);int r=read(fd,p,n);return r<0?-errno:r;}
static int genWrite(int fd,void *p,int n){assert(fd_open[fd]);if(++writes==write_fail_at)return -ENOSPC;int r=write(fd,p,n);return r<0?-errno:r;}
static int genRemove(char *p){return unlink(p)?-errno:0;}
static int mcSetFileInfo(int port,int slot,const char *from,const sceMcTblGetDir *info,int flags){(void)port;(void)slot;(void)from;(void)info;(void)flags;return 0;}
static int mcSync(int mode,void *c,int *r){(void)mode;(void)c;if(r)*r=0;return 1;}
static int mcGetInfo(int p,int s,int *type,int *a,int *b){(void)p;(void)s;if(type)*type=2;if(a)*a=1000;if(b)*b=1000;return 0;}
static int fileXioChStat(const char *p,const iox_stat_t *s,int f){(void)p;(void)s;(void)f;return 0;}
/* HDD and DVR partitions are the folders pfsN: and dvr_pfsN: here, mapped by the
 * real getHddParty() and getHddDVRPParty(). The Nth path lookup or the Nth mount
 * fails, and the Nth HDD mount can land in another slot (mount_slot[N]). */
static int party_calls,party_fail_at,mount_calls,mount_fail_at,mount_slot[16];
static int real_getHddParty(const char *path,const FILEINFO *file,char *party,char *dir);
static int real_getHddDVRPParty(const char *path,const FILEINFO *file,char *party,char *dir);
static int getDVRPPartyMountIndex(const char *party);
static int getHddParty(const char *p,const FILEINFO *f,char *a,char *b){return ++party_calls==party_fail_at?-1:real_getHddParty(p,f,a,b);}
static int getHddDVRPParty(const char *p,const FILEINFO *f,char *a,char *b){return ++party_calls==party_fail_at?-1:real_getHddDVRPParty(p,f,a,b);}
static int mountParty(const char *p){(void)p;return ++mount_calls==mount_fail_at?-1:mount_slot[mount_calls%16];}
static int mountDVRPParty(const char *p){return ++mount_calls==mount_fail_at?-1:getDVRPPartyMountIndex(p);}
static int fileShowTitles(void){return 0;}
static int filerGetGameTitle(const char *p,const FILEINFO *f,unsigned char *out){(void)p;(void)f;out[0]=0;return -1;}
static void transcpy_sjis(char *out,const unsigned char *in){strcpy(out,(const char *)in);}
/* A protected (exploit or system-update) folder: only a named entry can be one here. */
static int protected_folder,modify_answer=1,modify_calls,delete_questions,delete_fail;
static int filerIsExploitProtectedPath(const char *p,const FILEINFO *f){(void)p;return f!=NULL&&protected_folder;}
/* Like filerConfirmExploitAction: only a protected folder asks a question. */
static int filerConfirmExploitDelete(const char *p,const FILEINFO *f){if(filerIsExploitProtectedPath(p,f))delete_questions++;return 1;}
static int filerConfirmExploitModify(const char *p,const FILEINFO *f){(void)p;(void)f;modify_calls++;return modify_answer;}
static int remove_tree(const char *p){struct stat st;if(lstat(p,&st))return -errno;if(S_ISDIR(st.st_mode)){DIR *d=opendir(p);struct dirent *e;while(d&&(e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char c[MAX_PATH];snprintf(c,sizeof(c),"%s/%s",p,e->d_name);remove_tree(c);}if(d)closedir(d);return rmdir(p)?-errno:0;}return unlink(p)?-errno:0;}
static int delete(const char *p,const FILEINFO *f){if(delete_fail)return -EIO;char t[MAX_PATH];snprintf(t,sizeof(t),"%s%s",p,f->name);return remove_tree(t);}
static void markMx4sioDestinationAfterWrite(const char *p){(void)p;}
static int genCmpFileExt(const char *name,const char *ext){const char *p=strrchr(name,'.');return p!=NULL&&!strcasecmp(p+1,ext);}
/* Like the real newdir(), it maps an HDD or DVR parent to its partition's folder. */
static int newdir(const char *parent,const char *name){char p[MAX_PATH],party[MAX_NAME];int dvr=!strncmp(parent,"dvr_hdd",7),slot;
 if(dvr||!strncmp(parent,"hdd",3)){if((dvr?getHddDVRPParty:getHddParty)(parent,NULL,party,p)<0)return -1;
  if((slot=dvr?mountDVRPParty(party):mountParty(party))<0)return -1;p[dvr?7:3]='0'+slot;strcat(p,name);}
 else snprintf(p,sizeof(p),"%s%s",parent,name);
 return mkdir(p,0700)?-errno:0;}
static DIR *dirs[64];static char dir_path[64][MAX_PATH];
static int fileXioDopen(const char *p){DIR *d=opendir(p);if(!d)return -errno;for(int i=0;i<64;i++)if(!dirs[i]){dirs[i]=d;snprintf(dir_path[i],MAX_PATH,"%s",p);return i;}assert(0);return -1;}
static int fileXioDread(int fd,iox_dirent_t *r){struct dirent *e=readdir(dirs[fd]);if(!e)return 0;memset(r,0,sizeof(*r));snprintf(r->name,sizeof(r->name),"%s",e->d_name);char full[MAX_PATH*2];snprintf(full,sizeof(full),"%s%s",dir_path[fd],e->d_name);struct stat st;if(stat(full,&st))return -errno;r->stat.mode=S_ISDIR(st.st_mode)?FIO_S_IFDIR:FIO_S_IFREG;r->stat.size=st.st_size;return 1;}
static int fileXioDclose(int fd){closedir(dirs[fd]);dirs[fd]=0;return 0;}
static void clearMcTable(sceMcTblGetDir *mcT){memset((void *)mcT,0,sizeof(sceMcTblGetDir));}
'''
# The real HDD and DVR path mapping, under the names that the stubs above wrap.
hdd = (root / 'src/gui_hdd0_format.c').read_text()
code += function(hdd, 'static int getHddDeviceFromPath(')
code += 'static ' + function(hdd, 'int getHddParty(').replace('int getHddParty(', 'int real_getHddParty(', 1)
code += 'static ' + function(hdd, 'int getHddDVRPParty(').replace('int getHddDVRPParty(', 'int real_getHddDVRPParty(', 1)
code += 'static ' + function((root / 'src/filer_mount.c').read_text(), 'int getDVRPPartyMountIndex(')
reader = function(filer, 'int readGENERIC(const char *path, FILEINFO *info, int max)\n{')
code += 'static ' + reader
code += 'static int getDir(const char *p,FILEINFO *out){return readGENERIC(p,out,MAX_ENTRY-2);}\n'
code += 'static int applyMcInfoToVmcPath(const char *p,const sceMcTblGetDir *s){(void)p;(void)s;return 0;}\n'
# The real PSU layout and helpers (their includes are replaced by the stubs above).
code += psu_types[psu_types.index('typedef struct'):psu_types.rindex('#endif')]
code += psu_source[psu_source.index('#define PSU_HEADER_SIZE'):].replace(
    'void clear_psu_header', 'static void clear_psu_header').replace(
    'void pad_psu_header', 'static void pad_psu_header')
a = copy.index('/* One paste context')
b = copy.index('static int isMemoryCardLikePath(', a)
code += copy[a:b]
for signature in ('static int isMemoryCardLikePath(', 'static int isHddCommonParty(',
                  'static int calcTransferSpeed(', 'static int calcRemainingTime(',
                  'static int smoothTransferSpeed(', 'static int applyMcInfoToMcPath('):
    code += function(copy, signature)
code += copy[copy.index('int copy('):]
code += function(browser, 'static int waitForClipboardSourceDevice(')
code += function(browser, 'static void subfunc_Paste(')
# A failed check names its line in this file.
code += '#line %d "%s"' % (Path(__file__).read_text().splitlines().index('static char message[MAX_PATH];') + 1,
                           Path(__file__).name)
code += r'''
static char message[MAX_PATH];
static void put(const char *p,const void *data,int n){int fd=open(p,O_CREAT|O_TRUNC|O_WRONLY,0600);assert(fd>=0);assert(write(fd,data,n)==n);assert(!close(fd));}
static int exists(const char *p){return !access(p,F_OK);}
static int entries(const char *p){int n=0;DIR *d=opendir(p);struct dirent *e;if(!d)return -1;while((e=readdir(d)))if(strcmp(e->d_name,".")&&strcmp(e->d_name,".."))n++;closedir(d);return n;}
static int read_all(const char *p,unsigned char *out,int max){int fd=open(p,O_RDONLY);if(fd<0)return -1;int n=read(fd,out,max);close(fd);return n;}
static int filled(const char *p,int n,int byte){static unsigned char b[1<<19];if(read_all(p,b,sizeof(b))!=n)return 0;for(int i=0;i<n;i++)if(b[i]!=byte)return 0;return 1;}
static void clip(const char *path,const char *name,int attr,int mode){
 yn_answer=1;yn_calls=0;cancel_copy=0;fail_alloc_bytes=0;browser_cut=0;generation=clipIopResetGeneration=1;
 writes=write_fail_at=0;alloc_fail_stage=NULL;
 party_calls=party_fail_at=mount_calls=mount_fail_at=0;memset(mount_slot,0,sizeof(mount_slot));
 protected_folder=0;modify_answer=1;modify_calls=delete_questions=delete_fail=0;
 PasteMode=mode;strcpy(clipPath,path);nclipFiles=1;memset(clipFiles,0,sizeof(clipFiles));
 strcpy(clipFiles[0].name,name);clipFiles[0].stats.AttrFile=attr;
}
static void paste(const char *destination){message[0]=0;subfunc_Paste(message,(char *)destination);}
/* The failure message has this text; when it does not, it is shown. */
static int says(const char *text){if(strstr(message,text))return 1;fprintf(stderr,"message: %s\n",message);return 0;}
/* After every paste: no file or directory handle left open or closed twice, no heap block left. */
#define settled() settled_at(__LINE__)
static void settled_at(int line){int open_dirs=0;for(int i=0;i<64;i++)open_dirs+=dirs[i]!=NULL;
 if(open_dirs||handles||double_closes||allocs_live||bad_frees){
  fprintf(stderr,"%s:%d: not settled: %d directories and %d files open, %d double closes, %d heap blocks, %d bad frees\n",
          __FILE__,line,open_dirs,handles,double_closes,allocs_live,bad_frees);abort();}}
/* A PSU: root header, "." and "..", then each file's header and data padded to 1 KB. */
static void psu_entry(int fd,u16 attr,u32 size,const char *name){psu_header h;memset(&h,0,sizeof(h));h.attr=attr;h.size=size;snprintf((char *)h.name,sizeof(h.name),"%s",name);assert(write(fd,&h,sizeof(h))==sizeof(h));}
static void make_psu(const char *path,const char *folder,int count){
 int fd=open(path,O_CREAT|O_TRUNC|O_WRONLY,0600);assert(fd>=0);
 psu_entry(fd,0x8427,2+count,folder);psu_entry(fd,0x8427,0,".");psu_entry(fd,0x8427,0,"..");
 for(int i=0;i<count;i++){char n[32];unsigned char b[1024];snprintf(n,sizeof(n),"FILE%d.BIN",i);int length=100+i;
  psu_entry(fd,0x8497,length,n);memset(b,'A'+i,sizeof(b));assert(write(fd,b,length)==length);
  int pad=(1024-(length&1023))&1023;memset(b,0xFF,pad);assert(write(fd,b,pad)==pad);}
 close(fd);
}
int main(void){
 diag_stage=&copy_diag.stage;
 mkdir("mass:",0700);mkdir("mc0:",0700);mkdir("mc1:",0700);mkdir("udpfs:",0700);
 mkdir("pfs0:",0700);mkdir("dvr_pfs1:",0700);
 make_psu("mass:/SAVE.psu","BASLUS-00001",2);
 make_psu("pfs0:/SAVE.psu","BASLUS-00001",2);make_psu("dvr_pfs1:/SAVE.psu","BASLUS-00001",2);

 /* A PSU restores into a new folder. */
 clip("mass:/","SAVE.psu",0x8497,PM_PSU_RESTORE);paste("mc0:/");
 assert(!message[0]&&entries("mc0:/BASLUS-00001")==2);
 assert(filled("mc0:/BASLUS-00001/FILE0.BIN",100,'A')&&filled("mc0:/BASLUS-00001/FILE1.BIN",101,'B'));
 settled();

 /* Declining to overwrite an existing save keeps it, and closes the PSU. */
 put("mc0:/BASLUS-00001/OLD.BIN","old",3);
 clip("mass:/","SAVE.psu",0x8497,PM_PSU_RESTORE);yn_answer=-1;paste("mc0:/");
 assert(strstr(message,"Creating directory (-1)")&&yn_calls==1);
 assert(exists("mc0:/BASLUS-00001/OLD.BIN")&&entries("mc0:/BASLUS-00001")==3);
 settled();

 /* No memory for the PSU entry: the restore stops before it asks to overwrite
  * or deletes anything, so the old save stays whole. */
 clip("mass:/","SAVE.psu",0x8497,PM_PSU_RESTORE);fail_alloc_bytes=sizeof(FILEINFO);paste("mc0:/");
 assert(strstr(message,"Listing source (-12)")&&strstr(message,"Not enough memory")&&!yn_calls);
 assert(exists("mc0:/BASLUS-00001/OLD.BIN")&&entries("mc0:/BASLUS-00001")==3);
 settled();
 /* The same failure with no save there creates no folder. */
 clip("mass:/","SAVE.psu",0x8497,PM_PSU_RESTORE);fail_alloc_bytes=sizeof(FILEINFO);paste("mc1:/");
 assert(strstr(message,"Listing source (-12)")&&!exists("mc1:/BASLUS-00001"));
 settled();
 /* So does no memory for the files' copy buffer, which is taken before any
  * question too: the old save stays whole, and no folder is created. */
 clip("mass:/","SAVE.psu",0x8497,PM_PSU_RESTORE);alloc_fail_stage="Allocating buffer";paste("mc0:/");
 settled();
 assert(says("Allocating buffer (-12)")&&strstr(message,"Not enough memory")&&!yn_calls);
 assert(exists("mc0:/BASLUS-00001/OLD.BIN")&&entries("mc0:/BASLUS-00001")==3);
 clip("mass:/","SAVE.psu",0x8497,PM_PSU_RESTORE);alloc_fail_stage="Allocating buffer";paste("mc1:/");
 settled();
 assert(says("Allocating buffer (-12)")&&!exists("mc1:/BASLUS-00001"));
 /* A file bigger than that buffer (128 KB to a memory card) restores through it in chunks. */
 {static unsigned char big[300000+32];int fd=open("mass:/BIG.psu",O_CREAT|O_TRUNC|O_WRONLY,0600);assert(fd>=0);
  psu_entry(fd,0x8427,3,"BIGSAVE");psu_entry(fd,0x8427,0,".");psu_entry(fd,0x8427,0,"..");psu_entry(fd,0x8497,300000,"BIG.BIN");
  memset(big,'Z',300000);memset(big+300000,0xFF,32);assert(write(fd,big,sizeof(big))==sizeof(big));close(fd);}
 clip("mass:/","BIG.psu",0x8497,PM_PSU_RESTORE);paste("mc1:/");
 assert(!message[0]&&filled("mc1:/BIGSAVE/BIG.BIN",300000,'Z'));
 settled();remove_tree("mc1:/BIGSAVE");

 /* An accepted overwrite replaces the old save. */
 clip("mass:/","SAVE.psu",0x8497,PM_PSU_RESTORE);paste("mc0:/");
 assert(!message[0]&&yn_calls==1&&!exists("mc0:/BASLUS-00001/OLD.BIN")&&entries("mc0:/BASLUS-00001")==2);
 assert(!delete_questions&&!modify_calls);
 settled();

 /* A PSU whose folder is protected asks once, before anything else; declining
  * keeps the old save, frees the listing and closes the PSU. */
 put("mc0:/BASLUS-00001/OLD.BIN","old",3);
 clip("mass:/","SAVE.psu",0x8497,PM_PSU_RESTORE);protected_folder=1;modify_answer=-1;paste("mc0:/");
 assert(strstr(message,"Creating directory (-1)")&&modify_calls==1&&!yn_calls&&!delete_questions);
 assert(exists("mc0:/BASLUS-00001/OLD.BIN")&&entries("mc0:/BASLUS-00001")==3);
 settled();
 /* Accepting it replaces the save without asking the delete question again. */
 clip("mass:/","SAVE.psu",0x8497,PM_PSU_RESTORE);protected_folder=1;paste("mc0:/");
 assert(!message[0]&&modify_calls==1&&yn_calls==1&&!delete_questions);
 assert(!exists("mc0:/BASLUS-00001/OLD.BIN")&&entries("mc0:/BASLUS-00001")==2);
 settled();

 /* A destination folder that cannot be created closes the PSU too. */
 clip("mass:/","SAVE.psu",0x8497,PM_PSU_RESTORE);paste("missing:/");
 assert(strstr(message,"Creating directory (-2)"));
 settled();
 /* So does an accepted overwrite whose delete fails; the old save stays. */
 put("mc0:/BASLUS-00001/OLD.BIN","old",3);
 clip("mass:/","SAVE.psu",0x8497,PM_PSU_RESTORE);delete_fail=1;paste("mc0:/");
 assert(strstr(message,"Creating directory (-1)")&&yn_calls==1&&exists("mc0:/BASLUS-00001/OLD.BIN"));
 settled();

 /* A file whose first entry is not a folder is not a PSU (here another format
  * renamed to .psu, its header naming a file beside it, and a blank file). It
  * is refused and closed, and nothing is copied. */
 put("mass:/OTHER.BIN","other",5);
 {int fd=open("mass:/RENAMED.psu",O_CREAT|O_TRUNC|O_WRONLY,0600);assert(fd>=0);psu_entry(fd,0x4643,0,"OTHER.BIN");close(fd);}
 {unsigned char zero[512];memset(zero,0,sizeof(zero));put("mass:/BLANK.psu",zero,sizeof(zero));}
 clip("mass:/","RENAMED.psu",0x8497,PM_PSU_RESTORE);paste("mc1:/");
 assert(strstr(message,"Opening source (-1)")&&!exists("mc1:/OTHER.BIN"));
 settled();
 clip("mass:/","BLANK.psu",0x8497,PM_PSU_RESTORE);paste("mc1:/");
 assert(strstr(message,"Opening source (-1)")&&entries("mc1:")==0);
 settled();

 /* A PSU on the HDD or the DVR restores, and so does one restored to them. */
 clip("hdd0:/+SAVES/","SAVE.psu",0x8497,PM_PSU_RESTORE);paste("mc1:/");
 assert(!message[0]&&filled("mc1:/BASLUS-00001/FILE0.BIN",100,'A')&&filled("mc1:/BASLUS-00001/FILE1.BIN",101,'B'));
 settled();remove_tree("mc1:/BASLUS-00001");
 clip("mass:/","SAVE.psu",0x8497,PM_PSU_RESTORE);paste("dvr_hdd0:/__xdata/");
 assert(!message[0]&&filled("dvr_pfs1:/BASLUS-00001/FILE1.BIN",101,'B'));
 settled();remove_tree("dvr_pfs1:/BASLUS-00001");
 /* A restore maps its partitions twice, the second time with the .psu open.
  * When the source's or destination's HDD or DVR partition cannot be looked up
  * or mounted then, the .psu is closed once (settled() counts files left open
  * and files closed twice) and nothing is restored. */
 {const char *from[]={"hdd0:/+SAVES/","dvr_hdd0:/__xdata/","mass:/","mass:/"};
  const char *to[]={"mc1:/","mc1:/","hdd0:/+SAVES/","dvr_hdd0:/__xdata/"};
  for(int c=0;c<8;c++){
   clip(from[c/2],"SAVE.psu",0x8497,PM_PSU_RESTORE);*(c&1?&mount_fail_at:&party_fail_at)=2;paste(to[c/2]);
   settled();
   assert(says("Preparing devices (-1)")&&entries("mc1:")==0&&entries("pfs0:")==1&&entries("dvr_pfs1:")==1);}}
 /* So is one whose destination partition is mounted in the source's slot on
  * the second pass, which makes the two folder paths equal. */
 clip("hdd0:/+SAVES/","SAVE.psu",0x8497,PM_PSU_RESTORE);mount_slot[2]=1;paste("hdd0:/+OTHER/");
 settled();
 assert(says("Preparing devices (-1)")&&entries("pfs0:")==1);

 /* PSU backup of a save folder: header, ".", "..", then 512 + 1024 bytes per file. */
 unsigned char good[8192],now[8192];
 mkdir("mc0:/SAVEDIR",0700);put("mc0:/SAVEDIR/A.BIN","aaaa",4);put("mc0:/SAVEDIR/B.BIN","bbbbbbbb",8);
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);paste("mass:/");
 int size=read_all("mass:/SAVEDIR.psu",good,sizeof(good));
 assert(!message[0]&&size==3*512+2*(512+1024));
 settled();
 /* A backup whose PSU cannot be created says so, and frees its listing. */
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);paste("missing:/");
 assert(says("Creating PSU (-1)")&&!exists("missing:"));
 settled();
 /* No memory for the next backup's listing: the old backup is not removed. */
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);fail_alloc_bytes=MAX_ENTRY*sizeof(FILEINFO);paste("mass:/");
 assert(strstr(message,"Listing source (-12)")&&strstr(message,"Not enough memory"));
 assert(read_all("mass:/SAVEDIR.psu",now,sizeof(now))==size&&!memcmp(good,now,size));
 settled();
 /* A backup that stops inside a file leaves the .psu to the folder level that
  * opened it, which closes it once (settled() counts a second close): the
  * device fills up at the first file's entry header (write 4, after the
  * three root headers), its data (5) or its padding (6), the user cancels,
  * or there is no memory for the copy buffer. */
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);write_fail_at=4;paste("mass:/");
 assert(says("Writing PSU entry (-5)"));
 settled();
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);write_fail_at=5;paste("mass:/");
 assert(strstr(message,"Writing destination (-28)")&&strstr(message,"Not enough free space"));
 settled();
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);write_fail_at=6;paste("mass:/");
 assert(strstr(message,"Writing destination (-5)"));
 settled();
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);cancel_copy=1;yn_answer=-1;paste("mass:/");
 assert(strstr(message,"Copy cancelled (-1)"));
 settled();
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);alloc_fail_stage="Allocating buffer";paste("mass:/");
 assert(strstr(message,"Allocating buffer (-12)")&&strstr(message,"Not enough memory"));
 settled();
 /* With dated names and no overwriting, an existing name is skipped, and the
  * skip frees the listing. */
 config.PSU_DateNames=config.PSU_NoOverwrite=1;
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);paste("mass:/");
 assert(!message[0]&&read_all("mass:/SAVEDIR_0000-00-00_00-00-00.psu",now,sizeof(now))==size);
 settled();
 put("mass:/SAVEDIR_0000-00-00_00-00-00.psu","kept",4);
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);paste("mass:/");
 assert(!message[0]&&read_all("mass:/SAVEDIR_0000-00-00_00-00-00.psu",now,sizeof(now))==4);
 settled();
 config.PSU_DateNames=config.PSU_NoOverwrite=0;
 /* A subfolder cannot go into a PSU; the backup stops and frees its listing.
  * A folder named like the save at the destination must not let the
  * subfolder be written there as a PSU of its own. */
 mkdir("mc0:/SAVEDIR/SUB",0700);mkdir("mass:/SAVEDIR",0700);
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);paste("mass:/");
 assert(says("Adding subfolder to PSU (-1)")&&strstr(message,"\nSUB\n")&&!exists("mass:/SAVEDIR/SUB.psu"));
 settled();

 /* An ordinary folder paste with no memory for its listing creates nothing. */
 mkdir("udpfs:/TOP",0700);put("udpfs:/TOP/F.BIN","y",1);
 clip("udpfs:/","TOP",0x8427,PM_NORMAL);fail_alloc_bytes=MAX_ENTRY*sizeof(FILEINFO);paste("mc1:/");
 assert(strstr(message,"Listing source (-12)")&&!exists("mc1:/TOP"));
 settled();
 clip("udpfs:/","TOP",0x8427,PM_NORMAL);paste("mc1:/");
 assert(!message[0]&&filled("mc1:/TOP/F.BIN",1,'y'));
 settled();
 /* Declining to paste into the existing folder frees the listing. */
 clip("udpfs:/","TOP",0x8427,PM_NORMAL);yn_answer=-1;paste("mc1:/");
 assert(strstr(message,"Creating directory (-1)")&&yn_calls==1);
 settled();
 /* So does declining the protected-folder question after accepting the overwrite. */
 clip("udpfs:/","TOP",0x8427,PM_NORMAL);modify_answer=-1;paste("mc1:/");
 assert(strstr(message,"Creating directory (-1)")&&yn_calls==1&&modify_calls==1);
 settled();
 puts("PASS: PSU restore and backup, and folder pastes, keep old saves and backups when memory runs out "
      "or a question is declined, free every listing, close every PSU and refuse files that are not PSUs");
 return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='copy-psu-listing-') as folder:
    p = Path(folder)
    (p / 'test.c').write_text(code)
    exe = p / 'test'
    cc = shutil.which('gcc') or shutil.which('cc')
    if not cc:
        raise SystemExit('Host C compiler required')
    subprocess.run([cc, '-std=gnu99', '-Werror=implicit-function-declaration', '-Wno-format-truncation',
                    str(p / 'test.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], cwd=p, check=True, timeout=60)
