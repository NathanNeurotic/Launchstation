"""Run copy() and subfunc_Paste() through PSU backup, PSU restore and folder
pastes on POSIX fixtures, with the real psu_functions.c and readGENERIC().

Each folder level allocates its listing before it removes or creates anything
at the destination. When that allocation fails, an older PSU backup or save
must be left exactly as it was, and that level creates no folder. Every early
exit frees the listing, and a PSU restore that stops early closes its PSU. A
file whose first entry is not a folder is not a PSU and is refused and closed,
and so is a PSU cut short, or with a name that reaches outside its folder,
before it can replace a save. A Move deletes only what was copied: an item that copy() kept at the
destination (a dated PSU name already taken, or the file or folder itself)
stays, cut. A PSU backup replaces an older one only once it is complete, and
when that replacing fails after the older one is gone, the new one stays under
its temporary name. A backup named after a game title alone never replaces
another save's backup.

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
#define COPY_KEPT_DESTINATION 1
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
/* The new name typed for a pasted item (PM_RENAME), or none. */
static const char *typed;
static int keyboard(char *s,int n){(void)n;if(!typed)return 0;strcpy(s,typed);return (int)strlen(s);}
static int isPcCardCfPath(const char *s){(void)s;return 0;}
static int isPcCardCfOnline(void){return 1;}
static void ensureMemoryCardPortAccessible(int p){assert(p==0||p==1);}
static char *makeHostPath(char *d,char *s){if(d!=s)strcpy(d,s);return d;}
/* Reads from a file whose name ends in read_fail_name fail as on a device
 * error, and so does the next close of one ending in close_fail_name. */
static const char *read_fail_name,*close_fail_name;static char fd_name[1024][MAX_PATH];
static int named(int fd,const char *end){size_t a=strlen(fd_name[fd]),b=end?strlen(end):0;return end&&a>=b&&!strcmp(fd_name[fd]+a-b,end);}
static int genOpen(const char *p,int flags){int fd=open(p,flags,0600);if(fd<0)return -errno;assert(fd<1024);fd_open[fd]=1;handles++;snprintf(fd_name[fd],MAX_PATH,"%s",p);return fd;}
static int genClose(int fd){if(fd<0||fd>=1024||!fd_open[fd]){double_closes++;return -EBADF;}fd_open[fd]=0;handles--;
 if(named(fd,close_fail_name)){close_fail_name=NULL;close(fd);return -EIO;}return close(fd)?-errno:0;}
static s64 genLseek(int fd,s64 n,int whence){assert(fd_open[fd]);return lseek(fd,n,whence);}
static int genRead(int fd,void *p,int n){assert(fd_open[fd]);if(named(fd,read_fail_name))return -EIO;int r=read(fd,p,n);return r<0?-errno:r;}
static int genWrite(int fd,void *p,int n){assert(fd_open[fd]);if(++writes==write_fail_at)return -ENOSPC;int r=write(fd,p,n);return r<0?-errno:r;}
static int genRemove(char *p){return unlink(p)?-errno:0;}
/* Like genRenameNoReplace(): no rename on host or where genCanRenamePath() refuses
 * (the real one, below), none over an existing file, or one the driver refuses. */
static int renames,rename_fail;
static int genCanRenamePath(const char *path);
static int genRenameNoReplace(const char *from,const char *to){
 if(!strncmp(from,"host",4)||!genCanRenamePath(from))return -ENOSYS;
 if(rename_fail)return -EIO;
 if(!access(to,F_OK))return -EEXIST;
 renames++;return rename(from,to)?-errno:0;}
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
/* With game titles shown (show_titles), every save has the title game_title. */
static int show_titles;static const char *game_title;
static int fileShowTitles(void){return show_titles;}
static int filerGetGameTitle(const char *p,const FILEINFO *f,unsigned char *out){(void)p;(void)f;if(!game_title){out[0]=0;return -1;}strcpy((char *)out,game_title);return 0;}
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
/* Like the real newdir(), it maps an HDD or DVR parent to its partition's folder,
 * and cuts the new name as genLimObjName() does (on a VMC to 32 characters). */
static void genLimObjName(char *uLE_path,int reserve);
static int newdir(const char *parent,const char *name){char p[MAX_PATH],party[MAX_NAME];int dvr=!strncmp(parent,"dvr_hdd",7),slot;
 if(dvr||!strncmp(parent,"hdd",3)){if((dvr?getHddDVRPParty:getHddParty)(parent,NULL,party,p)<0)return -1;
  if((slot=dvr?mountDVRPParty(party):mountParty(party))<0)return -1;p[dvr?7:3]='0'+slot;strcat(p,name);}
 else snprintf(p,sizeof(p),"%s%s",parent,name);
 if(strncmp(parent,"mc",2))genLimObjName(p,0);  /* a memory card's own limit is left to mcman */
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
code += 'static ' + function((root / 'src/filer_fileops.c').read_text(), 'int genCanRenamePath(')
code += 'static ' + function((root / 'src/filer_fileops.c').read_text(), 'void genLimObjName(')
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
static int same(const char *p,const void *data,int n){static unsigned char b[8192];return read_all(p,b,sizeof(b))==n&&!memcmp(b,data,n);}
static int holds(const char *p,const char *text){return same(p,text,strlen(text));}
/* A PSU whose first header names the save folder 'folder', and that holds 'text'. */
static int backs_up(const char *p,const char *folder,const char *text){static unsigned char b[8192];psu_header h;int n=read_all(p,b,sizeof(b)),i,t=strlen(text);
 if(n<(int)sizeof(h))return 0;memcpy(&h,b,sizeof(h));if(strncmp((char *)h.name,folder,sizeof(h.name)))return 0;
 for(i=0;i+t<=n;i++)if(!memcmp(b+i,text,t))return 1;return 0;}
static void add(const char *name,int attr);
static void clip(const char *path,const char *name,int attr,int mode){
 yn_answer=1;yn_calls=0;cancel_copy=0;fail_alloc_bytes=0;browser_cut=0;generation=clipIopResetGeneration=1;
 writes=write_fail_at=0;alloc_fail_stage=NULL;renames=rename_fail=0;typed=NULL;
 read_fail_name=close_fail_name=NULL;show_titles=0;game_title=NULL;
 party_calls=party_fail_at=mount_calls=mount_fail_at=0;memset(mount_slot,0,sizeof(mount_slot));
 protected_folder=0;modify_answer=1;modify_calls=delete_questions=delete_fail=0;
 PasteMode=mode;strcpy(clipPath,path);nclipFiles=0;memset(clipFiles,0,sizeof(clipFiles));add(name,attr);
}
/* A clipboard entry as a listing makes it, its mcTable entry naming it too. */
static void add(const char *name,int attr){FILEINFO *f=&clipFiles[nclipFiles++];
 strcpy(f->name,name);f->stats.AttrFile=attr;snprintf((char *)f->stats.EntryName,sizeof(f->stats.EntryName),"%.31s",name);}
static void paste(const char *destination){message[0]=0;subfunc_Paste(message,(char *)destination);}
/* The failure message has this text; when it does not, it is shown. */
static int says(const char *text){if(strstr(message,text))return 1;fprintf(stderr,"message: %s\n",message);return 0;}
/* There is no failure message; when there is one, it is shown. */
static int quiet(void){if(!message[0])return 1;fprintf(stderr,"message: %s\n",message);return 0;}
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
/* A PSU of the save folder 'folder' that holds the file 'name' ("evil"). */
static void one_file_psu(const char *path,const char *folder,const char *name){int fd=open(path,O_CREAT|O_TRUNC|O_WRONLY,0600);unsigned char b[1024];
 assert(fd>=0);psu_entry(fd,0x8427,3,folder);psu_entry(fd,0x8427,0,".");psu_entry(fd,0x8427,0,"..");psu_entry(fd,0x8497,4,name);
 memset(b,0xFF,sizeof(b));memcpy(b,"evil",4);assert(write(fd,b,sizeof(b))==sizeof(b));close(fd);}
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
 /* The files take no buffer of their own, which could fail once the old save
  * is gone: here only FILE0.BIN's own (100 bytes) would. */
 clip("mass:/","SAVE.psu",0x8497,PM_PSU_RESTORE);fail_alloc_bytes=100;paste("mc0:/");
 settled();
 assert(quiet()&&yn_calls==1&&!exists("mc0:/BASLUS-00001/OLD.BIN")&&entries("mc0:/BASLUS-00001")==2);
 assert(filled("mc0:/BASLUS-00001/FILE0.BIN",100,'A')&&filled("mc0:/BASLUS-00001/FILE1.BIN",101,'B'));
 put("mc0:/BASLUS-00001/OLD.BIN","old",3);
 /* A .psu cut short (in a file's data, or in an entry's header), or holding a
  * subfolder, is refused before it asks to overwrite the save, which stays. */
 {static unsigned char b[8192];const int cut[]={3*512+(512+1024)+512+50,3*512+(512+1024)+100};
  assert(read_all("mass:/SAVE.psu",b,sizeof(b))==3*512+2*(512+1024));
  for(int c=0;c<2;c++){put("mass:/CUT.psu",b,cut[c]);
   clip("mass:/","CUT.psu",0x8497,PM_PSU_RESTORE);paste("mc0:/");
   settled();
   assert(says("Checking PSU (-1)")&&!yn_calls&&exists("mc0:/BASLUS-00001/OLD.BIN")&&entries("mc0:/BASLUS-00001")==3);}
  int fd=open("mass:/CUT.psu",O_CREAT|O_TRUNC|O_WRONLY,0600);assert(fd>=0);
  psu_entry(fd,0x8427,3,"BASLUS-00001");psu_entry(fd,0x8427,0,".");psu_entry(fd,0x8427,0,"..");psu_entry(fd,0x8427,1,"SUB");close(fd);
  clip("mass:/","CUT.psu",0x8497,PM_PSU_RESTORE);paste("mc0:/");
  settled();
  assert(says("Checking PSU (-1)")&&!yn_calls&&exists("mc0:/BASLUS-00001/OLD.BIN")&&entries("mc0:/BASLUS-00001")==3);
  unlink("mass:/CUT.psu");}
 /* No name in a .psu reaches outside its folder. A save folder named so is
  * no PSU: restored into mass:/SAVES/, ".." would be taken for an existing
  * folder there, and replacing it would delete mass:/. A file named so is
  * refused before the save is created, such as one that would be written
  * over mc0:/BOOT.ELF (on FAT a backslash separates folders too). */
 {const char *bad[]={"..",".","","A/B","../BOOT.ELF","..\\BOOT.ELF"};
  mkdir("mass:/SAVES",0700);put("mass:/SAVES/KEEP.BIN","keep",4);put("mc0:/BOOT.ELF","boot",4);
  for(int i=0;i<6;i++){
   one_file_psu("mass:/BAD.psu",bad[i],"X.BIN");
   clip("mass:/","BAD.psu",0x8497,PM_PSU_RESTORE);paste("mass:/SAVES/");
   settled();
   assert(says("Opening source (-1)")&&!yn_calls&&holds("mass:/SAVES/KEEP.BIN","keep")&&entries("mass:/SAVES")==1);
   one_file_psu("mass:/BAD.psu","BASLUS-00002",bad[i]);
   clip("mass:/","BAD.psu",0x8497,PM_PSU_RESTORE);paste("mc0:/");
   settled();
   assert(says("Checking PSU (-1)")&&holds("mc0:/BOOT.ELF","boot")&&!exists("mc0:/BASLUS-00002"));}
  remove_tree("mass:/SAVES");unlink("mass:/BAD.psu");unlink("mc0:/BOOT.ELF");}

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

 /* PSU backup of a save folder: header, ".", "..", then 512 + 1024 bytes per file.
  * It is written as SAVEDIR.ps~, which becomes SAVEDIR.psu once complete. */
 unsigned char good[8192],now[8192];
 mkdir("mc0:/SAVEDIR",0700);put("mc0:/SAVEDIR/A.BIN","aaaa",4);put("mc0:/SAVEDIR/B.BIN","bbbbbbbb",8);
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);paste("mass:/");
 int size=read_all("mass:/SAVEDIR.psu",good,sizeof(good));
 assert(!message[0]&&size==3*512+2*(512+1024)&&!exists("mass:/SAVEDIR.ps~"));
 settled();
 /* A backup whose PSU cannot be created says why, and frees its listing. */
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);paste("missing:/");
 assert(says("Creating PSU (-2)")&&strstr(message,"not found")&&!exists("missing:"));
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
  * or there is no memory for the copy buffer. Each time the older backup
  * stays as it was, and the unfinished one is removed. */
 put("mass:/SAVEDIR.psu","older backup",12);
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);write_fail_at=4;paste("mass:/");
 settled();
 assert(says("Writing PSU entry (-5)")&&holds("mass:/SAVEDIR.psu","older backup")&&!exists("mass:/SAVEDIR.ps~"));
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);write_fail_at=5;paste("mass:/");
 settled();
 assert(strstr(message,"Writing destination (-28)")&&strstr(message,"Not enough free space"));
 assert(holds("mass:/SAVEDIR.psu","older backup")&&!exists("mass:/SAVEDIR.ps~"));
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);write_fail_at=6;paste("mass:/");
 settled();
 assert(strstr(message,"Writing destination (-5)")&&holds("mass:/SAVEDIR.psu","older backup")&&!exists("mass:/SAVEDIR.ps~"));
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);cancel_copy=1;yn_answer=-1;paste("mass:/");
 settled();
 assert(strstr(message,"Copy cancelled (-1)")&&holds("mass:/SAVEDIR.psu","older backup")&&!exists("mass:/SAVEDIR.ps~"));
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);alloc_fail_stage="Allocating buffer";paste("mass:/");
 settled();
 assert(strstr(message,"Allocating buffer (-12)")&&strstr(message,"Not enough memory"));
 assert(holds("mass:/SAVEDIR.psu","older backup")&&!exists("mass:/SAVEDIR.ps~"));
 /* So does one whose last write, the PSU's first header again (write 12), fails. */
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);write_fail_at=12;paste("mass:/");
 settled();
 assert(says("Finishing PSU (-28)")&&holds("mass:/SAVEDIR.psu","older backup")&&!exists("mass:/SAVEDIR.ps~"));
 /* A complete backup replaces the older one, also beside an unfinished one
  * that a lost backup left, and also where the device refuses the rename
  * (then the backup is copied to its name). */
 put("mass:/SAVEDIR.ps~","unfinished",10);
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);paste("mass:/");
 settled();
 assert(!message[0]&&same("mass:/SAVEDIR.psu",good,size)&&!exists("mass:/SAVEDIR.ps~")&&renames==1);
 put("mass:/SAVEDIR.psu","older backup",12);
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);rename_fail=1;paste("mass:/");
 settled();
 assert(!message[0]&&same("mass:/SAVEDIR.psu",good,size)&&!exists("mass:/SAVEDIR.ps~")&&!renames);
 /* Once the older one is removed, the new one is the only complete backup of
  * that name: when it cannot take the name (a refused rename, then no memory
  * to copy it there), it stays as SAVEDIR.ps~, and the failure says so. The
  * next backup replaces it, as it replaces an unfinished one. */
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);rename_fail=1;fail_alloc_bytes=PSU_COPY_BUFFER;paste("mass:/");
 settled();
 assert(says("Replacing PSU, new one kept as .ps~ (-12)")&&!exists("mass:/SAVEDIR.psu")&&same("mass:/SAVEDIR.ps~",good,size));
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);paste("mass:/");
 settled();
 assert(quiet()&&same("mass:/SAVEDIR.psu",good,size)&&!exists("mass:/SAVEDIR.ps~"));
 /* UDPFS cannot rename, so there a complete backup is copied over the older
  * one. A backup that stops, or that has no memory for that copy, keeps the
  * older one. When the copy fails once the older one is removed (write 13,
  * after the backup's 12, or a failed read or close), no partial .psu is
  * left, and the new backup stays as SAVEDIR.ps~. */
 put("udpfs:/SAVEDIR.psu","older backup",12);
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);write_fail_at=5;paste("udpfs:/");
 settled();
 assert(says("Writing destination (-28)")&&holds("udpfs:/SAVEDIR.psu","older backup")&&!exists("udpfs:/SAVEDIR.ps~"));
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);alloc_fail_stage="Finishing PSU";paste("udpfs:/");
 settled();
 assert(says("Finishing PSU (-12)")&&holds("udpfs:/SAVEDIR.psu","older backup")&&!exists("udpfs:/SAVEDIR.ps~"));
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);paste("udpfs:/");
 settled();
 assert(!message[0]&&same("udpfs:/SAVEDIR.psu",good,size)&&!exists("udpfs:/SAVEDIR.ps~")&&!renames);
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);write_fail_at=13;paste("udpfs:/");
 settled();
 assert(says("Replacing PSU, new one kept as .ps~ (-28)")&&!exists("udpfs:/SAVEDIR.psu")&&same("udpfs:/SAVEDIR.ps~",good,size));
 put("udpfs:/SAVEDIR.psu","older backup",12);
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);read_fail_name=".ps~";paste("udpfs:/");
 settled();
 assert(says("Replacing PSU, new one kept as .ps~ (-5)")&&!exists("udpfs:/SAVEDIR.psu")&&same("udpfs:/SAVEDIR.ps~",good,size));
 put("udpfs:/SAVEDIR.psu","older backup",12);
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);close_fail_name=".psu";paste("udpfs:/");
 settled();
 assert(says("Replacing PSU, new one kept as .ps~ (-5)")&&!exists("udpfs:/SAVEDIR.psu")&&same("udpfs:/SAVEDIR.ps~",good,size));
 /* A backup whose own file does not close cleanly is not complete, so the
  * older one stays. */
 put("udpfs:/SAVEDIR.psu","older backup",12);
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);close_fail_name=".ps~";paste("udpfs:/");
 settled();
 assert(says("Finishing PSU (-5)")&&holds("udpfs:/SAVEDIR.psu","older backup")&&!exists("udpfs:/SAVEDIR.ps~"));
 /* With dated names and no overwriting, an existing name is skipped, and the
  * skip frees the listing. An unfinished backup of that name is not one. */
 config.PSU_DateNames=config.PSU_NoOverwrite=1;
 put("mass:/SAVEDIR_0000-00-00_00-00-00.ps~","unfinished",10);
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);paste("mass:/");
 settled();
 assert(!message[0]&&same("mass:/SAVEDIR_0000-00-00_00-00-00.psu",good,size)&&!exists("mass:/SAVEDIR_0000-00-00_00-00-00.ps~"));
 put("mass:/SAVEDIR_0000-00-00_00-00-00.psu","kept",4);
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);paste("mass:/");
 assert(!message[0]&&read_all("mass:/SAVEDIR_0000-00-00_00-00-00.psu",now,sizeof(now))==4);
 settled();
 /* Moving the save there (Cut, then Create PSU) writes no backup either, so
  * the save stays where it was, still cut, and the user is told why. Moved
  * with other saves, it is the only one left: they are backed up and gone. */
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);browser_cut=1;paste("mass:/");
 settled();
 assert(says("SAVEDIR was not moved\nA backup with that name already exists."));
 assert(entries("mc0:/SAVEDIR")==2&&holds("mass:/SAVEDIR_0000-00-00_00-00-00.psu","kept"));
 assert(nclipFiles==1&&!strcmp(clipFiles[0].name,"SAVEDIR")&&browser_cut);
 mkdir("mc0:/FIRST",0700);put("mc0:/FIRST/F.BIN","f",1);mkdir("mc0:/LAST",0700);put("mc0:/LAST/L.BIN","l",1);
 clip("mc0:/","FIRST",0x8427,PM_PSU_BACKUP);add("SAVEDIR",0x8427);add("LAST",0x8427);browser_cut=1;paste("mass:/");
 settled();
 assert(says("SAVEDIR was not moved\n")&&entries("mc0:/SAVEDIR")==2&&nclipFiles==1&&!strcmp(clipFiles[0].name,"SAVEDIR"));
 assert(!exists("mc0:/FIRST")&&exists("mass:/FIRST_0000-00-00_00-00-00.psu"));
 assert(!exists("mc0:/LAST")&&exists("mass:/LAST_0000-00-00_00-00-00.psu"));
 /* With more than one left, the first is named and the others counted. */
 mkdir("mc0:/FIRST",0700);put("mc0:/FIRST/F.BIN","f",1);
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);add("FIRST",0x8427);browser_cut=1;paste("mass:/");
 settled();
 assert(says("SAVEDIR (and 1 more) was not moved\n")&&entries("mc0:/SAVEDIR")==2&&holds("mc0:/FIRST/F.BIN","f"));
 assert(nclipFiles==2&&!strcmp(clipFiles[1].name,"FIRST"));
 remove_tree("mc0:/FIRST");
 config.PSU_DateNames=config.PSU_NoOverwrite=0;
 /* With game titles shown, a PSU is named after its save's title alone, so
  * two saves of one game get one name. The second must not replace the
  * first's backup (a Move would then lose the first save): it is named after
  * its folder too, as PSU_HugeNames names it. A save backed up again still
  * replaces its own backup, and a file there that is no PSU is kept too. */
 mkdir("mc0:/SAVE1",0700);put("mc0:/SAVE1/ONE.BIN","one",3);mkdir("mc0:/SAVE2",0700);put("mc0:/SAVE2/TWO.BIN","two",3);
 clip("mc0:/","SAVE1",0x8427,PM_PSU_BACKUP);add("SAVE2",0x8427);show_titles=1;game_title="SAME GAME";browser_cut=1;paste("mass:/");
 settled();
 assert(quiet()&&backs_up("mass:/SAME GAME.psu","SAVE1","ONE.BIN")&&backs_up("mass:/SAVE2_SAME GAME.psu","SAVE2","TWO.BIN"));
 assert(!exists("mc0:/SAVE1")&&!exists("mc0:/SAVE2")&&!nclipFiles);
 mkdir("mc0:/SAVE1",0700);put("mc0:/SAVE1/ONE.BIN","one again",9);
 clip("mc0:/","SAVE1",0x8427,PM_PSU_BACKUP);show_titles=1;game_title="SAME GAME";paste("mass:/");
 settled();
 assert(quiet()&&backs_up("mass:/SAME GAME.psu","SAVE1","one again")&&!exists("mass:/SAVE1_SAME GAME.psu"));
 put("mass:/OTHER GAME.psu","notes",5);
 clip("mc0:/","SAVE1",0x8427,PM_PSU_BACKUP);show_titles=1;game_title="OTHER GAME";paste("mass:/");
 settled();
 assert(quiet()&&holds("mass:/OTHER GAME.psu","notes")&&backs_up("mass:/SAVE1_OTHER GAME.psu","SAVE1","one again"));
 remove_tree("mc0:/SAVE1");
 /* A subfolder cannot go into a PSU; the backup stops and frees its listing.
  * A folder named like the save at the destination must not let the
  * subfolder be written there as a PSU of its own. */
 mkdir("mc0:/SAVEDIR/SUB",0700);mkdir("mass:/SAVEDIR",0700);put("mass:/SAVEDIR.psu","older backup",12);
 clip("mc0:/","SAVEDIR",0x8427,PM_PSU_BACKUP);paste("mass:/");
 settled();
 assert(says("Adding subfolder to PSU (-1)")&&strstr(message,"\nSUB\n")&&!exists("mass:/SAVEDIR/SUB.psu"));
 assert(holds("mass:/SAVEDIR.psu","older backup")&&!exists("mass:/SAVEDIR.ps~"));

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

 /* A Move onto the file itself writes nothing, so it must not delete it: here
  * through a new name that names the same file, and through one that does
  * once it is cut to fit the memory card's 32 characters. */
 mkdir("mass:/DIR",0700);put("mass:/DIR/M.BIN","m",1);
 clip("mass:/DIR/","M.BIN",0x8497,PM_RENAME);typed="DIR/M.BIN";browser_cut=1;paste("mass:/");
 settled();
 assert(says("M.BIN was not moved\nIt is already at the destination.")&&holds("mass:/DIR/M.BIN","m")&&nclipFiles==1);
 mkdir("mc1:/D",0700);put("mc1:/D/ABCDEFGHIJKLMNOPQRSTUVWXYZ012345","n",1);
 clip("mc1:/D/","ABCDEFGHIJKLMNOPQRSTUVWXYZ012345",0x8497,PM_RENAME);typed="D/ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456";
 browser_cut=1;paste("mc1:/");
 settled();
 assert(says("ABCDEFGHIJKLMNOPQRSTUVWXYZ012345 was not moved\nIt is already at the destination."));
 assert(holds("mc1:/D/ABCDEFGHIJKLMNOPQRSTUVWXYZ012345","n")&&nclipFiles==1);
 /* Nor a folder, whose files would each be at the destination already while
  * the folder itself was deleted: here a VMC folder whose new name names it
  * once cut to 32 characters, as newdir() cuts it there (so the paste would
  * ask to merge into that "existing" folder). */
 mkdir("vmc0:",0700);mkdir("vmc0:/E",0700);mkdir("vmc0:/E/ABCDEFGHIJKLMNOPQRSTUVWXYZ012345",0700);
 put("vmc0:/E/ABCDEFGHIJKLMNOPQRSTUVWXYZ012345/F.BIN","f",1);
 clip("vmc0:/E/","ABCDEFGHIJKLMNOPQRSTUVWXYZ012345",0x8427,PM_RENAME);typed="E/ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456";
 browser_cut=1;paste("vmc0:/");
 settled();
 assert(says("ABCDEFGHIJKLMNOPQRSTUVWXYZ012345 was not moved\nIt is already at the destination.")&&!yn_calls);
 assert(holds("vmc0:/E/ABCDEFGHIJKLMNOPQRSTUVWXYZ012345/F.BIN","f")&&nclipFiles==1);
 /* So a copy is never mistaken for one: copy() returns 0 for a file it copied,
  * here to a memory card, after which its result held an attribute request. */
 clip("udpfs:/TOP/","F.BIN",0x8497,PM_NORMAL);PM_flag[0]=PM_NORMAL;PM_file[0]=-1;
 assert(copy("mc1:/",clipPath,&clipFiles[0],0)==0&&filled("mc1:/F.BIN",1,'y'));
 settled();
 puts("PASS: PSU restore and backup, and folder pastes, keep old saves and backups when memory runs out "
      "or a question is declined, free every listing, close every PSU and refuse files that are not PSUs, "
      "and a Move deletes no source that it did not copy");
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
