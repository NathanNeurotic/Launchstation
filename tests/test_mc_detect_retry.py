"""A PS1 card that fails detection while it settles is retried (saildot4k f4605ad9).

Runs the real readMC() and waitMemoryCardReadyForBrowse() from src/filer.c against
scripted libmc results: detect errors then success, persistent detect failure, a
terminal format error, and a failed mcSync that must not reuse a stale result."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
filer = (root / "src/filer.c").read_text(encoding="utf-8")
retries = int(re.search(r"#define MC_BROWSER_RETRY_COUNT (\d+)", filer).group(1))


def function(signature):
    start = filer.index(signature)
    end = filer.index("{", start) + 1
    depth = 1
    while depth:
        depth += (filer[end] == "{") - (filer[end] == "}")
        end += 1
    return filer[start:end] + "\n"


code = r"""
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define NULL_ ((void *)0)
#define MAX_ENTRY 64
#define MAX_PATH 256
#define MAX_NAME 256
#define MC_BROWSER_RETRY_COUNT %(retries)d
#define MC_BROWSER_RETRY_DELAY_US 100000
#define sceMcResSucceed 0
#define sceMcResChangedCard -1
#define sceMcResNoFormat -2
#define sceMcResNoEntry -4
#define sceMcResFailDetect -12
#define sceMcResFailDetect2 -13
#define sceMcFileAttrSubdir 0x0020
typedef unsigned char u8;typedef unsigned short u16;typedef unsigned int u32;
typedef struct {u8 Resv2,Sec,Min,Hour,Day,Month;u16 Year;} sceMcStDateTime;
typedef struct {sceMcStDateTime _Create,_Modify;u32 FileSizeByte;u16 AttrFile,Reserve1;u32 Reserve2,PdaAplNo;u8 EntryName[32];}
 __attribute__((aligned(64))) sceMcTblGetDir;
typedef struct {char name[MAX_NAME];unsigned char title[66];sceMcTblGetDir stats;} FILEINFO;
static int mctype_PSx=1,time_valid,size_valid,dir_result=1,delays,getdirs,getinfos;
/* Each mcSync takes the next scripted (completed, result) pair. */
static int script_done[16],script_res[16],script_n,script_i;
static void script(int n,const int *done,const int *res){script_n=n;script_i=0;
 memcpy(script_done,done,n*sizeof(int));memcpy(script_res,res,n*sizeof(int));
 delays=getdirs=getinfos=0;dir_result=1;}
static int mcSync(int mode,int *cmd,int *result){(void)mode;(void)cmd;
 if(!result)return 1; /* the drain before mcGetInfo */
 assert(script_i<script_n);int k=script_i++;
 if(script_done[k])*result=script_res[k];
 else *result=12345; /* stale value: must never be used */
 return script_done[k];}
static int mcGetInfo(int port,int slot,int *type,int *free_,int *format){(void)port;(void)slot;(void)type;(void)free_;(void)format;getinfos++;return 0;}
static int mcGetDir(int port,int slot,const char *name,unsigned mode,int maxent,sceMcTblGetDir *table){
 (void)port;(void)slot;(void)name;(void)mode;(void)maxent;getdirs++;
 memset(&table[0],0,sizeof(table[0]));strcpy((char *)table[0].EntryName,"SAVE");return 0;}
static void DelayThread(int us){(void)us;delays++;}
static int filerPrepareMemoryCardRead(int port){(void)port;return sceMcResSucceed;}
static void dirOpenResult(int r){dir_result=r;}
%(wait)s
%(read)s
int main(void){
 FILEINFO info[4];int n;
 /* 1: a detect failure, then the card answers: listed on the first open */
 {int d[]={1,1,1},r[]={sceMcResFailDetect,sceMcResSucceed,1};script(3,d,r);}
 n=readMC("mc0:/",info,4);
 assert(n==1&&!strcmp(info[0].name,"SAVE")&&dir_result==0&&getdirs==2&&delays==1);
 /* 2: the second detect code is retried the same way */
 {int d[]={1,1,1},r[]={sceMcResFailDetect2,sceMcResSucceed,1};script(3,d,r);}
 n=readMC("mc1:/",info,4);
 assert(n==1&&dir_result==0&&getdirs==2);
 /* 3: no card at all: bounded, fails as before */
 {int d[16],r[16],i,k=0;for(i=0;i<MC_BROWSER_RETRY_COUNT;i++){d[k]=1;r[k++]=sceMcResFailDetect;
  if(i+1<MC_BROWSER_RETRY_COUNT){d[k]=1;r[k++]=sceMcResSucceed;}}script(k,d,r);}
 n=readMC("mc0:/",info,4);
 assert(n==0&&dir_result==-EIO&&getdirs==MC_BROWSER_RETRY_COUNT&&delays==MC_BROWSER_RETRY_COUNT-1);
 assert(script_i==script_n);
 /* 4: an unformatted card is final: one read, no wait */
 {int d[]={1},r[]={sceMcResNoFormat};script(1,d,r);}
 n=readMC("mc0:/",info,4);
 assert(n==0&&dir_result==-EIO&&getdirs==1&&delays==0&&getinfos==0);
 /* 5: after a changed card, a readiness mcSync that never completes reports a
  * detect failure, is retried, and stops at the bound without reusing 12345 */
 {int d[16],r[16],i,k=0;d[k]=1;r[k++]=sceMcResChangedCard;
  for(i=0;i<MC_BROWSER_RETRY_COUNT;i++){d[k]=0;r[k++]=0;}script(k,d,r);}
 n=readMC("mc0:/",info,4);
 assert(n==0&&dir_result==-EIO&&getdirs==1&&getinfos==MC_BROWSER_RETRY_COUNT);
 assert(delays==MC_BROWSER_RETRY_COUNT-1);
 /* 6: waitMemoryCardReadyForBrowse alone: detect, then ready */
 {int d[]={1,1},r[]={sceMcResFailDetect2,sceMcResSucceed};script(2,d,r);}
 assert(waitMemoryCardReadyForBrowse(0)==sceMcResSucceed&&getinfos==2&&delays==1);
 /* 7: a permission/format error from mcGetInfo is final */
 {int d[]={1},r[]={sceMcResNoFormat};script(1,d,r);}
 assert(waitMemoryCardReadyForBrowse(0)==sceMcResNoFormat&&getinfos==1&&delays==0);
 return 0;
}
""" % {
    "retries": retries,
    "wait": function("static int waitMemoryCardReadyForBrowse(int port)\n{"),
    "read": function("int readMC(const char *path, FILEINFO *info, int max)\n{"),
}

with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / "t.c"
    src.write_text(code, encoding="utf-8")
    subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Werror", "-Wno-unused-function",
                    "-fsanitize=address,undefined", str(src), "-o", str(Path(tmp) / "t")], check=True)
    subprocess.run([str(Path(tmp) / "t")], check=True)

print("PASS: PS1/PS2 card detect failures retry within the bound; format errors and stale mcSync results do not")
