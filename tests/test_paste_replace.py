"""Execute production replacement transactions on real files with injected failures."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/filer_copy.c').read_text()
start = source.index('static int copyPathMissing(')
end = source.index('static int isMemoryCardLikePath(', start)
production = source[start:end]
# These guards connect the tested transaction to normal/recursive/PSU-restored files.
copy = source[source.index('int copy('):]
assert 'dummy = genRemove(out)' not in copy
assert 'copyPrepareTarget(out, replace_target, replace_backup)' in copy
assert copy.index('genClose(out_fd);', copy.index('copy_file_data_done:')) < copy.index('copyVerifyTarget(out, original_size, copy_hash)') < copy.index('copyPublishTarget(out, replace_target, replace_backup)')
assert 'copy_hash = copyChecksum(copy_hash, copyBuff, buffSize)' in copy
header = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
typedef uint64_t u64;
typedef uint32_t u32;
typedef struct { unsigned mode; } iox_stat_t;
typedef struct { char EntryName[32]; } sceMcTblGetDir;
#define MAX_PATH 1024
#define FIO_O_RDONLY O_RDONLY
#define FIO_S_ISDIR S_ISDIR
static int rename_calls, fail_rename, fail_rollback, mc_result, denied, removed, fail_close;
static int false_rename, sync_calls, fail_sync;
static int DashFileProgress(const char *a,const char *n,u64 r,u64 t) { (void)a;(void)n;assert(r<=t);return 1; }
static void ensureMemoryCardPortAccessible(int p) { assert(p==0 || p==1); }
static int genGetStat(const char *p,iox_stat_t *s) {
 struct stat st; if(denied) return -EACCES;
 if(stat(p,&st)) return !strncmp(p,"mc",2) && errno==ENOENT ? -4 : -errno;
 s->mode=st.st_mode; return 0;
}
static int rename_file(const char *from,const char *to) {
 rename_calls++;
 if(false_rename) return 0;
 if(rename_calls==fail_rename || (fail_rollback && rename_calls>fail_rename)) return -EIO;
 return rename(from,to) ? -errno : 0;
}
static int fileXioRename(const char *a,const char *b) { return rename_file(a,b); }
static int mcSetFileInfo(int p,int slot,const char *from,sceMcTblGetDir *s,int flag) {
 char a[MAX_PATH],b[MAX_PATH], *leaf; assert(!slot && flag==0x10);
 snprintf(a,sizeof(a),"mc%d:%s",p,from); strcpy(b,a); leaf=strrchr(b,'/');
 assert(leaf); snprintf(leaf+1,sizeof(b)-(leaf+1-b),"%.32s",s->EntryName);
 mc_result=rename_file(a,b); return 0;
}
static int mcSync(int m,void *c,int *r) { (void)m;(void)c;if(++sync_calls==fail_sync)return -1;*r=mc_result;return 1; }
static int genOpen(const char *p,int flags) { int r=open(p,flags,0600);return r<0?-errno:r; }
static int genRead(int fd,void *p,int n) { return read(fd,p,n>17?17:n); }
static int genClose(int fd) { int r=close(fd);return fail_close?-EIO:r; }
static int genRemove(char *p) { removed++;return unlink(p); }
static void put(const char *p,const char *s) { int f=open(p,O_WRONLY|O_TRUNC|O_CREAT,0600);assert(f>=0);assert(write(f,s,strlen(s))==(int)strlen(s));assert(close(f)==0); }
static void expect(const char *p,const char *s) { char b[128]={0};int f=open(p,O_RDONLY);assert(f>=0);assert(read(f,b,sizeof(b))==(int)strlen(s));close(f);assert(!strcmp(b,s)); }
'''
main = r'''
int main(void) {
 char out[MAX_PATH],target[MAX_PATH],backup[MAX_PATH];
 const char *fresh="new ELF bytes: complete replacement";
 const char *paths[]={"disk/APP.ELF","mc0:/BOOT.ELF","mc1:/BOOT.ELF"};
 mkdir("disk",0700);mkdir("mc0:",0700);mkdir("mc1:",0700);
 for(int device=0;device<3;device++) for(int scenario=0;scenario<6;scenario++) {
  rename_calls=fail_rename=fail_rollback=denied=removed=fail_close=0;
  put(paths[device],"original ELF");strcpy(out,paths[device]);
  assert(!copyPrepareTarget(out,target,backup));
  assert(strcmp(out,target) && !strcmp(target,paths[device]));
  expect(target,"original ELF");put(out,fresh);
  u32 hash=copyChecksum(2166136261u,fresh,strlen(fresh));
  assert(!copyVerifyTarget(out,strlen(fresh),hash));
  if(scenario==1) { /* write/readback corruption: old entry is untouched */
   put(out,"partial");assert(copyVerifyTarget(out,strlen(fresh),hash)<0);
   expect(target,"original ELF");unlink(out);continue;
  }
  if(scenario==2) fail_rename=1; /* cannot park the old file */
  if(scenario==3) fail_rename=2; /* publish fails and rolls back */
  if(scenario==4) {fail_rename=2;fail_rollback=1;}
  if(scenario==5) { /* readback close failure */
   fail_close=1;assert(copyVerifyTarget(out,strlen(fresh),hash)<0);
   fail_close=0;expect(target,"original ELF");unlink(out);continue;
  }
  int r=copyPublishTarget(out,target,backup);
  if(!scenario) {assert(!r);expect(target,fresh);assert(access(backup,F_OK));}
  else if(scenario==4) {assert(r<0);expect(backup,"original ELF");expect(out,fresh);unlink(backup);unlink(out);}
  else {assert(r<0);expect(target,"original ELF");expect(out,fresh);unlink(out);}
 }
 strcpy(out,"disk/new.elf");assert(!copyPrepareTarget(out,target,backup) && !target[0]);
 denied=1;strcpy(out,"disk/APP.ELF");assert(copyPrepareTarget(out,target,backup)==-EACCES);denied=0;
 /* Stale recovery artifacts are never reused or deleted. */
 put("disk/LS-COPY-000.TMP","recover me");put("disk/LS-OLD-001.BAK","old recovery");
 strcpy(out,"disk/APP.ELF");assert(!copyPrepareTarget(out,target,backup));assert(strstr(out,"002"));
 expect("disk/LS-COPY-000.TMP","recover me");expect("disk/LS-OLD-001.BAK","old recovery");
 /* Drivers reporting success without renaming cannot discard the old file. */
 strcpy(out,"disk/APP.ELF");put(out,"original ELF");assert(!copyPrepareTarget(out,target,backup));put(out,fresh);
 false_rename=1;assert(copyPublishTarget(out,target,backup)<0);false_rename=0;
 expect(target,"original ELF");expect(out,fresh);unlink(out);
 /* An ambiguous libmc completion preserves all surviving copies. */
 for(int which=1;which<=2;which++) {
  rename_calls=fail_rename=fail_rollback=sync_calls=0;fail_sync=which;
  strcpy(out,"mc0:/BOOT.ELF");put(out,"original ELF");assert(!copyPrepareTarget(out,target,backup));put(out,fresh);
  assert(copyPublishTarget(out,target,backup)<0);expect(backup,"original ELF");
  if(which==1)expect(out,fresh);else expect(target,fresh);
  unlink(out);unlink(backup);fail_sync=0;
 }
 puts("PASS: verified replacements, MC slots, short readback, rollback, retained recovery and permission failures");
}
'''
with tempfile.TemporaryDirectory(prefix='paste-replace-') as folder:
    path = Path(folder)
    (path / 'test.c').write_text(header + production + main)
    subprocess.run([shutil.which('gcc'), '-std=c99', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], cwd=path, check=True)
