"""Execute the production native-update transaction with a mock app filesystem.

Tests existing custom sidecars, missing-only publication, legacy PSU packages,
staging collisions and failed updates. This does not emulate real PS2 filesystem
or HTTP behavior; the real PSU parser is exercised in test_app_packages.py.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/"src/self_update.c").read_text()

def function(signature):
    start=source.index(signature)
    end=source.index("{",start)+1
    depth=1
    while depth:
        depth+=(source[end]=="{")-(source[end]=="}")
        end+=1
    return source[start:end]+"\n"

program=r'''
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_PATH 256
#define MAX_NAME 128
#define UPDATE_MAX_ELF (16*1024*1024)
#define UPDATE_ELF_URL "test://latest/APP_LAUNCHSTATION.psu"
static const char *boot="mc0:/APP_LAUNCHSTATION/LAUNCHSTATION.ELF";
static const char *parent="mc0:/APP_LAUNCHSTATION/";
static char files[64][MAX_PATH];
static int count,downloads,sidecar_calls,legacy,fail_bgm_stage,fail_bgm_rename;
static int bad_digest,fail_elf_rename,bad_header;
static int has(const char *p) {
    for(int i=0;i<count;i++)if(!strcmp(files[i],p))return 1;
    return 0;
}
static int put(const char *p) {
    if(has(p))return 0;
    assert(count<64);
    snprintf(files[count++],MAX_PATH,"%s",p);
    return 1;
}
static int del(const char *p) {
    for(int i=0;i<count;i++)if(!strcmp(files[i],p)) {
        memmove(files[i],files[i+1],(size_t)(count-i-1)*MAX_PATH);
        count--;return 0;
    }
    return -1;
}
static int wleExists(const char *p) { return has(p); }
static int genRemove(const char *p) { return del(p); }
static const char *wleBootElfPath(void) { return boot; }
static int ensurePathDeviceStackReady(const char *p) {
    assert(!strcmp(p,parent));return 1;
}
static int parse_expected_sha(const char *text,unsigned char expected[32]) {
    (void)text;memset(expected,0x5a,32);return 1;
}
static int download_file(const char *url,const char *path,const char *ca,
                         int max_bytes,unsigned char actual[32],
                         char *error,int error_size) {
    (void)url;(void)ca;(void)error;(void)error_size;
    assert(max_bytes>=4096);downloads++;
    memset(actual,bad_digest?0:0x5a,32);
    put(path);
    return 8192;
}
static int selfUpdateExtractPsu(const char *pkg,const char *stage,int size) {
    assert(has(pkg) && size==8192);put(stage);return 4096;
}
static int selfUpdateExtractPsuSidecar(const char *pkg,const char *asset,
                                       const char *stage,int size) {
    assert(has(pkg) && size==8192);
    sidecar_calls++;
    if(legacy)return 0;
    if(fail_bgm_stage && !strcmp(asset,"BGM.ADP"))return -1;
    assert(!has(stage));put(stage);return 128;
}
static int checkELFheader(const char *stage) { assert(has(stage));return bad_header?-1:0; }
static int rename_leaf(const char *p,const char *old,const char *newname) {
    char from[MAX_PATH],to[MAX_PATH];
    assert(!strcmp(p,parent));
    snprintf(from,sizeof(from),"%s%s",p,old);
    snprintf(to,sizeof(to),"%s%s",p,newname);
    if(fail_bgm_rename && !strcmp(newname,"BGM.ADP"))return -1;
    if(fail_elf_rename && !strcmp(newname,"LAUNCHSTATION.ELF"))return -1;
    if(!has(from) || has(to))return -1;
    del(from);put(to);return 0;
}
'''
for sig in [
    "static int device_path(",
    "static int supported_boot_path(",
    "static int split_parent_leaf(",
    "static int join_leaf(",
]:
    program+=function(sig)
program+=function("int selfUpdateInstall(")
program+=r'''
static void reset(void) {
    count=downloads=sidecar_calls=0;
    legacy=fail_bgm_stage=fail_bgm_rename=0;
    bad_digest=fail_elf_rename=bad_header=0;
    put(boot);
}
static int install(char *msg) {
    return selfUpdateInstall("settings/legacy-ca.pem","sha256:mock",msg,512);
}
int main(void) {
    const char *ca="mc0:/APP_LAUNCHSTATION/WLE-UPDATE-CA.PEM";
    const char *bgm="mc0:/APP_LAUNCHSTATION/BGM.ADP";
    const char *ca_tmp="mc0:/APP_LAUNCHSTATION/WLE-UPDATE-CA.PEM.UPDATE";
    const char *bgm_tmp="mc0:/APP_LAUNCHSTATION/BGM.ADP.UPDATE";
    const char *backup="mc0:/APP_LAUNCHSTATION/LAUNCHSTATION.ELF.BAK";
    char msg[512];
    reset();
    assert(install(msg)==0);
    assert(has(ca)&&has(bgm)&&has(boot)&&has(backup));
    assert(!has(ca_tmp)&&!has(bgm_tmp)&&sidecar_calls==2&&downloads==1);
    reset();put(ca);put(bgm);
    assert(install(msg)==0);
    assert(has(ca)&&has(bgm)&&sidecar_calls==0&&has(backup));
    reset();put(ca); /* only restore missing music */
    assert(install(msg)==0);
    assert(has(ca)&&has(bgm)&&sidecar_calls==1);
    reset();legacy=1;
    assert(install(msg)==0);
    assert(!has(ca)&&!has(bgm)&&has(boot)&&has(backup));
    reset();put(ca_tmp);
    assert(install(msg)<0 && downloads==0&&has(boot)&&has(ca_tmp)&&!has(backup));
    reset();fail_bgm_stage=1;
    assert(install(msg)<0 && !has(ca_tmp)&&!has(bgm_tmp));
    assert(has(boot)&&!has(backup)&&!has(ca)&&!has(bgm));
    reset();fail_bgm_rename=1;
    assert(install(msg)<0 && has(boot)&&!has(backup)&&has(ca));
    assert(!has(bgm)&&!has(bgm_tmp)); /* previously absent CA may remain */
    reset();bad_digest=1;
    assert(install(msg)<0 && has(boot)&&!has(backup)&&!has(ca)&&!has(bgm));
    reset();bad_header=1;
    assert(install(msg)<0 && has(boot)&&!has(backup)&&!has(ca)&&!has(bgm));
    reset();fail_elf_rename=1;
    assert(install(msg)<0 && !has(boot)&&has(backup)); /* simulated rollback fails too */
    puts("PASS: updater installs missing-only sidecars; existing data and early failures preserve ELF");
}
'''
compiler=shutil.which("gcc") or shutil.which("clang")
assert compiler,"Host C compiler required"
with tempfile.TemporaryDirectory(prefix="launchstation-update-transaction-") as tmp:
    path=Path(tmp)/"test.c"
    exe=Path(tmp)/"test"
    path.write_text(program)
    subprocess.run([compiler,"-std=c11","-Wall","-Wextra","-Werror",
                    str(path),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=5)
