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
#include <string.h>
#include <errno.h>
typedef struct { unsigned char raw[8]; } sceMcStDateTime;
static int capture_result=1,restore_result,captures,restores;
static int filerCaptureSasTimestamp(const char *p,sceMcStDateTime *t) {
    (void)p;memset(t,0x6a,sizeof(*t));captures++;return capture_result;
}
static int filerRestoreSasTimestamp(const char *p,const sceMcStDateTime *t) {
    (void)p;assert(t->raw[0]==0x6a);restores++;return restore_result;
}
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_PATH 256
#define MAX_NAME 128
#define UPDATE_MAX_ELF (16*1024*1024)
#define UPDATE_SPACE_MARGIN (32*1024)
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
static char steps[16][64];
static int step_count;
static void DashStorageProbeStatus(const char *s) {
    assert(s && s[0] && step_count<16);snprintf(steps[step_count++],64,"%s",s);
}
static void expect_steps(const char *const *want,int n) {
    assert(step_count==n);
    for(int i=0;i<n;i++)assert(!strcmp(steps[i],want[i]));
}
typedef unsigned long long u64;
static u64 free_space=64ULL*1024*1024;
static int package_bytes=2234368;
static int filerFreeSpace(const char *p,u64 *out) { assert(!strcmp(p,parent));*out=free_space;return 1; }
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
/* RAM path: the package never touches the card. */
static int ram_ok,ram_downloads;
static unsigned char ram_token[16];
static unsigned char *update_ram_buffer(int bytes) { return ram_ok && bytes>0?ram_token:NULL; }
static void test_free(void *p) { assert(p==NULL || p==ram_token); }
#define free test_free
static int download_to_ram(const char *url,unsigned char *buf,int cap,const char *ca,
                           unsigned char actual[32],char *error,int error_size) {
    (void)url;(void)ca;(void)error;(void)error_size;
    assert(buf==ram_token && cap>0);downloads++;ram_downloads++;
    memset(actual,bad_digest?0:0x5a,32);
    return 8192;
}
static int selfUpdateExtractPsuMem(const unsigned char *pkg,int size,const char *stage) {
    assert(pkg==ram_token && size==8192);put(stage);return 4096;
}
static int selfUpdateExtractPsuSidecarMem(const unsigned char *pkg,int size,
                                          const char *asset,const char *stage) {
    assert(pkg==ram_token && size==8192);
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
program+=function("static int install_update_files(")
program+=function("int selfUpdateInstall(")
program+=r'''
static void reset(void) {
    count=downloads=sidecar_calls=captures=restores=restore_result=0;capture_result=1;
    legacy=fail_bgm_stage=fail_bgm_rename=0;
    bad_digest=fail_elf_rename=bad_header=0;
    free_space=64ULL*1024*1024;package_bytes=2234368;step_count=0;ram_ok=ram_downloads=0;
    put(boot);
}
static int install(char *msg) {
    int ret=selfUpdateInstall("settings/legacy-ca.pem","sha256:mock",package_bytes,msg,512);
    assert(captures==1 && restores==(capture_result>0));
    return ret;
}
int main(void) {
    const char *ca="mc0:/APP_LAUNCHSTATION/WLE-UPDATE-CA.PEM";
    const char *bgm="mc0:/APP_LAUNCHSTATION/BGM.ADP";
    const char *ca_tmp="mc0:/APP_LAUNCHSTATION/WLE-UPDATE-CA.PEM.UPDATE";
    const char *bgm_tmp="mc0:/APP_LAUNCHSTATION/BGM.ADP.UPDATE";
    const char *backup="mc0:/APP_LAUNCHSTATION/LAUNCHSTATION.ELF.BAK";
    const char *elf_tmp="mc0:/APP_LAUNCHSTATION/LAUNCHSTATION.ELF.UPDATE";
    const char *package_tmp="mc0:/APP_LAUNCHSTATION/LAUNCHSTATION.ELF.PACKAGE";
    char msg[512];
    reset();
    assert(install(msg)==0);
    assert(has(ca)&&has(bgm)&&has(boot)&&!has(backup)); /* no .BAK stays after an install */
    {
        static const char *const all[]={"Downloading the update from GitHub","Checking the download (SHA-256)",
            "Unpacking and checking the new ELF","Adding missing bundled files","Replacing the ELF"};
        expect_steps(all,5);
    }
    assert(!has(ca_tmp)&&!has(bgm_tmp)&&sidecar_calls==2&&downloads==1);
    reset();put(ca);put(bgm);put(backup); /* a .BAK left by an older build is removed */
    assert(install(msg)==0);
    assert(has(ca)&&has(bgm)&&sidecar_calls==0&&has(boot)&&!has(backup));
    {
        static const char *const none_missing[]={"Downloading the update from GitHub","Checking the download (SHA-256)",
            "Unpacking and checking the new ELF","Replacing the ELF"};
        expect_steps(none_missing,4);
    }
    reset();put(ca); /* only restore missing music */
    assert(install(msg)==0);
    assert(has(ca)&&has(bgm)&&sidecar_calls==1);
    reset();legacy=1;
    assert(install(msg)==0);
    assert(!has(ca)&&!has(bgm)&&has(boot)&&!has(backup));
    reset();put(ca_tmp);
    assert(install(msg)<0 && downloads==0&&has(boot)&&has(ca_tmp)&&!has(backup));
    /* An interrupted ELF stage/download must not be truncated or removed.
     * No network traffic or package parsing should begin in these cases. */
    reset();put(elf_tmp);
    assert(install(msg)<0 && downloads==0 && sidecar_calls==0);
    assert(has(elf_tmp) && has(boot) && !has(backup) && !has(package_tmp));
    reset();put(package_tmp);
    assert(install(msg)<0 && downloads==0 && sidecar_calls==0);
    assert(has(package_tmp) && has(boot) && !has(backup) && !has(elf_tmp));
    reset();put(elf_tmp);put(package_tmp);
    assert(install(msg)<0 && downloads==0 && has(elf_tmp) && has(package_tmp));
    assert(has(boot) && !has(backup));
    reset();fail_bgm_stage=1;
    assert(install(msg)<0 && !has(ca_tmp)&&!has(bgm_tmp));
    assert(has(boot)&&!has(backup)&&!has(ca)&&!has(bgm));
    reset();fail_bgm_rename=1;
    assert(install(msg)<0 && has(boot)&&!has(backup)&&has(ca));
    assert(!has(bgm)&&!has(bgm_tmp)); /* previously absent CA may remain */
    reset();bad_digest=1;
    assert(install(msg)<0 && has(boot)&&!has(backup)&&!has(ca)&&!has(bgm));
    {
        static const char *const stop[]={"Downloading the update from GitHub","Checking the download (SHA-256)"};
        expect_steps(stop,2); /* a bad digest stops before unpacking */
    }
    reset();bad_header=1;
    assert(install(msg)<0 && has(boot)&&!has(backup)&&!has(ca)&&!has(bgm));
    reset();fail_elf_rename=1;
    assert(install(msg)<0 && !has(boot)&&has(backup)); /* simulated rollback fails too */
    /* A nearly full card is refused before any download; an old .BAK is
     * cleared first to make room, and the installed ELF is untouched. */
    reset();put(backup);free_space=2*2234368+32*1024-1;
    assert(install(msg)<0 && downloads==0 && has(boot) && !has(backup) && strstr(msg,"Not enough free space"));
    expect_steps(NULL,0); /* refused before any step starts */
    reset();free_space=2*2234368+32*1024;
    assert(install(msg)==0 && downloads==1 && has(boot));
    reset();package_bytes=0;free_space=0; /* size unknown: no pre-check */
    assert(install(msg)==0 && downloads==1);
    /* With RAM to spare the card needs room for the unpacked files only. */
    reset();ram_ok=1;put(backup);free_space=2234368+32*1024;
    assert(install(msg)==0 && ram_downloads==1 && has(boot) && has(ca) && has(bgm));
    assert(!has(package_tmp) && !has(elf_tmp) && !has(backup));
    reset();ram_ok=1;free_space=2234368+32*1024-1;
    assert(install(msg)<0 && downloads==0 && has(boot) && strstr(msg,"Not enough free space"));
    reset();ram_ok=1;bad_digest=1;
    assert(install(msg)<0 && has(boot) && !has(elf_tmp) && !has(ca) && !has(bgm));
    reset();ram_ok=1;fail_bgm_stage=1;
    assert(install(msg)<0 && has(boot) && !has(ca_tmp) && !has(bgm_tmp) && !has(elf_tmp));
    reset();capture_result=-EIO;
    assert(install(msg)<0 && downloads==0 && has(boot) && !has(backup));
    reset();capture_result=0;
    assert(install(msg)==0 && restores==0);
    reset();restore_result=-EACCES;
    assert(install(msg)==0 && strstr(msg,"Could not restore") && has(boot));
    reset();restore_result=-EIO;bad_digest=1;
    assert(install(msg)<0 && strstr(msg,"Could not restore") && has(boot));
    puts("PASS: updater refuses to truncate old stages; missing-only sidecars and ELF recover safely");
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
