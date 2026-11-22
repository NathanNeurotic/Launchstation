"""Verbose log ring, disk checkpoints and GUI snapshots use real production code."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
s=(root/'src/hdd_install_log.c').read_text().replace('#include "launchelf.h"','').replace('#include "dash_internal.h"','')
code=r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "hdd_install.h"
typedef uint64_t u64;
typedef struct {int init_count,max_count;} ee_sema_t;
static u64 now=1000;
static int locked,fail_write;
static const char *filename;
static u64 Timer(void){return now;}
static int CreateSema(ee_sema_t*s){assert(s->init_count==1&&s->max_count==1);return 1;}
static int WaitSema(int id){assert(id==1&&!locked);locked=1;return 0;}
static int SignalSema(int id){assert(id==1&&locked);locked=0;return 0;}
#define MAX_PATH 1025
#define FIO_O_WRONLY O_WRONLY
#define FIO_O_CREAT O_CREAT
#define FIO_O_APPEND O_APPEND
static int genOpen(const char*path,int flags){assert(!locked&&!strcmp(path,"ata0:/PSBBN_DP_TEMP/INSTALLER.LOG"));return open(filename,flags,0600);}
static int genWrite(int fd,void*data,unsigned n){assert(!locked);if(fail_write)return -EIO;if(n>3)n=3;return write(fd,data,n);}
static int genClose(int fd){assert(!locked);return close(fd);}
static void dashWorkerStatus(const char*s,int done,int total){(void)s;(void)done;(void)total;}
'''
code+=s
code+=r'''
int main(int argc,char**argv){
 HddInstall in={0};HddInstallLogSnapshot snapshot;char line[HDD_INSTALL_LOG_COLS];
 assert(argc==2);filename=argv[1];in.owned=1;strcpy(in.stage,"ata0:/PSBBN_DP_TEMP");
 hddInstallLogStart();hddInstallLog("URL: https://archive.org/a-very-long-download-path-with-enough-characters-to-wrap-over-multiple-lines.tar.gz");
 hddInstallStatus("Downloading system.tar.gz",100,1000);now+=2300;hddInstallStatus("Downloading system.tar.gz",200,1000);
 hddInstallLogSnapshot(&snapshot);unsigned initial_lines=snapshot.next;
 for(int i=201;i<=1000;i++){now+=10;hddInstallStatus("Downloading system.tar.gz",i,1000);}
 hddInstallLogSnapshot(&snapshot);assert(snapshot.next==initial_lines&&snapshot.done==1000);
 hddInstallProgress("Receiving download headers",0,0);hddInstallLogSnapshot(&snapshot);assert(snapshot.next==initial_lines);
 hddInstallProgress("Downloading system.tar.gz",200,1000);
 hddInstallLogFlush(&in);hddInstallLogSnapshot(&snapshot);assert(!snapshot.disk_error&&snapshot.path[0]&&snapshot.elapsed==10300&&snapshot.idle==0);
 hddInstallRawTrace(3,8192,64,1,0);now+=7000;hddInstallLogSnapshot(&snapshot);assert(strstr(snapshot.io,"WAITING 7 s"));assert(snapshot.idle==7000&&snapshot.done==200&&snapshot.total==1000);
 hddInstallRawTrace(3,8192,64,0,-EIO);hddInstallLogSnapshot(&snapshot);assert(strstr(snapshot.io,"rc=-5"));
 for(unsigned i=0;i<600;i++)hddInstallLog("row %u",i);
 hddInstallLogSnapshot(&snapshot);assert(snapshot.next-snapshot.first==HDD_INSTALL_LOG_ROWS&&!hddInstallLogRead(snapshot.first-1,line));
 assert(hddInstallLogRead(snapshot.next-1,line)&&strstr(line,"row 599"));hddInstallLogFlush(&in);
 hddInstallLog("checkpoint retry");fail_write=1;hddInstallLogFlush(&in);hddInstallLogSnapshot(&snapshot);assert(snapshot.disk_error==-EIO);
 fail_write=0;hddInstallLogFlush(&in);hddInstallLogSnapshot(&snapshot);assert(!snapshot.disk_error);
 unsigned count=snapshot.next;hddInstallLogStop();hddInstallLog("must not appear");hddInstallLogSnapshot(&snapshot);assert(snapshot.next==count&&!hddInstallLogEnabled()&&!locked);
 /* Independent overall progress never resets with the current file, survives
  * log checkpoints, and reaches 100% only on an explicit successful finish. */
 hddInstallLogStart();hddInstallLogSnapshot(&snapshot);assert(!snapshot.overall&&!strcmp(snapshot.phase,"Preparing"));
 hddInstallOverall(1,1);hddInstallPhase(HDD_INSTALL_PHASE_FORMAT);hddInstallOverall(1,2);
 hddInstallLogSnapshot(&snapshot);assert(snapshot.overall==60&&!strcmp(snapshot.phase,"Formatting"));
 hddInstallPhase(HDD_INSTALL_PHASE_DOWNLOAD);hddInstallOverall(3,4);
 hddInstallLogSnapshot(&snapshot);assert(snapshot.overall==325);unsigned overall=snapshot.overall;
 hddInstallProgress("next file",0,100);hddInstallOverall(0,4);hddInstallPhase(HDD_INSTALL_PHASE_PREPARE);
 hddInstallLogSnapshot(&snapshot);assert(snapshot.overall==overall&&!strcmp(snapshot.phase,"Downloading"));
 hddInstallPhase(HDD_INSTALL_PHASE_CHECK);hddInstallOverall(UINT64_C(3000000000),UINT64_C(4000000000));
 hddInstallLogSnapshot(&snapshot);assert(snapshot.overall==475);
 hddInstallPhase(HDD_INSTALL_PHASE_INSTALL);hddInstallOverall(1,2);
 hddInstallLogSnapshot(&snapshot);assert(snapshot.overall==725);
 hddInstallPhase(HDD_INSTALL_PHASE_FINISH);hddInstallOverall(1,1);
 hddInstallLogSnapshot(&snapshot);assert(snapshot.overall==999);
 hddInstallLogStop();hddInstallPhase(HDD_INSTALL_PHASE_COMPLETE);hddInstallLogSnapshot(&snapshot);assert(snapshot.overall==999); /* failed/stopped session */
 hddInstallLogStart();hddInstallPhase(HDD_INSTALL_PHASE_COMPLETE);hddInstallLogSnapshot(&snapshot);
 assert(snapshot.overall==1000&&!strcmp(snapshot.phase,"Complete")&&snapshot.done==snapshot.total);
 hddInstallLogStop();
 puts("PASS: timestamped wrapped lines, live progress/idle time, bounded history, short writes, disk-error recovery and no file I/O under the log lock");
}
'''
with tempfile.TemporaryDirectory(prefix='hdd-log-') as directory:
 d=Path(directory);(d/'check.c').write_text(code)
 subprocess.run(['gcc','-std=c99','-Wall','-Wextra','-Werror','-I',str(root/'include'),str(d/'check.c'),'-o',str(d/'check')],check=True)
 subprocess.run([str(d/'check'),str(d/'INSTALLER.LOG')],check=True,timeout=10)
 log=(d/'INSTALLER.LOG').read_text()
 assert 'https://archive.org/' in log and 'older lines left the RAM buffer' in log and 'row 599' in log and 'LOG WRITE ERROR' in log
 assert 'must not appear' not in log
 assert 'done=' not in log and 'total=' not in log and log.count('Downloading system.tar.gz')==1
