# Adapted from saildot4k/Launchstation at 1057feb4a3870d2bc10d771c72ff0cfa3d20be73.
"""Run the real SAS encoder, snapshot and MC/PFS timestamp writers on host mocks."""
from datetime import datetime, timedelta
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/filer_actions.c').read_text()

def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

program = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define MAX_PATH 256
#define MAX_NAME 256
#define sceMcFileAttrSubdir 0x20
#define FIO_S_ISDIR(mode) ((mode)==2)
#define FIO_CST_CT 1
#define FIO_CST_AT 2
#define FIO_CST_MT 4
typedef struct { unsigned char Resv2,Sec,Min,Hour,Day,Month; unsigned short Year; } sceMcStDateTime;
typedef struct { sceMcStDateTime _Create,_Modify; unsigned int AttrFile; } sceMcTblGetDir;
typedef struct { char name[MAX_NAME]; sceMcTblGetDir stats; } FILEINFO;
typedef struct { int mode; unsigned char ctime[8],mtime[8],atime[8]; } iox_stat_t;
static int fixes,reads,writes,syncs,accesses,ready,mounts,slot;
static int fix_error,read_error,write_error,ready_error,mount_error,not_directory;
static sceMcStDateTime date,written_date;
static char written_path[MAX_PATH];
static int genFixPath(const char *p,char *out) {
    fixes++;
    if(fix_error)return -1;
    if(!strncmp(p,"hdd0:/__common/",15))snprintf(out,MAX_PATH,"pfs3:/%s",p+15);
    else snprintf(out,MAX_PATH,"%s",p);
    return 0;
}
static int genGetStat(const char *p,iox_stat_t *st) {
    reads++;assert(p[strlen(p)-1]!='/');
    assert(!strncmp(p,"mc",2) || !strncmp(p,"pfs3:/",6));
    if(read_error)return read_error;
    memset(st,0,sizeof(*st));st->mode=not_directory?1:2;
    memcpy(st->mtime,&date,8);return 0;
}
static void ensureMemoryCardPortAccessible(int port) { accesses++;slot=port; }
static int mcSync(int mode,void *cmd,int *result) {
    assert(mode==0 && !cmd);syncs++;if(result)*result=write_error;return 1;
}
static int mcSetFileInfo(int port,int mc_slot,const char *p,const sceMcTblGetDir *info,unsigned flags) {
    assert(mc_slot==0 && flags==2 && port==slot);writes++;
    written_date=info->_Modify;snprintf(written_path,MAX_PATH,"%s",p);return 0;
}
static int ensurePathDeviceStackReady(const char *p) { assert(!strncmp(p,"hdd0:/__common/",15));ready++;return !ready_error; }
static int getHddParty(const char *p,void *ignored,char *party,char *dir) {
    assert(!ignored);strcpy(party,"hdd0:__common");snprintf(dir,MAX_PATH,"pfs0:/%s",p+15);return 0;
}
static int mountParty(const char *party) { assert(!strcmp(party,"hdd0:__common"));mounts++;return mount_error?mount_error:3; }
static int fileXioChStat(const char *p,iox_stat_t *st,int flags) {
    assert(flags==(FIO_CST_CT|FIO_CST_AT|FIO_CST_MT));
    assert(!memcmp(st->ctime,st->mtime,8) && !memcmp(st->atime,st->mtime,8));
    writes++;memcpy(&written_date,st->mtime,8);snprintf(written_path,MAX_PATH,"%s",p);return write_error;
}
'''
for line in source.splitlines():
    if line.startswith('#define SAS_TIMESTAMP_'):
        program += line + '\n'
for signature in [
    'static int filerNameContainsTuna(',
    'static int isMemoryCardRootPath(',
    'static int isHddCommonPath(',
    'int filerCanOrganizeFolderTimestamps(',
    'int filerCanSetCustomTimestamp(',
    'static int isCustomDateLeapYear(',
    'static int getCustomDateDaysInMonth(',
    'static int customDateEditorTimestampEqual(',
    'static int setMemoryCardFolderTimestamp(',
    'static int setHddCommonFolderTimestamp(',
    'static int filerIsAutomaticTimestampExcludedFolder(',
    'static char sasTimestampToUpper(',
    'static void buildSasEffectiveFolderName(',
    'static int getSasTimestampCategory(',
    'static void buildSasTimestampPayload(',
    'static int getSasTimestampCharacterCode(',
    'static int getSasTimestampSlot(',
    'static void decrementSasTimestampDate(',
    'static void setSasTimestamp(',
    'static void sasFolderTimestamp(',
    'static int sasFolderPath(',
    'int filerCaptureSasTimestamp(',
    'int filerRestoreSasTimestamp(',
]:
    program += function(signature)
program += r'''
static void reset(const char *name) {
    fixes=reads=writes=syncs=accesses=ready=mounts=0;
    fix_error=read_error=write_error=ready_error=mount_error=not_directory=0;
    sasFolderTimestamp(name,&date);memset(&written_date,0,sizeof(written_date));
}
static void check(const char *path,const char *name,const char *expected,int port) {
    sceMcStDateTime captured;
    reset(name);
    assert(filerCaptureSasTimestamp(path,&captured)==1 && reads==1 && fixes==1);
    assert(customDateEditorTimestampEqual(&captured,&date));
    memset(&date,0,sizeof(date)); /* staging/renames have changed the date */
    assert(filerRestoreSasTimestamp(path,&captured)==0 && writes==1);
    assert(!strcmp(written_path,expected));
    assert(customDateEditorTimestampEqual(&written_date,&captured));
    if(port>=0)assert(slot==port && accesses==1 && syncs==2 && !mounts);
    else assert(mounts==1 && ready==1 && !accesses);
}
int main(void) {
    sceMcStDateTime captured;
    check("mc0:/APP_LAUNCHSTATION/","APP_LAUNCHSTATION","APP_LAUNCHSTATION",0);
    check("mc1:/APP_LAUNCHSTATION-DS","APP_LAUNCHSTATION-DS","APP_LAUNCHSTATION-DS",1);
    check("mc0:/BOOT/","BOOT","BOOT",0);
    check("hdd0:/__common/APP_LAUNCHSTATION/","APP_LAUNCHSTATION","pfs3:/APP_LAUNCHSTATION",-1);
    check("hdd0:/__common/APPS/APP_LAUNCHSTATION/","APP_LAUNCHSTATION","pfs3:/APPS/APP_LAUNCHSTATION",-1);
    const char *skip[]={"mc0:/","mc1:/","hdd0:/__common/","hdd0:/__common2/APP_LAUNCHSTATION/",
        "hdd0:/games/APP_LAUNCHSTATION/","hdd1:/__common/APP_LAUNCHSTATION/",
        "pfs0:/APP_LAUNCHSTATION/","usb0:/APP_LAUNCHSTATION/","ata0:/APP_LAUNCHSTATION/",
        "mmce0:/APP_LAUNCHSTATION/","mc0:/APPS/APP_LAUNCHSTATION/",
        "mc0:/BAEXEC-12345/","mc1:/FUNTUNA/","mc0:/./","mc0:/../",NULL};
    for(int i=0;skip[i];i++) {
        reset("APP_LAUNCHSTATION");
        assert(filerCaptureSasTimestamp(skip[i],&captured)==0 && !fixes && !reads);
        assert(filerRestoreSasTimestamp(skip[i],&date)<0 && !writes);
    }
    reset("APP_LAUNCHSTATION");date.Year=2026;
    assert(filerCaptureSasTimestamp("mc0:/APP_LAUNCHSTATION/",&captured)==0 && !writes);
    reset("APP_LAUNCHSTATION");date.Sec^=1;
    assert(filerCaptureSasTimestamp("mc0:/APP_LAUNCHSTATION/",&captured)==0);
    reset("APP_LAUNCHSTATION");fix_error=1;
    assert(filerCaptureSasTimestamp("mc0:/APP_LAUNCHSTATION/",&captured)==-ENODEV && !reads);
    reset("APP_LAUNCHSTATION");read_error=-EIO;
    assert(filerCaptureSasTimestamp("mc0:/APP_LAUNCHSTATION/",&captured)==-EIO);
    reset("APP_LAUNCHSTATION");not_directory=1;
    assert(filerCaptureSasTimestamp("mc0:/APP_LAUNCHSTATION/",&captured)==-ENOTDIR);
    reset("APP_LAUNCHSTATION");write_error=-EACCES;
    assert(filerRestoreSasTimestamp("mc0:/APP_LAUNCHSTATION/",&date)==-EACCES);
    reset("APP_LAUNCHSTATION");write_error=-EIO;
    assert(filerRestoreSasTimestamp("hdd0:/__common/APP_LAUNCHSTATION/",&date)==-EIO);
    reset("APP_LAUNCHSTATION");ready_error=1;
    assert(filerRestoreSasTimestamp("hdd0:/__common/APP_LAUNCHSTATION/",&date)<0 && !writes);
    reset("APP_LAUNCHSTATION");mount_error=-ENODEV;
    assert(filerRestoreSasTimestamp("hdd0:/__common/APP_LAUNCHSTATION/",&date)==-ENODEV && !writes);
    assert(filerCaptureSasTimestamp(NULL,&captured)==0);
    assert(filerCaptureSasTimestamp("mc0:/APP_LAUNCHSTATION/",NULL)==-EINVAL);
'''
# Independent fixed-point reference dates pin the encoding used for existing
# SAS installations, including category boundaries across December/January.
for folder, category, payload in [('APP_LAUNCHSTATION', 0, 'LAUNCHSTATION'),
                                  ('APP_LAUNCHSTATION-DS', 0, 'LAUNCHSTATIONDS'),
                                  ('BOOT', 10, 'BOOT'), ('APPS', 1, 'APPS'),
                                  ('NEUTRINO', 8, 'NEUTRINO')]:
    alphabet = ' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_-'
    digits = [alphabet.index(c) + 1 if c in alphabet else 40 for c in payload]
    numerator = sum(d * 40 ** (47 - i) for i, d in enumerate(digits[:48]))
    slot = min(86399, numerator * 86400 // 40 ** 48)
    expected = datetime(2099, 1, 1, 7, 59, 59) - timedelta(seconds=category*86400+slot)
    program += f'''    sasFolderTimestamp("{folder}",&date);
    assert(date.Year=={expected.year} && date.Month=={expected.month} && date.Day=={expected.day});
    assert(date.Hour=={expected.hour} && date.Min=={expected.minute} && date.Sec=={expected.second});
'''
program += '    puts("PASS: shared SAS encoding, MC0/MC1 and HDD snapshots/writers, exclusions, ordinary dates and native failures");\n}\n'
compiler = shutil.which('gcc') or shutil.which('clang')
assert compiler, 'Host C compiler required'
with tempfile.TemporaryDirectory(prefix='launchstation-update-sas-') as tmp:
    path, exe = Path(tmp)/'test.c', Path(tmp)/'test'
    path.write_text(program)
    for flags in ([], ['-DDS34']):
        subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-format-truncation', *flags,
                        str(path), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=5)
