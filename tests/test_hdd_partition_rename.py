"""Real APA rename handler and HDD Manager rename/visibility actions."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    depth = 0
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', source[start:], re.S):
        depth += (token.group() == "{") - (token.group() == "}")
        if not depth and token.group() == "}":
            return source[start:start + token.end()] + "\n"
    raise AssertionError(signature)


driver = (root / "iop/ps2hdd_osd/hdd_fio.c").read_text()
apa = (root / "iop/ps2hdd_osd/apa.c").read_text()
code = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libapa.h"
typedef struct {int unit;} iomanX_iop_file_t;
#include "hdd.h"
#define APA_IO_MODE_READ 0
static apa_header_t headers[3];
static apa_cache_t cache[3];
static hdd_file_slot_t slots[4],*hddFileSlots=slots;
static int apaMaxOpen=4,fioSema=7,locked,waits,signals,flushes,read_error,flush_error;
static int WaitSema(int id){assert(id==7&&!locked);locked=1;waits++;return 0;}
static int SignalSema(int id){assert(id==7&&locked);locked=0;signals++;return 0;}
int apaGetTime(apa_ps2time_t*t){memset(t,0,sizeof(*t));t->year=2026;return 0;}
apa_cache_t *apaCacheGetHeader(s32 dev,u32 lba,u32 mode,int*err){
 assert(dev==0&&lba%262144==0&&lba/262144<3&&!mode);if(read_error){*err=-EIO;return NULL;}
 unsigned i=lba/262144;cache[i].header=&headers[i];cache[i].device=0;cache[i].sector=lba;*err=0;return &cache[i];
}
void apaCacheFree(apa_cache_t*c){assert(c);}
int apaCacheFlushAllDirty(s32 dev){assert(dev==0&&locked);flushes++;return flush_error;}
'''
for sig in ("int apaCheckSum(", "apa_cache_t *apaGetNextHeader(", "apa_cache_t *apaFindPartition("):
    code += function(apa, sig)
for sig in ("static int apaRename(", "static int renameId(", "int hddReName("):
    code += function(driver, sig)
code += r'''
static void setup(const char *name,int collision){
 assert(!locked);memset(headers,0,sizeof(headers));memset(slots,0,sizeof(slots));memset(cache,0,sizeof(cache));
 strcpy(headers[0].id,"__mbr");headers[0].next=262144;
 memcpy(headers[1].id,name,strlen(name));headers[1].start=262144;headers[1].length=262144;headers[1].type=0x100;
 headers[1].nsub=2;headers[1].subs[0].start=8388608;headers[1].subs[0].length=16384;
 if(collision){strcpy(headers[2].id,"existing");headers[1].next=524288;headers[2].start=524288;headers[2].prev=262144;}
 flushes=read_error=flush_error=0;
}
static int renameCall(const char *old,const char *name){
 iomanX_iop_file_t f={0};int ret=hddReName(&f,old,name);assert(!locked&&waits==signals);return ret;
}
int main(void){
 char old[96],name[96];memset(old,0xa5,sizeof(old));memset(name,0x5a,sizeof(name));strcpy(old,"PP.APP");strcpy(name,"PP.APP.HIDDEN");
 setup("PP.APP",0);assert(!renameCall(old,name)&&flushes==1&&!strcmp(headers[1].id,"PP.APP.HIDDEN"));
 assert(headers[1].start==262144&&headers[1].length==262144&&headers[1].nsub==2&&headers[1].subs[0].start==8388608);
 assert(headers[1].checksum==(u32)apaCheckSum(&headers[1],1));
 assert(!renameCall("PP.APP.HIDDEN","PP.APP")&&!strcmp(headers[1].id,"PP.APP"));
 /* Exercise the entire 32-byte APA ID, including the final byte. */
 const char full[]="12345678901234567890123456789012";assert(strlen(full)==32);
 assert(!renameCall("PP.APP",full)&&!memcmp(headers[1].id,full,32));
 assert(!renameCall(full,"x")&&headers[1].id[0]=='x');for(unsigned i=1;i<32;i++)assert(!headers[1].id[i]);
 char tiny[]="x";assert(!renameCall(tiny,"short")); /* no 32-byte read past caller string */
 int before=flushes;assert(!renameCall("short","short")&&flushes==before);
 setup("PP.APP",1);assert(renameCall(old,"existing")==-EEXIST&&!flushes);
 assert(renameCall("missing","new")==-ENOENT&&!flushes);
 slots[0].f=(void*)1;strcpy(slots[0].id,"PP.APP");assert(renameCall(old,"new")==-EBUSY&&!flushes);slots[0].f=NULL;
 assert(renameCall("__mbr","new")==-EACCES&&!flushes);
 assert(renameCall(old,"__reserved")==-EACCES&&!flushes);
 const char *bad[]={"",".","..","bad/name","bad\\name","hdd0:bad","bad,name","bad\nname","123456789012345678901234567890123"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){before=waits;assert(renameCall(old,bad[i])<0&&waits==before&&!flushes);}
 read_error=1;assert(renameCall(old,"new")==-EIO&&!flushes);read_error=0;
 flush_error=-EIO;assert(renameCall(old,"new")==-EIO&&flushes==1);
 puts("PASS: real APA rename with dirty RPC padding, 32-byte IDs, no string overread, collision/busy/system protection, single lock release and disk error propagation");
}
'''

manager = (root / "src/dash_hdd.c").read_text()
ui = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_PART_NAME 32
#define MOUNT_LIMIT 4
#define HP_SYSTEM 2
#define DI_WARNING 1
#define DASH_TOAST_OK 2
#define HOP_RENAME 3
typedef struct {char name[33];int type;} HddPart;
typedef struct {char a[40],b[80];int op,ret;} HddJob;
static char mountedParty[4][256],last_name[40];static int Party_vmcIndex[4];
static int unmounts[4],refuse_unmount,submits,driver_calls,driver_error,errors,confirmed,cancel,refreshed,sel;
static char keyboard_name[64],driver_name[40];
static void unmountParty(int i){unmounts[i]++;if(!refuse_unmount)mountedParty[i][0]=0;}
static int fileXioRename(const char*old,const char*name){assert(!strncmp(old,"hdd0:",5)&&strlen(old)<=37);assert(!strncmp(name,"hdd0:",5));driver_calls++;strcpy(driver_name,name+5);return driver_error;}
static void dashStrCopy(char*out,const char*in,unsigned n){assert(strlen(in)<n);strcpy(out,in);}
static HddJob *jobNew(int op){HddJob*j=calloc(1,sizeof(*j));j->op=op;return j;}
static void hddJobFree(HddJob*j){free(j);}
static void dashToast(int kind,const char*s){assert(kind==2&&s);}
static void afterChange(void){refreshed++;}
static int findPart(const char*s){strcpy(last_name,s);return 4;}
static int dashModalKeyboard(const char*title,char*name,int max){assert(title&&max==32);if(cancel)return 0;strcpy(name,keyboard_name);return 1;}
static void dashModalMessage(int kind,const char*title,const char*body,const char*detail){assert(kind==1&&title&&body);(void)detail;errors++;}
static int dashModalConfirm(const char*title,const char*body,const char*button,int danger){assert(strstr(title,"HOSDMenu HDD Browser")&&strstr(body,"PSBBN DP entries")&&(!strcmp(button,"Hide")||!strcmp(button,"Show"))&&!danger);confirmed++;return !cancel;}
'''
ui += function(manager, "static int unmountForRename(")
a = manager.index("\t\tcase HOP_RENAME:")
b = manager.index("\t\tcase HOP_RENAME_GAME:", a)
ui += 'static int runJob(HddJob*j,const char*t,int cancelable){char in[48],out[48];assert(t&&!cancelable);submits++;switch(j->op){\n' + manager[a:b] + 'default:assert(0);}return 1;}\n'
for sig in ("static void renamePartition(", "static void actRename(", "static int partitionHidden(", "static void actVisibility("):
    ui += function(manager, sig)
ui += r'''
static void setup(void){memset(mountedParty,0,sizeof(mountedParty));for(unsigned i=0;i<4;i++){Party_vmcIndex[i]=-1;unmounts[i]=0;}refuse_unmount=submits=driver_calls=driver_error=errors=confirmed=cancel=refreshed=0;last_name[0]=0;}
int main(void){HddPart p={.name="PP.APP",.type=0};
 setup();strcpy(mountedParty[0],"hdd0:unrelated");strcpy(mountedParty[2],"hdd0:PP.APP");actVisibility(&p);
 assert(confirmed==1&&submits==1&&driver_calls==1&&refreshed==1&&sel==4&&!strcmp(last_name,"PP.APP.HIDDEN"));
 assert(unmounts[2]==1&&!unmounts[0]&&!strcmp(mountedParty[0],"hdd0:unrelated"));
 setup();strcpy(p.name,"PP.APP.HIDDEN");assert(partitionHidden(p.name));
 actVisibility(&p);assert(confirmed==1&&submits==1&&!strcmp(driver_name,"PP.APP"));
 setup();cancel=1;actVisibility(&p);assert(confirmed==1&&!submits);
 setup();strcpy(p.name,"PP.APP");strcpy(mountedParty[1],"hdd0:PP.APP");Party_vmcIndex[1]=0;actVisibility(&p);assert(errors==1&&!driver_calls&&!unmounts[1]);
 setup();strcpy(mountedParty[1],"hdd0:PP.APP");refuse_unmount=1;actVisibility(&p);assert(errors==1&&!driver_calls&&unmounts[1]==1);
 setup();driver_error=-EEXIST;actVisibility(&p);assert(errors==1&&!refreshed&&!strcmp(driver_name,"PP.APP.HIDDEN"));
 setup();strcpy(p.name,"12345678901234567890123456");actVisibility(&p);assert(errors==1&&!confirmed&&!submits);
 setup();strcpy(p.name,"1234567890123456789012345");actVisibility(&p);assert(driver_calls==1&&!strcmp(driver_name,"1234567890123456789012345.HIDDEN")&&strlen(driver_name)==32);
 assert(!partitionHidden("PP.APP.hidden")&&!partitionHidden("PP.APP.HIDDEN1"));
 setup();strcpy(p.name,"PP.APP");strcpy(keyboard_name,"12345678901234567890123456789012");actRename(&p);assert(driver_calls==1&&strlen(driver_name)==32);
 setup();strcpy(keyboard_name,"bad,name");actRename(&p);assert(errors==1&&!submits);
 setup();strcpy(p.name,"__system");strcpy(keyboard_name,"new");actRename(&p);assert(!submits);
 puts("PASS: exact rename IDs, full keyboard length, HOSDMenu hide/show, long-name refusal, retained mounts, VMC protection and useful error results");
}
'''
with tempfile.TemporaryDirectory(prefix="hdd-rename-") as tmp:
    tmp = Path(tmp)
    (tmp / "types.h").write_text("#include <stdint.h>\ntypedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef int32_t s32;typedef uint64_t u64;\n")
    (tmp / "hdd-ioctl.h").write_text("#define APA_IDMAX 32\n#define APA_PASSMAX 8\n#define APA_MAXSUB 64\n#define APA_FLAG_SUB 1\n")
    for name, text in (("driver", code), ("ui", ui)):
        (tmp / f"{name}.c").write_text(text)
        subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined",
                        "-I", str(tmp), "-I", str(root / "iop/ps2hdd_osd"), str(tmp / f"{name}.c"), "-o", str(tmp / name)], check=True)
        subprocess.run([str(tmp / name)], check=True, timeout=10)
