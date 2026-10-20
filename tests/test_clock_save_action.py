"""Exercise explicit clock persistence and real-file settings readback failures."""
from pathlib import Path
import shutil
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
def function(path, signature):
 source=(root/path).read_text();a=source.index(signature);b=source.index('{',a)+1;depth=1
 while depth:
  depth+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[a:b]+'\n'
code=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#define MAX_PATH 1024
#define FIO_O_CREAT O_CREAT
#define FIO_O_WRONLY O_WRONLY
#define FIO_O_TRUNC O_TRUNC
#define FIO_O_RDONLY O_RDONLY
#define DI_WARNING 1
#define DI_CHECK 2
static struct { char software_clock[24]; } dashCfg;
static int accepted=1,commits,updates,wait_ok=1,pending,failed,last_icon,unrelated_failed;
static void dashStrCopy(char *a,const char *b,int n){snprintf(a,n,"%s",b);}
static int dashModalConfirm(const char *t,const char *b,const char *a,int x){assert(strstr(t,"settings")&&b&&a&&!x);return accepted;}
static int menuTitleUpdateAsync(int force){assert(force);updates++;return 1;}
static void settingsCommit(void){commits++;}
static int dashStoreFlushWait(const char *s){assert(!strcmp(s,"Saving clock"));return wait_ok;}
static int dashStoreSettingsPending(void){return pending;}
static int dashStoreSettingsFailed(void){return failed;}
static int __attribute__((unused)) failedSaves(void){return failed||unrelated_failed;}
static void dashModalMessage(int icon,const char *title,const char *body,const char *path){assert(title&&body&&!path);last_icon=icon;}
static int corrupt,fail_write_close,fail_read_close,short_write,read_error,opens,open_error;
static int genFixPath(const char *p,char *out){strcpy(out,p);return 0;}
static void configEnsureSysconfDir(const char *p){(void)p;}
static int genOpen(const char *p,int flags){opens++;return opens==open_error?-1:open(p,flags,0600);}
static int genClose(int fd){int r=close(fd);return (opens==1&&fail_write_close)||(opens==2&&fail_read_close)?-1:r;}
static int genWrite(int fd,void *data,int n){return write(fd,data,short_write?n-1:n);}
static int genRead(int fd,void *data,int n){
 if(read_error)return -1;
 int r=read(fd,data,n>7?7:n);if(corrupt&&r>0)((char*)data)[0]^=1;return r;
}
'''
code+=function('src/dash_settings.c','static int settingsSaveSoftwareClock(')
code+=function('src/dash_store.c','int dashWriteFile(')
code+=r'''
int main(void){
 strcpy(dashCfg.software_clock,"2026-10-19 10:00:00");accepted=0;
 assert(!settingsSaveSoftwareClock("2026-10-19 10:00:10"));
 assert(!commits&&!updates&&!strcmp(dashCfg.software_clock,"2026-10-19 10:00:00"));
 accepted=1;assert(settingsSaveSoftwareClock("2026-10-19 10:00:10"));
 assert(commits==1&&updates==1&&last_icon==DI_CHECK&&!strcmp(dashCfg.software_clock,"2026-10-19 10:00:10"));
 wait_ok=0;assert(!settingsSaveSoftwareClock("2026-10-19 10:00:20")&&last_icon==DI_WARNING);wait_ok=1;
 pending=1;assert(!settingsSaveSoftwareClock("2026-10-19 10:00:30")&&last_icon==DI_WARNING);pending=0;
 failed=1;assert(!settingsSaveSoftwareClock("2026-10-19 10:00:40")&&last_icon==DI_WARNING);
 failed=0;unrelated_failed=1;
 assert(settingsSaveSoftwareClock("2026-10-19 10:00:50")&&last_icon==DI_CHECK);
 const char data[]="Dash_SoftwareClock = 2026-10-19 10:00:10\r\n";
 for(int scenario=0;scenario<8;scenario++){
  opens=corrupt=short_write=read_error=fail_write_close=fail_read_close=open_error=0;
  if(scenario==1)corrupt=1;
  if(scenario==2)short_write=1;
  if(scenario==3)read_error=1;
  if(scenario==4)fail_write_close=1;
  if(scenario==5)fail_read_close=1;
  if(scenario==6)open_error=1;
  if(scenario==7)open_error=2;
  assert((dashWriteFile("DASHBOARD.CNF",data,sizeof(data)-1)==0)==(scenario==0));
 }
 puts("PASS: clock save confirmation, exact seconds, cancellation, failed saves and settings readback/close failures");
}
'''
with tempfile.TemporaryDirectory(prefix='clock-save-') as folder:
 p=Path(folder);(p/'test.c').write_text(code)
 subprocess.run([shutil.which('gcc'),'-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],cwd=p,check=True)
