"""Compile and fault-inject Launchstation's stock-firmware MMCE publish policy.

Do not assume that sd2psXtd supports rename. A failed/ambiguous rename must
never trigger a destructive copy over an existing destination.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_browser.c").read_text(encoding="utf-8")
start = source.index("static int mmcePublishInactiveSnapshot(")
at = source.index("{", start) + 1
depth = 1
while depth:
    depth += (source[at] == "{") - (source[at] == "}")
    at += 1
prod = source[start:at]

assert "mmcePublishInactiveSnapshot(stage, dest)" in source
assert "fileXioRename(stage, dest)" in prod
assert "wleExists(dest) || !wleExists(stage)" in prod
assert "mcCardBackupNoEccNewFile(stage, dest, cardExportProgress)" in prod
assert "mcCardValidateNoEcc(dest)" in prod
assert prod.index("wleExists(dest) || !wleExists(stage)") < prod.index("mcCardBackupNoEccNewFile(")
assert "genRemove(remove_path)" in prod

h = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#define MAX_PATH 1024
#define MC_CARD_EXPORT_OK 0
#define cardExportProgress NULL
static int stage_present,dest_present,renamed,copied,deleted,valid,fail_copy,ambiguous,checks;
static int is_stage(const char *p){return strstr(p,".PART")!=NULL;}
static int wleExists(const char *p){checks++;return is_stage(p)?stage_present:dest_present;}
static int fileXioRename(const char *a,const char *b){
    assert(is_stage(a)&&!is_stage(b));renamed++;
    if(ambiguous){dest_present=1;return -1;}
    if(valid==11){stage_present=0;return -1;} /* uncertain source loss */
    if(valid==12){dest_present=1;stage_present=0;return 0;}
    return -1;
}
static int mcCardValidateNoEcc(const char *p){
    assert(!is_stage(p));return valid==0||valid==12?0:-1;
}
static int mcCardBackupNoEccNewFile(const char *a,const char *b,void *progress){
    (void)progress;assert(is_stage(a)&&!is_stage(b));
    assert(stage_present && !dest_present);
    copied++;dest_present=1;return fail_copy ? -1 : 0;
}
static void dashStrCopy(char *out,const char *in,size_t size){
    snprintf(out,size,"%s",in);
}
static int genRemove(char *path){
    assert(is_stage(path));deleted++;stage_present=0;return 0;
}
'''
h += "\n"+prod+"\n"
h += r'''
static void reset(void){
 stage_present=1;dest_present=0;renamed=copied=deleted=checks=fail_copy=ambiguous=valid=0;
}
int main(void){
 const char *stage="mmce0:/WLE-Recovered/RECOVER-001.mcd.PART";
 const char *dest="mmce0:/WLE-Recovered/RECOVER-001.mcd";
 reset();valid=12;
 assert(mmcePublishInactiveSnapshot(stage,dest));
 assert(renamed==1 && !copied && dest_present && !stage_present);

 reset();
 assert(mmcePublishInactiveSnapshot(stage,dest));
 assert(renamed==1 && copied==1 && deleted==1 && dest_present && !stage_present);

 reset();dest_present=1;
 assert(!mmcePublishInactiveSnapshot(stage,dest));
 assert(!renamed && !copied && !deleted && stage_present);

 reset();ambiguous=1;
 assert(!mmcePublishInactiveSnapshot(stage,dest));
 assert(renamed==1 && !copied && !deleted);

 reset();valid=11;
 assert(!mmcePublishInactiveSnapshot(stage,dest));
 assert(renamed==1 && !copied && !deleted);

 reset();fail_copy=1;
 assert(!mmcePublishInactiveSnapshot(stage,dest));
 assert(renamed==1 && copied==1 && !deleted);

 reset();valid=4;
 assert(!mmcePublishInactiveSnapshot(stage,dest));
 assert(renamed==1 && copied==1 && !deleted);

 reset();
 assert(!mmcePublishInactiveSnapshot(NULL,dest));
 assert(!mmcePublishInactiveSnapshot(stage,NULL));
 assert(!renamed && !copied);
 puts("PASS: stock MMCE publish tolerates missing rename and fails closed on ambiguity");
}
'''
cc=shutil.which("gcc") or shutil.which("clang")
assert cc, "A host C compiler is required"
with tempfile.TemporaryDirectory(prefix="mmce-rename-") as tmp:
    src=Path(tmp)/"publish.c"
    exe=Path(tmp)/"publish"
    src.write_text(h,encoding="utf-8")
    subprocess.run([cc,"-std=c99","-Wall","-Wextra","-Werror",str(src),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
