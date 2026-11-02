"""The classic file browser keeps its listing on the heap, one per call.

A listing of MAX_ENTRY entries is about 900 KB and the EE main thread has a
128 KB stack, so getFilePath() allocates it and browseFilePath() works in it.
The text editor that the browser opens can open another browser, so calls nest
and each needs its own listing, freed when that call returns.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
browser = (root / 'src/filer_browser.c').read_text()


def function(signature):
    a = browser.index(signature)
    b = browser.index('{', a) + 1
    depth = 1
    while depth:
        depth += (browser[b] == '{') - (browser[b] == '}')
        b += 1
    return browser[a:b] + '\n'


# The browser itself keeps no listing in its frame.
browse = function('static int browseFilePath(char *out, int cnfmode, FILEINFO *files)\n{')
assert not re.search(r'\bFILEINFO\s+\w+\s*\[', browse), 'a FILEINFO array is back in browseFilePath()'
assert re.search(r'\bFILEINFO\s+\w+\s*\[', function('static void submenu_func_GetSize(')) is None

code = r'''
#include <assert.h>
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define MAX_NAME 256
#define MAX_ENTRY 2048
typedef struct {unsigned char data[64];} __attribute__((aligned(64))) sceMcTblGetDir;
typedef struct {char name[MAX_NAME];unsigned char title[66];sceMcTblGetDir stats;} FILEINFO;
static int fail_alloc,allocations,live,calls;static void *outer;
static void *test_memalign(size_t a,size_t n){
 assert(a==64&&n==MAX_ENTRY*sizeof(FILEINFO));if(fail_alloc)return NULL;
 allocations++;live++;return memalign(a,n);
}
static void test_free(void *p){assert(p&&live>0);live--;free(p);}
#define memalign test_memalign
#define free test_free
int getFilePath(char *out, int cnfmode);
/* The browser: uses every entry; cnfmode 2 opens a nested browser, as the editor can. */
static int browseFilePath(char *out,int cnfmode,FILEINFO *files){
 calls++;assert(((uintptr_t)files&63)==0);memset(files,0x5A,MAX_ENTRY*sizeof(FILEINFO));
 if(cnfmode==2){outer=files;assert(getFilePath(out,1)==1&&live==1);assert(files[MAX_ENTRY-1].name[0]==0x5A);}
 else if(outer)assert(files!=outer);
 strcpy(out,"mc0:/BOOT/BOOT.ELF");return cnfmode==0?-1:1;
}
'''
code += function('int getFilePath(char *out, int cnfmode)\n{')
code += r'''
int main(void){
 char out[64]="unchanged";
 assert(getFilePath(out,1)==1&&!strcmp(out,"mc0:/BOOT/BOOT.ELF")&&allocations==1&&!live);
 assert(getFilePath(out,0)==-1&&allocations==2&&!live); /* left without choosing */
 assert(getFilePath(out,2)==1&&allocations==4&&calls==4&&!live); /* nested: one listing each */
 fail_alloc=1;strcpy(out,"unchanged");
 assert(getFilePath(out,1)==-1&&!strcmp(out,"unchanged")&&calls==4); /* no memory: as if left */
 puts("PASS: the classic browser lists into its own heap block per call, freed on return, and "
      "keeps no listing in its frame");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='browser-heap-') as folder:
    p = Path(folder)
    (p / 'test.c').write_text(code)
    exe = p / 'test'
    cc = shutil.which('gcc') or shutil.which('cc')
    if not cc:
        raise SystemExit('Host C compiler required')
    subprocess.run([cc, '-std=gnu99', '-Wall', '-Werror', str(p / 'test.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=20)
