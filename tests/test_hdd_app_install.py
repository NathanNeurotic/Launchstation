"""Production app transaction against a local ATA/PFS facade, with crash injection."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_hdd_app.c").read_text()


def function(signature):
    start = source.index(signature)
    depth = 0
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', source[start:], re.S):
        depth += (token.group() == "{") - (token.group() == "}")
        if not depth and token.group() == "}":
            return source[start:start + token.end()] + "\n"
    raise AssertionError(signature)


code = r'''
#define _DEFAULT_SOURCE
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>
#include "hdd_app.h"
#include "hdd_install.h"
#include "hdd_header_injector.h"
#include "cloud_http.h"
#define MAX_PATH 1025
#define MAX_RECURSE 16
#define APP_MAX_ASSET (512*1024)
#define APP_TEMP "ata0:/LAUNCHSTATION_APP_TEMP"
#define MOUNT_LIMIT 4
#define FIO_O_RDONLY O_RDONLY
#define FIO_O_WRONLY O_WRONLY
#define FIO_O_CREAT O_CREAT
#define FIO_O_TRUNC O_TRUNC
#define FIO_MT_RDWR 0
#define FIO_S_ISDIR S_ISDIR
#define FIO_S_ISREG S_ISREG
#define HDIOC_FLUSH 1
typedef int64_t s64;
typedef struct {unsigned mode,attr,size,private_5,private_0;} iox_stat_t;
typedef struct {iox_stat_t stat;char name[256];} iox_dirent_t;
static int vmcMounted[2];static char mountedParty[MOUNT_LIMIT][256];
static int fault_at,operation;static jmp_buf crashed;static DIR *directories[32];
static unsigned create_count,format_count,header_count,allocation_checks;
static unsigned downloads,metadata_requests;static int asset_case;
static void crashpoint(void) { if(++operation==fault_at)longjmp(crashed,1); }
static int result(int value){return value<0?-errno:value;}
static int genOpen(const char*p,int mode){return result(open(p,mode,0600));}
static s64 genLseek(int fd,s64 offset,int whence){return lseek(fd,offset,whence);}
static int genRead(int fd,void*p,int n){return result(read(fd,p,n>173?173:n));}
static int genWrite(int fd,void*p,int n){int r=result(write(fd,p,n>97?97:n));crashpoint();return r;}
static int genClose(int fd){return result(close(fd));}
static int genMkdir(const char*p,int mode){int r=result(mkdir(p,mode));crashpoint();return r;}
static int genRemove(char*p){return result(unlink(p));}
static int genRmdir(char*p){return result(rmdir(p));}
static int genGetStat(const char*p,iox_stat_t*s){struct stat st;if(stat(p,&st)<0)return -errno;memset(s,0,sizeof(*s));s->mode=st.st_mode;s->size=st.st_size;return 0;}
int hddInstallRecoveryFault(void){return 0;}
void hddInstallRecoveryObserve(int ret){(void)ret;}
static int isFtpServerRunning(void){return 0;}
static int isSmbServerRunning(void){return 0;}
static void unmountParty(int i){assert(i>=0&&i<4);mountedParty[i][0]=0;}
static void dashWorkerStatus(const char*t,int d,int n){assert(t&&d>=0&&n>=0);}
static int fileXioDevctl(const char*p,int cmd,void*a,int b,void*c,int d){(void)a;(void)b;(void)c;(void)d;assert(!strcmp(p,"hdd0:")&&cmd==HDIOC_FLUSH);crashpoint();return 0;}
static int fileXioDopen(const char*p){for(int i=0;i<32;i++)if(!directories[i]){directories[i]=opendir(p);return directories[i]?i:-errno;}return -EMFILE;}
static int fileXioDread(int fd,iox_dirent_t*e){struct dirent*d=readdir(directories[fd]);if(!d)return 0;strcpy(e->name,d->d_name);struct stat s;if(fstatat(dirfd(directories[fd]),d->d_name,&s,0)<0)return -errno;memset(&e->stat,0,sizeof(e->stat));e->stat.mode=s.st_mode;return 1;}
static int fileXioDclose(int fd){int r=closedir(directories[fd]);directories[fd]=NULL;return result(r);}
static void partPath(char out[256],const char*path){assert(!strncmp(path,"hdd0:",5));snprintf(out,256,"part/%s",path+5);}
static int fileXioGetStat(const char*p,iox_stat_t*s){char path[256];partPath(path,p);int r=genGetStat(path,s);if(r<0)return r;s->mode=0x100;s->attr=0;s->size=8*2048;s->private_5=2048*2048;return 0;}
static int fileXioMount(const char*mount,const char*part,int mode){char path[256];(void)mode;assert(!strcmp(mount,"pfs3:"));partPath(path,part);if(access(path,F_OK))return -ENOENT;assert(!symlink(path,"pfs3:"));return 0;}
static int fileXioUmount(const char*p){assert(!strcmp(p,"pfs3:"));return result(unlink(p));}
static int fileXioRename(const char*from,const char*to){char a[256],b[256];if(!strncmp(from,"hdd0:",5)){partPath(a,from);partPath(b,to);from=a;to=b;}int r=result(rename(from,to));crashpoint();return r;}
static uint64_t GetTimerSystemTime(void){return 0x12345678;}
static int hddCreatePfsExtent(unsigned mb,const char*name){char path[256];assert(mb==8&&!strncmp(name,"+LSAPP.",7));snprintf(path,sizeof(path),"part/%s",name);assert(!mkdir(path,0700));create_count++;crashpoint();return 0;}
static int hddFormatPfs(const char *p){
    char path[256], marker[300], res[300];partPath(path,p);
    snprintf(marker,sizeof(marker),"%s/res/LSAPP.ID",path);unlink(marker);
    snprintf(res,sizeof(res),"%s/res",path);rmdir(res);
    format_count++;crashpoint();return 0;
}
int hddCheckAllocation(uint32_t sectors){assert(sectors==8*2048);allocation_checks++;return 0;}
int InjectHddAppAssets(const char*part,const char*cnf,const char*settings,const void*list,unsigned ls,const void*del,unsigned ds){assert(part&&strstr(cnf,"ata0:/APPS/APP_TEST/BOOT.ELF")&&!strncmp(settings,"PS2X\n",5)&&list&&ls&&del&&ds);header_count++;crashpoint();return 0;}
static const char commit[]="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
int hddInstallCommit(const char*json,char out[41]) {assert(!strcmp(json,"fixture"));strcpy(out,commit);return 1;}
int cloudHttpRequest(const char*method,const char*host,const char*path,const char*headers,const void*body,int len,const char*ca,CloudHttpResponse*r,char*error,int cap){
    (void)ca;(void)error;(void)cap;assert(!strcmp(method,"GET")&&!strcmp(host,"api.github.com")&&strstr(path,"/git/ref/heads/main")&&headers&&!body&&!len);
    metadata_requests++;r->status=200;r->body=strdup("fixture");r->body_len=7;return 0;
}
void cloudHttpResponseFree(CloudHttpResponse*r){free(r->body);memset(r,0,sizeof(*r));}
int cloudHttpDownloadAlignedFd(const char*url,int fd,const char*ca,int max,int*size,unsigned char digest[32],void (*progress)(int,int,void *),void*ctx,char*error,int cap){
    (void)ca;(void)error;(void)cap;downloads++;assert(strstr(url,commit)&&max==APP_MAX_ASSET&&!digest&&progress);
    const char *data;unsigned n;
    if(strstr(url,"Game-Installer.sh")){
        data=asset_case==4?"create_icon_sys() {\ncat <<EOL\nPS2X\ntitle0=$(evil)\nEOL\n}\n":
          "create_icon_sys() {\ncat > \"$icon_sys_filename\" <<EOL\nPS2X\ntitle0=$title\ntitle1=$publisher\nEOL\n}\n";n=strlen(data);
    }else if(strstr(url,"/icons/art/")){
        if(asset_case==1&&strstr(url,"APP_TEST"))return -ENOENT;
        data=asset_case==5?"not a PNG at all...........":"\211PNG\r\n\032\n................";n=strlen(data);
    }else {
        assert(strstr(url,"_LST.ico")||strstr(url,"_DEL.ico"));
        if(asset_case==2)return -EIO; /* network/TLS must not fall back */
        if(asset_case==3)return -EPERM; /* non-404 HTTP failure */
        if(asset_case==1&&strstr(url,"APP_TEST"))return -ENOENT;
        data="HDD icon model";n=strlen(data);
    }
    assert(write(fd,data,n)==n);*size=n;progress(n,n,ctx);return 0;
}
'''
# AppInstall is the actual frontend/backend context.
code += source[source.index("typedef struct {"):source.index("static int join(")]
for signature in ("static int join(", "static int flush(", "static int finish(", "static int transfer(",
                  "static int writeFile(", "static int readFile(", "static int verifyPackage(", "static int cachePackage(",
                  "static int exists(", "static int mkdirChecked(",
                  "static int releasePfs(", "static int identity(", "static int checkOwner(", "static int writeOwner(",
                  "static void pendingName(", "static int readJournal(", "static int checkpoint(", "static int mountPartition(",
                  "static int unmountPartition(", "static int partitionOwner(",
                  "static int existingPartition(", "static int validateTarget(", "static int stageRestartable(",
                  "static int apiCommit(", "static void progress(", "static int download(", "static int assets(", "static int copyFile(",
                  "static int copyTree(", "static int removeTree(", "static int renameAbsent(",
                  "static int pendingPartition(", "static int installHeader(", "static int stageAppFiles(", "static void apply("):
    code += function(signature)
code += r'''
static const unsigned char payload[52]={0x7f,'E','L','F',1,1,1,[16]=2,[18]=8};
static void rawFile(const char*path,const void*p,unsigned n){int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0600);assert(fd>=0&&write(fd,p,n)==n);close(fd);}
static void setup(AppInstall *in,int update){
    memset(in,0,sizeof(*in));assert(!mkdir("ata0:",0700));assert(!mkdir("ata0:/APPS",0700));assert(!mkdir("ata0:/stage",0700));assert(!mkdir("part",0700));
    strcpy(in->app.folder,"APP_TEST");strcpy(in->app.partition,"PP.APP_TEST");strcpy(in->app.title,"Test app");strcpy(in->app.boot,"BOOT.ELF");strcpy(in->app.asset,"APP_TEST");
    strcpy(in->stage,"ata0:/stage");strcpy(in->target,"ata0:/APPS/APP_TEST");strcpy(in->pending,"+LSAPP.12345678");strcpy(in->settings,"PS2X\ntitle0=Test app\n");
    static SaveArchiveEntry entry;memset(&entry,0,sizeof(entry));strcpy(entry.name,"BOOT.ELF");entry.data=payload;entry.size=sizeof(payload);entry.mode=0x8497;
    in->save.files=&entry;in->save.count=1;in->app.elf[0]=0;in->app.elf_count=1;
    in->package_size=4096;in->crc=0x12345678;in->journal.package_crc=in->crc;in->journal.package_size=in->package_size;in->journal.update=!!update;strcpy(in->journal.boot,"BOOT.ELF");
    rawFile("ata0:/stage/JACKET.PNG","PNG",3);rawFile("ata0:/stage/LIST.ICO","LIST",4);rawFile("ata0:/stage/DELETE.ICO","DELETE",6);
    if(update){
        assert(!mkdir(in->target,0700));unsigned current=in->crc;in->crc=0x87654321;assert(!writeOwner(in,in->target,1));in->crc=current;
        rawFile("ata0:/APPS/APP_TEST/BOOT.ELF","old app",7);rawFile("ata0:/APPS/APP_TEST/USER.CFG","keep",4);
        assert(!mkdir("ata0:/APPS/APP_TEST/config",0700));rawFile("ata0:/APPS/APP_TEST/config/extra.cfg","keep2",5);
        assert(!mkdir("part/PP.APP_TEST",0700));assert(!mkdir("part/PP.APP_TEST/res",0700));assert(!writeOwner(in,"part/PP.APP_TEST/res",0));
        if(update==7){assert(!rename("part/PP.APP_TEST","part/PP.APP_TEST.HIDDEN"));assert(!validateTarget(in)&&!strcmp(in->installed_partition,"PP.APP_TEST.HIDDEN"));}
    }
    assert(!checkpoint(in,HA_STAGED));operation=0;create_count=format_count=header_count=0;
}
static void verify(AppInstall*in,int update){
    assert(in->result==0&&in->journal.phase==HA_DONE);
    assert(!checkOwner(in,in->target,1));unsigned char *data=NULL;unsigned n=0;
    assert(!readFile("ata0:/APPS/APP_TEST/BOOT.ELF",&data,&n,100)&&n==sizeof(payload)&&!memcmp(data,payload,n));free(data);
    if(update){assert(access("ata0:/APPS/APP_TEST/USER.CFG",F_OK)==0&&access("ata0:/APPS/APP_TEST/config/extra.cfg",F_OK)==0);assert(!create_count&&!format_count);}
    else assert(create_count==1&&format_count>=1);
    assert(!access(update==7?"part/PP.APP_TEST.HIDDEN/res/LSAPP.ID":"part/PP.APP_TEST/res/LSAPP.ID",F_OK)&&access("part/+LSAPP.12345678",F_OK)<0);
    assert(header_count>=1);
}
int main(int argc,char**argv){
    assert(argc==3);int update=atoi(argv[1]),crash=atoi(argv[2]);

    if(update==8){
        AppInstall *in=calloc(1,sizeof(*in));setup(in,0);
        rawFile("ata0:/stage/PACKAGE.NEW.PSU","partial",7);
        assert(!stageRestartable(in->stage));
        assert(!mkdir("ata0:/stage/NEW",0700));
        assert(stageRestartable(in->stage)==-EEXIST);
        assert(!rmdir("ata0:/stage/NEW"));
        rawFile("ata0:/stage/UNRELATED","keep",4);
        assert(stageRestartable(in->stage)==-EEXIST);
        assert(!access("ata0:/stage/UNRELATED",F_OK));
        free(in);return 0;
    }
    if(update==4){
        AppInstall*in=calloc(1,sizeof(*in));setup(in,0);
        static unsigned char package[4096];memset(package,0x55,sizeof(package));in->package=package;
        assert(!cachePackage(in)&&!verifyPackage(in,"ata0:/stage/PACKAGE.PSU"));
        operation=0;
        assert(!cachePackage(in)&&operation==0);
        rawFile("ata0:/stage/PACKAGE.PSU","old",3);
        fault_at=1;
        if(!setjmp(crashed)){cachePackage(in);assert(0);}
        fault_at=0;
        unsigned char *old=NULL;unsigned length=0;
        assert(!readFile("ata0:/stage/PACKAGE.PSU",&old,&length,4096)&&length==3&&!memcmp(old,"old",3));free(old);
        assert(!cachePackage(in)&&!verifyPackage(in,"ata0:/stage/PACKAGE.PSU"));
        free(in);return 0;
    }
    if(update==5){
        AppInstall*in=calloc(1,sizeof(*in));setup(in,1);
        assert(!stageAppFiles(in,"ata0:/stage/NEW")&&!checkpoint(in,HA_FILES));
        rawFile("ata0:/APPS/APP_TEST/USER.CFG","new config",10);
        assert(!unlink("ata0:/APPS/APP_TEST/config/extra.cfg"));
        in->resumed=1;apply(in);
        assert(in->result==0&&in->journal.phase==HA_DONE);
        unsigned char *data=NULL;unsigned size=0;
        assert(!readFile("ata0:/APPS/APP_TEST/USER.CFG",&data,&size,20)&&size==10&&!memcmp(data,"new config",10));free(data);
        assert(access("ata0:/APPS/APP_TEST/config/extra.cfg",F_OK)<0);
        free(in);return 0;
    }
    if(update==6){
        AppInstall*in=calloc(1,sizeof(*in));setup(in,0);
        assert(!mkdir("part/+LSAPP.12345678",0700)&&!mkdir("part/+LSAPP.12345678/res",0700));
        in->crc++;assert(!writeOwner(in,"part/+LSAPP.12345678/res",1));in->crc--;
        assert(pendingPartition(in)==-EEXIST);
        in->crc++;assert(!checkOwner(in,"part/+LSAPP.12345678/res",1));
        free(in);return 0;
    }
    if(update==3){
        AppInstall*in=calloc(1,sizeof(*in));setup(in,0);
        assert(!validateTarget(in)&&allocation_checks==1);
        assert(!mkdir(in->target,0700));assert(validateTarget(in)==-EEXIST);assert(!rmdir(in->target));
        assert(!mkdir("part/PP.APP_TEST",0700));assert(validateTarget(in)==-ENOENT); /* unowned PP */
        assert(!mkdir("part/PP.APP_TEST/res",0700));assert(!writeOwner(in,"part/PP.APP_TEST/res",0));
        assert(validateTarget(in)==-ENOENT); /* missing ATA app */
        assert(!mkdir(in->target,0700));assert(!writeOwner(in,in->target,1));assert(!validateTarget(in)&&in->journal.update);
        rawFile("ata0:/APPS/APP_TEST/LSAPP.ID","unrelated",9);assert(validateTarget(in)==-EEXIST);
        vmcMounted[0]=1;assert(releasePfs()==-EBUSY);vmcMounted[0]=0;assert(!releasePfs());
        free(in);return 0;
    }
    if(update==2){
        AppInstall*in=calloc(1,sizeof(*in));setup(in,0);asset_case=crash;
        int ret=assets(in);
        if(asset_case==2||asset_case==3)assert(ret<0&&downloads==2&&metadata_requests==1);
        else if(asset_case==4)assert(ret<0&&downloads==1);
        else if(asset_case==5)assert(ret<0&&downloads==4);
        else {assert(ret==0&&!strncmp(in->settings,"PS2X\n",5));assert(downloads==(asset_case==1?9u:4u));assert(metadata_requests==(asset_case==1?2u:1u));}
        assert(!create_count&&!format_count&&!header_count);free(in);return 0;
    }
    AppInstall *in=calloc(1,sizeof(*in));setup(in,update);
    fault_at=crash;
    if(!setjmp(crashed))apply(in);
    else {
        fault_at=0;
        /* Simulated restart loses EE journal state and all mounted handles. */
        for(int i=0;i<32;i++)if(directories[i]){closedir(directories[i]);directories[i]=NULL;}
        for(int fd=3;fd<256;fd++)close(fd);
        unlink("pfs3:");in->mounted=0;memset(&in->journal,0,sizeof(in->journal));
        strcpy(in->pending,"+LSAPP.12345678");
        assert(!readJournal(in));in->resumed=1;apply(in);
    }
    verify(in,update);printf("PASS operations=%d crash=%d update=%d\n",operation,crash,update);free(in);
}
'''
with tempfile.TemporaryDirectory(prefix="hdd-app-install-") as tmp:
    tmp = Path(tmp)
    (tmp / "test.c").write_text(code)
    subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(root / "include"),
                    str(root / "src/hdd_app.c"), str(root / "src/save_archive.c"), str(root / "src/save_lzari.c"),
                    str(root / "src/save_sign.c"), str(tmp / "test.c"), "-lz", "-o", str(tmp / "test")], check=True)
    for case in range(6):
        with tempfile.TemporaryDirectory(dir=tmp) as work:
            subprocess.run([str(tmp / "test"), "2", str(case)], cwd=work, check=True)
    for case in (3, 4, 5, 6, 8):
        with tempfile.TemporaryDirectory(dir=tmp) as work:
            subprocess.run([str(tmp / "test"), str(case), "0"], cwd=work, check=True)
    for update in (0, 1, 7):
        with tempfile.TemporaryDirectory(dir=tmp) as work:
            output = subprocess.check_output([str(tmp / "test"), str(update), "0"], cwd=work, text=True)
        operations = int(re.search(r"operations=(\d+)", output)[1])
        for crash in range(1, operations + 1):
            with tempfile.TemporaryDirectory(dir=tmp) as work:
                subprocess.run([str(tmp / "test"), str(update), str(crash)], cwd=work, check=True, stdout=subprocess.DEVNULL)
    print("PASS: app assets, cache verification, protected staging, pending ownership, hidden launcher updates and interruption replay")
