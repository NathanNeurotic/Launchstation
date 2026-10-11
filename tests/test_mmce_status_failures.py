"""Execute the MMCE status aggregation and rendering against failed/empty scans.

A loaded driver is not evidence of accessible media. Both failed unit scans
must not display "No cards", while a successful scan of an empty unit may.
This is a host-side status test, not a hardware/media presence test.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/"src/dash_tools.c").read_text()

def extract(signature):
    start=source.index(signature)
    pos=source.index("{",start)+1
    depth=1
    while depth:
        depth+=(source[pos]=="{")-(source[pos]=="}")
        pos+=1
    return source[start:pos]+"\n"

start=source.index("typedef struct\n{\n\tchar root[MAX_PATH];\n\tMmceBackupOptions options;")
end=source.index("static int mmce_status_job;",start)
job_definition=source[start:end]
program=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_PATH 256
typedef struct { int keep,format; } MmceBackupOptions;
typedef struct { int candidates,pending,unchanged,failed; } MmceBackupScanStats;
typedef struct { int placeholder; } WleIgrStatusStats;
enum { WLE_IGR_STATUS_CURRENT=1, WLE_IGR_STATUS_OUTDATED,
       WLE_IGR_STATUS_NOT_INSTALLED, WLE_IGR_STATUS_PARTIAL };
static struct {char igr_backup_path[MAX_PATH]; int mmce_backup_format;} dashCfg={
    "mass0:/Backups",0
};
static int scan_ret[2],scan_calls,refreshes;
static MmceBackupScanStats scan_stats[2];
static int mmceBackupScanCards(int unit,const char *root,
                               const MmceBackupOptions *opts,
                               MmceBackupScanStats *out,void *progress) {
    (void)opts;(void)progress;
    assert(!strcmp(root,"mass0:/Backups"));
    assert(unit>=0 && unit<2);
    scan_calls++;
    if(scan_ret[unit]>=0)*out=scan_stats[unit];
    return scan_ret[unit];
}
static int wleIgrStatus(const char *root,WleIgrStatusStats *status) {
    (void)root;(void)status;return WLE_IGR_STATUS_NOT_INSTALLED;
}
'''
program+=job_definition
program+=r'''
static int mmce_status_job;
static char mmce_status_text[48]="Not checked";
static char igr_status_text[32]="Not checked";
void dashToolsRefreshMmceStatus(void) { refreshes++; }
'''
program+=extract("static void mmceStatusRun(void *arg)\n{")
program+=extract("static void mmceStatusDone(void *arg)\n{")
program+=r'''
static void reset_scans(int first,int second) {
    scan_ret[0]=first;scan_ret[1]=second;
    memset(scan_stats,0,sizeof(scan_stats));
    scan_calls=refreshes=0;
}
static void check(int do_scan,const char *root,const char *expect) {
    MmceStatusJob *j=(MmceStatusJob *)calloc(1,sizeof(*j));
    assert(j);
    snprintf(j->root,sizeof(j->root),"%s",root);
    j->options.format=dashCfg.mmce_backup_format;
    j->scan_mmce=do_scan;
    if(do_scan && root[0])mmceStatusRun(j);
    else j->igr_status=WLE_IGR_STATUS_NOT_INSTALLED;
    mmce_status_job=42;
    mmceStatusDone(j);
    assert(!strcmp(mmce_status_text,expect));
    assert(mmce_status_job==0);
}
int main(void) {
    reset_scans(-1,-1);
    check(1,"mass0:/Backups","MMCE unavailable");
    assert(scan_calls==2);
    reset_scans(0,-1);
    check(1,"mass0:/Backups","No cards");
    reset_scans(-1,0);
    check(1,"mass0:/Backups","No cards");
    reset_scans(0,0);
    check(1,"mass0:/Backups","No cards");
    reset_scans(0,-1);
    scan_stats[0].candidates=3;scan_stats[0].pending=2;
    check(1,"mass0:/Backups","2 pending");
    reset_scans(0,-1);
    scan_stats[0].candidates=2;scan_stats[0].failed=1;
    check(1,"mass0:/Backups","1 scan error");
    reset_scans(-1,-1);
    check(0,"mass0:/Backups","Not checked");
    check(1,"","Set destination");
    puts("PASS: MMCE scan failure is unavailable; successful empty scan is no cards");
}
'''
compiler=shutil.which("gcc") or shutil.which("clang")
assert compiler,"Host C compiler required"
with tempfile.TemporaryDirectory(prefix="launchstation-mmce-status-") as temp:
    path=Path(temp)/"test.c"
    exe=Path(temp)/"test"
    path.write_text(program)
    subprocess.run([compiler,"-std=c11","-Wall","-Wextra","-Werror",
                    str(path),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=5)
