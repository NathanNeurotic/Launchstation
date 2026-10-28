"""Exercise production MMCE history retention and safe targeted backup integration."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / "src/mmce_backup.c").read_text(encoding="utf-8")
header = (root / "include/mmce_backup.h").read_text(encoding="utf-8")


def extract(signature):
    start = src.index(signature)
    pos = src.index("{", start) + 1
    depth = 1
    while depth:
        depth += (src[pos] == "{") - (src[pos] == "}")
        pos += 1
    return src[start:pos] + "\n"


retention = extract("static int prune_history(")
sidecar = extract("static int sidecar_path(")
unique = extract("static int unique_history_path(")
targeted = extract("int mmceBackupCardPath(")
backup = extract("static int backup_one(")

assert "mmceBackupCardPath" in header
assert "protect_name, name)" in backup, "new verified snapshot must survive retention"
assert "mcCardBackupNoEcc(source, staged, NULL)" in backup
assert "fileXioRename(staged, dest)" in backup
assert backup.index("fileXioRename(staged, dest)") < backup.index("prune_history(")
assert "memchr(p, '\\\\', len)" in targeted
assert "mcCardValidateNoEcc(source)" in targeted
assert "history_name_match(protect_history + dir_len + 1, options->format)" in targeted
assert "strchr(protect_history + dir_len + 1, '/') != NULL" in targeted
assert "wleExists(protect_history)" in targeted
assert "backup_one(source, relative, history_root, &exact" in targeted
assert "exact.verify = 1;" in targeted, "a pre-restore copy must never be skipped by fingerprint"
assert "fileXioRename" not in targeted
assert "genRemove" not in targeted
assert '"%s.PART"' in unique, "must preserve interrupted backup files"

c = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MAX_PATH 1024
#define MAX_NAME 256
typedef struct {char name[MAX_NAME];} iox_dirent_t;
static struct {char name[MAX_NAME];int live;} names[8];
static int count, index_next, removals, fail_remove, occupied_stage, fp_removals;
static void seed(int n) {
    int i;
    count=n;index_next=0;removals=0;fail_remove=0;
    for(i=0;i<n;i++) {
        snprintf(names[i].name,sizeof(names[i].name),"WLE-000%d.mcd",i+1);
        names[i].live=1;
    }
}
static int remaining(void) {
    int i,n=0;
    for(i=0;i<count;i++)n+=names[i].live;
    return n;
}
static int genDopen(const char *path) {(void)path;index_next=0;return 4;}
static int genDclose(int fd) {(void)fd;return 0;}
static int fileXioDread(int fd,iox_dirent_t *e) {
    (void)fd;
    while(index_next<count) {
        int i=index_next++;
        if (names[i].live) {
            snprintf(e->name,sizeof(e->name),"%s",names[i].name);
            return 1;
        }
    }
    return 0;
}
static int history_name_match(const char *s,int format) {
    (void)format;
    return !strncmp(s,"WLE-",4) && strstr(s,".mcd") != NULL;
}
static int join_path(char *out,size_t size,const char *a,const char *b) {
    int n=snprintf(out,size,"%s/%s",a,b);
    return n>=0 && n<(int)size;
}
static int genRemove(const char *path) {
    int i;
    const char *base=strrchr(path,'/');
    base=base?base+1:path;
    if(fail_remove)return -1;
    if(strstr(base,".mcd.FP")){fp_removals++;return -1;} /* sidecar: may be absent */
    for(i=0;i<count;i++) {
        if(names[i].live && !strcmp(names[i].name,base)) {
            names[i].live=0;removals++;return 0;
        }
    }
    return -1;
}
static void backup_stamp(char *out,size_t size) {
    snprintf(out,size,"20261012-001122");
}
static const char *backup_ext(int format) {(void)format;return "mcd";}
static int wleExists(const char *p) {
    return occupied_stage && strstr(p,"WLE-20261012-001122.mcd.PART") != NULL;
}
'''
c += sidecar + retention + unique
c += r'''
int main(void) {
    char name[256],path[1024];
    seed(3);
    assert(prune_history("mass0:/history",1,0,"WLE-0001.mcd","WLE-0003.mcd")==0);
    assert(remaining()==2 && names[0].live && !names[1].live && names[2].live);
    assert(removals==1);

    seed(2);
    assert(prune_history("mass0:/history",1,0,"WLE-0001.mcd","WLE-0002.mcd")==0);
    assert(remaining()==2 && removals==0); /* Safety beats keep=1. */

    seed(3);
    fp_removals=0;
    assert(prune_history("mass0:/history",1,0,NULL,"WLE-0003.mcd")==0);
    assert(remaining()==1 && names[2].live && removals==2);
    assert(fp_removals==2); /* each pruned copy's fingerprint sidecar goes too */

    seed(3);
    assert(prune_history("mass0:/history",0,0,NULL,"WLE-0003.mcd")==0);
    assert(remaining()==3 && removals==0);

    seed(3);
    fail_remove=1;
    assert(prune_history("mass0:/history",1,0,"WLE-0001.mcd","WLE-0003.mcd")==-1);
    assert(remaining()==3 && removals==0);

    occupied_stage=1;
    assert(unique_history_path("mass0:/history",0,name,sizeof(name),path,sizeof(path)));
    assert(!strcmp(name,"WLE-20261012-001122-01.mcd"));
    occupied_stage=0;
    assert(unique_history_path("mass0:/history",0,name,sizeof(name),path,sizeof(path)));
    assert(!strcmp(name,"WLE-20261012-001122.mcd"));
    assert(!unique_history_path("mass0:/history",0,name,4,path,sizeof(path)));

    puts("PASS: protected old and new MMCE snapshots are retained and staged names are preserved");
    return 0;
}
'''
compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler is required"
with tempfile.TemporaryDirectory(prefix="mmce-targeted-") as temp:
    source = Path(temp) / "targeted.c"
    exe = Path(temp) / "targeted"
    source.write_text(c, encoding="utf-8")
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                    str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

print("PASS: targeted MMCE snapshots preserve restore sources and staging")
