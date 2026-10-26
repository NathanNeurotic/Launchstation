"""Exercise Ember's real launch function and signal ordering across USB reset."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'src/ember.c').read_text();a=source.index('int LaunchEmberGame(');b=source.index('\n}\n',a)+3
prefix=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MAX_PATH 1025
#define EMBER_NAME_MAX 180
#define POPSTARTER_OK 1
#define DPRINTF(...) ((void)0)
typedef struct {int unused;} POPSTARTER_CANDIDATE_LIST;
static struct {int disable_ember,cdrom_disable_gameid;} config,*setting=&config;
static int usb,stage,ids,signals,launches,osd,identity_ok=1,cleanup_ok=1,remount_ok=1,elf_ok=1;
static int parseEmberGamePath(const char *p,char *u,size_t un,char *g,size_t gn){(void)p;snprintf(u,un,"%s",usb?"usb0:/Ember":"mmce0:/Ember");snprintf(g,gn,"Game folder");return 1;}
static void setEmberMessage(char *m,size_t n,const char *t,const char *d){(void)d;snprintf(m,n,"%s",t);}
static int readableFile(const char *p){(void)p;return 1;}
static int appendFile(char *o,size_t n,const char *d,const char *f){snprintf(o,n,"%s/%s",d,f);return 1;}
static int validBios(const char *p){(void)p;return 1;}
static int ensureLocalEmber(const char *u,char *e,size_t n,char *m,size_t mn){(void)m;(void)mn;snprintf(e,n,"%s/ember.elf",u);return 1;}
static void initPopstarterCandidates(POPSTARTER_CANDIDATE_LIST *c){(void)c;}
static void addPopstarterCandidate(POPSTARTER_CANDIDATE_LIST *c,const char *p){(void)c;(void)p;}
static int preparePopstarterCandidateLaunch(POPSTARTER_CANDIDATE_LIST *c,char *s,size_t sn,char *f,size_t fn,char *p,size_t pn,int *kind){(void)c;snprintf(s,sn,"elf");snprintf(f,fn,"%s/ember.elf",usb?"usb0:/Ember":"mmce0:/Ember");snprintf(p,pn,"%s","");*kind=1;return elf_ok?1:-1;}
static void LaunchArgsClear(void){}
static int LaunchArgsLoadFromBuffer(const char *s,const char *g,int n,char *m,size_t mn){(void)s;(void)m;(void)mn;assert(n==11 && !strcmp(g,"Game folder"));return 1;}
static int buildEmberGameID(const char *p,char *id,size_t n){assert(stage==0 && setting && strstr(p,"Game"));snprintf(id,n,"SLUS_123.45");ids++;return identity_ok;}
static int CleanUpForEmberExec(void){assert(stage==0);if(!cleanup_ok)return 0;stage=1;setting=NULL;return 1;}
static int prepareEmberUsbHandoffIop(const char *e,const char *p,char *out,size_t n){(void)e;(void)p;assert(stage==1 && !signals);stage=2;snprintf(out,n,"mass1:/Ember/ember.elf");return remount_ok;}
static void ExecOSD(int n,void *args){(void)n;(void)args;osd++;}
static void displayRetroGemGameID(const char *id,int frames){assert(stage==(usb?2:1) && setting==NULL && !strcmp(id,"SLUS_123.45") && frames==2);signals++;}
static void RunLoaderElfKeepIOPWithArgv0(const char *f,const char *p,const char *arg,int kind){(void)p;(void)kind;assert(!strcmp(f,arg));assert(!strcmp(f,usb?"mass1:/Ember/ember.elf":"mmce0:/Ember/ember.elf"));launches++;}
'''
program=prefix+source[a:b]+r'''
int main(void){char msg[200];
 for(usb=0;usb<2;usb++)for(int disabled=0;disabled<2;disabled++)for(int id=0;id<2;id++){
  setting=&config;config.cdrom_disable_gameid=disabled;identity_ok=id;stage=ids=signals=launches=osd=0;
  assert(LaunchEmberGame("usb0:/Ember/games/Game/Game.cue",msg,sizeof(msg)));
  assert(launches==1 && ids==!disabled && signals==(!disabled && id) && !osd);
 }
 usb=1;setting=&config;config.cdrom_disable_gameid=0;identity_ok=1;stage=ids=signals=launches=osd=0;remount_ok=0;
 assert(!LaunchEmberGame("usb0:/Ember/games/Game/Game.cue",msg,sizeof(msg)) && ids==1 && !signals && !launches && osd==1);
 remount_ok=1;setting=&config;stage=ids=signals=launches=osd=0;cleanup_ok=0;
 assert(!LaunchEmberGame("usb0:/Ember/games/Game/Game.cue",msg,sizeof(msg)) && !signals && !launches && !osd);
 cleanup_ok=1;stage=ids=signals=launches=0;config.disable_ember=1;
 assert(!LaunchEmberGame("usb0:/Ember/games/Game/Game.cue",msg,sizeof(msg)) && !ids && !signals && !launches);
 config.disable_ember=0;elf_ok=0;
 assert(!LaunchEmberGame("usb0:/Ember/games/Game/Game.cue",msg,sizeof(msg)) && !ids && !signals && !launches);
}
'''
with tempfile.TemporaryDirectory(prefix='ember-retrogem-') as td:
 p=Path(td);(p/'test.c').write_text(program)
 subprocess.run([shutil.which('cc') or shutil.which('gcc'),'-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)
assert 'displayRetroGemGameID(game_id, 2)' in source
assert 'SET_GAMEID' not in source and 'setNeutrinoMmceGameID' not in source
print('PASS: Ember RetroGem preference/ID matrix, cache before cleanup, display after successful USB remount, unchanged argv and no signal on failed launch')
