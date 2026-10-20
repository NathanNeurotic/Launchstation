"""Execute the real MMCE SIO2 bus handoff with emulated register ownership."""
from pathlib import Path
import shutil
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
def function(path,sig):
 s=(root/path).read_text();a=s.index(sig);b=s.index('{',a)+1;d=1
 while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]+'\n'
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef uint32_t u32;
static u32 control,port1[4],port2[4],sio2_save_ctrl,sio2_save_port_ctrl1,sio2_save_port_ctrl2;
static int mmce_port,sio2_save_port,locked,owned,ack;
static u32 mmce_sio2_port_ctrl1=0xdead,mmce_sio2_port_ctrl2=0xbeef;
static int handler(void *a){(void)a;return 1;}
static int (*mmce_sio2_intr_handler_ptr)(void *)=handler;
static void *mmce_sio2_intr_arg_ptr;
static void sio2man_hook_sio2_lock(void){assert(!locked);locked=1;ack=0;}
static void sio2man_hook_sio2_unlock(void){assert(locked&&!owned&&ack);locked=0;}
static u32 inl_sio2_ctrl_get(void){assert(locked);return control;}
static u32 inl_sio2_portN_ctrl1_get(int p){assert(locked);return port1[p];}
static u32 inl_sio2_portN_ctrl2_get(int p){assert(locked);return port2[p];}
static void inl_sio2_portN_ctrl1_set(int p,u32 v){assert(locked&&owned);port1[p]=v;}
static void inl_sio2_portN_ctrl2_set(int p,u32 v){assert(locked&&owned);port2[p]=v;}
static void inl_sio2_ctrl_set(u32 v){assert(locked&&owned);control=v;}
static void inl_sio2_stat_set(u32 v){assert(locked&&owned&&v==3);ack=1;}
static void sio2man_hook_sio2_set_intr_handler(int (*h)(void *),void *a){
 (void)a;assert(locked);
 if(!h){assert(ack&&port1[sio2_save_port]==sio2_save_port_ctrl1&&port2[sio2_save_port]==sio2_save_port_ctrl2);}
 owned=h!=NULL;
}
'''
for sig in ('void mmce_sio2_lock()','void mmce_sio2_unlock()'):
 code+=function('iop/mmceman/src/mmce_sio2.c',sig)
code+=r'''
int main(void){
 for(int port=2;port<=3;port++)for(int timeout=0;timeout<2;timeout++){
  for(int i=0;i<4;i++){port1[i]=0x100+i;port2[i]=0x200+i;}
  mmce_port=port;control=0x31;
  mmce_sio2_lock();assert(port1[port]==0xdead&&port2[port]==0xbeef);
  control=timeout?0x3ac:0x3a1;mmce_sio2_unlock();
  assert(control==(0x31|0xc)&&!locked&&!owned);
  for(int i=0;i<4;i++)assert(port1[i]==(u32)(0x100+i)&&port2[i]==(u32)(0x200+i));
 }
 puts("PASS: both MMCE slots restore timing, IRQ and lock ownership after success or timeout");
}
'''
start=function('iop/mmceman/src/mmceman.c','int __start(')
assert 'if (!mmce_fs_register())' in start
assert start.index('if (!mmce_fs_register())') < start.index('mmce_sio2_deinit();') < start.index('return MODULE_NO_RESIDENT_END;',start.index('if (!mmce_fs_register())'))
with tempfile.TemporaryDirectory(prefix='mmce-bus-') as folder:
 p=Path(folder);(p/'test.c').write_text(code)
 subprocess.run([shutil.which('gcc'),'-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
