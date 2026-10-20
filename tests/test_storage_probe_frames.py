"""Execute the real mount retry loop with a clock and delayed mounts."""
from pathlib import Path
import shutil
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/dash_devices.c').read_text();a=source.index('static int probeRoot(');b=source.index('\n}\n',a)+3
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t u64;
static u64 tick,ready_at,previous_open;
static int frames,opens,closes,active=1,never_ready;
static u64 Timer(void){return tick;}
static int fileXioDopen(const char *p){
 assert(!strcmp(p,"mx4sio:/"));
 if(opens++) assert(tick-previous_open>=100 || tick>=2000);
 previous_open=tick;return !never_ready && tick>=ready_at?7:-1;
}
static void fileXioDclose(int fd){assert(fd==7);closes++;}
static int dashLoadingActive(void){return active;}
static void dashLoadingFrame(const char *s,int d,int t){assert(s&&!d&&!t);frames++;tick+=16;}
static void DelayThread(int us){assert(us==16000||us==1000);tick+=us/1000;}
'''+source[a:b]+r'''
static void reset(void){tick=ready_at=previous_open=0;frames=opens=closes=never_ready=0;active=1;}
int main(void){
 reset();assert(probeRoot("mx4sio:/",2000)&&opens==1&&closes==1&&!frames);
 reset();never_ready=1;assert(!probeRoot("mx4sio:/",2000));
 assert(tick>=2000&&tick<2020&&opens<23&&frames>110&&!closes);
 reset();ready_at=1700;assert(probeRoot("mx4sio:/",2000));assert(tick>=1700&&closes==1&&frames>95);
 reset();active=0;never_ready=1;assert(!probeRoot("mx4sio:/",2000)&&!frames);
 reset();never_ready=1;assert(!probeRoot("mx4sio:/",0)&&opens==1&&!frames&&!tick);
 puts("PASS: animated returned waits, preserved probe cadence, full delayed-mount window and one-shot probes");
}
'''
with tempfile.TemporaryDirectory(prefix='storage-frames-') as folder:
 p=Path(folder);(p/'test.c').write_text(code)
 subprocess.run([shutil.which('gcc'),'-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
