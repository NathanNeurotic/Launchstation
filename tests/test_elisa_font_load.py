"""Production optional-font loader rejects incomplete files before publication."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
s=(root/'src/filer.c').read_text();a=s.index('int filerEnsureElisaFont(');b=s.index('\n}\n',a)+3
prefix=r'''
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define MAX_PATH 1025
#define FIO_O_RDONLY 1
#define FALSE 0
#define TRUE 1
static unsigned char *elisaFnt;
static int elisa_failed,size=55016,pos,chunk=11,fail_at=-1,alloc_fail,opens,closes;
static int genFixPath(const char *in,char *out){(void)in;strcpy(out,"mass0:/ELISA100.FNT");return 0;}
static int genOpen(const char *p,int flags){(void)p;(void)flags;opens++;pos=0;return 1;}
static int genLseek(int fd,int off,int how){(void)fd;(void)off;if(how==SEEK_END)return size;pos=0;return 0;}
static int genRead(int fd,void *out,int n){(void)fd;assert(elisaFnt==NULL);if(fail_at>=0 && pos>=fail_at)return 0;if(n>chunk)n=chunk;memset(out,0xA5,n);pos+=n;return n;}
static void genClose(int fd){(void)fd;closes++;}
static void *memalign(int a,size_t n){(void)a;return alloc_fail?NULL:malloc(n);}
'''
program=prefix+s[a:b]+r'''
int main(void){
 assert(filerEnsureElisaFont() && elisaFnt && pos==55016 && opens==1 && closes==1);
 assert(elisaFnt[0]==0xA5 && elisaFnt[55015]==0xA5);
 assert(filerEnsureElisaFont() && opens==1);free(elisaFnt);elisaFnt=NULL;
 fail_at=100;assert(!filerEnsureElisaFont() && !elisaFnt && elisa_failed && closes==2);
 assert(!filerEnsureElisaFont() && opens==2);
 elisa_failed=0;fail_at=-1;size=12;assert(!filerEnsureElisaFont() && !elisaFnt && closes==3);
 elisa_failed=0;size=55016;alloc_fail=1;assert(!filerEnsureElisaFont() && !elisaFnt && closes==4);
}
'''
with tempfile.TemporaryDirectory(prefix='elisa-load-') as td:
 p=Path(td);(p/'test.c').write_text(program)
 subprocess.run([shutil.which('cc') or shutil.which('gcc'),'-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)
print('PASS: complete fragmented font read, delayed publication, short file/read and allocation failure')
