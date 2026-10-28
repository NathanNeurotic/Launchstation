"""Compile VMCMAN's rename operation against bounded card/RPC fixtures.

The EE copy transaction passes a sibling leaf. Card names occupy 32 bytes and
need no terminator when all 32 are used; metadata outside the name stays zero.
An optional source path runs the same checks on the pre-fix implementation.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'iop/vmcman/src/mcdev.c'
source = source.read_text()
ps2 = (root / 'iop/vmcman/src/ps2mc_fio.c').read_text()
ps1 = (root / 'iop/vmcman/src/ps1mc_fio.c').read_text()


def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


code = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define BUILDING_VMCMAN 1
#define EFORMAT 48
#define sceMcResSucceed 0
#define sceMcFileAttrFile 0x10
#define sceMcTypePS1 1
typedef struct { int unit; } MC_IO_FIL_T;
typedef struct { unsigned char metadata[32]; char EntryName[32]; } sceMcTblGetDir;
static struct { int cardform,cardtype; } mcman_devinfos[2][1];
static int mcman_io_sema, mcman_mc_port, mcman_mc_slot, locked;
static int detected, detect_result, set_result, set_calls;
static char expected_name[33];
static void WaitSema(int s) { (void)s; assert(!locked); locked = 1; }
static void SignalSema(int s) { (void)s; assert(locked); locked = 0; }
static void mcman_unit2card(int unit) { assert(unit < 2); mcman_mc_port = unit; mcman_mc_slot = 0; }
static int McDetectCard2(int port, int slot) { assert(port == mcman_mc_port && !slot); detected++; return detect_result; }
static int McSetFileInfo(int port, int slot, const char *old, sceMcTblGetDir *info, int flags) {
    assert(locked && port == mcman_mc_port && !slot && flags == 0x10);
    assert(!strcmp(old, "/LONG-PARENT-FOLDER/LS-COPY-000.TMP"));
    size_t length = strlen(expected_name);
    assert(length <= sizeof(info->EntryName));
    assert(!memcmp(info->EntryName, expected_name, length));
    for (size_t i = length; i < sizeof(info->EntryName); i++) assert(!info->EntryName[i]);
    for (size_t i = 0; i < sizeof(info->metadata); i++) assert(!info->metadata[i]);
    set_calls++;
    return set_result;
}
'''
code += function('int mcman_ioerrcode(') + function('int mc_rename(MC_IO_FIL_T *f, const char *old, const char *new_)\n{')
# Exercise the production receivers' C-string boundaries with nonzero bytes
# after the table, so a 32-byte entry cannot accidentally find stack padding.
ps2_start = ps2.index('char entry_name[', ps2.index('int mcman_setinfo2('))
ps2_end = ps2.index('if (dirInfo.fsindex < 2)', ps2_start)
ps1_start = ps1.index('char entry_name[', ps1.index('int mcman_setinfo1('))
ps1_call = ps1.index('r = mcman_getPS1direntry(', ps1_start)
ps1_end = ps1.index(';', ps1_call) + 1
assert 'mcman_getdirinfo(port, slot, &mfe, entry_name, NULL, 1)' in ps2
code += r'''
#define sceMcResNoEntry -4
static int mcman_chrpos(const char *s, int c) { const char *p = strchr(s,c); return p ? p-s : -1; }
static int validate_ps2(sceMcTblGetDir *info) {
    int r;
'''+ps2[ps2_start:ps2_end]+r'''
    return 0;
}
static int mcman_getPS1direntry(int port, int slot, const char *name, void **entry, int flags) {
    (void)entry; assert(!port && !slot && flags == 1);
    assert(strlen(name) == 32 && !memcmp(name, expected_name,32)); return 0;
}
static int validate_ps1(sceMcTblGetDir *info) {
    int r,port=0,slot=0;void *fse1;
'''+ps1[ps1_start:ps1_end]+r'''
    return r;
}
'''
code += r'''
int main(void) {
    MC_IO_FIL_T file;
    const char *old = "/LONG-PARENT-FOLDER/LS-COPY-000.TMP";
    for (int port = 0; port < 2; port++) for (int mounted = 0; mounted < 2; mounted++) {
        file.unit = port; mcman_devinfos[port][0].cardform = mounted;
        mcman_devinfos[port][0].cardtype = 2;
        for (int i = 0; i <= 33; i++) {
            int length = i ? i : 32; /* Reproduce the rejected valid boundary first. */
            char name[34]; memset(name, 'A', length); name[length] = 0;
            if (length <= 32) strcpy(expected_name, name);
            detect_result = set_result = set_calls = detected = 0;
            int result = mc_rename(&file, old, name);
            assert(result == (length <= 32 ? 0 : -ENAMETOOLONG));
            assert(set_calls == (length <= 32) && detected == !mounted && !locked);
        }
        strcpy(expected_name, "BOOT.ELF"); set_result = -3; /* card full */
        assert(mc_rename(&file, old, expected_name) == -ENOSPC && !locked);
        if (!mounted) {
            detect_result = -8; set_calls = 0;
            assert(mc_rename(&file, old, expected_name) == -EIO && !set_calls && !locked);
        }
        detect_result=set_result=0;mcman_devinfos[port][0].cardtype=sceMcTypePS1;
        for(int length=19;length<=21;length++) {
            memset(expected_name,'P',length);expected_name[length]=0;set_calls=0;
            assert(mc_rename(&file,old,expected_name)==(length<=20?0:-ENAMETOOLONG));
            assert(set_calls==(length<=20)&&!locked);
        }
    }
    struct { sceMcTblGetDir info; char poison[64]; } request;
    memset(&request,'/',sizeof(request)); request.poison[63]=0;
    memset(request.info.EntryName,'Z',32);memset(expected_name,'Z',32);expected_name[32]=0;
    assert(validate_ps2(&request.info)==0 && validate_ps1(&request.info)==0);
    const char *invalid[]={"",".","..","A/B"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        strcpy(request.info.EntryName,invalid[i]);assert(validate_ps2(&request.info)==sceMcResNoEntry);
    }
    strcpy(request.info.EntryName,"BOOT.ELF");assert(validate_ps2(&request.info)==0);
    puts("PASS: VMCMAN sibling renames enforce PS1/PS2 20/32-byte limits, initialize metadata, reject overflow and propagate card errors");
    puts("PASS: PS1/PS2 metadata receivers bound full-width card names before C-string lookup");
}
'''
with tempfile.TemporaryDirectory(prefix='vmc-rename-') as folder:
    folder = Path(folder)
    (folder / 'test.c').write_text(code)
    exe = folder / 'test'
    subprocess.run([shutil.which('gcc') or shutil.which('cc'), '-std=c99', '-Wall', '-Wextra',
                    '-Werror', str(folder / 'test.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
