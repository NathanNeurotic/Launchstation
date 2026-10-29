"""
Exercise production interactive/shared rename code on host device fixtures.
Ported from saildot4k/Launchstation (df7214d, 931e4e2).

Drivers, card RPCs and mounts are adapters; this is not console validation.
An optional filer_actions.c path reproduces defects in an earlier implementation.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
actions = (Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'src/filer_actions.c').read_text()
fileops = (root / 'src/filer_fileops.c').read_text()
hdd = (root / 'src/gui_hdd0_format.c').read_text()
filer = (root / 'src/filer.c').read_text()


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#define XFROM 1
#define DVRP 1
#define ETH 1
#define UDPFS 1
#define MAX_PATH 1024
#define MAX_NAME 256
#define FIO_O_RDONLY O_RDONLY
#define FIO_S_ISDIR S_ISDIR
#define sceMcFileAttrSubdir 0x20
#define IOCTL_RENAME 0xFEEDC0DE
#define IOCTL_BUF_SIZE 1024
#define HOST_RENAME_PATH_BYTES 256
#define FILER_CONFLICT_NONE 0
#define FILER_CONFLICT_FILE 1
#define FILER_CONFLICT_DIR 2
#define stricmp strcasecmp
typedef struct { unsigned char dates[16];unsigned size;unsigned short AttrFile,reserved;unsigned extra[2];char EntryName[32]; } sceMcTblGetDir;
typedef struct { char name[MAX_NAME];sceMcTblGetDir stats; } FILEINFO;
typedef struct { unsigned mode; } iox_stat_t;
typedef struct { iox_stat_t stat;char name[256]; } iox_dirent_t;
static int load_ok,loaded,protected_path,cf_online,stat_error,driver_error,false_success;
static int submit_error,sync_error,info_error,card_type,card_job,card_result,mctype_PSx,completion_error;
static int host_use_Bsl;
static int handles,last_fd=-1,rename_calls,ioctl_calls,set_calls,close_error;
static int case_insensitive,mutation_calls,fail_at,applied_error_at;
static int native_case_calls,case_support,case_error,case_applied_error,dread_calls;
static int case_stat_error;
static int scan_calls,scan_fail_at,scan_close_fail_at,scan_open_fail_at,scan_bad_name_at;
static char fd_path[1024][MAX_PATH];
static int fd_directory[1024];
static DIR *fd_stream[1024];
static int getHddParty(const char*,const FILEINFO*,char*,char*);
static int getHddDVRPParty(const char*,const FILEINFO*,char*,char*);
static int mountParty(const char *p){return p[3]=='1'?7:4;}
static int getDVRPPartyMountIndex(const char *p){return strstr(p,"__xdata")?1:0;}
static int mountDVRPParty(const char *p){return getDVRPPartyMountIndex(p);}
static int isPcCardCfPath(const char *p){return p&&!strncmp(p,"cf",2);}
static int isPcCardCfOnline(void){return cf_online;}
static int ensurePathDeviceStackReady(const char *p){(void)p;assert(!handles);loaded++;return load_ok;}
static int filerIsExploitProtectedPath(const char *p,const FILEINFO *f){(void)p;(void)f;return protected_path;}
static void ensureMemoryCardPortAccessible(int p){assert(p==0||p==1);}
static char *makeHostPath(char *out,char *in);
static int genFixPath(const char *p,char *out){
 char party[MAX_NAME];
 if(!strncmp(p,"hdd",3)){if(getHddParty(p,NULL,party,out)<0)return -1;out[3]='0'+mountParty(party);}
 else if(!strncmp(p,"dvr_hdd",7)){if(getHddDVRPParty(p,NULL,party,out)<0)return -1;out[7]='0'+mountDVRPParty(party);}
 else if(!strncmp(p,"cf:",3))snprintf(out,MAX_PATH,"cf0:%s",p+3);
 else if(!strncmp(p,"mass:",5))snprintf(out,MAX_PATH,"usb0:%s",p+5);
 else strcpy(out,p);
 if(!strncmp(p,"host:",5)){makeHostPath(out,out);return 0;}
 if(out[strlen(out)-1]==':')strcat(out,"/");
 return 0;
}
static void physical(const char *p,char *out){
 assert(strlen(p)<MAX_PATH);strcpy(out,p);char *colon=strchr(out,':');
 if(!strncmp(p,"host",4))for(char *s=colon+1;*s;s++)if(*s=='\\')*s='/';
 if(colon&&!colon[1])strcat(out,"/");
 if(colon&&colon[1]&&colon[1]!='/'){assert(strlen(out)+1<MAX_PATH);memmove(colon+2,colon+1,strlen(colon+1)+1);colon[1]='/';}
}
static void resolve_case(char *path){
 if(!case_insensitive||!access(path,F_OK))return;
 char parent[MAX_PATH];strcpy(parent,path);char *sep=strrchr(parent,'/');if(!sep)return;
 char *leaf=path+(sep-parent)+1;*sep=0;DIR *d=opendir(parent);if(!d)return;
 struct dirent *e;while((e=readdir(d)))if(!strcasecmp(e->d_name,leaf)){strcpy(leaf,e->d_name);break;}
 closedir(d);
}
static int mutate(const char *from,const char *to){
 char a[MAX_PATH],b[MAX_PATH];strcpy(a,from);strcpy(b,to);resolve_case(a);resolve_case(b);
 if(!strcasecmp(from,to)&&strcmp(from,to)){
  native_case_calls++;
  if(case_insensitive&&!strcmp(a,b)){
   if(case_error)return -case_error;
   if(case_support==0)return -EEXIST;
   if(case_support==2)return 0;
   strcpy(b,to); /* Driver preserves the requested spelling. */
  }
 }
 mutation_calls++;
 if(mutation_calls==fail_at)return -ENOSPC;
 if(rename(a,b))return -errno;
 if(case_applied_error&&!strcasecmp(from,to)&&strcmp(from,to))return -case_applied_error;
 return mutation_calls==applied_error_at?-EIO:0;
}
static int genGetStat(const char *p,iox_stat_t *out){
 if(!strncmp(p,"host",4))return -ENOSYS;
 if(stat_error&&strstr(p,"/NEW.BIN"))return -stat_error;
 if(case_stat_error&&strstr(p,"/old.bin"))return -case_stat_error;
 if(!strncmp(p,"mmce",4)&&strlen(strchr(p,':')+1)>254)return -ENAMETOOLONG;
 char path[MAX_PATH];physical(p,path);resolve_case(path);struct stat s;if(stat(path,&s))return -errno;out->mode=s.st_mode;return 0;
}
static int open_handle(const char *p,int directory){
 char path[MAX_PATH];physical(p,path);resolve_case(path);int fd=open(path,O_RDONLY|(directory?O_DIRECTORY:0));
 if(fd<0)return -errno;assert(fd<1024);snprintf(fd_path[fd],MAX_PATH,"%s",path);
 fd_directory[fd]=directory;handles++;last_fd=fd;return fd;
}
static int fileXioOpen(const char *p,int f,int m){(void)m;assert(f==O_RDONLY);return open_handle(p,0);}
static int fileXioDopen(const char *p){return open_handle(p,1);}
static int close_handle(int fd){assert(handles>0);handles--;assert(!close(fd));return close_error?-EIO:0;}
static int fileXioClose(int fd){return close_handle(fd);}
static int fileXioDclose(int fd){assert(fd_directory[fd]);if(fd_stream[fd]){closedir(fd_stream[fd]);fd_stream[fd]=NULL;}return close_handle(fd);}
static int genOpen(const char *p,int f){return fileXioOpen(p,f,0);}
static int genClose(int fd){return fileXioClose(fd);}
static int genDopen(char *p){
 scan_calls++;if(scan_calls==scan_open_fail_at)return -EACCES;
 return fileXioDopen(p);
}
static int fileXioDread(int fd,iox_dirent_t *out){
 dread_calls++;
 assert(fd_directory[fd]);if(scan_calls==scan_fail_at)return -EIO;
 if(scan_calls==scan_bad_name_at){memset(out->name,'X',sizeof(out->name));return 1;}
 if(!fd_stream[fd]){fd_stream[fd]=fdopendir(dup(fd));assert(fd_stream[fd]);}
 struct dirent *e=readdir(fd_stream[fd]);if(!e)return 0;
 snprintf(out->name,sizeof(out->name),"%s",e->d_name);return 1;
}
static int genDclose(int fd){int ret=fileXioDclose(fd);return scan_calls==scan_close_fail_at?-EIO:ret;}
static int fileXioRename(const char *a,const char *b){
 rename_calls++;if(driver_error)return -driver_error;if(false_success)return 0;
 if(!strncmp(a,"host",4)||!strncmp(a,"udpfs",5)||!strncmp(a,"xfrom",5))return -ENOSYS;
 char from[MAX_PATH],to[MAX_PATH];physical(a,from);
 if(!strncmp(a,"vmc",3)){
  if(strchr(b,':')||strchr(b,'/'))return -EINVAL;
  if(strlen(b)>(card_type==1?20:31))return -ENAMETOOLONG;
  strcpy(to,from);char *leaf=strrchr(to,'/');assert(leaf);snprintf(leaf+1,sizeof(to)-(leaf+1-to),"%s",b);
 }else physical(b,to);
 return mutate(from,to);
}
static int mcGetInfo(int p,int s,int *type,int *a,int *b){
 (void)p;(void)s;(void)a;(void)b;*type=card_type;card_job=0;card_result=info_error;return submit_error==1?-1:0;
}
static int mcSync(int mode,void *cmd,int *result){
 (void)mode;(void)cmd;if(sync_error==card_job+1)return -1;*result=card_result;return 1;
}
static int cardSetInfo(const char *device,const char *from,const sceMcTblGetDir *info,int flags){
 assert(flags==0x10);set_calls++;if(submit_error==2)return -1;
 char old[MAX_PATH],to[MAX_PATH],leaf[33];memcpy(leaf,info->EntryName,32);leaf[32]=0;
 assert(strlen(leaf)<=(card_type==1?20:31));snprintf(old,sizeof(old),"%s/%s",device,from);strcpy(to,old);
 char *sep=strrchr(to,'/');assert(sep);snprintf(sep+1,sizeof(to)-(sep+1-to),"%s",leaf);
 card_job=1;card_result=completion_error?completion_error:driver_error?-3:0;
 if(!card_result&&!false_success){int ret=mutate(old,to);card_result=ret==-ENOSPC?-3:ret==-EEXIST?-6:ret?-8:0;}
 return 0;
}
static int mcSetFileInfo(int p,int s,const char *from,const sceMcTblGetDir *info,int flags){
 (void)s;char device[12];snprintf(device,sizeof(device),"mc%d:",p);return cardSetInfo(device,from,info,flags);
}
static int xfromGetInfo(int p,int s,int *type,int *a,int *b){return mcGetInfo(p,s,type,a,b);}
static int xfromSync(int m,void *c,int *r){return mcSync(m,c,r);}
static int xfromSetFileInfo(int p,int s,const char *from,const sceMcTblGetDir *info,int flags){
 assert(!p&&!s);return cardSetInfo("xfrom0:",from,info,flags);
}
static int fileXioIoctl(int fd,int request,void *data){
 assert(fd==last_fd&&(unsigned)request==IOCTL_RENAME);ioctl_calls++;
 const char *payload=data;size_t n=strlen(payload);assert(n<256);
 for(size_t i=n;i<IOCTL_BUF_SIZE;i++)assert(!payload[i]);
 if(driver_error)return -ENOSPC;if(false_success)return 0;
 assert(!strstr(payload,"\\/")&&!strstr(payload,"/\\"));
 char remote[MAX_PATH],to[MAX_PATH];snprintf(remote,sizeof(remote),"host:%s",payload);physical(remote,to);
 return mutate(fd_path[fd],to);
}
static void reset(void){assert(!handles);load_ok=cf_online=1;card_type=2;last_fd=-1;
 loaded=protected_path=stat_error=driver_error=false_success=submit_error=sync_error=info_error=0;
 rename_calls=ioctl_calls=set_calls=close_error=completion_error=host_use_Bsl=0;
 case_insensitive=mutation_calls=fail_at=applied_error_at=0;
 native_case_calls=case_support=case_error=case_applied_error=dread_calls=0;
 case_stat_error=0;
 scan_calls=scan_fail_at=scan_close_fail_at=scan_open_fail_at=scan_bad_name_at=0;}
static void put(const char *p){int fd=open(p,O_CREAT|O_TRUNC|O_WRONLY,0644);assert(fd>=0);assert(write(fd,"exact file bytes",16)==16);assert(!close(fd));}
static void remove_entry(const char *p){
 struct stat st;if(stat(p,&st))return;if(!S_ISDIR(st.st_mode)){assert(!unlink(p));return;}
 DIR *d=opendir(p);assert(d);struct dirent *e;
 while((e=readdir(d)))if(strcmp(e->d_name,".")&&strcmp(e->d_name,"..")){
  char child[MAX_PATH];snprintf(child,sizeof(child),"%s/%s",p,e->d_name);remove_entry(child);
 }assert(!closedir(d));assert(!rmdir(p));
}
static void make_entry(const char *p,int directory){remove_entry(p);if(directory){assert(!mkdir(p,0700));char child[MAX_PATH];snprintf(child,sizeof(child),"%s/CHILD.BIN",p);put(child);}else put(p);}
'''
code += function(filer, 'char *makeHostPath(').replace('char *makeHostPath(', 'static char *makeHostPath(', 1)
for sig in ('static int getHddDeviceFromPath(', 'int getHddParty(', 'int getHddDVRPParty('):
    code += function(hdd, sig).replace('int getHddParty(', 'static int getHddParty(').replace('int getHddDVRPParty(', 'static int getHddDVRPParty(')
for sig in ('static int ensurePathPcCardCfOnline(', 'static void normalizePcCardCfPath(',
            'int genCanRenamePath(', 'static int renameCardResult(', 'int genRenameNoReplace('):
    code += function(fileops, sig)
for sig in ('static int filerPathConflictType(', 'static int filerPathExistsForConflict('):
    code += function(actions, sig)
if 'static int filerRenameHost(' in actions:
    code += function(actions, 'static int filerRenameHost(')
if 'static int filerRenameSiblingOnce(' in actions:
    code += actions[actions.index('static int filerRenameSiblingOnce('):actions.index('int Rename(')]
if 'static const char *getXfromRelativePath(' in actions:
    code += function(actions, 'static const char *getXfromRelativePath(')
code += function(actions, 'int Rename(')
code += r'''
int main(void){
 const struct {const char *path;int sensitive;} devices[]={
  {"mc0:/",1},{"mc1:/",1},{"vmc0:/",1},{"vmc1:/",1},{"usb0:/",0},{"usb1:/",0},{"mass:/",0},{"mass0:/",0},{"mass9:/",0},
  {"ata0:/",0},{"ata1:/",0},{"mx4sio:/",0},{"mmce0:/",0},{"mmce1:/",0},{"cf:/",0},{"cf0:/",0},{"pfs4:/",1},{"pfs7:/",1},
  {"hdd0:/__common/",1},{"hdd1:/__common/",1},{"dvr_hdd0:/__xcontents/",1},{"dvr_hdd0:/__xdata/",1},{"xfrom0:/",1},{"host:/",0}
 };
 char path[MAX_PATH],parent[MAX_PATH],old[MAX_PATH],to[MAX_PATH];int operations=0;
 for(unsigned d=0;d<sizeof(devices)/sizeof(devices[0]);d++)for(int nested=0;nested<2;nested++){
  snprintf(path,sizeof(path),"%s%s",devices[d].path,nested?"NESTED/":"");reset();
  char root_path[MAX_PATH],mapped[MAX_PATH];assert(!genFixPath(devices[d].path,root_path));char *colon=strchr(root_path,':');assert(colon);
  colon[1]=0;mkdir(root_path,0700);assert(!genFixPath(path,mapped));physical(mapped,parent);if(nested)mkdir(parent,0700);
  snprintf(old,sizeof(old),"%sOLD.BIN",parent);snprintf(to,sizeof(to),"%sNEW.BIN",parent);
  for(int directory=0;directory<2;directory++){
   reset();
   FILEINFO file={0};strcpy(file.name,"OLD.BIN");file.stats.AttrFile=(directory?0x20:0x10)|0x180;
   memset(file.stats.EntryName,0xa7,32);FILEINFO unchanged=file;
   remove_entry(to);make_entry(old,directory);struct stat before,after;assert(!stat(old,&before));
   int renamed=Rename(path,&file,"NEW.BIN");if(renamed)fprintf(stderr,"Rename %s directory=%d failed: %d\n",path,directory,renamed);
   assert(!renamed);assert(!memcmp(&file,&unchanged,sizeof(file))&&!handles);
   assert(access(old,F_OK)<0&&!stat(to,&after)&&after.st_ino==before.st_ino&&after.st_mode==before.st_mode&&after.st_mtime==before.st_mtime);
   if(directory){char child[MAX_PATH];snprintf(child,sizeof(child),"%s/CHILD.BIN",to);assert(!access(child,F_OK));}
   else{char bytes[17]={0};int fd=open(to,O_RDONLY);assert(read(fd,bytes,sizeof(bytes))==16&&!memcmp(bytes,"exact file bytes",16));close(fd);}
   operations++;
   for(int conflict_dir=0;conflict_dir<2;conflict_dir++){
    reset();make_entry(old,directory);make_entry(to,conflict_dir);
    assert(Rename(path,&file,"NEW.BIN")==-EEXIST&&!handles&&!rename_calls&&!ioctl_calls&&!set_calls);
    assert(!access(old,F_OK)&&!access(to,F_OK));operations++;
   }
   reset();remove_entry(to);assert(!Rename(path,&file,"OLD.BIN")&&!rename_calls&&!ioctl_calls&&!set_calls&&!handles);operations++;
   for(int failure=0;failure<2;failure++){
    reset();false_success=!failure;driver_error=failure?ENOSPC:0;
    int r=Rename(path,&file,"NEW.BIN");assert(r==(failure?-ENOSPC:-EIO));
    assert(!access(old,F_OK)&&access(to,F_OK)<0&&!handles&&!memcmp(&file,&unchanged,sizeof(file)));operations++;
   }
   if(strncmp(path,"host",4)){
    reset();stat_error=EACCES;assert(Rename(path,&file,"NEW.BIN")==-EACCES);
    assert(!rename_calls&&!set_calls&&!handles&&!access(old,F_OK));operations++;
   }
   char case_to[MAX_PATH];snprintf(case_to,sizeof(case_to),"%sold.bin",parent);
   for(int insensitive=0;insensitive<(devices[d].sensitive?1:2);insensitive++){
    reset();case_insensitive=insensitive;case_support=1;remove_entry(case_to);make_entry(old,directory);
    assert(!stat(old,&before));
    if(insensitive&&!strncmp(path,"mmce",4)){
     case_error=1;
     assert(Rename(path,&file,"old.bin")==-1&&native_case_calls==1&&!mutation_calls&&!handles);
     assert(!memcmp(&file,&unchanged,sizeof(file))&&!stat(old,&after)&&after.st_ino==before.st_ino&&access(case_to,F_OK)<0);
     operations++;continue;
    }
    assert(!Rename(path,&file,"old.bin")&&mutation_calls==1&&!handles);
    if(!insensitive)assert(!dread_calls&&native_case_calls==1);
    assert(!memcmp(&file,&unchanged,sizeof(file))&&access(old,F_OK)<0&&!stat(case_to,&after));
    assert(after.st_ino==before.st_ino&&after.st_mode==before.st_mode&&after.st_mtime==before.st_mtime);
    if(directory){char child[MAX_PATH];snprintf(child,sizeof(child),"%s/CHILD.BIN",case_to);assert(!access(child,F_OK));}
    else{char bytes[17]={0};int fd=open(case_to,O_RDONLY);assert(read(fd,bytes,sizeof(bytes))==16&&!memcmp(bytes,"exact file bytes",16));close(fd);}
    remove_entry(case_to);operations++;
   }
   for(int conflict_dir=0;conflict_dir<2;conflict_dir++){
    reset();make_entry(old,directory);make_entry(case_to,conflict_dir);
    assert(Rename(path,&file,"old.bin")==-EEXIST&&!mutation_calls&&!handles);
    assert(!access(old,F_OK)&&!access(case_to,F_OK));remove_entry(case_to);operations++;
   }
   /* A third case-equivalent entry is valid on a case-sensitive filesystem.
    * Renaming OLD.BIN to unused old.bin must leave Old.Bin untouched. */
   reset();char third[MAX_PATH];snprintf(third,sizeof(third),"%sOld.Bin",parent);
   make_entry(old,directory);make_entry(third,!directory);assert(!stat(third,&before));
   assert(!Rename(path,&file,"old.bin")&&mutation_calls==1&&native_case_calls==1&&!dread_calls&&!handles);
   assert(!stat(third,&after)&&after.st_ino==before.st_ino&&after.st_mode==before.st_mode);
   assert(!access(case_to,F_OK)&&access(old,F_OK)<0);remove_entry(case_to);remove_entry(third);operations++;
   remove_entry(old);remove_entry(to);
  }
 }
 /* Case-sensitive drivers never stage/retry a failed case-only rename. */
 for(unsigned d=0;d<sizeof(devices)/sizeof(devices[0]);d++)if(devices[d].sensitive)for(int directory=0;directory<2;directory++)for(int fault=0;fault<3;fault++){
  reset();char mapped[MAX_PATH];assert(!genFixPath(devices[d].path,mapped));physical(mapped,parent);
  snprintf(old,sizeof(old),"%sOLD.BIN",parent);snprintf(to,sizeof(to),"%sold.bin",parent);make_entry(old,directory);
  FILEINFO f={0};strcpy(f.name,"OLD.BIN");f.stats.AttrFile=directory?sceMcFileAttrSubdir:0;
  if(fault==0)fail_at=1;if(fault==1)false_success=1;if(fault==2)applied_error_at=1;
  assert(Rename(devices[d].path,&f,"old.bin")== (fault==0?-ENOSPC:-EIO));
  assert(rename_calls+set_calls==1&&!dread_calls&&!handles&&mutation_calls==(fault==1?0:1));
  assert(!access(fault==2?to:old,F_OK));remove_entry(old);remove_entry(to);operations++;
 }
 /* Each driver gets ONE native attempt. A successful no-op is unsupported,
  * including a hypothetical MMCE firmware that differs from today's SdFat. */
 const char *native_devices[]={"usb0:/","usb1:/","mass:/","mass9:/","ata0:/","ata1:/","mx4sio:/","cf:/","cf0:/","host:/","host:/NESTED/","mmce0:/","mmce1:/"};
 for(unsigned d=0;d<sizeof(native_devices)/sizeof(native_devices[0]);d++)for(int directory=0;directory<2;directory++)for(int mode=0;mode<2;mode++){
  reset();case_insensitive=1;case_support=mode?2:1;host_use_Bsl=1;
  char mapped[MAX_PATH];assert(!genFixPath(native_devices[d],mapped));physical(mapped,parent);
  snprintf(old,sizeof(old),"%sOLD.BIN",parent);snprintf(to,sizeof(to),"%sold.bin",parent);make_entry(old,directory);
  FILEINFO f={0};strcpy(f.name,"OLD.BIN");f.stats.AttrFile=directory?sceMcFileAttrSubdir:0;FILEINFO unchanged=f;
  struct stat before,after;assert(!stat(old,&before));
  assert(Rename(native_devices[d],&f,"old.bin")== (mode?-EOPNOTSUPP:0)&&mutation_calls==(mode?0:1)&&native_case_calls==1&&!handles);
  const char *surviving=mode?old:to;
  assert(!memcmp(&f,&unchanged,sizeof(f))&&!stat(surviving,&after)&&after.st_ino==before.st_ino&&after.st_mode==before.st_mode&&after.st_mtime==before.st_mtime);
  assert(access(mode?to:old,F_OK)<0);
  if(directory){char child[MAX_PATH];snprintf(child,sizeof(child),"%s/CHILD.BIN",surviving);assert(!access(child,F_OK));}
  else{char bytes[17]={0};int fd=open(surviving,O_RDONLY);assert(read(fd,bytes,sizeof(bytes))==16&&!memcmp(bytes,"exact file bytes",16));close(fd);}
  remove_entry(surviving);operations++;
 }
 const int native_errors[]={EACCES,EIO,ENOSPC,ETIMEDOUT,ENOSYS,EOPNOTSUPP,EEXIST,8,1};
 for(unsigned d=0;d<sizeof(native_devices)/sizeof(native_devices[0]);d++)for(int directory=0;directory<2;directory++)for(unsigned e=0;e<sizeof(native_errors)/sizeof(native_errors[0]);e++){
  reset();case_insensitive=1;case_error=native_errors[e];
  char mapped[MAX_PATH];assert(!genFixPath(native_devices[d],mapped));physical(mapped,parent);
  snprintf(old,sizeof(old),"%sOLD.BIN",parent);make_entry(old,directory);
  FILEINFO f={0};strcpy(f.name,"OLD.BIN");f.stats.AttrFile=directory?sceMcFileAttrSubdir:0;
  assert(Rename(native_devices[d],&f,"old.bin")==-native_errors[e]&&native_case_calls==1&&!mutation_calls&&!handles&&!access(old,F_OK));
  remove_entry(old);operations++;
 }
 for(unsigned d=0;d<sizeof(native_devices)/sizeof(native_devices[0]);d++)for(int directory=0;directory<2;directory++){
  reset();case_insensitive=1;case_support=1;case_applied_error=EIO;
  char mapped[MAX_PATH];assert(!genFixPath(native_devices[d],mapped));physical(mapped,parent);
  snprintf(old,sizeof(old),"%sOLD.BIN",parent);snprintf(to,sizeof(to),"%sold.bin",parent);make_entry(old,directory);
  FILEINFO f={0};strcpy(f.name,"OLD.BIN");f.stats.AttrFile=directory?sceMcFileAttrSubdir:0;
  assert(Rename(native_devices[d],&f,"old.bin")==-EIO&&native_case_calls==1&&mutation_calls==1&&!handles&&!access(to,F_OK)&&access(old,F_OK)<0);
  remove_entry(to);operations++;
 }
 for(unsigned e=0;e<sizeof(native_errors)/sizeof(native_errors[0]);e++){
  reset();case_insensitive=1;case_stat_error=native_errors[e];FILEINFO f={0};strcpy(f.name,"OLD.BIN");put("usb0:/OLD.BIN");
  assert(Rename("usb0:/",&f,"old.bin")==-native_errors[e]&&!mutation_calls&&!native_case_calls&&!dread_calls&&!handles&&!access("usb0:/OLD.BIN",F_OK));
  remove_entry("usb0:/OLD.BIN");operations++;
 }
 for(int phase=1;phase<=2;phase++)for(int fault=0;fault<4;fault++){
  reset();case_insensitive=case_support=1;FILEINFO f={0};strcpy(f.name,"OLD.BIN");put("usb0:/OLD.BIN");
  if(fault==0)scan_open_fail_at=phase;if(fault==1)scan_fail_at=phase;
  if(fault==2)scan_close_fail_at=phase;if(fault==3)scan_bad_name_at=phase;
  assert(Rename("usb0:/",&f,"old.bin")<0&&!handles&&mutation_calls==phase-1&&native_case_calls==phase-1);
  const char *surviving=phase==1?"usb0:/OLD.BIN":"usb0:/old.bin";assert(!access(surviving,F_OK));remove_entry(surviving);operations++;
 }
 /* Native support and rejection at maximum name/path lengths. */
 const struct {const char *path;int length,type,windows,insensitive;} case_limits[]={
  {"usb0:/",255,2,0,1},{"mc0:/",20,1,0,0},{"mc1:/",31,2,0,0},
  {"vmc0:/",20,1,0,0},{"vmc1:/",31,2,0,0},{"xfrom0:/",31,2,0,0},
  {"host:/",255,2,0,1},{"host:/",255,2,1,1},{"host:/NESTED/",248,2,0,1},{"host:/NESTED/",248,2,1,1}
 };
 for(unsigned i=0;i<sizeof(case_limits)/sizeof(case_limits[0]);i++)for(int directory=0;directory<2;directory++)for(int support=0;support<(case_limits[i].insensitive?2:1);support++){
  reset();case_insensitive=case_limits[i].insensitive;case_support=support;card_type=case_limits[i].type;host_use_Bsl=case_limits[i].windows;
  char source[256],target[256];memset(source,'A',case_limits[i].length);source[case_limits[i].length]=0;
  memset(target,'a',case_limits[i].length);target[case_limits[i].length]=0;
  snprintf(old,sizeof(old),"%s%s",case_limits[i].path,source);snprintf(to,sizeof(to),"%s%s",case_limits[i].path,target);
  make_entry(old,directory);FILEINFO f={0};strcpy(f.name,source);f.stats.AttrFile=directory?sceMcFileAttrSubdir:0;
  int rejected=case_limits[i].insensitive&&!support;
  assert(Rename(case_limits[i].path,&f,target)== (rejected?-EEXIST:0)&&mutation_calls==(rejected?0:1)&&!handles);
  assert(!access(rejected?old:to,F_OK)&&access(rejected?to:old,F_OK)<0);
  remove_entry(old);remove_entry(to);operations++;
 }
 for(int port=0;port<2;port++)for(int directory=0;directory<2;directory++)for(int support=0;support<2;support++){
  reset();case_insensitive=1;case_support=support;if(!support)case_error=1;char folder[247];memset(folder,'D',246);folder[246]=0;
  snprintf(path,sizeof(path),"mmce%d:/%s/",port,folder);mkdir(path,0700);
  snprintf(old,sizeof(old),"%sAA.BIN",path);snprintf(to,sizeof(to),"%saa.bin",path);
  make_entry(old,directory);FILEINFO f={0};strcpy(f.name,"AA.BIN");f.stats.AttrFile=directory?sceMcFileAttrSubdir:0;
  assert(Rename(path,&f,"aa.bin")== (support?0:-1)&&mutation_calls==support&&native_case_calls==1&&!handles);
  assert(!access(support?to:old,F_OK)&&access(support?old:to,F_OK)<0);
  remove_entry(old);remove_entry(to);operations++;
 }
 /* Directory errors before/after one native MMCE attempt stop without retry. */
 for(int phase=1;phase<=2;phase++)for(int fault=0;fault<4;fault++){
  reset();case_insensitive=case_support=1;FILEINFO f={0};strcpy(f.name,"OLD.BIN");put("mmce0:/OLD.BIN");
  if(fault==0)scan_open_fail_at=phase;if(fault==1)scan_fail_at=phase;
  if(fault==2)scan_close_fail_at=phase;if(fault==3)scan_bad_name_at=phase;
  assert(Rename("mmce0:/",&f,"old.bin")<0&&!handles&&mutation_calls==phase-1);
  const char *surviving=phase==1?"mmce0:/OLD.BIN":"mmce0:/old.bin";
  assert(!access(surviving,F_OK));remove_entry(surviving);operations++;
 }
 /* A stale spelling must not identify a case-insensitive source. */
 reset();case_insensitive=1;FILEINFO stale_file={0};strcpy(stale_file.name,"OLD.BIN");put("usb0:/Old.Bin");
 assert(Rename("usb0:/",&stale_file,"old.bin")==-EEXIST&&!mutation_calls&&!handles);
 assert(!access("usb0:/Old.Bin",F_OK));remove_entry("usb0:/Old.Bin");operations++;
 /* Old workaround candidate names are ordinary unrelated entries now. */
 for(int length=1;length<=11;length++)for(int support=0;support<2;support++){
  reset();case_insensitive=1;case_support=support;char source[12],target[12],temp[12];memset(source,'A',length);source[length]=0;memset(target,'a',length);target[length]=0;
  if(length>=11)strcpy(temp,"lsrn000.tmp");else if(length>=4)strcpy(temp,"r000");else snprintf(temp,sizeof(temp),"%0*d",length,0);
  snprintf(old,sizeof(old),"usb0:/%s",source);snprintf(to,sizeof(to),"usb0:/%s",target);char occupied[MAX_PATH];snprintf(occupied,sizeof(occupied),"usb0:/%s",temp);
  put(old);put(occupied);FILEINFO f={0};strcpy(f.name,source);
  assert(Rename("usb0:/",&f,target)== (support?0:-EEXIST)&&mutation_calls==support&&native_case_calls==1&&!handles&&!access(occupied,F_OK));
  assert(!access(support?to:old,F_OK)&&access(support?old:to,F_OK)<0);
  remove_entry(occupied);remove_entry(old);remove_entry(to);operations++;
 }
 reset();case_insensitive=case_support=1;FILEINFO short_file={0};strcpy(short_file.name,"A");put("usb0:/A");
 for(int i=0;i<10;i++){snprintf(to,sizeof(to),"usb0:/%d",i);put(to);}
 assert(!Rename("usb0:/",&short_file,"a")&&mutation_calls==1&&native_case_calls==1&&!handles&&!access("usb0:/a",F_OK));
 for(int i=0;i<10;i++){snprintf(to,sizeof(to),"usb0:/%d",i);assert(!access(to,F_OK));remove_entry(to);}remove_entry("usb0:/a");operations++;
 /* Card name limits: never shorten the requested name or write FILEINFO. */
 const char *cards[]={"mc0:/","mc1:/","vmc0:/","vmc1:/","xfrom0:/"};
 for(unsigned d=0;d<sizeof(cards)/sizeof(cards[0]);d++)for(int type=1;type<=2;type++){
  int limit=type==1?20:31;assert(!genFixPath(cards[d],parent));snprintf(old,sizeof(old),"%sOLD.BIN",parent);
  FILEINFO file={0};strcpy(file.name,"OLD.BIN");FILEINFO unchanged=file;
  for(int length=limit;length<=limit+1;length++){
   reset();card_type=type;char name[34];memset(name,'N',length);name[length]=0;
   snprintf(to,sizeof(to),"%s%s",parent,name);remove_entry(to);make_entry(old,0);
   int r=Rename(cards[d],&file,name);assert(r==(length==limit?0:-ENAMETOOLONG));
   assert(!handles&&!memcmp(&file,&unchanged,sizeof(file)));
   assert(length==limit?(access(old,F_OK)<0&&!access(to,F_OK)):(!access(old,F_OK)&&access(to,F_OK)<0));
   remove_entry(old);remove_entry(to);operations++;
  }
 }
 const char *rpcs[]={"mc0:/","mc1:/","xfrom0:/"};
 for(unsigned d=0;d<sizeof(rpcs)/sizeof(rpcs[0]);d++)for(int fault=0;fault<3;fault++){
  reset();FILEINFO file={0};strcpy(file.name,"OLD.BIN");snprintf(old,sizeof(old),"%sOLD.BIN",rpcs[d]);make_entry(old,0);
  if(fault==0)submit_error=1;if(fault==1)sync_error=1;if(fault==2)info_error=-2;
  assert(Rename(rpcs[d],&file,"NEW.BIN")<0&&!handles&&!access(old,F_OK));remove_entry(old);operations++;
 }
 /* Invalid names/paths, protected folders, failed initialization and CF removal. */
 FILEINFO file={0};strcpy(file.name,"OLD.BIN");
 const char *invalid[]={"",".","..","A/B","A\\B","ata0:NEW","A\nB"};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){reset();assert(Rename("usb0:/",&file,invalid[i])==-EINVAL&&!loaded&&!handles);}
 reset();assert(Rename("cdfs:/",&file,"NEW.BIN")==-EROFS&&!loaded);
 reset();assert(Rename("udpfs:/",&file,"NEW.BIN")==-ENOSYS&&!loaded);
 assert(!genCanRenamePath("cdfs:/")&&!genCanRenamePath("udpfs:/")&&genCanRenamePath("host:/"));
 reset();protected_path=1;assert(Rename("mc0:/",&file,"NEW.BIN")==-EPERM&&!loaded);
 reset();load_ok=0;assert(Rename("usb0:/",&file,"NEW.BIN")==-ENODEV);
 reset();cf_online=0;assert(Rename("cf:/",&file,"NEW.BIN")==-ENODEV&&!rename_calls);
 reset();assert(genRenameNoReplace("usb0:/OLD","usb1:/NEW")==-EXDEV);
 reset();assert(genRenameNoReplace("usb0:/OLD","usb0:/SUB/NEW")==-EXDEV);
 char huge[MAX_PATH+1];memset(huge,'A',sizeof(huge)-1);huge[sizeof(huge)-1]=0;
 reset();assert(Rename(huge,&file,"NEW.BIN")==-ENAMETOOLONG&&!loaded);
 huge[MAX_NAME]=0;assert(Rename("usb0:/",&file,huge)==-ENAMETOOLONG&&!loaded);
 /* FAT filename maximum and host's complete 256-byte wire path limit. */
 reset();char name[256];memset(name,'L',255);name[255]=0;put("usb0:/OLD.BIN");snprintf(to,sizeof(to),"usb0:/%s",name);
 assert(!Rename("usb0:/",&file,name)&&!access(to,F_OK));remove_entry(to);
 reset();put("host:/OLD.BIN");snprintf(to,sizeof(to),"host:/%s",name);
 assert(!Rename("host:/",&file,name)&&!access(to,F_OK)&&!handles);remove_entry(to);
 reset();put("host:/NESTED/OLD.BIN");assert(Rename("host:/NESTED/",&file,name)==-ENAMETOOLONG&&!ioctl_calls&&!handles);remove_entry("host:/NESTED/OLD.BIN");
 for(int windows=0;windows<2;windows++)for(int directory=0;directory<2;directory++){
  reset();host_use_Bsl=windows;file.stats.AttrFile=directory?sceMcFileAttrSubdir:0;
  make_entry("host:/NESTED/OLD.BIN",directory);remove_entry("host:/NESTED/NEW.BIN");
  assert(!Rename("host:/NESTED/",&file,"NEW.BIN")&&ioctl_calls==1&&!handles);
  assert(!access("host:/NESTED/NEW.BIN",F_OK));remove_entry("host:/NESTED/NEW.BIN");operations++;
 }
 file.stats.AttrFile=0;
 const int completions[]={-3,-4,-5,-6,-7,-8};const int errors[]={ENOSPC,ENOENT,EACCES,EEXIST,EMFILE,EIO};
 for(unsigned c=0;c<sizeof(completions)/sizeof(completions[0]);c++){
  reset();completion_error=completions[c];put("mc0:/OLD.BIN");
  assert(Rename("mc0:/",&file,"NEW.BIN")==-errors[c]&&set_calls==1&&!handles&&!access("mc0:/OLD.BIN",F_OK));
  remove_entry("mc0:/OLD.BIN");operations++;
 }
 reset();info_error=-1;put("mc0:/OLD.BIN");assert(!Rename("mc0:/",&file,"NEW.BIN"));remove_entry("mc0:/NEW.BIN");operations++;
 reset();submit_error=2;put("mc0:/OLD.BIN");assert(Rename("mc0:/",&file,"NEW.BIN")==-EIO&&!handles&&!access("mc0:/OLD.BIN",F_OK));remove_entry("mc0:/OLD.BIN");operations++;
 reset();sync_error=2;put("mc0:/OLD.BIN");assert(Rename("mc0:/",&file,"NEW.BIN")==-EIO&&!handles&&!access("mc0:/NEW.BIN",F_OK));remove_entry("mc0:/NEW.BIN");operations++;
 reset();close_error=1;put("host:/OLD.BIN");assert(Rename("host:/",&file,"NEW.BIN")==-EIO&&!handles&&!access("host:/NEW.BIN",F_OK));remove_entry("host:/NEW.BIN");operations++;
 /* MMCE's one-byte wire count includes NUL: 254 relative path bytes fit,
  * 255 must fail before a rename is sent, including long parent paths. */
 for(int port=0;port<2;port++)for(int too_long=0;too_long<2;too_long++){
  reset();char folder[247];memset(folder,'D',246);folder[246]=0;
  snprintf(path,sizeof(path),"mmce%d:/%s/",port,folder);mkdir(path,0700);
  strcpy(file.name,"O.BIN");snprintf(old,sizeof(old),"%sO.BIN",path);put(old);
  const char *leaf=too_long?"NNN.BIN":"NN.BIN";snprintf(to,sizeof(to),"%s%s",path,leaf);remove_entry(to);
  assert(Rename(path,&file,leaf)==(too_long?-ENAMETOOLONG:0)&&!handles);
  assert(too_long?(!rename_calls&&!access(old,F_OK)&&access(to,F_OK)<0):(!access(to,F_OK)&&access(old,F_OK)<0));
  remove_entry(old);remove_entry(to);operations++;
 }
 printf("PASS: %d host rename cases across 24 device/alias paths: native-only rename, unsupported case changes, case-sensitive coexistence, metadata, conflicts, driver/directory/RPC failures and name/path limits\n",operations);
}
'''

# The classic browser hides rename where the driver cannot rename. Name
# entry keeps this tree's 36-character keyboards; longer names are refused
# by the driver checks above rather than truncated.
browser = (root / 'src/filer_browser.c').read_text()
assert 'if (!genCanRenamePath(path))\n\t\tenable[RENAME] = FALSE;' in browser
with tempfile.TemporaryDirectory(prefix='rename-devices-') as folder:
    folder = Path(folder)
    (folder / 'test.c').write_text(code)
    cc = shutil.which('gcc') or shutil.which('cc')
    exe = folder / 'test'
    subprocess.run([cc, '-std=gnu99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-Wno-unused-variable', '-Wno-misleading-indentation',
                    '-Wno-format-truncation',
                    '-Werror=implicit-function-declaration', str(folder / 'test.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], cwd=folder, check=True, timeout=20)
