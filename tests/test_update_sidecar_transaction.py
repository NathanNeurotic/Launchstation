"""Execute the production native-update install with a mock app filesystem.

Tests existing custom sidecars, missing-only sidecars, legacy PSU packages,
the in-place ELF overwrite and failed updates. This does not emulate real PS2 filesystem
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
#define FIO_O_WRONLY 2
#define FIO_O_CREAT 0x200
#define FIO_O_TRUNC 0x400
static const char *boot="mc0:/APP_LAUNCHSTATION/LAUNCHSTATION.ELF";
static const char *parent="mc0:/APP_LAUNCHSTATION/";
static char files[64][MAX_PATH];
static int count,downloads,sidecar_calls,legacy,fail_bgm_stage;
static int bad_digest,bad_header,bad_locate;
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
static unsigned old_size=2234368;
static int filerFreeSpace(const char *p,u64 *out) { assert(!strcmp(p,parent));*out=free_space;return 1; }
static int genRemove(const char *p) { return del(p); }
static const char *wleBootElfPath(void) { return boot; }
static int ensurePathDeviceStackReady(const char *p) {
    assert(!strcmp(p,parent));return 1;
}
static int parse_expected_sha(const char *text,unsigned char expected[32]) {
    (void)text;memset(expected,0x5a,32);return 1;
}
/* RAM is the only path: a package of 8192 bytes whose ELF is 4096 bytes at offset 2048. */
static int ram_ok,ram_downloads;
static unsigned char ram_token[8192];
static unsigned char *update_ram_buffer(int bytes) { return ram_ok && bytes>0?ram_token:NULL; }
static void test_free(void *p) { assert(p==NULL || p==ram_token); }
#define free test_free
static int download_to_ram(const char *url,unsigned char *buf,int cap,const char *ca,
                           unsigned char actual[32],char *error,int error_size) {
    (void)url;(void)ca;(void)error;(void)error_size;
    assert(buf==ram_token && cap>0);downloads++;ram_downloads++;
    memset(actual,bad_digest?0:0x5a,32);
    memset(ram_token,0,sizeof(ram_token));
    for(int i=0;i<4096;i++)ram_token[2048+i]=(unsigned char)(i*7+1);
    memcpy(ram_token+2048,"\x7f" "ELF\x01\x01\x01\x00",8);
    ram_token[2048+16]=bad_header?3:2;ram_token[2048+17]=0;ram_token[2048+18]=8;ram_token[2048+19]=0;
    return 8192;
}
static int selfUpdateLocatePsuElf(const unsigned char *pkg,int size,int *offset) {
    assert(pkg==ram_token && size==8192);
    if(bad_locate)return -1;
    *offset=2048;return 4096;
}
static int selfUpdateExtractPsuSidecarMem(const unsigned char *pkg,int size,
                                          const char *asset,const char *dest) {
    assert(pkg==ram_token && size==8192);
    sidecar_calls++;
    if(legacy)return 0;
    if(fail_bgm_stage && !strcmp(asset,"BGM.ADP"))return -1;
    assert(!has(dest));put(dest);return 128;
}
/* The card: opening the ELF's own path must truncate (O_CREAT|O_TRUNC), and nothing else is created. */
static int fail_open,fail_write_at,fail_close,opens,written,closed;
static unsigned char card[8192];
static int genOpen(const char *p,int flags) {
    assert(!strcmp(p,boot));
    assert((flags&FIO_O_WRONLY) && (flags&FIO_O_CREAT) && (flags&FIO_O_TRUNC));
    opens++;
    if(fail_open)return -EIO;
    written=0;return 3;
}
static int genWrite(int fd,void *buf,int size) {
    assert(fd==3);
    if(fail_write_at && written+size>fail_write_at)return -ENOSPC;
    memcpy(card+written,buf,(size_t)size);written+=size;return size;
}
static int genClose(int fd) { assert(fd==3);closed++;return fail_close?-EIO:0; }
static void write_progress(int d,int t) { (void)d;(void)t; }
typedef struct { struct { unsigned FileSizeByte; } stats; } FILEINFO;
static int find_file_info(const char *p,const char *leaf,FILEINFO *out) {
    char path[MAX_PATH];
    snprintf(path,sizeof(path),"%s%s",p,leaf);
    out->stats.FileSizeByte=old_size;return has(path);
}
'''
for sig in [
    "static int device_path(",
    "static int supported_boot_path(",
    "static int split_parent_leaf(",
    "static int join_leaf(",
]:
    program+=function(sig)
program+=function("static int looks_like_elf(")
program+=function("static int overwrite_elf(")
program+=function("static int install_update_files(")
program+=function("int selfUpdateInstall(")
program+=r'''
static void reset(void) {
    count=downloads=sidecar_calls=captures=restores=restore_result=0;capture_result=1;
    legacy=fail_bgm_stage=bad_digest=bad_header=bad_locate=0;
    fail_open=fail_write_at=fail_close=opens=written=closed=0;
    free_space=64ULL*1024*1024;package_bytes=2234368;old_size=2234368;step_count=0;ram_ok=1;ram_downloads=0;
    memset(card,0,sizeof(card));
    put(boot);
}
static int install(char *msg) {
    int ret=selfUpdateInstall("settings/legacy-ca.pem","sha256:mock",package_bytes,msg,512);
    assert(captures==1 && restores==(capture_result>0));
    return ret;
}
static int card_is_new(void) { return written==4096 && !memcmp(card,ram_token+2048,4096); }
int main(void) {
    const char *ca="mc0:/APP_LAUNCHSTATION/WLE-UPDATE-CA.PEM";
    const char *bgm="mc0:/APP_LAUNCHSTATION/BGM.ADP";
    const char *backup="mc0:/APP_LAUNCHSTATION/LAUNCHSTATION.ELF.BAK";
    const char *elf_tmp="mc0:/APP_LAUNCHSTATION/LAUNCHSTATION.ELF.UPDATE";
    const char *package_tmp="mc0:/APP_LAUNCHSTATION/LAUNCHSTATION.ELF.PACKAGE";
    char msg[512];
    reset();
    assert(install(msg)==0);
    assert(has(ca)&&has(bgm)&&has(boot)&&!has(backup)&&count==3); /* no other file is ever made */
    assert(opens==1 && closed==1 && card_is_new());
    {
        static const char *const all[]={"Downloading the update from GitHub","Checking the download (SHA-256)",
            "Checking the new ELF","Adding missing bundled files","Replacing the ELF"};
        expect_steps(all,5);
    }
    assert(sidecar_calls==2&&downloads==1&&ram_downloads==1);
    reset();put(ca);put(bgm);put(backup); /* a .BAK left by an older build is removed */
    assert(install(msg)==0);
    assert(has(ca)&&has(bgm)&&sidecar_calls==0&&has(boot)&&!has(backup)&&card_is_new());
    {
        static const char *const none_missing[]={"Downloading the update from GitHub","Checking the download (SHA-256)",
            "Checking the new ELF","Replacing the ELF"};
        expect_steps(none_missing,4);
    }
    reset();put(ca); /* only restore missing music */
    assert(install(msg)==0);
    assert(has(ca)&&has(bgm)&&sidecar_calls==1&&card_is_new());
    reset();legacy=1;
    assert(install(msg)==0);
    assert(!has(ca)&&!has(bgm)&&has(boot)&&!has(backup)&&card_is_new());
    /* Files an older build left behind are ignored, never truncated or removed. */
    reset();put(elf_tmp);put(package_tmp);
    assert(install(msg)==0 && has(elf_tmp) && has(package_tmp) && card_is_new());
    /* Every refusal happens before the running ELF is opened. */
    reset();fail_bgm_stage=1;
    assert(install(msg)<0 && opens==0 && has(boot) && !has(bgm));
    reset();bad_digest=1;
    assert(install(msg)<0 && opens==0 && has(boot)&&!has(ca)&&!has(bgm));
    {
        static const char *const stop[]={"Downloading the update from GitHub","Checking the download (SHA-256)"};
        expect_steps(stop,2); /* a bad digest stops before unpacking */
    }
    reset();bad_header=1;
    assert(install(msg)<0 && opens==0 && has(boot)&&!has(ca)&&!has(bgm));
    reset();bad_locate=1;
    assert(install(msg)<0 && opens==0 && has(boot)&&!has(ca)&&!has(bgm));
    /* A failed write reports the step and the error, and leaves no stray file. */
    reset();fail_open=1;
    assert(install(msg)<0 && strstr(msg,"step 1") && strstr(msg,"error -5") && count==3 && has(boot));
    reset();fail_write_at=2048;
    assert(install(msg)<0 && strstr(msg,"step 2") && strstr(msg,"error -28") && closed==1 && count==3);
    reset();fail_close=1;
    assert(install(msg)<0 && strstr(msg,"step 3") && count==3);
    /* Growth over the old ELF must fit; the old ELF's own space is reused. */
    reset();old_size=1000;free_space=4096+32*1024-1000-1;
    assert(install(msg)<0 && opens==0 && strstr(msg,"Not enough free space"));
    reset();old_size=1000;free_space=4096+32*1024-1000;
    assert(install(msg)==0 && card_is_new());
    /* No RAM for the package: refuse before downloading anything. */
    reset();ram_ok=0;
    assert(install(msg)<0 && downloads==0 && opens==0 && has(boot) && strstr(msg,"free memory"));
    reset();package_bytes=0; /* unknown size: no RAM buffer */
    assert(install(msg)<0 && downloads==0 && opens==0);
    reset();capture_result=-EIO;
    assert(install(msg)<0 && downloads==0 && has(boot));
    reset();capture_result=0;
    assert(install(msg)==0 && restores==0);
    reset();restore_result=-EACCES;
    assert(install(msg)==0 && strstr(msg,"Could not restore") && has(boot));
    reset();restore_result=-EIO;bad_digest=1;
    assert(install(msg)<0 && strstr(msg,"Could not restore") && has(boot));
    puts("PASS: updater writes the verified ELF over the running one and makes no other file");
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
