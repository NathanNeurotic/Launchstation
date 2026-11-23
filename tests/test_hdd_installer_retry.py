"""Production installer job dispatch/retry checkpoints, without a physical disk."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/dash_hdd.c').read_text()


def function(signature):
    a = source.index(signature)
    depth = 0
    for t in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', source[a:], re.S):
        depth += (t.group() == '{') - (t.group() == '}')
        if not depth and t.group() == '}': return source[a:a + t.end()] + '\n'
    raise AssertionError(signature)


code = r'''
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "hdd_install.h"
#define DASH_JOB_VISIBLE 1
#define DASH_TOAST_WARN 2
enum { HOP_DOWNLOAD_INSTALLER=1,HOP_INSTALL_SOFTWARE=2,HOP_RESUME_LOOKUP=3 };
typedef struct {int op,ret,format_started,abandoned;HddInstall *installer;HddFormatPlan format_plan;unsigned resume_sequence;} HddJob;
static int scenario,submits,applies,populates,finishes,boots,cleanups,downloads,creates,finds,physical,core,prepared,rawloads,freed;
static jmp_buf fatal;
static int loading;
static int dashLoadingActive(void){return loading;}
static void dashLoadingBegin(const char *s){assert(s&&!loading);loading=1;}
static void dashLoadingEnd(void){assert(loading&&!hddInstallRecoveryFault());loading=0;}
int hddInstallResumeLoad(HddInstall *in,HddFormatPlan *p,unsigned *seq){(void)in;(void)p;(void)seq;return -ENOENT;}
int hddInstallResumeSave(HddInstall *in){assert(in);return 0;}
void hddInstallLog(const char *s,...){assert(s);}
void hddInstallLogFlush(HddInstall *in){assert(in);}
void hddInstallProgress(const char *s,int a,int b){assert(s&&a<=b);}
static void filerSizeCacheInvalidate(const char *s){assert(s);}
int hddInstallMountStage(HddInstall *in){assert(in);return hddInstallRecoveryFault()?-502:0;}
int hddInstallUnmountStage(HddInstall *in){assert(in);return hddInstallRecoveryFault()?-502:0;}
int hddInstallCreateStage(HddInstall *in){assert(!in->stage_ready);creates++;in->stage_ready=in->owned=1;return 0;}
int hddInstallFindStage(HddInstall *in){assert(in->stage_ready&&in->owned);finds++;return 0;}
int hddInstallDownload(HddInstall *in){assert(in);downloads++;if(scenario==2&&downloads==1){hddInstallRecoveryObserve(-502);return -502;}return 0;}
int hddInstallApply(HddInstall *in){assert(in&&!in->applied);applies++;return 0;}
int hddInstallPsbbnPayload(void *context,const HddFormatIo *io){
 HddInstall *in=context;(void)io;populates++;
 if((scenario==1||scenario==7||scenario==8||scenario==10)&&populates==1){hddInstallRecoveryObserve(-502);return -502;}
 if(scenario==3){hddInstallRecoveryObserve(-502);return -502;}
 if(scenario==6)return -EINVAL;
 in->populated=1;return 0;
}
int hddInstallFinish(const HddFormatPlan *p,const char *mbr,const char *hash,int *started,int(*populate)(void*,const HddFormatIo*),void *context){
 assert(p&&mbr&&hash&&started&&context);finishes++;*started=1;
 int ret=populate?populate(context,NULL):0;
 if(ret<0)return ret;
 if(scenario==4&&finishes==1){hddInstallRecoveryObserve(-502);return -502;}
 return 0;
}
int hddInstallEnableBoot(HddInstall *in){assert(in&&!hddInstallRecoveryFault()&&!in->committed);boots++;return 0;}
int hddInstallCleanup(HddInstall *in){
 assert(in->committed);cleanups++;
 if(scenario==5&&cleanups==1){hddInstallRecoveryObserve(-502);return -502;}
 in->owned=0;return 0;
}
static void hddJobFree(HddJob *j){assert(j);freed++;}
static void hddJobDone(void *j){assert(j);}
static void dashToast(int icon,const char *s){assert(icon==2&&s);}
int resetInstallerIopTransport(void){physical++;return scenario!=10;}
int reloadInstallerCoreAfterReset(void){core++;return scenario!=8;}
int dashHddInstallPrepareStorage(HddInstall *in){assert(in);prepared++;return 0;}
int hddFormatLoad(void){rawloads++;return 0;}
void dashHddInstallRecoveryFailed(void){longjmp(fatal,1);}
'''
a = source.index('\t\tcase HOP_DOWNLOAD_INSTALLER:', source.index('static void hddJobRun('))
b = source.index('\t\tcase HOP_UNMOUNT:', a)
code += 'static void hddJobRun(void *arg){HddJob *j=arg;switch(j->op){\n' + source[a:b] + 'default:break;}}\n'
code += r'''
static int dashWorkerSubmit(const char *s,int flags,void(*run)(void*),void(*done)(void*),void *arg){
 assert(s&&flags==1&&done);if(scenario==9)return -1;submits++;run(arg);return submits;
}
int dashHddInstallAwait(int id,const char *s){
 assert(id&&s);
 if(scenario==7&&submits==1){hddInstallRecoveryRequestReset();assert(resetInstallerIopTransport());hddInstallRecoveryResetComplete();}
 return 1;
}
static int dashWorkerAwait(int id,const char *s,int cancelable){(void)cancelable;assert(id&&s);return 1;}
'''
code += function('static int runJob(')
code += r'''
int main(void){
 for(scenario=0;scenario<=10;scenario++){
  loading=0;submits=applies=populates=finishes=boots=cleanups=downloads=creates=finds=physical=core=prepared=rawloads=freed=0;
  HddInstall in={.flags=7,.jail=1,.owned=1,.stage_ready=scenario!=2};strcpy(in.stage,"ata0:/PSBBN_DP_TEMP");
  HddJob j={.op=scenario==2?HOP_DOWNLOAD_INSTALLER:HOP_INSTALL_SOFTWARE,.installer=&in,.format_plan={.apa_sectors=131072u*2048}};
  if(scenario==8||scenario==10){
   if(!setjmp(fatal)){runJob(&j,"Installing",0);assert(0);}
   assert(physical==1&&core==(scenario==8?1:0)&&submits==1&&!freed&&!hddInstallRecoveryActive());
   continue;
  }
  int ok=runJob(&j,"Installer",0);assert(!hddInstallRecoveryActive());
  if(scenario==9){assert(!ok&&freed==1&&!submits);continue;}
  assert(ok&&!freed);
  if(scenario==3){assert(j.ret==-502&&submits==3&&in.recovery_attempts==2&&physical==3&&core==3&&!boots&&!cleanups);continue;}
  if(scenario==6){assert(j.ret==-EINVAL&&submits==1&&!physical&&!boots);continue;}
  assert(!j.ret);
  if(scenario==0){assert(submits==1&&!physical&&applies==1&&populates==1&&boots==1);}
  else{assert(submits==2&&in.recovery_attempts==1&&physical==1&&core==1&&prepared==1);}
  if(scenario==2){assert(creates==1&&finds==1&&downloads==2&&!applies);}
  if(scenario==1||scenario==7){assert(applies==1&&populates==2&&finishes==2&&boots==1);}
  if(scenario==4){assert(applies==1&&populates==1&&finishes==2&&boots==1);}
  if(scenario==5){assert(applies==1&&populates==1&&finishes==1&&boots==1&&cleanups==2);}
 }
 puts("PASS: two bounded retries, no whole-drive reformat, completed apply/population/boot checkpoints, download staging retained and non-ATA errors stop normally");
}
'''
with tempfile.TemporaryDirectory(prefix='hdd-installer-retry-') as directory:
    d = Path(directory)
    (d / 'test.c').write_text(code)
    subprocess.run(['gcc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                    '-I', str(root / 'include'), str(d / 'test.c'), str(root / 'src/hdd_install_recovery.c'),
                    '-o', str(d / 'test')], check=True)
    subprocess.run([str(d / 'test')], check=True, timeout=10)
