"""Exercise physical-card opening with transient MCSERV detection failures.

Use the real EE handshake, presence update and directory reader with serialized
RPC replies. This does not emulate a console's electrical timing. An optional
filer snapshot is an executable negative control.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'src/filer.c').read_text()


def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


code = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define MC_BROWSER_RETRY_COUNT 4
#define MC_BROWSER_RETRY_DELAY_US 100000
#define MAX_PATH 128
#define MAX_ENTRY 32
/* PS2SDK libmc-common.h error values, shared by MCMAN and MCSERV. */
#define sceMcResSucceed 0
#define sceMcResChangedCard -1
#define sceMcResNoFormat -2
#define sceMcResNoEntry -4
#define sceMcResDeniedPermit -5
#define sceMcResFailDetect -12
#define sceMcResFailDetect2 -13
#define sceMcResDeniedPS1Permit -51
#define sceMcFileAttrSubdir 0x20
typedef struct { unsigned AttrFile,FileSizeByte;char EntryName[32]; } sceMcTblGetDir;
typedef struct { char name[64];sceMcTblGetDir stats; } FILEINFO;
static int root_mc_polled[2],root_mc_present[2],root_mc_known_type[2];
static int root_mc_known_free[2],root_mc_failures[2];
static int mctype_PSx,time_valid,size_valid,open_result;
static void dirOpenResult(int n) { open_result=n; }
static void dirReadFailed(int n) { (void)n; }
static int port,info_replies[8],dir_replies[8],info_calls,dir_calls;
static int info_sync[8],pending,pending_result,pending_sync,delays,init_error,drains,dir_start_error,dir_sync=1;
static int filerPrepareMemoryCardRead(int);
static void filerRootMcPollIdle(void) { assert(!pending);drains++; }
static void ensureMemoryCardPortAccessible(int p) { assert(p==port); }
static int isPathDeviceStackLoaded(const char *path) { assert(!strcmp(path,"mc1:/"));return 1; }
static int isMemoryCardRpcReady(void) { return 1; }
static int dashWorkerOnThread(void) { return 0; }
static void ensureCoreIoStackReady(void) {}
static void DelayThread(int usec) { assert(usec==100000&&!pending);delays++; }
static int mcGetInfo(int p,int slot,int *type,void *free,void *format) {
    assert(p==port&&!slot&&!free&&!format&&!pending&&info_calls<8);
    *type=1;pending=1;pending_result=info_replies[info_calls];
    pending_sync=info_sync[info_calls++];return 0;
}
static int mcSync(int mode,void *command,int *out) {
    assert(!mode&&!command);
    if(!pending)return -1;
    pending=0;if(out&&pending_sync>0)*out=pending_result;
    return pending_sync;
}
static int mcGetDir(int p,int slot,const char *path,int flags,int max,sceMcTblGetDir *out) {
    assert(p==port&&!slot&&!strcmp(path,"/*")&&!flags&&max>0&&!pending&&dir_calls<8);
    if(dir_start_error)return dir_start_error;
    pending=1;pending_result=dir_replies[dir_calls++];pending_sync=dir_sync;
    memset(out,0,sizeof(*out));strcpy(out->EntryName,"BASLUS-TEST");
    out->AttrFile=0x9417;out->FileSizeByte=16384;return 0;
}
'''
for signature in ('static int waitMemoryCardReadyForBrowse(',
                  'int filerPrepareMemoryCardRead(', 'int readMC('):
    code += function(signature)
code += r'''
static void reset(void) {
    memset(root_mc_polled,0,sizeof(root_mc_polled));
    memset(root_mc_present,0,sizeof(root_mc_present));
    memset(root_mc_known_type,0,sizeof(root_mc_known_type));
    for(int i=0;i<8;i++){info_replies[i]=0;dir_replies[i]=1;info_sync[i]=1;}
    info_calls=dir_calls=pending=delays=init_error=drains=dir_start_error=0;dir_sync=1;mctype_PSx=0;
}
static int browse(FILEINFO *files) {
    char path[8];snprintf(path,sizeof(path),"mc%d:/",port);
    return readMC(path,files,MAX_ENTRY);
}
int main(void) {
    FILEINFO files[MAX_ENTRY];
    for(port=0;port<2;port++) {
        for(int error=-13;error<=-12;error++)for(int failures=1;failures<4;failures++) {
            reset();for(int i=0;i<failures;i++)info_replies[i]=error;
            assert(browse(files)==1&&info_calls==failures+1&&dir_calls==1&&delays==failures);
            assert(!strcmp(files[0].name,"BASLUS-TEST")&&files[0].stats.FileSizeByte==16384);
            assert(root_mc_present[port]&&root_mc_polled[port]&&root_mc_known_type[port]==1);
            assert(!root_mc_present[1-port]&&!time_valid&&size_valid&&!pending);
            reset();for(int i=0;i<4;i++)info_replies[i]=error;
            assert(browse(files)==0&&info_calls==4&&!dir_calls&&delays==3);
            assert(!root_mc_present[port]&&!pending);
            reset();for(int i=0;i<failures;i++)dir_replies[i]=error;
            assert(browse(files)==1&&dir_calls==failures+1&&delays==failures&&!pending);
            reset();for(int i=0;i<4;i++)dir_replies[i]=error;
            assert(browse(files)==0&&dir_calls==4&&delays==3&&!pending);
        }
        reset();dir_start_error=sceMcResDeniedPermit;
        assert(browse(files)==0&&!dir_calls&&!pending&&open_result==-EIO);
        for(int sync=-1;sync<=0;sync++) {
            reset();dir_sync=sync;
            assert(browse(files)==0&&dir_calls==4&&open_result==-EIO&&!pending);
        }
        /* Changed-card acknowledgement and transient detection may interleave. */
        reset();info_replies[0]=-1;info_replies[1]=-12;info_replies[2]=-13;
        assert(browse(files)==1&&info_calls==4&&delays==3);
        /* Incomplete/failed sync must not reuse an earlier changed-card result. */
        for(int sync=-1;sync<=0;sync++) {
            reset();info_replies[0]=-1;
            for(int i=1;i<4;i++)info_sync[i]=sync;
            assert(browse(files)==0&&info_calls==4&&!dir_calls&&!root_mc_present[port]);
        }
        const int permanent[]={sceMcResNoFormat,sceMcResDeniedPS1Permit,sceMcResDeniedPermit};
        for(unsigned i=0;i<sizeof(permanent)/sizeof(*permanent);i++) {
            reset();info_replies[0]=permanent[i];
            assert(browse(files)==0&&info_calls==1&&!dir_calls&&!delays);
            reset();dir_replies[0]=permanent[i];
            assert(browse(files)==0&&dir_calls==1&&!delays);
            reset();dir_replies[0]=-12;info_replies[1]=permanent[i];
            assert(browse(files)==0&&dir_calls==1);
        }
    }
    puts("PASS: physical PS1 cards in both slots recover from transient info/directory detection errors; retries are bounded, failed syncs and permanent errors remain failures");
}
'''
compiler = shutil.which('gcc') or shutil.which('clang')
assert compiler, 'Host C compiler required'
with tempfile.TemporaryDirectory(prefix='ps1-card-open-') as folder:
    folder = Path(folder)
    program, binary = folder / 'test.c', folder / 'test'
    program.write_text(code)
    subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror',
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-pie", "-no-pie", str(program), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10, cwd=folder)
