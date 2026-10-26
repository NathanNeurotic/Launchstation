"""Execute actual VCD handoff: RetroGem yes, MMCE selection never applicable."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'src/popstarter.c').read_text()
a=source.index('int LaunchPopstarterVcd(');b=source.index('\n}\n',a)+3
launch=source[a:b]
assert 'setNeutrinoMmceGameID' not in source and 'SET_GAMEID' not in source
prefix=r'''
#define MMCE 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MAX_PATH 1025
#define POPSTARTER_OK 0
#define POPSTARTER_ERR_UNSUPPORTED_PATH -1
#define POPSTARTER_ERR_OPEN_POPSTARTER -2
typedef struct {int unused;} POPSTARTER_CANDIDATE_LIST;
static struct {int disable_popstarter,reboot_iop_elf_load,cdrom_disable_gameid;} config,*setting=&config;
static int stage,signals,launches,valid_id=1,prep_ok=1,valid_vcd=1,cleanup_ok=1;
static void initPopstarterCandidates(POPSTARTER_CANDIDATE_LIST *p){(void)p;}
static int prepareLaunch(const char *p,char *arg,size_t n,POPSTARTER_CANDIDATE_LIST *c){(void)c;snprintf(arg,n,"%s",p);return prep_ok?0:-1;}
static int validateVcd(const char *p){(void)p;return valid_vcd?0:-1;}
static int buildPopstarterVcdGameID(const char *p,char *id,size_t n){(void)p;snprintf(id,n,"SLUS_123.45");return valid_id;}
static int preparePopstarterCandidateLaunch(POPSTARTER_CANDIDATE_LIST *c,char *p,size_t pn,char *f,size_t fn,char *party,size_t ps,int *kind){(void)c;snprintf(p,pn,"mass0:/POPSTARTER.ELF");snprintf(f,fn,"mass0:/POPSTARTER.ELF");snprintf(party,ps,"%s","");*kind=1;return 0;}
static int isHddDevicePath(const char *p){(void)p;return 0;}
static int isHddPfsPath(const char *p){(void)p;return 0;}
static int isHddPartyPath(const char *p){(void)p;return 0;}
static void unmountAll(void){}
static void CleanUpForExec(void){assert(stage==0);stage=1;setting=NULL;}
static int CleanUpForKeepIopExec(const char *s){(void)s;stage=1;if(cleanup_ok)setting=NULL;return cleanup_ok;}
static void displayRetroGemGameID(const char *id,int frames){assert(stage==1 && !strcmp(id,"SLUS_123.45") && frames==2);signals++;}
static void RunLoaderElf(const char *f,const char *p,const char *arg,int kind,int reset){(void)f;(void)p;(void)arg;(void)kind;(void)reset;assert(stage==1);stage=2;launches++;}
static void setPopstarterMessage(char *m,size_t n,int ret,const char *p,const char *elf){(void)ret;(void)p;(void)elf;snprintf(m,n,"failed");}
'''
program=prefix+launch+r'''
int main(void){
 char msg[100];
 for(int reset=0;reset<2;reset++)for(int disabled=0;disabled<2;disabled++)for(int id=0;id<2;id++){
  setting=&config;config.reboot_iop_elf_load=reset;config.cdrom_disable_gameid=disabled;valid_id=id;
  stage=signals=launches=0;
  assert(LaunchPopstarterVcd("mass0:/game.VCD",msg,sizeof(msg))==0);
  assert(launches==1 && signals==(id && !disabled));
 }
 setting=&config;stage=signals=launches=0;config.disable_popstarter=1;
 assert(LaunchPopstarterVcd("mass0:/game.VCD",msg,sizeof(msg))<0 && !signals && !launches && !stage);
 config.disable_popstarter=0;prep_ok=0;
 assert(LaunchPopstarterVcd("mass0:/game.VCD",msg,sizeof(msg))<0 && !signals && !launches);
 prep_ok=1;valid_vcd=0;
 assert(LaunchPopstarterVcd("mass0:/game.VCD",msg,sizeof(msg))<0 && !signals && !launches);
 valid_vcd=1;config.reboot_iop_elf_load=0;cleanup_ok=0;
 assert(LaunchPopstarterVcd("mass0:/game.VCD",msg,sizeof(msg))<0 && !signals && !launches);
}
'''
with tempfile.TemporaryDirectory(prefix='pops-retrogem-') as td:
 p=Path(td);(p/'test.c').write_text(program)
 subprocess.run([shutil.which('cc') or shutil.which('gcc'),'-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)
print('PASS: POPStarter RetroGem ID/preference, reset/keep-IOP ordering, failed launch suppression; no MMCE selector')
