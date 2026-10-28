"""Compile and execute the real MMCE backup publication function with fault injection.

Tests write ordering without requiring a PS2 or touching actual memory cards.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/mmce_backup.c").read_text(encoding="utf-8")


def function(signature):
    start = source.index(signature)
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos] + "\n"


code = r"""
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define MAX_PATH 1024
#define MAX_NAME 256
#define MMCE_BACKUP_FORMAT_MCD 0
#define MMCE_BACKUP_FORMAT_PCSX2 1
#define MC_CARD_EXPORT_OK 0

typedef struct { int keep, format, verify; } MmceBackupOptions;
typedef struct { int candidates, backed_up, unchanged, failed; } MmceBackupStats;

static int previous, comparison, export_result, collision, rename_result, prune_result;
static int copies, renames, cleanups, prunes;
static int fingerprint, fp_calls, compares, sidecars;
static char last_sidecar[MAX_PATH];
static const char *expected_protect;
static char last_source[MAX_PATH], last_stage[MAX_PATH], last_published[MAX_PATH];

static int card_history_dir(const char *root, const char *relative, char *out, size_t n) {
    (void)root; (void)relative;
    return snprintf(out,n,"mass0:/history/card") < (int)n;
}
static int ensure_dir_tree(const char *path) { (void)path; return 0; }
static int newest_history(const char *dir, int format, const char *exclude,
                          char *name, size_t n) {
    (void)dir; (void)format; (void)exclude;
    if (previous > 0) snprintf(name,n,"WLE-old.mcd");
    return previous;
}
static int join_path(char *out, size_t n, const char *a, const char *b) {
    return snprintf(out,n,"%s/%s",a,b) < (int)n;
}
static int files_equal_serial(const char *a, const char *b) {
    (void)a; (void)b; compares++; return comparison;
}
static int fingerprint_matches(const char *source, const char *history) {
    (void)source;
    assert(!strcmp(history,"mass0:/history/card/WLE-old.mcd") ||
           !strcmp(history,"mass0:/history/card/WLE-old.ps2") || strstr(history,"WLE-old"));
    fp_calls++; return fingerprint;
}
static void write_sidecar(const char *history) {
    /* only committed history: never a .PART stage */
    assert(strstr(history,".PART") == NULL);
    sidecars++;
    snprintf(last_sidecar,sizeof(last_sidecar),"%s",history);
}
static int unique_history_path(const char *dir, int format, char *name,
                               size_t name_size, char *path, size_t path_size) {
    const char *ext = format == MMCE_BACKUP_FORMAT_PCSX2 ? "ps2" : "mcd";
    snprintf(name,name_size,"WLE-new.%s",ext);
    return snprintf(path,path_size,"%s/%s",dir,name) < (int)path_size;
}
static int mcCardBackupNoEcc(const char *source, const char *dest, void *progress) {
    (void)progress;
    copies++;
    snprintf(last_source,sizeof(last_source),"%s",source);
    snprintf(last_stage,sizeof(last_stage),"%s",dest);
    assert(strstr(dest,".PART") != NULL);
    return export_result;
}
static int mcCardExportPcsx2(const char *source, const char *dest, void *progress) {
    return mcCardBackupNoEcc(source,dest,progress);
}
static int genRemove(char *path) {
    assert(strstr(path,".PART") != NULL);
    cleanups++;
    return 0;
}
static int wleExists(const char *path) { (void)path; return collision; }
static int fileXioRename(const char *source, const char *dest) {
    assert(strstr(source,".PART") && !strstr(dest,".PART"));
    assert(copies == 1 && prunes == 0);
    renames++;
    snprintf(last_published,sizeof(last_published),"%s",dest);
    return rename_result;
}
static int prune_history(const char *dir, int keep, int format,
                         const char *protect_name, const char *fresh_name) {
    (void)dir; (void)keep; (void)format;
    assert(renames == 1);
    assert(fresh_name != NULL && !strncmp(fresh_name,"WLE-new.",8));
    assert((expected_protect == NULL && protect_name == NULL) ||
           (expected_protect != NULL && protect_name != NULL &&
            !strcmp(expected_protect,protect_name)));
    prunes++;
    return prune_result;
}
static void reset(void) {
    previous=0; comparison=0; export_result=0; collision=0;
    rename_result=0; prune_result=0;
    copies=renames=cleanups=prunes=0;
    fingerprint=-1; fp_calls=compares=sidecars=0; last_sidecar[0]=0;
    expected_protect=NULL;
    last_source[0]=last_stage[0]=last_published[0]=0;
}
"""
code += function("static int backup_one(")
code += r"""
int main(void) {
    MmceBackupOptions opts={10,MMCE_BACKUP_FORMAT_MCD,0};
    MmceBackupStats stats;
    int result;

    reset(); memset(&stats,0,sizeof(stats));
    expected_protect="WLE-old.mcd";
    result=backup_one("mmce0:/MemoryCards/C1.mcd","C1.mcd","mass0:/backup",&opts,expected_protect,&stats);
    assert(result==0 && stats.backed_up==1 && stats.failed==0);
    assert(copies==1 && renames==1 && prunes==1 && cleanups==0);
    assert(strstr(last_published,".mcd") && !strstr(last_published,".PART"));

    reset(); memset(&stats,0,sizeof(stats)); export_result=-1;
    result=backup_one("source","card.mcd","backup",&opts,NULL,&stats);
    assert(result<0 && stats.failed==1 && copies==1 && !renames && !prunes && cleanups==1);

    reset(); memset(&stats,0,sizeof(stats)); rename_result=-1;
    result=backup_one("source","card.mcd","backup",&opts,NULL,&stats);
    assert(result<0 && stats.failed==1 && copies==1 && renames==1 && !prunes && cleanups==1);

    reset(); memset(&stats,0,sizeof(stats)); collision=1;
    result=backup_one("source","card.mcd","backup",&opts,NULL,&stats);
    assert(result<0 && stats.failed==1 && copies==1 && !renames && !prunes && cleanups==1);

    reset(); memset(&stats,0,sizeof(stats)); previous=1; comparison=1;
    result=backup_one("source","card.mcd","backup",&opts,NULL,&stats);
    assert(result==0 && stats.unchanged==1 && !copies && !renames && !prunes);

    reset(); memset(&stats,0,sizeof(stats)); previous=1; comparison=-1;
    result=backup_one("source","card.mcd","backup",&opts,NULL,&stats);
    assert(result<0 && stats.failed==1 && !copies && !renames && !prunes);

    reset(); memset(&stats,0,sizeof(stats)); previous=-1;
    result=backup_one("source","card.mcd","backup",&opts,NULL,&stats);
    assert(result<0 && stats.failed==1 && !copies && !renames && !prunes);

    opts.format=MMCE_BACKUP_FORMAT_PCSX2;
    reset(); memset(&stats,0,sizeof(stats)); previous=1; comparison=1;
    result=backup_one("source","card.mcd","backup",&opts,NULL,&stats);
    assert(result==0 && stats.unchanged==1 && !stats.backed_up);
    assert(copies==1 && cleanups==1 && !renames && !prunes);

    reset(); memset(&stats,0,sizeof(stats)); previous=1; comparison=-1;
    result=backup_one("source","card.mcd","backup",&opts,NULL,&stats);
    assert(result<0 && stats.failed==1 && copies==1 && cleanups==1 && !renames && !prunes);

    reset(); memset(&stats,0,sizeof(stats)); previous=1; comparison=0;
    result=backup_one("source","card.mcd","backup",&opts,NULL,&stats);
    assert(result==0 && stats.backed_up==1 && !stats.failed && copies==1);
    assert(renames==1 && prunes==1 && !cleanups);
    assert(strstr(last_stage,".ps2.PART") && strstr(last_published,".ps2"));

    reset(); memset(&stats,0,sizeof(stats)); prune_result=-1;
    result=backup_one("source","card.mcd","backup",&opts,NULL,&stats);
    assert(result<0 && stats.backed_up==1 && stats.failed==1);
    assert(copies==1 && renames==1 && prunes==1); /* committed snapshot survives */

    /* Index fingerprint (IGR fast path). */
    opts.format=MMCE_BACKUP_FORMAT_MCD; opts.verify=0;
    reset(); memset(&stats,0,sizeof(stats)); previous=1; fingerprint=1;
    result=backup_one("source","card.mcd","backup",&opts,NULL,&stats);
    assert(result==0 && stats.unchanged==1 && fp_calls==1 && !compares && !copies && !renames && !prunes);

    reset(); memset(&stats,0,sizeof(stats)); previous=1; fingerprint=0;
    result=backup_one("source","card.mcd","backup",&opts,NULL,&stats);
    assert(result==0 && stats.backed_up==1 && !compares && copies==1 && renames==1 && prunes==1);
    assert(sidecars==1 && !strcmp(last_sidecar,last_published));

    reset(); memset(&stats,0,sizeof(stats)); previous=1; fingerprint=-1; comparison=1;
    result=backup_one("source","card.mcd","backup",&opts,NULL,&stats);
    assert(result==0 && stats.unchanged==1 && compares==1 && !copies);
    assert(sidecars==1 && strstr(last_sidecar,"WLE-old.mcd")); /* seeded for next time */

    reset(); memset(&stats,0,sizeof(stats)); previous=1; fingerprint=-1; comparison=-1;
    result=backup_one("source","card.mcd","backup",&opts,NULL,&stats);
    assert(result<0 && stats.failed==1 && !copies && !prunes && !sidecars);

    /* verify = 1 never trusts the fingerprint. */
    opts.verify=1;
    reset(); memset(&stats,0,sizeof(stats)); previous=1; fingerprint=1; comparison=0;
    result=backup_one("source","card.mcd","backup",&opts,NULL,&stats);
    assert(result==0 && !fp_calls && compares==1 && stats.backed_up==1 && copies==1);

    /* No previous history: nothing to compare against, and no fingerprint read. */
    opts.verify=0;
    reset(); memset(&stats,0,sizeof(stats)); previous=0; fingerprint=1;
    result=backup_one("source","card.mcd","backup",&opts,NULL,&stats);
    assert(result==0 && !fp_calls && stats.backed_up==1 && sidecars==1);

    opts.format=MMCE_BACKUP_FORMAT_PCSX2;
    reset(); memset(&stats,0,sizeof(stats)); previous=1; fingerprint=1;
    result=backup_one("source","card.mcd","backup",&opts,NULL,&stats);
    assert(result==0 && stats.unchanged==1 && !copies);

    puts("PASS: atomic MMCE backup publication and pruning fault paths");
    return 0;
}
"""
compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler is required"
with tempfile.TemporaryDirectory(prefix="mmce-publish-") as temp:
    main = Path(temp) / "publish.c"
    executable = Path(temp) / "publish"
    main.write_text(code, encoding="utf-8")
    subprocess.run([compiler,"-std=c99","-Wall","-Wextra","-Werror",
                    "-Wno-unused-parameter",str(main),"-o",str(executable)],check=True)
    subprocess.run([str(executable)],check=True)
