"""Run production Move bookkeeping against files; no recursive delete fixture."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/filer_copy.c').read_text()
manifest = source[source.index('#define MOVE_MANIFEST_LIMIT'):source.index('static char *psu_restore_buff;')]
code = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
typedef uint8_t u8;typedef uint32_t u32;
#define MAX_PATH 1024
#define FIO_S_ISDIR S_ISDIR
typedef struct {u32 mode,size,hisize;u8 ctime[8],mtime[8];} iox_stat_t;
static int fail_alloc,fail_delete,deleted,add_late;
static size_t peak;
static const char *stage;
static void filerCopyDiagStage(const char *s){stage=s;}
static int genFixPath(const char *p,char *out){strcpy(out,p);return 0;}
static int genGetStat(const char *p,iox_stat_t *out){
 struct stat s;if(stat(p,&s))return -errno;
 memset(out,0,sizeof(*out));out->mode=s.st_mode;out->size=s.st_size;out->hisize=(uint64_t)s.st_size>>32;
 int64_t t=s.st_ctime;memcpy(out->ctime,&t,8);t=s.st_mtime;memcpy(out->mtime,&t,8);return 0;
}
static void put(const char *p){int fd=open(p,O_CREAT|O_TRUNC|O_WRONLY,0600);assert(fd>=0);assert(write(fd,"data",4)==4);assert(!close(fd));}
static void *test_realloc(void *p,size_t n){if(fail_alloc)return NULL;if(n>peak)peak=n;return realloc(p,n);}
#define realloc test_realloc
static int genRemove(char *p){if(fail_delete)return -EACCES;deleted++;return unlink(p)?-errno:0;}
static int genRmdir(char *p){if(add_late){put("folder/LATE");add_late=0;}return rmdir(p)?-errno:0;}
static void ensureMemoryCardPortAccessible(int p){(void)p;assert(0);}
static int mcDelete(int p,int s,const char *n){(void)p;(void)s;(void)n;assert(0);return -EIO;}
static int mcSync(int m,void *c,int *r){(void)m;(void)c;(void)r;assert(0);return -EIO;}
'''+manifest+r'''
int main(void){
 /* Copy errors never call Finish; discard does not remove anything. */
 put("A");filerMoveBegin(1);assert(!moveRecord("","A"));filerMoveEnd();assert(!access("A",F_OK)&&!deleted);
 /* Unprovable EOF keeps even confirmed files, before the first deletion. */
 filerMoveBegin(1);assert(!moveRecord("","A"));move_manifest.error=-ENOTSUP;
 assert(filerMoveFinish()==-ENOTSUP&&!deleted&&!access("A",F_OK));filerMoveEnd();
 /* Preflight covers the whole selection: a changed later file keeps A too. */
 put("B");filerMoveBegin(1);assert(!moveRecord("","A")&&!moveRecord("","B"));
 int fd=open("B",O_WRONLY|O_APPEND);assert(fd>=0&&write(fd,"!",1)==1);close(fd);
 assert(filerMoveFinish()==-EIO&&!deleted&&!access("A",F_OK));filerMoveEnd();
 /* Known copies move, while kept entries can be rewound out of the manifest. */
 filerMoveBegin(1);assert(!moveRecord("","A"));unsigned mark=filerMoveMark();assert(!moveRecord("","B"));
 filerMoveRewind(mark);assert(!filerMoveFinish()&&access("A",F_OK)&&!access("B",F_OK));filerMoveEnd();
 /* A new/unseen child is never discovered by a deletion traversal. */
 assert(!mkdir("folder",0700));put("folder/KNOWN");filerMoveBegin(1);
 assert(!moveRecord("","folder")&&!moveRecord("folder/","KNOWN"));add_late=1;
 assert(filerMoveFinish()==-ENOTEMPTY&&!access("folder/LATE",F_OK)&&access("folder/KNOWN",F_OK));filerMoveEnd();
 assert(!unlink("folder/LATE")&&!rmdir("folder"));
 /* Empty and nested directories are removed in postorder. */
 assert(!mkdir("folder",0700)&&!mkdir("folder/empty",0700));put("folder/KNOWN");
 filerMoveBegin(1);assert(!moveRecord("","folder")&&!moveRecord("folder/","empty")&&!moveRecord("folder/","KNOWN"));
 assert(!filerMoveFinish()&&access("folder",F_OK));filerMoveEnd();
 /* Allocation and cap failures do not begin cleanup. */
 filerMoveBegin(1);fail_alloc=1;assert(moveRecord("","B")==-ENOMEM&&!move_manifest.used);fail_alloc=0;filerMoveEnd();
 filerMoveBegin(1);int ret;while(!(ret=moveRecord("","B")));assert(ret==-ENOMEM&&peak==MOVE_MANIFEST_LIMIT);
 assert(!access("B",F_OK));filerMoveEnd();
 /* Deletion failures keep the entry and return the actual error. */
 filerMoveBegin(1);assert(!moveRecord("","B"));fail_delete=1;
 assert(filerMoveFinish()==-EACCES&&!access("B",F_OK));filerMoveEnd();
 assert(!move_manifest.data&&!move_manifest.enabled);
 printf("PASS: Move preflight, unknown EOF, changed/unseen files, postorder, kept entries, failure cleanup; peak manifest %zu bytes\n",peak);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='move-manifest-') as folder:
    p = Path(folder)
    (p / 'test.c').write_text(code)
    subprocess.run([shutil.which('gcc') or 'gcc', '-std=gnu99', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(p / 'test.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], cwd=p, check=True, timeout=30)
