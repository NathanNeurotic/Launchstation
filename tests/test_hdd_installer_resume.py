"""Durable installer journals: interrupted writes, corruption, ownership and layout."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
code = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "hdd_install_resume.h"
#define MAX_PATH 1025
#define EXFAT
#define FIO_O_RDONLY O_RDONLY
#define FIO_O_WRONLY O_WRONLY
#define FIO_O_CREAT O_CREAT
#define FIO_O_TRUNC O_TRUNC
#define FIO_MT_RDONLY 2
#define HDIOC_FLUSH 1
#define HDIOC_TOTALSECTOR 2
#define HDIOC_READSECTOR 3
typedef int64_t s64;
typedef struct { unsigned mode,attr,private_0,private_5,size; } iox_stat_t;
typedef struct {uint32_t lba,size;} hddAtaTransfer_t;
static const char *directory;
static int mounted,fail_after=-1,written,wrong_owner,wrong_disk,wrong_serial,flushes;
static HddFormatPlan actual;
static int genOpen(const char *path,int flags){
 char local[2048];const char *p=strrchr(path,'/');assert(p&&p[1]);
 if(!strncmp(path,"pfs2:",5))assert(mounted);
 snprintf(local,sizeof(local),"%s/%.*s-%s",directory,(int)(p-strchr(path,'/')-1),strchr(path,'/')+1,p+1);
 int fd=open(local,flags,0600);return fd<0?-errno:fd;
}
static int genRead(int fd,void *p,unsigned n){return read(fd,p,n>37?37:n);}
static int genWrite(int fd,const void*p,unsigned n){
 if(fail_after>=0&&written>=fail_after)return -EIO;
 if(n>31)n=31;written+=n;return write(fd,p,n);
}
#define genLseek lseek
#define genClose close
static int genRemove(char *path){
 char local[2048];const char *first=strchr(path,'/'),*last=strrchr(path,'/');
 snprintf(local,sizeof(local),"%s/%.*s-%s",directory,(int)(last-first-1),first+1,last+1);
 return unlink(local)<0?-errno:0;
}
static int fileXioMount(const char *m,const char*p,int flags){assert(!strcmp(m,"pfs2:")&&!strcmp(p,"hdd0:__common")&&flags==2&&!mounted);mounted=1;return 0;}
static int fileXioUmount(const char *m){assert(!strcmp(m,"pfs2:")&&mounted);mounted=0;return 0;}
static void put32(unsigned char*p,unsigned n){for(unsigned i=0;i<4;i++)p[i]=n>>(i*8);}
static int fileXioDevctl(const char *dev,int cmd,void *arg,unsigned n,void *out,unsigned size){
 assert(!strcmp(dev,"hdd0:"));
 if(cmd==HDIOC_FLUSH){assert(!arg&&!n&&!out&&!size);flushes++;return 0;}
 if(cmd==HDIOC_TOTALSECTOR)return actual.sectors+wrong_disk;
 assert(cmd==HDIOC_READSECTOR&&arg&&n==8&&out&&size==512);
 hddAtaTransfer_t *r=arg;assert(r->lba==actual.exfat_start&&r->size==1);
 unsigned char *p=out;memset(p,0,512);memcpy(p+3,"EXFAT   ",8);
 put32(p+64,actual.exfat_start);put32(p+72,actual.exfat_sectors);put32(p+100,actual.serial+wrong_serial);return 0;
}
static int fileXioGetStat(const char *path,iox_stat_t*st){(void)path;(void)st;return -ENOENT;}
int hddGetApaCapacity(uint32_t *limit){*limit=actual.kind==HDD_FORMAT_APA_JAIL?actual.apa_sectors:actual.sectors;return 0;}
int hddInstallFindStage(HddInstall *in){return in->owned&&!wrong_owner?0:-ENOENT;}
void hddInstallRelease(HddInstall *in){free(in->ca_data);free(in->launcher_files);in->ca_data=NULL;in->launcher_files=NULL;in->launcher_count=in->ca_size=0;}
void hddInstallLog(const char *s,...){assert(s);}
'''
code += (root / 'src/hdd_install_resume.c').read_text()
code += (root / 'src/hdd_install_resume_ps2.c').read_text().replace('#include "launchelf.h"', '')
code += r'''
static void original(HddInstall *in,int jail){
 memset(in,0,sizeof(*in));in->flags=3;in->jail=jail;in->owned=in->stage_ready=1;
 strcpy(in->stage,jail?"ata0:/PSBBN_DP_TEMP_1":"pfs2:/PSBBN_DP_TEMP");
 memset(in->owner,0x62,sizeof(in->owner));
 assert(!hddFormatPlan(&actual,jail?HDD_FORMAT_APA_JAIL:HDD_FORMAT_APA,1000215216u,jail?40960:0,12345));
}
int main(int argc,char **argv){
 assert(argc==2);directory=argv[1];
 for(int jail=0;jail<2;jail++){
  HddInstall in={0},loaded={0};HddFormatPlan plan={0};unsigned sequence=0;
  original(&in,jail);mounted=!jail;hddInstallResumeBegin(&in,&actual,0);
  assert(!hddInstallResumeSave(&in)&&flushes>=2); /* seq 1 */
  in.downloaded_assets=1<<HI_MBR;assert(!hddInstallResumeSave(&in)); /* seq 2 */
  in.downloaded_assets|=1<<HI_HOSD;fail_after=31;written=0;
  assert(hddInstallResumeSave(&in)==-EIO); /* torn seq 3 cannot replace seq 2 */
  fail_after=-1;hddInstallResumeEnd();mounted=0;
  assert(!hddInstallResumeLoad(&loaded,&plan,&sequence)&&sequence==2&&loaded.resumed&&!loaded.recovery_attempts);
  assert(!strcmp(in.stage,loaded.stage));
  assert(loaded.downloaded_assets==(1u<<HI_MBR)&&!memcmp(&plan,&actual,sizeof(plan))&&!loaded.ca_data);
  hddInstallRelease(&loaded);memset(&loaded,0,sizeof(loaded));
  wrong_disk=1;assert(hddInstallResumeLoad(&loaded,&plan,&sequence)==-EINVAL&&!loaded.ca_data);wrong_disk=0;
  wrong_owner=1;assert(hddInstallResumeLoad(&loaded,&plan,&sequence)==-EINVAL);wrong_owner=0;
  if(jail){wrong_serial=1;assert(hddInstallResumeLoad(&loaded,&plan,&sequence)==-EINVAL);wrong_serial=0;}
  mounted=!jail;assert(!hddInstallResumeRemove(&in));mounted=0;
  assert(hddInstallResumeLoad(&loaded,&plan,&sequence)==-ENOENT);hddInstallRelease(&loaded);
 }
 HddInstall in={0},out={0};HddFormatPlan p;unsigned seq;
 unsigned char buffer[HDD_INSTALL_RESUME_MAX],copy[HDD_INSTALL_RESUME_MAX];
 original(&in,1);in.manifest_ready=1;memset(in.commit,'a',40);
 in.launcher_count=1;in.launcher_files=calloc(1,sizeof(*in.launcher_files));assert(in.launcher_files);
 strcpy(in.launcher_files[0].path,"scripts/assets/neutrino/modules/ata_bd.irx");in.launcher_files[0].size=100;
 int size=hddInstallResumeEncode(&in,&actual,22,buffer,sizeof(buffer));assert(size>24);
 assert(!hddInstallResumeDecode(&out,&p,&seq,buffer,size)&&seq==22&&out.launcher_count==1);
 assert(!strcmp(out.launcher_files[0].path,in.launcher_files[0].path));hddInstallRelease(&out);memset(&out,0,sizeof(out));
 for(int n=0;n<size;n++)assert(hddInstallResumeDecode(&out,&p,&seq,buffer,n)<0&&!out.launcher_files);
 for(int n=0;n<size;n++){
  memcpy(copy,buffer,size);copy[n]^=1;
  assert(hddInstallResumeDecode(&out,&p,&seq,copy,size)<0&&!out.launcher_files);
 }
 const char *bad[]={"scripts/assets/neutrino/../evil","scripts/assets/neutrino//evil","scripts/assets/neutrino/evil/","../../elsewhere","scripts/assets/OPL/./bad","scripts/assets/neutrino/x:bad"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){
  strcpy(in.launcher_files[0].path,bad[i]);assert(hddInstallResumeEncode(&in,&actual,1,buffer,sizeof(buffer))<0);
 }
 hddInstallRelease(&in);
 puts("PASS: little-endian bounded journals, CRC/truncation rejection, torn-write fallback, durable close/flush checkpoints, restart reload, drive/owner/serial checks and restricted runtime paths");
}
'''
with tempfile.TemporaryDirectory(prefix='hdd-installer-resume-') as directory:
    d = Path(directory)
    (d / 'test.c').write_text(code)
    subprocess.run(['gcc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
                    '-fsanitize=undefined', '-I', str(root / 'include'), str(d / 'test.c'),
                    str(root / 'src/hdd_format.c'), str(root / 'src/hdd_format_recovery.c'),
                    str(root / 'src/hdd_format_upcase.c'), str(root / 'src/hdd_install_plan.c'),
                    str(root / 'src/hdd_install_recovery.c'), '-lz', '-o', str(d / 'test')], check=True)
    subprocess.run([str(d / 'test'), str(d)], check=True, timeout=20)

# Run the actual resume action. No format command/service exists in this
# facade, so accidentally routing resume through formatting fails to compile.
source = (root / 'src/dash_hdd.c').read_text()
a = source.index('static void actResume(void)')
depth = 0
for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', source[a:], re.S):
    depth += (token.group() == '{') - (token.group() == '}')
    if not depth and token.group() == '}':
        action = source[a:a + token.end()]
        break
ui = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hdd_install_resume.h"
#define ETH
#define EXFAT
#define DI_WARNING 1
#define DASH_TOAST_OK 2
enum { HOP_RESUME_LOOKUP=1,HOP_DOWNLOAD_INSTALLER,HOP_INSTALL_SOFTWARE };
typedef struct { const char *label,*value; } DashTableRow;
typedef struct {int op,ret,format_started;unsigned resume_sequence;HddInstall *installer;HddFormatPlan format_plan;} HddJob;
static int console_is_PSX,sel,top,ftp,cancel,lookup_error,committed,download_error,network_error;
static float top_anim;
static int confirmation,lookups,downloads,installs,network,rawloads,resets,starts,ends,finished;
static int isFtpServerRunning(void){return ftp;}
static int dashWorkerAwaitAll(const char*s){assert(s);return 1;}
static void dashSoundShutdown(void){}
static void releaseMounts(void){}
static int loadHddModules(void){return 1;}
static void loadAtaModules(void){}
static HddJob *jobNew(int op){HddJob*j=calloc(1,sizeof(*j));assert(j);j->op=op;return j;}
static void hddJobFree(HddJob*j){free(j);}
static int runJob(HddJob*j,const char *s,int cancelable){
 assert(s&&!cancelable);
 if(j->op==HOP_RESUME_LOOKUP){
  lookups++;j->ret=lookup_error;if(j->ret)return 1;
  HddInstall*in=j->installer;in->flags=7;in->jail=1;in->owned=in->stage_ready=in->resumed=1;
  in->committed=committed;strcpy(in->stage,"ata0:/PSBBN_DP_TEMP");strcpy(in->language.name,"English");
  in->music_mb=in->contents_mb=1024;j->resume_sequence=9;j->format_plan.kind=HDD_FORMAT_APA_JAIL;
 }else if(j->op==HOP_DOWNLOAD_INSTALLER){assert(confirmation&&!committed&&network);downloads++;j->ret=download_error;}
 else{assert(j->op==HOP_INSTALL_SOFTWARE&&confirmation);installs++;j->format_started=!committed;j->ret=0;}
 return 1;
}
static void dashModalMessage(int icon,const char *title,const char *body,const char *detail){assert(icon==1&&title&&body);(void)detail;}
static int dashModalConfirmTable(const char *title,const DashTableRow *rows,int n,const char *button,int danger){
 assert(!strcmp(title,"Resume HDD installation")&&!strcmp(button,"Resume")&&!danger&&n>=9&&n<=14&&lookups==1);
 assert(!strcmp(rows[n-1].label,"Formatting")&&!strcmp(rows[n-1].value,"Keep existing drive layout"));
 assert(!starts&&!network&&!downloads&&!installs);confirmation++;return !cancel;
}
static void dashSoundMaintain(void){}
static void dashBgmStart(void){}
void hddInstallRelease(HddInstall *in){assert(in);}
void dashHddInstallDebugBegin(HddInstall *in,const HddFormatPlan *p){assert(in&&p&&confirmation);}
void hddInstallLog(const char *s,...){assert(s);}
void hddInstallResumeBegin(HddInstall *in,const HddFormatPlan *p,unsigned sequence){assert(in&&p&&sequence==9&&confirmation);starts++;}
void hddInstallResumeEnd(void){ends++;}
int dashHttpsCaPath(char *path,int size){assert(confirmation&&starts);snprintf(path,size,"mc0:/WLE-UPDATE-CA.PEM");return 1;}
int hddInstallLoadCa(HddInstall *in){assert(strstr(in->ca_path,"mc0:/"));return 0;}
int dashHddInstallPrepareStorage(HddInstall *in){assert(in&&confirmation&&starts);network++;return network_error;}
int hddFormatLoad(void){assert(confirmation&&!committed&&downloads);rawloads++;return 0;}
static void rebootIopAndReloadCoreStackSilent(void){assert(installs);resets++;}
static void dashToast(int icon,const char*s){assert(icon==2&&s);finished++;}
void dashHddInstallDebugEnd(HddInstall *in,int ret){assert(in&&starts);(void)ret;}
static void afterChange(void){}
const char *hddInstallGameName(unsigned n){(void)n;return "Neutrino";}
'''
ui += action + r'''
static void reset(void){confirmation=lookups=downloads=installs=network=rawloads=resets=starts=ends=finished=0;cancel=lookup_error=committed=download_error=network_error=0;}
int main(void){
 reset();actResume();assert(lookups==1&&confirmation==1&&network==1&&downloads==1&&installs==1&&rawloads==1&&resets==1&&starts==1&&ends==1&&finished==1);
 reset();committed=1;actResume();assert(installs==1&&!network&&!downloads&&!rawloads&&!resets&&finished==1);
 reset();cancel=1;actResume();assert(lookups==1&&confirmation==1&&!starts&&!network&&!downloads&&!installs);
 reset();lookup_error=-EINVAL;actResume();assert(lookups==1&&!confirmation&&!starts&&!network&&!installs);
 reset();download_error=-EIO;actResume();assert(downloads==1&&!installs&&ends==1&&!finished);
 reset();network_error=-EIO;actResume();assert(network==1&&!downloads&&!installs&&ends==1&&!finished);
 reset();ftp=1;actResume();assert(!lookups&&!confirmation&&!starts);
 puts("PASS: resume reads before confirmation, shows saved choices, has no format path, skips completed boot/download work and preserves cache on cancel/failure");
}
'''
with tempfile.TemporaryDirectory(prefix='hdd-resume-ui-') as directory:
    d = Path(directory)
    (d / 'test.c').write_text(ui)
    subprocess.run(['gcc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'include'),
                    str(d / 'test.c'), '-o', str(d / 'test')], check=True)
    subprocess.run([str(d / 'test')], check=True, timeout=10)
