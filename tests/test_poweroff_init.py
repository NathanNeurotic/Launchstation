"""Execute demand-only poweroff setup, including module and RPC failures."""
from pathlib import Path
import shutil
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/init.c').read_text()
def function(sig):
 a=source.index(sig);b=source.index('{',a)+1;d=1
 while d:d+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[a:b]+'\n'
prod=function('void setupPowerOff(void)\n{')+function('int isPowerOffReady(')
assert 'load_ps2dev9(' not in prod
code=r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#define DPRINTF(...) ((void)0)
static int have_poweroff,done_setupPowerOff,module_id=3,module_result,rpc_result;
static int module_calls,rpc_calls,callbacks;
static char poweroff_irx[1];static int size_poweroff_irx=1;
static void poweroffHandler(int i){(void)i;}
static int SifExecModuleBuffer(const void *b,int s,int len,void *a,int *r){
 assert(b==poweroff_irx&&s==1&&!len&&!a);module_calls++;*r=module_result;return module_id;
}
static int poweroffInit(void){rpc_calls++;return rpc_result;}
static void poweroffSetCallback(void *fn,void *arg){assert(fn&&!arg);callbacks++;}
'''+prod+r'''
int main(void){
 module_id=-1;setupPowerOff();assert(!have_poweroff&&!isPowerOffReady()&&!rpc_calls&&!callbacks);
 module_id=3;module_result=-1;setupPowerOff();assert(!have_poweroff&&!isPowerOffReady()&&!rpc_calls);
 module_result=0;rpc_result=-1;setupPowerOff();assert(have_poweroff&&!isPowerOffReady()&&rpc_calls==1&&!callbacks);
 rpc_result=0;setupPowerOff();assert(isPowerOffReady()&&callbacks==1&&module_calls==3&&rpc_calls==2);
 setupPowerOff();assert(callbacks==1&&module_calls==3&&rpc_calls==2);
 puts("PASS: power-off loads only its service, rejects failed module/RPC starts and retries safely");
}
'''
actions=(root/'src/main_actions.c').read_text();a=actions.index('} else if (!stricmp(path, setting->Misc_PS2PowerOff))');b=actions.index('} else if',a+10)
assert actions[a:b].index('if (!isPowerOffReady())')<actions[a:b].index('closeAllAndPoweroff();')
with tempfile.TemporaryDirectory(prefix='poweroff-init-') as folder:
 p=Path(folder);(p/'test.c').write_text(code)
 subprocess.run([shutil.which('gcc'),'-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
