"""Run the full production clipboard/paste/copy sequence on POSIX fixtures.

Drivers/RPCs are replaced by bounded file adapters and injected failures. The
copy orchestration, recursion, direct overwrite and diagnostics are real.
A udpfs: destination can lose a written file, or keep an old longer one, as a
UDPFS server that reuses a handle from before an IOP reboot does. This does
not emulate the UDPFS protocol, libmc or physical memory cards.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
copy=(root/'src/filer_copy.c').read_text()
browser=(root/'src/filer_browser.c').read_text()
assert 'dummy = genRemove(out);' in copy
assert 'out_fd = genOpen(out, FIO_O_WRONLY | FIO_O_TRUNC | FIO_O_CREAT);' in copy
for removed in ('copyPrepareTarget', 'copyLookupPath', 'copyVerifyTarget',
                'copyPublishTarget', 'copyOpenTarget', 'LS-COPY-', 'LS-OLD-', 'FIO_O_EXCL'):
    assert removed not in copy, removed

def function(source, signature):
    a=source.index(signature);b=source.index('{',a)+1;depth=1
    while depth:
        depth+=(source[b]=='{')-(source[b]=='}');b+=1
    return source[a:b]+'\n'

code=r'''
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
#define FIO_O_EXCL O_EXCL
#define FIO_O_TRUNC O_TRUNC
#define FIO_S_ISDIR S_ISDIR
#define FIO_S_ISREG S_ISREG
#define FIO_CST_ATTR 1
#define FIO_CST_CT 2
#define FIO_CST_AT 4
#define FIO_CST_MT 8
#define LNG(x) #x
#define DPRINTF(...) ((void)0)
typedef struct {int Year,Month,Day,Hour,Min,Sec;} Date;
typedef struct {Date _Create,_Modify;unsigned FileSizeByte,AttrFile,Reserve1,Reserve2;char EntryName[32];} __attribute__((aligned(64))) sceMcTblGetDir; /* as in libmc.h */
typedef struct {char name[MAX_NAME];unsigned char title[MAX_NAME];sceMcTblGetDir stats;} FILEINFO;
typedef struct {unsigned mode,attr;char ctime[8],mtime[8],atime[8];} iox_stat_t;
static struct {int PSU_HugeNames,PSU_DateNames,PSU_NoOverwrite;} config,*setting=&config;
static int PSU_content;
static int PM_flag[MAX_RECURSE],PM_file[MAX_RECURSE],PasteMode,PasteProgress_f,mctype_PSx;
static int nclipFiles,browser_cut,browser_pushed,browser_cd;
static FILEINFO clipFiles[MAX_ENTRY];static char clipPath[MAX_PATH];
static unsigned clipIopResetGeneration,generation;
static u64 written_size,PasteTime,clock_ms;
static int source_ready=1,load_ok=1,reset_preflight,read_cap=1048576,write_cap=1048576,cancel_copy;
static int fail_open,fail_rewind,fail_read,fail_write,fail_close,fail_listing,fail_out_open,early_eof,fail_measure;
static int fd_kind[1024],handles,read_calls,write_calls;
/* Folder listings come from the heap, aligned like FILEINFO; allocations of
 * one chosen size can be made to fail. */
static size_t fail_alloc_bytes;static int listing_allocs,listings_live;static void *listing_ptr[MAX_RECURSE];
static void *test_memalign(size_t a,size_t n){
 assert(a>=__alignof__(FILEINFO));if(fail_alloc_bytes&&n==fail_alloc_bytes)return NULL;
 void *p=memalign(a,n);if(p&&n==MAX_ENTRY*sizeof(FILEINFO)){assert(listings_live<MAX_RECURSE);listing_ptr[listings_live++]=p;listing_allocs++;}return p;
}
static void test_free(void *p){for(int i=0;i<listings_live;i++)if(listing_ptr[i]==p){listing_ptr[i]=listing_ptr[--listings_live];break;}free(p);}
#define memalign test_memalign
#define free test_free
static unsigned fd_generation[1024];
static u64 Timer(void){return ++clock_ms;}
static unsigned getIopResetGeneration(void){return generation;}
static int prepareTransferDeviceStacks(const char *a,const char *b){assert(!handles);(void)a;(void)b;if(reset_preflight)generation++;return load_ok?0:-1;}
static int clipboardSourceDeviceReady(void){return source_ready;}
static void waitUntilTimer(u64 end){clock_ms=end;}
static void drawMsg(const char *s){(void)s;}
static int ynDialog(const char *s){(void)s;return cancel_copy?-1:1;}
static int readpad(void){return cancel_copy;}
static int DashIsActive(void){return 1;}
static int DashFileProgress(const char *a,const char *b,u64 r,u64 t){(void)a;(void)b;assert(r<=t);return 1;}
/* alternate the worker and inline chunk paths; both must behave the same */
static int transfer_calls;
static int DashFileTransferChunk(void (*run)(void *),void *arg){if(transfer_calls++&1)return 0;run(arg);return 1;}
static void nonDialog(char *s){(void)s;}
static int keyboard(char *s,int n){(void)s;(void)n;return 0;}
static int isPcCardCfPath(const char *s){(void)s;return 0;}
static int isPcCardCfOnline(void){return 1;}
static void ensureMemoryCardPortAccessible(int p){assert(p==0||p==1);}
static char *makeHostPath(char *d,char *s){if(d!=s)strcpy(d,s);return d;}
static void genLimObjName(char *p,int reserve){(void)p;(void)reserve;}
static int genGetStat(const char *p,iox_stat_t *out){(void)p;(void)out;assert(!"paste must not stat the destination");return -EIO;}
/* A UDPFS server can give the next write-open of a name a handle it kept from
 * before an IOP reboot. UDPFS_LOST: the file was removed before the open, so
 * every write and the close succeed and nothing is left. UDPFS_TAIL: the old
 * file could not be removed and O_TRUNC is not applied, so a longer old file
 * keeps its tail. */
enum {UDPFS_OK,UDPFS_LOST,UDPFS_TAIL};
static int udpfs_stale,opens,udpfs_read_opens;
/* PS2-Servers' udpfs_server.py reads a .cso/.ciso/.zso/.ziso/.chd opened by
 * its own name decompressed when it is a container it can read: the image
 * (decoded_size bytes, not starting with the container's magic) instead. */
static int udpfs_decompress,decoded_size;
static int genCmpFileExt(const char *p,const char *e){const char *d=strrchr(p,'.');return d&&!strcasecmp(d+1,e);}
static const char *decompressed(const char *p){
 static const char *const ext[]={"cso","ciso","zso","ziso","chd"};char head[8]={0};int i,fd;
 for(i=0;i<5&&!genCmpFileExt(p,ext[i]);i++);
 if(i==5||(fd=open(p,O_RDONLY))<0)return p;
 i=read(fd,head,sizeof(head));close(fd);
 if(i<8||(memcmp(head,"CISO",4)&&memcmp(head,"ZISO",4)&&memcmp(head,"MComprHD",8)))return p;
 unsigned char image[8192]={0};assert(decoded_size<=(int)sizeof(image));
 fd=open("decoded.img",O_CREAT|O_TRUNC|O_WRONLY,0600);assert(fd>=0&&write(fd,image,decoded_size)==decoded_size);close(fd);
 return "decoded.img";
}
static int genOpen(const char *p,int flags){
 int udpfs=!strncmp(p,"udpfs:",6),stale=udpfs&&(flags&O_WRONLY)?udpfs_stale:UDPFS_OK;
 opens++;if(udpfs&&flags==O_RDONLY)udpfs_read_opens++;
 if(udpfs&&flags==O_RDONLY&&udpfs_decompress)p=decompressed(p);
 if(fail_open&&flags==O_RDONLY&&udpfs)return -ENOENT;
 if(fail_out_open&&(flags&O_WRONLY))return -EACCES;
 if(stale)udpfs_stale=UDPFS_OK;
 int fd=open(p,stale==UDPFS_TAIL?flags&~O_TRUNC:flags,0600);if(fd<0)return -errno;assert(fd<1024);
 if(stale==UDPFS_LOST)assert(!unlink(p));
 fd_kind[fd]=(flags&O_WRONLY)?2:udpfs?1:3;
 fd_generation[fd]=generation;handles++;return fd;
}
static int genClose(int fd){int kind=fd_kind[fd];assert(kind&&handles>0);fd_kind[fd]=0;handles--;int r=close(fd);return fail_close&&kind==2?-EIO:r;}
static s64 genLseek(int fd,s64 n,int whence){assert(fd_generation[fd]==generation);if(fail_measure&&whence==SEEK_END)return -EIO;if(fail_rewind&&whence==SEEK_SET)return -EIO;return lseek(fd,n,whence);}
static int genRead(int fd,void *p,int n){assert(fd_generation[fd]==generation);read_calls++;if(early_eof&&fd_kind[fd]==1)return 0;if(fail_read&&fd_kind[fd]==1)return -ETIMEDOUT;int r=read(fd,p,n>read_cap?read_cap:n);return r<0?-errno:r;}
static int genWrite(int fd,void *p,int n){assert(fd_generation[fd]==generation);write_calls++;if(fail_write)return -ENOSPC;int r=write(fd,p,n>write_cap?write_cap:n);return r<0?-errno:r;}
static int genRemove(char *p){if(udpfs_stale==UDPFS_TAIL&&!strncmp(p,"udpfs:",6))return -EBUSY;return unlink(p)?-errno:0;}
static int fileXioRename(const char *a,const char *b){assert(!"paste must not require rename");(void)a;(void)b;return -EIO;}
static int mc_result;
static int mcSetFileInfo(int port,int slot,const char *from,const sceMcTblGetDir *info,int flags){
 (void)slot;if(flags==0x10){char a[MAX_PATH],b[MAX_PATH];snprintf(a,sizeof(a),"mc%d:/%s",port,from);strcpy(b,a);char *leaf=strrchr(b,'/');assert(leaf);snprintf(leaf+1,sizeof(b)-(leaf+1-b),"%.32s",info->EntryName);mc_result=fileXioRename(a,b);}else mc_result=0;return 0;
}
static int mcSync(int mode,void *c,int *r){(void)mode;(void)c;if(r)*r=mc_result;return 1;}
static int mcGetInfo(int p,int s,int *type,int *a,int *b){(void)p;(void)s;if(type)*type=2;if(a)*a=1000;if(b)*b=1000;return 0;}
static int fileXioChStat(const char *p,const iox_stat_t *s,int f){assert(strncmp(p,"mmce",4));(void)p;(void)s;(void)f;return 0;}
static int getHddParty(const char *p,const FILEINFO *f,char *a,char *b){(void)p;(void)f;(void)a;(void)b;assert(0);return -1;}
static int mountParty(const char *p){(void)p;assert(0);return -1;}
static int fileShowTitles(void){return 0;}
static int filerGetGameTitle(const char *p,const FILEINFO *f,unsigned char *out){(void)p;(void)f;out[0]=0;return -1;}
static void transcpy_sjis(char *out,const unsigned char *in){strcpy(out,(const char *)in);}
static int filerConfirmExploitDelete(const char *p,const FILEINFO *f){(void)p;(void)f;return 1;}
static int filerConfirmExploitModify(const char *p,const FILEINFO *f){(void)p;(void)f;return 1;}
static int filerIsExploitProtectedPath(const char *p,const FILEINFO *f){(void)p;(void)f;return 0;}
static int move_deletes;static const char *move_target="mmce0:/MOVE.BIN";
static int delete(const char *p,const FILEINFO *f){char source[MAX_PATH];assert(!handles);assert(!access(move_target,F_OK));snprintf(source,sizeof(source),"%s%s",p,f->name);move_deletes++;return genRemove(source);}
static void markMx4sioDestinationAfterWrite(const char *p){(void)p;}
static int newdir(const char *parent,const char *name){char p[MAX_PATH];snprintf(p,sizeof(p),"%s%s",parent,name);return mkdir(p,0700)?-errno:0;}
static int getDir(const char *p,FILEINFO *out){
 if(fail_listing)return -EIO;DIR *d=opendir(p);if(!d)return -errno;struct dirent *e;int n=0;
 while((e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;assert(n<MAX_ENTRY);memset(&out[n],0,sizeof(out[n]));snprintf(out[n].name,sizeof(out[n].name),"%s",e->d_name);char full[MAX_PATH];snprintf(full,sizeof(full),"%s%s",p,e->d_name);struct stat st;assert(!stat(full,&st));out[n].stats.AttrFile=S_ISDIR(st.st_mode)?sceMcFileAttrSubdir:0;out[n].stats.FileSizeByte=st.st_size;n++;}closedir(d);return n;
}
static int unexpected(void){assert(!"PSU branch is outside this ordinary-copy fixture");return -1;}
#define psu_backup_create_root_image(...) unexpected()
#define psu_backup_finalize_root_image(...) unexpected()
#define psu_backup_begin_file_entry(...) unexpected()
#define psu_backup_write_padding(...) unexpected()
#define psu_restore_read_file_entry(...) unexpected()
#define psu_restore_open_root_image(...) unexpected()
#define psu_restore_apply_entry_stats_to_mc(...) unexpected()
static int applyMcInfoToVmcPath(const char *p,const sceMcTblGetDir *s){(void)p;(void)s;return 0;}

'''
# Use the real PSU layout declarations; these branches are compiled but not executed.
psu=(root/'include/psu_types.h').read_text();code+=psu[psu.index('typedef struct'):psu.rindex('#endif')]
a=copy.index('/* One paste context');b=copy.index('static int isMemoryCardLikePath(',a);code+=copy[a:b]
for sig in ('static int isMemoryCardLikePath(', 'static int isHddCommonParty(',
            'static int calcTransferSpeed(', 'static int calcRemainingTime(',
            'static int smoothTransferSpeed(', 'static int applyMcInfoToMcPath(',
            'static int udpfsReadsDecompressed(', 'static int verifyUdpfsCopy('):
    code+=function(copy,sig)
code+=copy[copy.index('int copy('):]
code+=function(browser,'static int waitForClipboardSourceDevice(')
code+=function(browser,'static void subfunc_Paste(')
code+=r'''
static void put(const char *p,const unsigned char *data,int n){int fd=open(p,O_CREAT|O_TRUNC|O_WRONLY,0600);assert(fd>=0);assert(write(fd,data,n)==n);assert(!close(fd));}
static int matches(const char *p,const unsigned char *data,int n){unsigned char out[70000];int fd=open(p,O_RDONLY);if(fd<0)return 0;int count=read(fd,out,sizeof(out));close(fd);return count==n&&!memcmp(data,out,n);}
static unsigned char input[65537];
static void reset(void){assert(!handles&&!listings_live);source_ready=load_ok=1;reset_preflight=0;fail_open=fail_rewind=fail_read=fail_write=fail_close=fail_listing=cancel_copy=fail_out_open=early_eof=fail_measure=0;fail_alloc_bytes=0;listing_allocs=0;read_calls=write_calls=0;udpfs_stale=UDPFS_OK;udpfs_decompress=decoded_size=0;opens=udpfs_read_opens=0;browser_cut=0;browser_cd=0;generation=1;clipIopResetGeneration=1;PasteMode=PM_NORMAL;strcpy(clipPath,"udpfs:/");nclipFiles=1;memset(clipFiles,0,sizeof(clipFiles));strcpy(clipFiles[0].name,"BOOT.ELF");clipFiles[0].stats.FileSizeByte=sizeof(input);}
int main(void){
 assert(sizeof(copy_diag)<=128); /* fixed runtime budget, no log allocation */
 mkdir("udpfs:",0700);mkdir("mc0:",0700);mkdir("mc1:",0700);mkdir("usb0:",0700);mkdir("host:",0700);mkdir("mmce0:",0700);mkdir("mmce1:",0700);
 for(int i=0;i<(int)sizeof(input);i++)input[i]=(unsigned char)(i*19+7);
 put("udpfs:/BOOT.ELF",input,sizeof(input));
 const char *destinations[]={"mc0:/","mc1:/","usb0:/","mmce0:/","mmce1:/"};char message[MAX_PATH],target[MAX_PATH];
 for(int device=0;device<5;device++)for(int attempt=0;attempt<3;attempt++){
  reset();message[0]=0;char dest[12];strcpy(dest,destinations[device]);
  subfunc_Paste(message,dest);assert(!message[0]&&!handles);
  snprintf(target,sizeof(target),"%sBOOT.ELF",dest);assert(matches(target,input,sizeof(input)));
 }
 /* Legacy host: cannot stat files; new and shorter replacement writes must work. */
 for(int attempt=0;attempt<3;attempt++){
  int length=attempt==2?17:(int)sizeof(input);
  put("usb0:/HOST.BIN",input,length);reset();strcpy(clipPath,"usb0:/");strcpy(clipFiles[0].name,"HOST.BIN");
  message[0]=0;subfunc_Paste(message,"host:/");assert(!message[0]&&!handles);
  assert(matches("host:/HOST.BIN",input,length));
 }
 /* Exercise all fixture boundary lengths through ordinary and replacement I/O. */
 const int lengths[]={0,1,16383,16384,16385,65535,65536,65537};
 for(unsigned length=0;length<sizeof(lengths)/sizeof(lengths[0]);length++){
  put("udpfs:/BOUNDARY.BIN",input,lengths[length]);unlink("mc0:/BOUNDARY.BIN");
  for(int attempt=0;attempt<2;attempt++){
   reset();strcpy(clipFiles[0].name,"BOUNDARY.BIN");clipFiles[0].stats.FileSizeByte=lengths[length];
   message[0]=0;subfunc_Paste(message,"mc0:/");assert(!message[0]&&!handles);
   assert(matches("mc0:/BOUNDARY.BIN",input,lengths[length]));
  }
 }
 const unsigned char old[]="original ELF";
 const char *stages[]={"Opening source","Rewinding source","Reading source","Writing destination","Closing destination","Copy cancelled","Preparing devices","Checking source","Opening destination","Reading source","Measuring source"};
 for(int failure=0;failure<11;failure++){
  reset();put("mc0:/BOOT.ELF",old,sizeof(old));
  if(failure==0)fail_open=1;if(failure==1)fail_rewind=1;if(failure==2)fail_read=1;
  if(failure==3)fail_write=1;if(failure==4)fail_close=1;if(failure==5)cancel_copy=1;
  if(failure==6)load_ok=0;if(failure==7){source_ready=0;reset_preflight=1;}
  if(failure==8)fail_out_open=1;if(failure==9)early_eof=1;if(failure==10)fail_measure=1;
  message[0]=0;subfunc_Paste(message,"mc0:/");
  assert(message[0]&&strstr(message,stages[failure])&&strstr(message,"fixture-head"));
  assert(!handles);
  if(failure==0||failure==1||failure==6||failure==7||failure==10)
   assert(matches("mc0:/BOOT.ELF",old,sizeof(old)));
  else if(failure==4) assert(matches("mc0:/BOOT.ELF",input,sizeof(input))); /* close error retains output, as upstream */
  else assert(access("mc0:/BOOT.ELF",F_OK)); /* upstream direct replacement cleanup */
  if(failure==3){assert(strstr(message,"0/65537 B")&&strstr(message,"(-28)"));}
  if(failure==7)assert(strstr(message,"IOP 1>2"));
 }
 /* Direct copying does not depend on a driver's missing-entry stat status. */
 reset();unlink("mc1:/BOOT.ELF");
 for(int attempt=0;attempt<3;attempt++){
  message[0]=0;subfunc_Paste(message,"mc1:/");assert(!message[0]&&!handles);
  assert(matches("mc1:/BOOT.ELF",input,sizeof(input)));
 }
 /* Cross-device source/destination matrix. Any destination stat/rename call
  * aborts in the adapters, including ordinary MC-to-MMCE replacements. */
 const char *cross_sources[]={"usb0:/","mc0:/","mmce0:/"};
 const char *cross_destinations[]={"mc1:/","mmce0:/","mmce1:/","usb0:/"};
 put("usb0:/EISDIR.ELF",input,sizeof(input));
 put("mc0:/EISDIR.ELF",input,sizeof(input));
 put("mmce0:/EISDIR.ELF",input,sizeof(input));
 for(int source=0;source<3;source++)for(int destination=0;destination<4;destination++) {
  if(!strcmp(cross_sources[source],cross_destinations[destination]))continue;
  snprintf(target,sizeof(target),"%sEISDIR.ELF",cross_destinations[destination]);
  unlink(target);
  for(int attempt=0;attempt<2;attempt++){
   reset();
   strcpy(clipPath,cross_sources[source]);
   strcpy(clipFiles[0].name,"EISDIR.ELF");
   message[0]=0;subfunc_Paste(message,(char*)cross_destinations[destination]);
   assert(!message[0]&&!handles);
   assert(matches(target,input,sizeof(input)));
  }
 }

 /* Nested ordinary folders use the same production recursion. */
 reset();put("mc0:/MOVE.BIN",input,sizeof(input));
 strcpy(clipPath,"mc0:/");strcpy(clipFiles[0].name,"MOVE.BIN");browser_cut=1;
 message[0]=0;subfunc_Paste(message,"mmce0:/");
 assert(!message[0]&&move_deletes==1&&!nclipFiles);
 assert(access("mc0:/MOVE.BIN",F_OK)&&matches("mmce0:/MOVE.BIN",input,sizeof(input)));
 reset();put("mc0:/MOVE.BIN",input,sizeof(input));
 strcpy(clipPath,"mc0:/");strcpy(clipFiles[0].name,"MOVE.BIN");browser_cut=1;fail_write=1;
 message[0]=0;subfunc_Paste(message,"mmce0:/");
 assert(message[0]&&move_deletes==1&&nclipFiles==1);
 assert(matches("mc0:/MOVE.BIN",input,sizeof(input))&&!handles);
 /* A udpfs: copy that a stale server handle lost, or wrote over an old longer
  * file without truncating it, is caught by reading its size back after the
  * close: the paste fails, a Move keeps its source and a wrong copy is
  * removed. Zero-byte files are checked too. */
 const unsigned char longer[300]={0};
 for(int stale=UDPFS_LOST;stale<=UDPFS_TAIL;stale++)for(int bytes=0;bytes<=257;bytes+=257)for(int cut=0;cut<2;cut++){
  reset();put("mc0:/LOST.BIN",input,bytes);unlink("udpfs:/LOST.BIN");
  if(stale==UDPFS_TAIL)put("udpfs:/LOST.BIN",longer,sizeof(longer));
  strcpy(clipPath,"mc0:/");strcpy(clipFiles[0].name,"LOST.BIN");clipFiles[0].stats.FileSizeByte=bytes;
  browser_cut=cut;udpfs_stale=stale;int deletes=move_deletes;
  message[0]=0;subfunc_Paste(message,"udpfs:/");
  if(!strstr(message,stale==UDPFS_LOST?"Verifying destination (-2)":"Verifying destination (-5)")){
   fprintf(stderr,"FAIL %s udpfs: copy of %d bytes (%s) was reported as: \"%s\"\n",
           stale==UDPFS_LOST?"lost":"old longer",bytes,cut?"Move":"Paste",message);
   return 1;
  }
  assert(!handles&&move_deletes==deletes&&nclipFiles==1);
  assert(matches("mc0:/LOST.BIN",input,bytes)&&access("udpfs:/LOST.BIN",F_OK));
 }
 /* The same inside a folder: the Move keeps the whole source folder. */
 reset();mkdir("mc0:/MOVEDIR",0700);put("mc0:/MOVEDIR/IN.BIN",input,257);
 strcpy(clipPath,"mc0:/");strcpy(clipFiles[0].name,"MOVEDIR");clipFiles[0].stats.AttrFile=sceMcFileAttrSubdir;
 browser_cut=1;udpfs_stale=UDPFS_LOST;int deletes=move_deletes;
 message[0]=0;subfunc_Paste(message,"udpfs:/");
 assert(strstr(message,"Verifying destination (-2)")&&strstr(message,"\nIN.BIN\n"));
 assert(!handles&&move_deletes==deletes&&nclipFiles==1&&matches("mc0:/MOVEDIR/IN.BIN",input,257));
 assert(access("udpfs:/MOVEDIR/IN.BIN",F_OK));
 /* A good udpfs: paste opens each file exactly once more than the same paste
  * to usb0:, read-only, and a Move deletes its source only after that. */
 mkdir("mc0:/THREE",0700);put("mc0:/THREE/A.BIN",input,sizeof(input));put("mc0:/THREE/B.BIN",input,1);put("mc0:/THREE/C.BIN",input,0);
 int usb_opens=0;
 for(int device=0;device<2;device++){
  const char *dest=device?"udpfs:/":"usb0:/";const int lengths[]={sizeof(input),1,0};
  reset();strcpy(clipPath,"mc0:/");strcpy(clipFiles[0].name,"THREE");clipFiles[0].stats.AttrFile=sceMcFileAttrSubdir;
  message[0]=0;subfunc_Paste(message,(char *)dest);assert(!message[0]&&!handles);
  for(int i=0;i<3;i++){snprintf(target,sizeof(target),"%sTHREE/%c.BIN",dest,'A'+i);assert(matches(target,input,lengths[i]));}
  if(!device){usb_opens=opens;assert(usb_opens==2*3&&!udpfs_read_opens);}
  else if(opens!=usb_opens+3||udpfs_read_opens!=3){
   fprintf(stderr,"FAIL 3 files pasted to udpfs: took %d opens (%d read-only on udpfs:), %d to usb0:\n",opens,udpfs_read_opens,usb_opens);
   return 1;
  }
 }
 reset();put("mc0:/MOVED.BIN",input,257);strcpy(clipPath,"mc0:/");strcpy(clipFiles[0].name,"MOVED.BIN");
 browser_cut=1;move_target="udpfs:/MOVED.BIN";deletes=move_deletes;
 message[0]=0;subfunc_Paste(message,"udpfs:/");
 assert(!message[0]&&!handles&&move_deletes==deletes+1&&!nclipFiles&&udpfs_read_opens==1);
 assert(access("mc0:/MOVED.BIN",F_OK)&&matches("udpfs:/MOVED.BIN",input,257));
 /* A compressed image that the server reads back decompressed, larger or
  * smaller than the container, is still the copy: Paste and Move keep it, and
  * a Move deletes its source after the one read-back. */
 static const struct {const char *name,*magic;} images[]={
  {"GAME.cso","CISO"},{"GAME.CSO","CISO"},{"GAME.ciso","CISO"},{"GAME.zso","ZISO"},{"GAME.ziso","ZISO"},{"GAME.chd","MComprHD"}};
 unsigned char image[900];char source[MAX_PATH];
 for(unsigned n=0;n<sizeof(images)/sizeof(images[0]);n++)for(int larger=0;larger<2;larger++)for(int cut=0;cut<2;cut++){
  memcpy(image,input,sizeof(image));memcpy(image,images[n].magic,strlen(images[n].magic));
  snprintf(source,sizeof(source),"mc0:/%s",images[n].name);put(source,image,sizeof(image));
  snprintf(target,sizeof(target),"udpfs:/%s",images[n].name);unlink(target);
  reset();strcpy(clipPath,"mc0:/");strcpy(clipFiles[0].name,images[n].name);clipFiles[0].stats.FileSizeByte=sizeof(image);
  browser_cut=cut;move_target=target;udpfs_decompress=1;decoded_size=larger?5000:600;deletes=move_deletes;
  message[0]=0;subfunc_Paste(message,"udpfs:/");
  if(message[0]||!matches(target,image,sizeof(image))){
   fprintf(stderr,"FAIL %s read back decompressed as %d bytes, %s: \"%s\", copy %s\n",images[n].name,decoded_size,
           cut?"Move":"Paste",message,access(target,F_OK)?"removed":"kept");
   return 1;
  }
  assert(!handles&&udpfs_read_opens==1&&move_deletes==deletes+cut&&nclipFiles==!cut);
  assert(cut?access(source,F_OK)!=0:matches(source,image,sizeof(image)));
 }
 move_target="udpfs:/MOVED.BIN";
 /* A server that reads the container back as stored still gets the exact
  * check: an old longer .cso that O_TRUNC never cut fails and is removed. */
 memcpy(image,input,sizeof(image));memcpy(image,"CISO",4);put("mc0:/GAME.cso",image,sizeof(image));
 {unsigned char older[1200]={0};memcpy(older,"CISO",4);put("udpfs:/GAME.cso",older,sizeof(older));}
 reset();strcpy(clipPath,"mc0:/");strcpy(clipFiles[0].name,"GAME.cso");clipFiles[0].stats.FileSizeByte=sizeof(image);
 browser_cut=1;udpfs_stale=UDPFS_TAIL;deletes=move_deletes;
 message[0]=0;subfunc_Paste(message,"udpfs:/");
 if(!strstr(message,"Verifying destination (-5)")){
  fprintf(stderr,"FAIL old longer GAME.cso on a server that reads it as stored was reported as: \"%s\"\n",message);
  return 1;
 }
 assert(!handles&&move_deletes==deletes&&nclipFiles==1&&matches("mc0:/GAME.cso",image,sizeof(image))&&access("udpfs:/GAME.cso",F_OK));
 /* A close that failed is not overridden by a size that reads back right. */
 reset();put("mc0:/CLOSE.BIN",input,257);strcpy(clipPath,"mc0:/");strcpy(clipFiles[0].name,"CLOSE.BIN");clipFiles[0].stats.FileSizeByte=257;
 browser_cut=1;fail_close=1;deletes=move_deletes;
 message[0]=0;subfunc_Paste(message,"udpfs:/");
 if(!strstr(message,"Closing destination (-5)")){
  fprintf(stderr,"FAIL a udpfs: copy whose close failed was reported as: \"%s\"\n",message);
  return 1;
 }
 assert(!handles&&move_deletes==deletes&&nclipFiles==1&&matches("mc0:/CLOSE.BIN",input,257));
 /* Nested ordinary folders use the same production recursion. */
 reset();mkdir("udpfs:/NESTED",0700);put("udpfs:/NESTED/CHILD.BIN",input,257);
 strcpy(clipFiles[0].name,"NESTED");clipFiles[0].stats.AttrFile=sceMcFileAttrSubdir;
 message[0]=0;subfunc_Paste(message,"mc0:/");assert(!message[0]);assert(matches("mc0:/NESTED/CHILD.BIN",input,257));
 /* A failed source listing must not report a successful empty folder copy. */
 reset();strcpy(clipFiles[0].name,"NESTED");clipFiles[0].stats.AttrFile=sceMcFileAttrSubdir;fail_listing=1;
 message[0]=0;subfunc_Paste(message,"mc1:/");assert(strstr(message,"Listing source (-5)"));
 assert(!handles);
 /* A folder level that cannot get heap for its listing fails cleanly. */
 reset();strcpy(clipFiles[0].name,"NESTED");clipFiles[0].stats.AttrFile=sceMcFileAttrSubdir;fail_alloc_bytes=MAX_ENTRY*sizeof(FILEINFO);
 message[0]=0;subfunc_Paste(message,"usb0:/");
 assert(strstr(message,"Listing source (-12)")&&strstr(message,"Not enough memory")&&!handles);
 /* Names longer than 8 and 12 characters reach the device whole, at the top
  * level and inside a folder, and each folder level lists into its own heap
  * block. The host compiler does not reproduce the ps2build Clang prologue bug
  * that cut those names, so this checks the rewrite; the bug itself is caught
  * by scripts/check_stack_realign.py on the built ELFs in CI. */
 reset();mkdir("udpfs:/A_LONG_FOLDER_NAME",0700);
 put("udpfs:/A_LONG_FOLDER_NAME/RIPTOPL_LONG_NAME.ELF",input,300);put("udpfs:/RIPTOPL.ELF",input,301);
 strcpy(clipFiles[0].name,"RIPTOPL.ELF");clipFiles[0].stats.FileSizeByte=301;
 strcpy(clipFiles[1].name,"A_LONG_FOLDER_NAME");clipFiles[1].stats.AttrFile=sceMcFileAttrSubdir;nclipFiles=2;
 message[0]=0;subfunc_Paste(message,"mc1:/");assert(!message[0]&&!handles&&listing_allocs==1);
 assert(matches("mc1:/RIPTOPL.ELF",input,301)&&matches("mc1:/A_LONG_FOLDER_NAME/RIPTOPL_LONG_NAME.ELF",input,300));
 /* copy() works on its own copy of each entry, so the clipboard is unchanged. */
 assert(!strcmp(clipFiles[1].name,"A_LONG_FOLDER_NAME")&&clipFiles[1].stats.AttrFile==sceMcFileAttrSubdir);
 /* A failure inside the folder names the whole child in the dialog. */
 reset();strcpy(clipFiles[0].name,"A_LONG_FOLDER_NAME");clipFiles[0].stats.AttrFile=sceMcFileAttrSubdir;fail_write=1;
 message[0]=0;subfunc_Paste(message,"usb0:/");
 assert(strstr(message,"Writing destination (-28)")&&strstr(message,"\nRIPTOPL_LONG_NAME.ELF\n")&&!handles);
 /* An empty folder can be pasted repeatedly even without a game-save title. */
 reset();mkdir("udpfs:/EMPTY",0700);strcpy(clipFiles[0].name,"EMPTY");clipFiles[0].stats.AttrFile=sceMcFileAttrSubdir;
 for(int attempt=0;attempt<3;attempt++){message[0]=0;subfunc_Paste(message,"mc0:/");assert(!message[0]&&!handles);}
 /* Repeated operations clear prior error state, and formatting is bounded. */
 reset();message[0]=0;subfunc_Paste(message,"mc0:/");assert(!message[0]);
 char bounded[17];memset(bounded,'x',sizeof(bounded));filerCopyDiagFormat(bounded,16,-1);assert(!bounded[15]&&bounded[16]=='x');
 assert(!listings_live); /* every folder level freed its listing */
 puts("PASS: full paste/copy sequence, both slots, upstream direct overwrite, failures, recursion and bounded diagnostics; "
      "lost or over-long udpfs: copies fail and keep a Move's source");
}
'''
with tempfile.TemporaryDirectory(prefix='copy-pipeline-') as folder:
    p=Path(folder);(p/'test.c').write_text(code);exe=p/'test'
    cc=shutil.which('gcc') or shutil.which('cc')
    if not cc: raise SystemExit('Host C compiler required')
    subprocess.run([cc,'-std=gnu99','-Werror=implicit-function-declaration',str(p/'test.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],cwd=p,check=True,timeout=20)
