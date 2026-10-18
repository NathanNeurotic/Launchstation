"""Host regression for the selective Oct 2026 wLaunchELF_R3Z backport.

Runs the production comparator and SJIS-aware title joiner (not a Python
reimplementation) and checks UI gating, translations, and preserved devices.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
sort = (root / "src/gui_sort.c").read_text()
copy = (root / "src/filer_copy.c").read_text()
browser = (root / "src/filer_browser.c").read_text()
actions = (root / "src/filer_actions.c").read_text()
dash = (root / "src/dash_browser.c").read_text()
header = (root / "include/launchelf.h").read_text()
shared = (root / "include/filer_shared.h").read_text()

def c_function(text, prefix):
    start = text.index(prefix)
    first = text.index("{", start)
    depth = 1
    pos = first + 1
    while depth:
        depth += (text[pos] == "{") - (text[pos] == "}")
        pos += 1
    return text[start:pos]

comparator = c_function(sort, "int cmpFile(")
join = c_function(copy, "static void joinIconSysTitleLines(")
test = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
typedef uint64_t u64;
#define TRUE 1
#define FALSE 0
#define sceMcFileAttrSubdir 0x20
typedef struct { unsigned int AttrFile; u64 _Modify; } Stats;
typedef struct { char name[128]; unsigned char title[66]; Stats stats; } FILEINFO;
int file_sort=1, file_sort_descending=0, file_show=1, time_valid=1;
static int genCmpFileExt(const char *name,const char *ext) {
    const char *dot=strrchr(name,'.'); return dot && strcmp(dot+1,ext)==0;
}
'''
# The comparator now uses the production four-mode predicates; host harness
# must compile their real implementation rather than a reimplemented stub.
start = shared.index("enum {\n\tFILE_SHOW_NAME = 0")
show_helpers = shared[start:shared.index("extern int file_sort;", start)]
test += show_helpers + "\n"
test += comparator + "\n" + join
test += r'''
int main(void) {
    FILEINFO a={0}, b={0};
    strcpy(a.name,"A.ELF"); strcpy(b.name,"B.ELF");
    assert(cmpFile(&a,&b)<0);
    file_sort_descending=1;
    assert(cmpFile(&a,&b)>0);
    b.stats.AttrFile=sceMcFileAttrSubdir;
    assert(cmpFile(&a,&b)>0); /* folder stays first even descending */
    b.stats.AttrFile=0;
    strcpy(b.name,"B.TXT");
    assert(cmpFile(&a,&b)<0); /* ELF stays first even descending */

    /* Compare entries within the same file class for sort ordering. */
    strcpy(a.name,"A.TXT");
    file_sort=3; file_show=1; file_sort_descending=0;
    a.stats._Modify=100; b.stats._Modify=200;
    assert(cmpFile(&a,&b)<0); /* oldest first */
    file_sort_descending=1;
    assert(cmpFile(&a,&b)>0); /* newest first */

    file_sort=2; file_sort_descending=0;
    strcpy((char *)a.title,"ABC"); strcpy((char *)b.title,"XYZ");
    assert(cmpFile(&a,&b)<0);
    file_sort_descending=1;
    assert(cmpFile(&a,&b)>0);

    unsigned char title[66]={0};
    strcpy((char*)title, "HelloWorld");
    joinIconSysTitleLines(title,5);
    assert(!strcmp((char*)title,"Hello World"));
    joinIconSysTitleLines(title,5);
    assert(!strcmp((char*)title,"Hello World"));
    strcpy((char*)title,"Hello World");
    joinIconSysTitleLines(title,6);
    assert(!strcmp((char*)title,"Hello World"));
    title[0]=0x81;title[1]=0x40;
    memcpy(title+2,"Start",6);
    joinIconSysTitleLines(title,1); /* midway through Shift-JIS character */
    assert(title[0]==0x81 && title[1]==0x40 && title[2]=='S');
    memset(title,'X',64);title[64]=0;
    joinIconSysTitleLines(title,32);
    assert(strlen((char*)title)==65 && title[65]==0);
    puts("PASS: native sort direction, folder/ELF grouping, SJIS/PSU title join");
    return 0;
}
'''
assert "unsigned char title[32 * 2 + 2]" in header
assert "extern int file_sort_descending;" in shared
assert "file_sort_descending = (file_sort == 1) && !file_sort_descending;" in browser
assert "file_sort_descending = (file_sort == 2) && !file_sort_descending;" in browser
assert "file_sort_descending = (file_sort == 3) && !file_sort_descending;" in browser
assert "file_sort_descending != entry_file_sort_descending" in browser
assert "joinIconSysTitleLines(out, title_line_break);" in copy
assert 'memcmp(icon_header, "PS2D", 4)' in copy
assert 'title_line_break = icon_header[6] | (icon_header[7] << 8)' in copy
assert "TIMESTAMP_ORGANIZE_TUNA" in actions
assert "isMemoryCardRootPath(path) && filerCanSetCustomTimestamp(path, file)" in actions
assert "if (mode == TIMESTAMP_ORGANIZE_TUNA)" in actions
assert "count = allow_tuna ? TIMESTAMP_ORGANIZE_COUNT : TIMESTAMP_ORGANIZE_TUNA" in actions
assert "F_TUNA" not in dash and "en[TIMEMANIP]" not in dash
assert "F_DATES" in dash and "opTimestamps();" in dash
for path in sorted((root / "Lang").glob("*.LNG")):
    s = path.read_text()
    ids = [int(n) for n in re.findall(r"^lang\((\d+),",s,re.M)]
    assert len(ids)==len(set(ids)), f"duplicate translation ID in {path}"
    assert 'lang(466, Old_to_New,' in s and 'lang(467, New_to_Old,' in s
    assert 'lang(453, PC_Recv_Progress,' in s, f"PC protocol l10n changed in {path}"

with tempfile.TemporaryDirectory(prefix="r3z-oct15-") as td:
    src=Path(td)/"test.c"
    exe=Path(td)/"test"
    src.write_text(test)
    subprocess.run(["cc","-std=gnu11","-Wall","-Wextra","-Werror",
                    str(src),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print("PASS: native browser wiring, Tuna gating, no localization ID collision")
