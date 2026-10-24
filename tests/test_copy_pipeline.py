"""Run the full production clipboard/paste/copy sequence on POSIX fixtures.

Drivers/RPCs are replaced by bounded file adapters and injected failures. The
copy orchestration, recursion, replacement transaction and diagnostics are real.
SDK open flags and FatFs stat results are adapted explicitly. This does not
emulate a UDPFS server, libmc or physical memory cards.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
copy=(root/'src/filer_copy.c').read_text()
browser=(root/'src/filer_browser.c').read_text()
fileops=(root/'src/filer_fileops.c').read_text()

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
#define EXFAT 1
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
#define sceMcFileAttrPDAExec 0x0800
#define sceMcFileAttrDupProhibit 0x80
#define FIO_O_RDONLY 0x0001
#define FIO_O_WRONLY 0x0002
#define FIO_O_CREAT 0x0200
#define FIO_O_EXCL 0x0800
#define FIO_O_TRUNC 0x0400
#define FIO_S_ISDIR S_ISDIR
#define FIO_CST_ATTR 1
#define FIO_CST_CT 2
#define FIO_CST_AT 4
#define FIO_CST_MT 8
#define LNG(x) #x
#define DPRINTF(...) ((void)0)
typedef struct {int Year,Month,Day,Hour,Min,Sec;} Date;
typedef struct {Date _Create,_Modify;unsigned FileSizeByte,AttrFile,Reserve1,Reserve2;char EntryName[32];} sceMcTblGetDir;
typedef struct {char name[MAX_NAME];unsigned char title[MAX_NAME];sceMcTblGetDir stats;} FILEINFO;
typedef struct {unsigned mode,attr;char ctime[8],mtime[8],atime[8];} iox_stat_t;
static struct {int PSU_HugeNames,PSU_DateNames,PSU_NoOverwrite;} config,*setting=&config;
static int PSU_content;
static int PM_flag[MAX_RECURSE],PM_file[MAX_RECURSE],PasteMode,PasteProgress_f,mctype_PSx;
static int nclipFiles,browser_cut,browser_pushed,browser_cd;
static FILEINFO clipFiles[MAX_ENTRY];static char clipPath[MAX_PATH];
static unsigned clipIopResetGeneration,generation;
static u64 written_size,PasteTime,clock_ms;
static int source_ready=1,load_ok=1,reset_preflight,read_cap=4096,write_cap=37,cancel_copy;
static int fail_open,fail_rewind,fail_read,fail_write,fail_close,fail_listing,fail_stat,fail_out_open,early_eof,fail_measure,corrupt_verify,fail_publish;
static int fd_kind[1024],handles,read_calls,write_calls;
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
static void nonDialog(char *s){(void)s;}
static int keyboard(char *s,int n){(void)s;(void)n;return 0;}
static int isPcCardCfPath(const char *s){(void)s;return 0;}
static int isPcCardCfOnline(void){return 1;}
static void ensureMemoryCardPortAccessible(int p){assert(p==0||p==1);}
static char *makeHostPath(char *d,char *s){if(d!=s)strcpy(d,s);return d;}
static void genLimObjName(char *p,int reserve){(void)p;(void)reserve;}
static int normalizeFatFsStatResult(const char *,int);
static int genGetStat(const char *p,iox_stat_t *out){
 struct stat s;
 if(!strncmp(p,"host:",5))return -EIO;
 if(fail_stat&&!strncmp(p,"mc0:",4))return -EACCES;
 if(stat(p,&s)) {
  int raw=!strncmp(p,"usb0:",5)&&errno==ENOENT?-4:(!strncmp(p,"mc",2)&&(p[2]=='0'||p[2]=='1')&&p[3]==':'&&errno==ENOENT?1:-errno);
  return normalizeFatFsStatResult(p,raw);
 }
 out->mode=s.st_mode;return 0;
}
static int genOpen(const char *p,int flags){
 if(fail_open&&flags==FIO_O_RDONLY&&!strncmp(p,"udpfs:",6))return -ENOENT;
 if(fail_out_open&&(flags&FIO_O_WRONLY))return -EACCES;
 /* Physical MC treats FIO_O_EXCL (0x0800) as PDAExec, not exclusive open. */
 if ((flags&FIO_O_WRONLY) && (!strncmp(p,"mc0:",4)||!strncmp(p,"mc1:",4))) assert(!(flags&sceMcFileAttrPDAExec));
 if ((flags&FIO_O_WRONLY) && !strncmp(p,"usb0:",5)) assert(flags&FIO_O_EXCL);
 int host_flags=(flags&FIO_O_WRONLY)?O_WRONLY:O_RDONLY;
 if(flags&FIO_O_CREAT)host_flags|=O_CREAT;
 if(flags&FIO_O_TRUNC)host_flags|=O_TRUNC;
 if(flags&FIO_O_EXCL)host_flags|=O_EXCL;
 int fd=open(p,host_flags,0600);if(fd<0)return -errno;assert(fd<1024);
 fd_kind[fd]=(flags&FIO_O_WRONLY)?2:!strncmp(p,"udpfs:",6)?1:3;
 fd_generation[fd]=generation;handles++;return fd;
}
static int genClose(int fd){int kind=fd_kind[fd];assert(kind&&handles>0);fd_kind[fd]=0;handles--;int r=close(fd);return fail_close&&kind==2?-EIO:r;}
static s64 genLseek(int fd,s64 n,int whence){assert(fd_generation[fd]==generation);if(fail_measure&&whence==SEEK_END)return -EIO;if(fail_rewind&&whence==SEEK_SET)return -EIO;return lseek(fd,n,whence);}
static int genRead(int fd,void *p,int n){assert(fd_generation[fd]==generation);read_calls++;if(early_eof&&fd_kind[fd]==1&&read_calls>1)return 0;if(fail_read&&fd_kind[fd]==1)return -ETIMEDOUT;int r=read(fd,p,n>read_cap?read_cap:n);if(corrupt_verify&&fd_kind[fd]==3&&r>0)((char*)p)[0]^=1;return r<0?-errno:r;}
static int genWrite(int fd,void *p,int n){assert(fd_generation[fd]==generation);write_calls++;if(fail_write&&write_calls>=2)return -ENOSPC;int r=write(fd,p,n>write_cap?write_cap:n);return r<0?-errno:r;}
static int genRemove(char *p){return unlink(p)?-errno:0;}
static int fileXioRename(const char *a,const char *b){return fail_publish?-EIO:rename(a,b)?-errno:0;}
static int mc_result;
static int mcSetFileInfo(int port,int slot,const char *from,const sceMcTblGetDir *info,int flags){
 (void)slot;if(flags==0x10){char a[MAX_PATH],b[MAX_PATH];snprintf(a,sizeof(a),"mc%d:/%s",port,from);strcpy(b,a);char *leaf=strrchr(b,'/');assert(leaf);snprintf(leaf+1,sizeof(b)-(leaf+1-b),"%.32s",info->EntryName);mc_result=fileXioRename(a,b);}else mc_result=0;return 0;
}
static int mcSync(int mode,void *c,int *r){(void)mode;(void)c;if(r)*r=mc_result;return 1;}
static int mcGetInfo(int p,int s,int *type,int *a,int *b){(void)p;(void)s;if(type)*type=2;if(a)*a=1000;if(b)*b=1000;return 0;}
static int fileXioChStat(const char *p,const iox_stat_t *s,int f){(void)p;(void)s;(void)f;return 0;}
static int getHddParty(const char *p,const FILEINFO *f,char *a,char *b){(void)p;(void)f;(void)a;(void)b;assert(0);return -1;}
static int mountParty(const char *p){(void)p;assert(0);return -1;}
static int fileShowTitles(void){return 0;}
static int filerGetGameTitle(const char *p,const FILEINFO *f,unsigned char *out){(void)p;(void)f;out[0]=0;return -1;}
static void transcpy_sjis(char *out,const unsigned char *in){strcpy(out,(const char *)in);}
static int filerConfirmExploitDelete(const char *p,const FILEINFO *f){(void)p;(void)f;return 1;}
static int filerConfirmExploitModify(const char *p,const FILEINFO *f){(void)p;(void)f;return 1;}
static int filerIsExploitProtectedPath(const char *p,const FILEINFO *f){(void)p;(void)f;return 0;}
static int delete(const char *p,const FILEINFO *f){(void)p;(void)f;assert(0);return -1;}
static void markMx4sioDestinationAfterWrite(const char *p){(void)p;}
static int genCmpFileExt(const char *p,const char *e){(void)p;(void)e;return 0;}
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
if 'static int isMcmanStatPath(' in fileops:
    code+=function(fileops,'static int isMcmanStatPath(')
code+=function(fileops,'static int normalizeFatFsStatResult(')
a=copy.index('/* One paste context');b=copy.index('static int isMemoryCardLikePath(',a);code+=copy[a:b]
for sig in ('static int isMemoryCardLikePath(', 'static int isHddCommonParty(',
            'static int copyReadChunk(', 'static int copyWriteChunk(',
            'static int calcTransferSpeed(', 'static int calcRemainingTime(',
            'static int smoothTransferSpeed(', 'static int applyMcInfoToMcPath('):
    code+=function(copy,sig)
code+=copy[copy.index('int copy('):]
code+=function(browser,'static int waitForClipboardSourceDevice(')
code+=function(browser,'static void subfunc_Paste(')
code+=r'''
static void put(const char *p,const unsigned char *data,int n){int fd=open(p,O_CREAT|O_TRUNC|O_WRONLY,0600);assert(fd>=0);assert(write(fd,data,n)==n);assert(!close(fd));}
static int matches(const char *p,const unsigned char *data,int n){unsigned char out[70000];int fd=open(p,O_RDONLY);if(fd<0)return 0;int count=read(fd,out,sizeof(out));close(fd);return count==n&&!memcmp(data,out,n);}
static unsigned char input[65537];
static void reset(void){assert(!handles);source_ready=load_ok=1;reset_preflight=0;fail_open=fail_rewind=fail_read=fail_write=fail_close=fail_listing=cancel_copy=fail_stat=fail_out_open=early_eof=fail_measure=corrupt_verify=fail_publish=0;read_calls=write_calls=0;browser_cut=0;browser_cd=0;generation=1;clipIopResetGeneration=1;PasteMode=PM_NORMAL;strcpy(clipPath,"udpfs:/");nclipFiles=1;memset(clipFiles,0,sizeof(clipFiles));strcpy(clipFiles[0].name,"BOOT.ELF");clipFiles[0].stats.FileSizeByte=sizeof(input);}
int main(void){
 assert(sizeof(copy_diag)<=128); /* fixed runtime budget, no log allocation */
 mkdir("udpfs:",0700);mkdir("mc0:",0700);mkdir("mc1:",0700);mkdir("usb0:",0700);mkdir("host:",0700);
 for(int i=0;i<(int)sizeof(input);i++)input[i]=(unsigned char)(i*19+7);
 put("udpfs:/BOOT.ELF",input,sizeof(input));
 const char *destinations[]={"mc0:/","mc1:/","usb0:/"};char message[MAX_PATH],target[MAX_PATH];
 for(int device=0;device<3;device++)for(int attempt=0;attempt<3;attempt++){
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
 const char *stages[]={"Opening source","Rewinding source","Reading source","Writing destination","Closing destination","Copy cancelled","Preparing devices","Checking source","Checking destination","Opening destination","Reading source","Measuring source","Verifying replacement","Publishing replacement"};
 for(int failure=0;failure<14;failure++){
  reset();put("mc0:/BOOT.ELF",old,sizeof(old));
  if(failure==0)fail_open=1;if(failure==1)fail_rewind=1;if(failure==2)fail_read=1;
  if(failure==3)fail_write=1;if(failure==4)fail_close=1;if(failure==5)cancel_copy=1;
  if(failure==6)load_ok=0;if(failure==7){source_ready=0;reset_preflight=1;}
  if(failure==8)fail_stat=1;if(failure==9)fail_out_open=1;if(failure==10)early_eof=1;
  if(failure==11)fail_measure=1;if(failure==12)corrupt_verify=1;if(failure==13)fail_publish=1;
  message[0]=0;subfunc_Paste(message,"mc0:/");
  assert(message[0]&&strstr(message,stages[failure])&&strstr(message,"fixture-head"));
  assert(!handles&&matches("mc0:/BOOT.ELF",old,sizeof(old)));
  if(failure==3){assert(strstr(message,"37/65537 B")&&strstr(message,"(-28)")&&strstr(message,"Not enough free space"));}
  if(failure==7)assert(strstr(message,"IOP 1>2"));
  if(failure==8 || failure==9)assert(strstr(message,"Access denied"));
  if(failure==0)assert(strstr(message,"was not found"));
  if(failure==2)assert(strstr(message,"did not respond in time"));
 }
 const int codes[]={-ENOSPC,-EACCES,-EROFS,-ENOENT,-ENODEV,-EIO,-ETIMEDOUT,-ENOMEM,-EEXIST,-999};
 const char *reasons[]={"Not enough free space","Access denied","read-only","was not found","unavailable","I/O failure","did not respond in time","Not enough memory","already exists","Keep the error code"};
 for(unsigned i=0;i<sizeof(codes)/sizeof(*codes);i++){
  filerCopyDiagBegin("usb0:/","mc0:/");char expected[20];snprintf(expected,sizeof(expected),"(%d)",codes[i]);
  filerCopyDiagFormat(message,sizeof(message),codes[i]);
  assert(strstr(message,reasons[i])&&strstr(message,expected)&&strstr(message,"fixture-head"));
 }
 /* Nested ordinary folders use the same production recursion. */
 reset();mkdir("udpfs:/NESTED",0700);put("udpfs:/NESTED/CHILD.BIN",input,257);
 strcpy(clipFiles[0].name,"NESTED");clipFiles[0].stats.AttrFile=sceMcFileAttrSubdir;
 message[0]=0;subfunc_Paste(message,"mc0:/");assert(!message[0]);assert(matches("mc0:/NESTED/CHILD.BIN",input,257));
 /* A failed source listing must not report a successful empty folder copy. */
 reset();strcpy(clipFiles[0].name,"NESTED");clipFiles[0].stats.AttrFile=sceMcFileAttrSubdir;fail_listing=1;
 message[0]=0;subfunc_Paste(message,"mc1:/");assert(strstr(message,"Listing source (-5)"));
 /* An empty folder can be pasted repeatedly even without a game-save title. */
 reset();mkdir("udpfs:/EMPTY",0700);strcpy(clipFiles[0].name,"EMPTY");clipFiles[0].stats.AttrFile=sceMcFileAttrSubdir;
 for(int attempt=0;attempt<3;attempt++){message[0]=0;subfunc_Paste(message,"mc0:/");assert(!message[0]&&!handles);}
 /* Same-directory paste is a no-op, even for Cut. Preserve the source and
  * do not perform device setup, copy I/O, or move-source deletion. */
 for(int cut=0;cut<2;cut++) {
  reset();strcpy(clipPath,"mc0:/");browser_cut=cut;message[0]=0;
  put("mc0:/BOOT.ELF",input,sizeof(input));load_ok=source_ready=0;
  subfunc_Paste(message,"mc0:/");
  assert(!message[0] && browser_cd && !handles && !read_calls && !write_calls);
  assert(nclipFiles==(cut?0:1));assert(matches("mc0:/BOOT.ELF",input,sizeof(input)));
 }
 /* Repeated operations clear prior error state, and formatting is bounded. */
 reset();message[0]=0;subfunc_Paste(message,"mc0:/");assert(!message[0]);
 char bounded[17];memset(bounded,'x',sizeof(bounded));filerCopyDiagFormat(bounded,16,-1);assert(!bounded[15]&&bounded[16]=='x');
 puts("PASS: full paste/copy sequence, both slots, replacement, failures, recursion and bounded diagnostics");
}
'''
with tempfile.TemporaryDirectory(prefix='copy-pipeline-') as folder:
    p=Path(folder);(p/'test.c').write_text(code);exe=p/'test'
    cc=shutil.which('gcc') or shutil.which('cc')
    if not cc: raise SystemExit('Host C compiler required')
    subprocess.run([cc,'-std=gnu99','-Werror=implicit-function-declaration',str(p/'test.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],cwd=p,check=True,timeout=20)
