"""Run the production native transfer progress setter, including large files."""
from pathlib import Path
import shutil
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root/'src/dash_ui.c').read_text()
a=source.index('int DashFileProgress(')
b=source.index('void dashLoadingFrame(',a)
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t u64;
static int active=1, worker, loading_active=1, draws, loading_done,loading_total,loading_file_operation;
static float loading_fraction;
static char loading_title[64],loading_status[128],loading_remaining[64];
static u64 tick,loading_last_draw;
static int DashIsActive(void){return active;}
static int dashWorkerOnThread(void){return worker;}
static u64 Timer(void){return tick;}
static void dashStrCopy(char *a,const char *b,int cap){snprintf(a,cap,"%s",b);}
/* fake worker: a submitted job finishes after three drawn frames */
static void (*job_run)(void *);static void *job_arg;static int job_id,job_done_id,job_frames,submit_fail;
static int dashWorkerSubmit(const char *l,int f,void (*r)(void *),void (*d)(void *),void *a){(void)l;(void)f;assert(!d);if(submit_fail)return -1;assert(!job_run);job_run=r;job_arg=a;job_frames=0;return ++job_id;}
static int dashWorkerDone(int id){return job_done_id>=id;}
static void loadingDraw(void){draws++;loading_last_draw=tick;if(job_run&&++job_frames==3){job_run(job_arg);job_run=NULL;job_done_id=job_id;}}
static void chunkStep(void *a){++*(int *)a;}
static void dashFileLoadingBegin(const char *s){loading_active=1;snprintf(loading_title,sizeof(loading_title),"%s",s);}
'''+source[a:b]+r'''
int main(void){
 assert(DashFileProgress("Pasting file","BOOT.ELF",8ULL<<30,8ULL<<30));
 assert(!strcmp(loading_status,"BOOT.ELF")&&!strcmp(loading_remaining,"Remaining size: 8192.00 MB"));
 assert(loading_done==0&&loading_total==10000&&draws==1);
 tick=20;DashFileProgress("Pasting file","BOOT.ELF",4ULL<<30,8ULL<<30);
 assert(loading_done==5000&&draws==2);
 tick=21;DashFileProgress("Pasting file","BOOT.ELF",4ULL<<30,8ULL<<30);assert(draws==2);
 DashFileProgress("Pasting file","BOOT.ELF",0,8ULL<<30);assert(loading_done==10000&&draws==3);
 loading_fraction=1;DashFileProgress("Pasting file","SECOND.ELF",1024,1024);
 assert(loading_fraction==0&&loading_done==0&&!strcmp(loading_remaining,"Remaining size: 0.00 MB"));
 DashFileProgress("Pasting file","empty",0,0);assert(loading_done==10000);
 DashFileProgress("Pasting file","bad count",200,100);assert(loading_done==0);
 char long_name[256];memset(long_name,'x',sizeof(long_name)-1);long_name[255]=0;
 DashFileProgress("Pasting file",long_name,100,200);loading_fraction=0.5f;
 DashFileProgress("Pasting file",long_name,50,200);assert(loading_fraction==0.5f);
 int old=draws;worker=1;assert(!DashFileProgress("Pasting file","unsafe",0,0));
 DashFileDeleteProgress("unsafe");assert(draws==old);worker=0;
 active=0;assert(!DashFileProgress("Pasting file","classic",0,0));active=1;
 tick+=20;DashFileDeleteProgress("nested.bin");
 assert(!strcmp(loading_title,"Deleting file")&&!strcmp(loading_status,"nested.bin"));
 assert(!loading_remaining[0]&&!loading_total&&!loading_done&&loading_fraction==0);
 loading_total=10;loading_done=3;DashFileDeleteProgress("another.bin");
 assert(loading_total==10&&loading_done==3);
 /* a transfer step runs on the worker while the overlay keeps drawing */
 int ran=0;loading_file_operation=1;old=draws;
 assert(DashFileTransferChunk(chunkStep,&ran)==1&&ran==1&&draws==old+3);
 submit_fail=1;assert(!DashFileTransferChunk(chunkStep,&ran)&&ran==1);submit_fail=0;
 worker=1;assert(!DashFileTransferChunk(chunkStep,&ran));worker=0;
 active=0;assert(!DashFileTransferChunk(chunkStep,&ran));active=1;
 loading_active=0;assert(!DashFileTransferChunk(chunkStep,&ran));loading_active=1;
 assert(ran==1&&draws==old+3);
 puts("PASS: MB units, 64-bit sizes, bounded progress, throttling, per-file reset, thread ownership and worker chunks");
}
'''
# Renderer uses the scene tick once and bounds its fill to the track.
renderer=source[source.index('static void loadingDraw('):source.index('void dashLoadingBegin(')]
assert renderer.count('dashVisualUpdate()')==1
assert '(w - 60) * loading_fraction' in renderer and '* p + 8' not in renderer
assert 'dashTextFit(x + 84, y + 26' in renderer
copy=(root/'src/filer_copy.c').read_text()
assert 'dash_progress = DashFileProgress("Pasting file", file.name, size, original_size);' in copy
assert 'if (!dash_progress || !DashFileTransferChunk(copyChunkRun, &chunk))' in copy
# The chunk wait draws the overlay until the worker reports the job done.
chunk=source[source.index('int DashFileTransferChunk('):source.index('void DashFileDeleteProgress(')]
assert 'dashWorkerOnThread()' in chunk and '!loading_active' in chunk
assert 'while (!dashWorkerDone(id))' in chunk and 'loadingDraw();' in chunk
assert 'buffSize = 16384' not in copy
assert 'DashFileDeleteProgress(file->name)' in (root/'src/filer_actions.c').read_text()
with tempfile.TemporaryDirectory(prefix='transfer-progress-') as folder:
 p=Path(folder);(p/'test.c').write_text("#define localizeDashboardText(s) (s)\n#define localizeDashboardFormat(s) (s)\n" + code)
 subprocess.run([shutil.which('gcc'),'-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
