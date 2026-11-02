"""Execute native browser sorting with the production text classifier."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
browser = (root / "src/dash_browser.c").read_text()
header = (root / "include/launchelf.h").read_text()


def function(source, signature):
    # Skip forward declarations.
    match = re.search(re.escape(signature) + r"[^;{]*\{", source)
    assert match, signature
    start = match.start()
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define sceMcFileAttrSubdir 0x20
typedef uint64_t u64;
typedef struct {
    char name[256], title[128];
    struct {
        unsigned AttrFile, Reserve2, FileSizeByte;
        struct { unsigned Year, Month, Day, Hour, Min, Sec; } _Modify;
    } stats;
} FILEINFO;
'''
code += function(header, "static inline unsigned char wle_ascii_tolower(")
code += function(header, "static inline int wle_stricmp(")
code += "#define stricmp wle_stricmp\n"
code += function((root / "src/filer_fileops.c").read_text(), "int genCmpFileExt(")
code += function((root / "src/main_fileops.c").read_text(), "int IsTextEditorFileType(")
for first in ("FB_SORT_STORED", "FB_TYPE_FOLDER"):
    code += re.search(r"enum \{\s+" + first + r"[\s\S]+?\n\};", browser).group() + "\n"
for name in ("fb_sort", "fb_sort_desc"):
    code += re.search(r"^static int " + name + r"(?: = \w+)?;", browser, re.M).group() + "\n"
for signature in ("static int isDir(",
                  "static u64 fbFileBytes(", "static u64 fbFileTime(",
                  "static const char *fbFileKind(", "static int fbFileType(",
                  "static int fbCompare(", "static void fbSortFiles("):
    code += function(browser, signature)
code += r'''
static int replies[2], choice_calls, refreshes;
static int dashModalChoice(const char *title,const char *const *choices,int n,int initial) {
    (void)initial;
    if(choice_calls==0) {
        assert(!strcmp(title,"Sort by") && n==FB_SORT_COUNT);
        const char *expected[]={"As stored","Name","Game title","Size","Date-Time","Kind","Type"};
        assert(n==(int)(sizeof(expected)/sizeof(expected[0])));
        for(int i=0;i<n;i++) assert(!strcmp(choices[i],expected[i]));
    } else assert(choice_calls==1 && !strcmp(title,"Sort direction") && n==2);
    return replies[choice_calls++];
}
static void refresh(void) { refreshes++; }
enum { F_SORT=1 };
static void chooseSort(void) {
    int r;
'''
for name in ("sort_names", "direction_names"):
    code += re.search(r"^\s*static const char \*const " + name + r"\[.*$", browser, re.M).group() + "\n"
start = browser.index("\t\tcase F_SORT: {")
end = browser.index("\t\tcase F_BOOKMARK:", start)
code += "switch(F_SORT) {\n" + browser[start:end] + "}\n}\n"
code += r'''
static FILEINFO entry(const char *name,int folder) {
    FILEINFO f={0};
    snprintf(f.name,sizeof(f.name),"%s",name);
    f.stats.AttrFile=folder?sceMcFileAttrSubdir:0;
    return f;
}
int main(void) {
    assert(fb_sort==FB_SORT_TYPE && !fb_sort_desc); /* Production startup defaults. */
    /* Four major groups: folders, executables, text, then everything else.
     * Text/other files group extensions before filenames, ignoring case.
     * Game files share the final group; extensionless files lead that group. */
    const char *names[]={"0-folder","alpha.elf","Zebra",
        "10.ELF","2.ELF","alpha.xlf","Zebra.kElF",
        "0.ARG","alpha.arg","Zeta.ARG","alpha.cfg","Beta.CFG","Zeta.cFg",
        "0.txt","10.txt","2.TXT","Alpha.txt","beta.TXT","Zulu.tXt","Zeta.yaml",
        "alpha.","Beta","Z-last","Alpha.BIN","Delta.bin","Zulu.bin","Alpha.cue","Zeta.CUE",
        "0.dat","beta.DAT","Zulu.dat","Alpha.exe","Echo.EXE","Beta.ISO","Zeta.iso",
        "alpha.png","Beta.PNG","0.VCD","alpha.vcd","Zulu.vcd","Zeta.zzz"};
    enum { N=sizeof(names)/sizeof(names[0]) };
    FILEINFO files[N];
    fb_sort=FB_SORT_TYPE;
    for(fb_sort_desc=0;fb_sort_desc<2;fb_sort_desc++) for(int shift=0;shift<N;shift++) {
        for(int i=0;i<N;i++) {
            int source=(N-1-i+shift)%N;
            files[i]=entry(names[source],source<3);
        }
        fbSortFiles(files,N);
        for(int i=0;i<N;i++) assert(!strcmp(files[i].name,names[i]));
        /* Comparator consistency matters when names and formats interleave. */
        for(int i=0;i<N;i++) for(int j=0;j<N;j++) {
            int ab=fbCompare(&files[i],&files[j]),ba=fbCompare(&files[j],&files[i]);
            assert((ab<0)==(ba>0) && (ab==0)==(ba==0));
        }
    }
    const char *text[]={"TXT","CHT","CFG","INI","CNF","PBT","JS","LUA","XML","TOML","YAML","YML","ARG"};
    for(unsigned i=0;i<sizeof(text)/sizeof(text[0]);i++) {
        char name[32];snprintf(name,sizeof(name),"settings.%s",text[i]);
        FILEINFO f=entry(name,0);assert(fbFileType(&f)==FB_TYPE_TEXT);
    }
    /* Keep the established Name and extension sort behavior, including
     * descending inside groups and folders remaining first. */
    FILEINFO folder=entry("z-folder",1),a=entry("alpha.ELF",0),z=entry("Zebra.cfg",0);
    for(fb_sort_desc=0;fb_sort_desc<2;fb_sort_desc++) {
        fb_sort=FB_SORT_NAME;
        assert(fbCompare(&folder,&a)<0);
        assert((fbCompare(&a,&z)<0)==!fb_sort_desc);
        fb_sort=FB_SORT_KIND;
        assert(fbCompare(&folder,&a)<0);
        assert((fbCompare(&a,&z)>0)==!fb_sort_desc);
    }
    fbSortFiles(files,0);fbSortFiles(files,1);
    /* Choosing Type after Descending must reset direction and skip its popup. */
    fb_sort=FB_SORT_NAME;fb_sort_desc=1;replies[0]=FB_SORT_TYPE;
    chooseSort();
    assert(choice_calls==1 && refreshes==1 && fb_sort==FB_SORT_TYPE && !fb_sort_desc);
    choice_calls=0;chooseSort();assert(choice_calls==1 && refreshes==1);
    choice_calls=0;replies[0]=FB_SORT_NAME;replies[1]=1;chooseSort();
    assert(choice_calls==2 && refreshes==2 && fb_sort==FB_SORT_NAME && fb_sort_desc==1);
    choice_calls=0;replies[0]=FB_SORT_TYPE;chooseSort();
    assert(choice_calls==1 && refreshes==3 && !fb_sort_desc);
    choice_calls=0;replies[0]=-1;chooseSort();
    assert(choice_calls==1 && refreshes==3 && fb_sort==FB_SORT_TYPE);
    puts("PASS: Name default, optional Type order, four Type groups, extension/name ordering, text classification, direction handling and existing sorts");
}
'''
compiler = shutil.which("gcc") or shutil.which("cc")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="launchstation-browser-sort-") as folder:
    source, binary = Path(folder) / "sort.c", Path(folder) / "sort"
    source.write_text(code)
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                    str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=15)
