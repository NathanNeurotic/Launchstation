"""Paste, Move and Delete folders whose listing fails or is too long.

Runs the production paste sequence (subfunc_Paste and copy()) and delete() on
POSIX folders. The listing is a fixture with the contract the real one keeps
(tests/test_listing_complete.py runs that): getDir(), the browser's listing,
shows what a local reader could read, which is nothing when the folder does not
open, the entries before a failed read, or the first MAX_ENTRY - 2; and
getDirComplete() returns an error unless it read the whole folder. Here
MAX_ENTRY is 16, so a listing holds 14 entries.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
copy = (root / 'src/filer_copy.c').read_text()
browser = (root / 'src/filer_browser.c').read_text()
actions = (root / 'src/filer_actions.c').read_text()


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
#define FIO_O_TRUNC O_TRUNC
#define FIO_CST_ATTR 1
#define FIO_CST_CT 2
#define FIO_CST_AT 4
#define FIO_CST_MT 8
#define LNG(x) #x
#define DPRINTF(...) ((void)0)
typedef struct {int Year,Month,Day,Hour,Min,Sec;} Date;
typedef struct {Date _Create,_Modify;unsigned FileSizeByte,AttrFile,Reserve1,Reserve2;char EntryName[32];} __attribute__((aligned(64))) sceMcTblGetDir;
typedef struct {char name[MAX_NAME];unsigned char title[MAX_NAME];sceMcTblGetDir stats;} FILEINFO;
typedef struct {unsigned mode,attr;char ctime[8],mtime[8],atime[8];} iox_stat_t;
static struct {int PSU_HugeNames,PSU_DateNames,PSU_NoOverwrite;} config,*setting=&config;
static int PSU_content;
static int PM_flag[MAX_RECURSE],PM_file[MAX_RECURSE],PasteMode,PasteProgress_f,mctype_PSx;
static int nclipFiles,browser_cut,browser_pushed,browser_cd;
static FILEINFO clipFiles[MAX_ENTRY];static char clipPath[MAX_PATH];
static unsigned clipIopResetGeneration=1,generation=1;
static u64 written_size,PasteTime,clock_ms;
static int handles;
/* Folder listings are on the heap; one chosen allocation size can fail. */
static size_t fail_alloc_bytes;static int listings_live,listing_calls;
static void *test_memalign(size_t a,size_t n){
 assert(a>=__alignof__(FILEINFO));if(fail_alloc_bytes&&n==fail_alloc_bytes)return NULL;
 void *p=memalign(a,n);if(p&&n>=MAX_ENTRY*sizeof(FILEINFO))listings_live++;return p;
}
static void test_free(void *p){if(p&&malloc_usable_size(p)>=MAX_ENTRY*sizeof(FILEINFO))listings_live--;free(p);}
#define memalign test_memalign
#define free test_free
static u64 Timer(void){return ++clock_ms;}
static unsigned getIopResetGeneration(void){return generation;}
static int prepareTransferDeviceStacks(const char *a,const char *b){(void)a;(void)b;return TRANSFER_STACK_READY;}
static int clipboardSourceDeviceReady(void){return 1;}
static void waitUntilTimer(u64 end){clock_ms=end;}
static void drawMsg(const char *s){(void)s;}
static int ynDialog(const char *s){(void)s;return 1;}
static int readpad(void){return 0;}
static int DashIsActive(void){return 1;}
static int DashFileProgress(const char *a,const char *b,u64 r,u64 t){(void)a;(void)b;assert(r<=t);return 1;}
static int DashFileTransferChunk(void (*run)(void *),void *arg){run(arg);return 1;}
static void DashFileDeleteProgress(const char *name){(void)name;}
static void nonDialog(char *s){(void)s;}
static int keyboard(char *s,int n){(void)s;(void)n;return 0;}
static int isPcCardCfPath(const char *s){(void)s;return 0;}
static int isPcCardCfOnline(void){return 1;}
static int ensurePathDeviceStackReady(const char *p){(void)p;return 1;}
static void ensureMemoryCardPortAccessible(int p){(void)p;assert(!"no memory card here");}
static char *makeHostPath(char *d,char *s){if(d!=s)strcpy(d,s);return d;}
static void genLimObjName(char *p,int reserve){(void)p;(void)reserve;}
static int genGetStat(const char *p,iox_stat_t *out){(void)p;(void)out;assert(!"paste must not stat the destination");return -EIO;}
/* Runs once, when a chosen source file is opened while its folder is copied. */
static char hook_at[MAX_PATH];static void (*hook)(void);
static int genOpen(const char *p,int flags){
 if(hook_at[0]&&!strcmp(p,hook_at)){hook_at[0]=0;hook();}
 int fd=open(p,flags,0600);if(fd<0)return -errno;handles++;return fd;
}
static int genClose(int fd){assert(handles>0);handles--;return close(fd);}
static s64 genLseek(int fd,s64 n,int whence){return lseek(fd,n,whence);}
static int genRead(int fd,void *p,int n){int r=read(fd,p,n);return r<0?-errno:r;}
static int genWrite(int fd,void *p,int n){int r=write(fd,p,n);return r<0?-errno:r;}
/* A device that reports a file deleted that is still there. */
static int genRemove(char *p){if(strstr(p,"STICKY"))return 0;return unlink(p)?-errno:0;}
static int genRmdir(char *p){return rmdir(p)?-errno:0;}
static int fileXioRename(const char *a,const char *b){(void)a;(void)b;assert(!"no rename");return -EIO;}
static int fileXioRmdir(const char *p){(void)p;assert(!"no HDD here");return -EIO;}
static int fileXioRemove(const char *p){(void)p;assert(!"no HDD here");return -EIO;}
static int mcSetFileInfo(int port,int slot,const char *from,const sceMcTblGetDir *info,int flags){(void)port;(void)slot;(void)from;(void)info;(void)flags;return 0;}
static int mcSync(int mode,void *c,int *r){(void)mode;(void)c;if(r)*r=0;return 1;}
static int mcGetInfo(int p,int s,int *type,int *a,int *b){(void)p;(void)s;(void)type;(void)a;(void)b;return 0;}
static int mcDelete(int port,int slot,const char *p){(void)port;(void)slot;(void)p;assert(!"no memory card here");return 0;}
static int fileXioChStat(const char *p,const iox_stat_t *s,int f){(void)p;(void)s;(void)f;return 0;}
static int getHddParty(const char *p,const FILEINFO *f,char *a,char *b){(void)p;(void)f;(void)a;(void)b;assert(0);return -1;}
static int mountParty(const char *p){(void)p;assert(0);return -1;}
static int fileShowTitles(void){return 0;}
static int filerGetGameTitle(const char *p,const FILEINFO *f,unsigned char *out){(void)p;(void)f;out[0]=0;return -1;}
static void transcpy_sjis(char *out,const unsigned char *in){strcpy(out,(const char *)in);}
static int filerConfirmExploitDelete(const char *p,const FILEINFO *f){(void)p;(void)f;return 1;}
static int filerConfirmExploitModify(const char *p,const FILEINFO *f){(void)p;(void)f;return 1;}
static int filerIsExploitProtectedPath(const char *p,const FILEINFO *f){(void)p;(void)f;return 0;}
static void markMx4sioDestinationAfterWrite(const char *p){(void)p;}
static int genCmpFileExt(const char *p,const char *e){(void)p;(void)e;return 0;}
static int newdir(const char *parent,const char *name){char p[MAX_PATH];snprintf(p,sizeof(p),"%s%s",parent,name);return mkdir(p,0700)?-errno:0;}

/* The listing. A fault on a folder makes its open fail, or its read fail after
 * some entries; a fault set 'once' is gone after the first listing. */
static struct {char path[MAX_PATH];int open_error,fail_after,fail_code,once;} faults[8];static int nfaults;
static void fault(const char *p,int open_error,int fail_after,int fail_code,int once){
 assert(nfaults<8);snprintf(faults[nfaults].path,MAX_PATH,"%s",p);faults[nfaults].open_error=open_error;
 faults[nfaults].fail_after=fail_after;faults[nfaults].fail_code=fail_code;faults[nfaults++].once=once;
}
static int by_name(const void *a,const void *b){return strcmp(*(char *const *)a,*(char *const *)b);}
/* Lists up to max entries in name order. Returns the count and sets *error. */
static int list(const char *p,FILEINFO *out,int max,int *error){
 int fail_after=-1,fail_code=0,n=0,count=0;char *names[256];
 *error=0;listing_calls++;
 for(int i=0;i<nfaults;i++)if(!strcmp(faults[i].path,p)){
  if(faults[i].open_error)*error=faults[i].open_error;
  fail_after=faults[i].fail_after;fail_code=faults[i].fail_code;
  if(faults[i].once)faults[i]=faults[--nfaults];
  break;
 }
 if(*error)return 0;
 DIR *d=opendir(p);if(!d){*error=-errno;return 0;}
 struct dirent *e;
 while((e=readdir(d)))if(strcmp(e->d_name,".")&&strcmp(e->d_name,"..")){assert(count<256);names[count++]=strdup(e->d_name);}
 closedir(d);qsort(names,count,sizeof(names[0]),by_name);
 for(int i=0;i<count;i++){
  if(i==fail_after){*error=fail_code;break;}
  if(n==max)break;
  char full[MAX_PATH];struct stat st;snprintf(full,sizeof(full),"%s%s",p,names[i]);assert(!stat(full,&st));
  memset(&out[n],0,sizeof(out[n]));snprintf(out[n].name,sizeof(out[n].name),"%s",names[i]);
  out[n].stats.AttrFile=S_ISDIR(st.st_mode)?sceMcFileAttrSubdir:0;out[n].stats.FileSizeByte=st.st_size;n++;
 }
 for(int i=0;i<count;i++)free(names[i]);
 return n;
}
static int getDir(const char *p,FILEINFO *out){int error;return list(p,out,MAX_ENTRY-2,&error);}
static int getDirComplete(const char *p,FILEINFO *out,int *more){
 int error,n=list(p,out,MAX_ENTRY-1,&error);
 if(more)*more=0;
 if(error)return error;
 if(n>MAX_ENTRY-2){if(!more)return -E2BIG;*more=1;}
 return n;
}
static int unexpected(void){assert(!"PSU branch is outside this fixture");return -1;}
#define psu_backup_create_root_image(...) unexpected()
#define psu_backup_finalize_root_image(...) unexpected()
#define psu_backup_begin_file_entry(...) unexpected()
#define psu_backup_write_padding(...) unexpected()
#define psu_restore_read_file_entry(...) unexpected()
#define psu_restore_open_root_image(...) unexpected()
#define psu_restore_apply_entry_stats_to_mc(...) unexpected()
static int applyMcInfoToVmcPath(const char *p,const sceMcTblGetDir *s){(void)p;(void)s;return 0;}
int delete (const char *path, const FILEINFO *file);
'''
psu = (root / 'include/psu_types.h').read_text()
code += psu[psu.index('typedef struct'):psu.rindex('#endif')]
a = copy.index('/* One paste context')
code += copy[a:copy.index('static int isMemoryCardLikePath(', a)]
for sig in ('static int isMemoryCardLikePath(', 'static int isHddCommonParty(',
            'static int calcTransferSpeed(', 'static int calcRemainingTime(',
            'static int smoothTransferSpeed(', 'static int applyMcInfoToMcPath('):
    code += function(copy, sig)
code += copy[copy.index('int copy('):]
code += function(browser, 'static int waitForClipboardSourceDevice(')
code += function(browser, 'static void subfunc_Paste(')
code += function(actions, 'int delete (const char *path, const FILEINFO *file)\n{')
code += r'''
static void put(const char *p){int fd=open(p,O_CREAT|O_TRUNC|O_WRONLY,0600);assert(fd>=0);assert(write(fd,p,strlen(p))==(ssize_t)strlen(p));assert(!close(fd));}
static void tree(const char *dir,int files){
 char p[MAX_PATH];assert(!mkdir(dir,0700));
 for(int i=0;i<files;i++){snprintf(p,sizeof(p),"%s/F%03d",dir,i);put(p);}
}
static int count(const char *dir){
 DIR *d=opendir(dir);if(!d)return -1;int n=0;struct dirent *e;
 while((e=readdir(d)))n+=strcmp(e->d_name,".")&&strcmp(e->d_name,"..");
 closedir(d);return n;
}
static FILEINFO folder(const char *name){FILEINFO f;memset(&f,0,sizeof(f));strcpy(f.name,name);f.stats.AttrFile=sceMcFileAttrSubdir;return f;}
static void swap_file(void){put("usb0:/SWAP/NEW");assert(!unlink("usb0:/SWAP/F000"));}
static void unplug_late(void){fault("usb0:/LATE/",-ENODEV,-1,0,1);}
static char message[MAX_PATH];static int paste_mode=PM_NORMAL;
/* Pastes (or with cut, moves) one folder from usb0:/ into dest. */
static void paste_into(const char *name,int cut,const char *dest){
 assert(!handles&&!listings_live);
 strcpy(clipPath,"usb0:/");nclipFiles=1;clipFiles[0]=folder(name);browser_cut=cut;PasteMode=paste_mode;
 message[0]=0;subfunc_Paste(message,(char *)dest);
 assert(!handles&&!listings_live); /* every file closed and every listing freed */
}
static void paste(const char *name,int cut){paste_into(name,cut,"mmce0:/");}
static int remove_folder(const char *name){
 FILEINFO f=folder(name);assert(!listings_live);
 int ret=delete("usb0:/",&f);
 assert(!listings_live);nfaults=0;return ret;
}
int main(void){
 assert(!mkdir("usb0:",0700)&&!mkdir("mmce0:",0700));
 /* A folder that does not open is not pasted as an empty folder. */
 tree("usb0:/LOCKED",3);fault("usb0:/LOCKED/",-EACCES,-1,0,0);
 paste("LOCKED",0);
 assert(strstr(message,"Listing source (-13)"));
 paste("LOCKED",1);
 assert(strstr(message,"Listing source (-13)")&&count("usb0:/LOCKED")==3&&nclipFiles==1); /* Move keeps its source */
 assert(count("mmce0:/LOCKED")==-1); /* listed before anything is made at the destination */
 /* A PSU backup of it keeps an older backup, which is replaced only after the listing. */
 put("mmce0:/LOCKED.psu");paste_mode=PM_PSU_BACKUP;paste("LOCKED",0);paste_mode=PM_NORMAL;
 assert(strstr(message,"Listing source (-13)")&&!access("mmce0:/LOCKED.psu",F_OK));
 nfaults=0;
 /* A listing that fails partway is not taken as the whole folder. Here only the
  * first listing fails, so a Move that went on would delete what it never copied. */
 tree("usb0:/PART",5);fault("usb0:/PART/",0,2,-EIO,1);
 paste("PART",1);
 assert(strstr(message,"Listing source (-5)")&&count("usb0:/PART")==5&&nclipFiles==1);
 assert(count("mmce0:/PART")==-1);
 nfaults=0;
 /* A folder over the listing cap is refused, not cut to its first 14 entries. */
 tree("usb0:/BIG",MAX_ENTRY-1);
 paste("BIG",1);
 assert(strstr(message,"Listing source (-7)")&&strstr(message,"more than 2046 items"));
 assert(count("usb0:/BIG")==MAX_ENTRY-1&&count("mmce0:/BIG")==-1&&nclipFiles==1);
 /* USB (FAT, exFAT) ends a listing early with no error when a read fails, which
  * looks like the end of the folder. Listed again after its contents are copied,
  * the folder holds more, so the Move stops and keeps all of its source. */
 tree("usb0:/QUIET",5);fault("usb0:/QUIET/",0,2,0,1);
 paste("QUIET",1);
 assert(strstr(message,"Listing source again (-5)")&&count("usb0:/QUIET")==5&&nclipFiles==1);
 assert(count("mmce0:/QUIET")==2); /* what the short listing held was copied */
 /* A folder pasted into itself is listed before its copy is made in it: listed
  * again, it holds that copy too, so a Move stops instead of deleting both. */
 tree("usb0:/SELF",3);
 paste_into("SELF",1,"usb0:/SELF/");
 assert(strstr(message,"Listing source again (-5)")&&nclipFiles==1);
 assert(count("usb0:/SELF")==4&&count("usb0:/SELF/SELF")==3);
 /* A file replaced by another while the folder is copied leaves as many entries,
  * but not the same ones, so a Move keeps the new file it never copied. */
 tree("usb0:/SWAP",3);strcpy(hook_at,"usb0:/SWAP/F002");hook=swap_file;
 paste("SWAP",1);
 assert(strstr(message,"Listing source again (-5)")&&count("usb0:/SWAP")==3&&nclipFiles==1&&!hook_at[0]);
 /* A source that no longer lists once its contents are copied keeps a Move's source. */
 tree("usb0:/LATE",2);strcpy(hook_at,"usb0:/LATE/F001");hook=unplug_late;
 paste("LATE",1);
 assert(strstr(message,"Listing source again (-19)")&&count("usb0:/LATE")==2&&nclipFiles==1&&!hook_at[0]);
 /* A folder that fills a listing exactly, and an empty folder, still move. */
 tree("usb0:/FULL",MAX_ENTRY-4);assert(!mkdir("usb0:/FULL/EMPTY",0700));
 tree("usb0:/FULL/SUB",4);
 paste("FULL",1);
 assert(!message[0]&&count("usb0:/FULL")==-1&&count("mmce0:/FULL")==MAX_ENTRY-2);
 assert(count("mmce0:/FULL/EMPTY")==0&&count("mmce0:/FULL/SUB")==4&&!nclipFiles);
 /* Delete empties a folder of any size one listing at a time. */
 tree("usb0:/HUGE",40);tree("usb0:/HUGE/NESTED",MAX_ENTRY+3);
 listing_calls=0;
 assert(remove_folder("HUGE")==0&&count("usb0:/HUGE")==-1);
 assert(listing_calls==3+2); /* 41 entries in listings of 15, 15 and 11; then 19 in 15 and 4 */
 /* A folder that does not open, or whose listing fails partway, stops the
  * delete before anything listed in it is removed. */
 tree("usb0:/NOPE",4);fault("usb0:/NOPE/",-EACCES,-1,0,0);
 assert(remove_folder("NOPE")==-EACCES&&count("usb0:/NOPE")==4);
 tree("usb0:/HALF",6);fault("usb0:/HALF/",0,3,-EIO,0);
 assert(remove_folder("HALF")==-EIO&&count("usb0:/HALF")==6);
 /* A failure deeper down is returned as it is, and stops the delete there. */
 tree("usb0:/OUTER",0);tree("usb0:/OUTER/INNER",2);fault("usb0:/OUTER/INNER/",-EACCES,-1,0,0);
 assert(remove_folder("OUTER")==-EACCES&&count("usb0:/OUTER/INNER")==2);
 /* With no memory for a listing, nothing is deleted. */
 fail_alloc_bytes=(MAX_ENTRY+1)*sizeof(FILEINFO);
 assert(remove_folder("HALF")==-ENOMEM&&count("usb0:/HALF")==6);
 fail_alloc_bytes=0;
 assert(remove_folder("HALF")==0&&count("usb0:/HALF")==-1);
 /* Entries the device says it deleted but still lists end the delete with an
  * error instead of listing them again forever. */
 char p[MAX_PATH];tree("usb0:/STUCK",3);
 for(int i=0;i<MAX_ENTRY;i++){snprintf(p,sizeof(p),"usb0:/STUCK/A_STICKY%02d",i);put(p);}
 assert(remove_folder("STUCK")==-EIO&&count("usb0:/STUCK")==MAX_ENTRY+3); /* listed after them, F000-F002 stay */
 puts("PASS: Paste and Move refuse a source folder that does not open, fails partway or is over the "
      "listing cap before touching the destination, or that holds other entries when listed again after its "
      "contents are copied, keeping a Move's source; "
      "Delete stops on those before removing anything listed, "
      "empties a folder of any size, returns the error that stopped it and frees every listing");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='listing-callers-') as folder:
    p = Path(folder)
    (p / 'test.c').write_text(code)
    exe = p / 'test'
    cc = shutil.which('gcc') or shutil.which('cc')
    if not cc:
        raise SystemExit('Host C compiler required')
    subprocess.run([cc, '-std=gnu99', '-Werror=implicit-function-declaration', str(p / 'test.c'), '-o', str(exe)],
                   check=True)
    subprocess.run([str(exe)], cwd=p, check=True, timeout=60)
