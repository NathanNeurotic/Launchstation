"""Production app installer UI/queue, including source-device reset and immediate feedback."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
_header = (root / "include/dash_internal.h").read_text()
icon_enum = _header[_header.index("enum DashIcon {"):_header.index("};", _header.index("enum DashIcon {")) + 2]
source = (root / 'src/dash_hdd_app.c').read_text()


def function(signature):
    start = source.index(signature)
    depth = 0
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', source[start:], re.S):
        depth += (token.group() == '{') - (token.group() == '}')
        if not depth and token.group() == '}':
            return source[start:start + token.end()] + '\n'
    raise AssertionError(signature)


code = r'''
#define _DEFAULT_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hdd_app.h"
#include "hdd_install.h"
@@ICONS@@
#define MAX_PATH 1025
typedef struct {const char *label,*value;} DashTableRow;
static int console_is_PSX,vmcMounted[2],ftp;
static int scenario,source_ready,loaded,snaps,certificates,networks,applied,reviews,releases,picks,messages,loading,frames,summary;
static char current[MAX_PATH],summary_text[200];
static void dashLoadingBegin(const char*t){assert(t&&*t);loading=1;}
static void dashLoadingFrame(const char*t,int done,int total){assert(loading&&t&&!done&&!total);frames++;}
static void dashLoadingEnd(void){loading=0;}
static void dashStrCopy(char*out,const char*in,int n){snprintf(out,n,"%s",in);}
static int dashWorkerAwaitAll(const char*t){assert(t);return 1;}
static int isFtpServerRunning(void){return ftp;}
static int isSmbServerRunning(void){return 0;}
static int dashModalConfirm(const char*t,const char*b,const char*ok,int danger){assert(t&&b&&ok&&!danger);return 1;}
static int dashPickFiles(const char*t,const char*dir,const char*ext,char***paths){
    assert(t&&!*dir&&!strcmp(ext,"PSU"));picks++;
    if(scenario==8){*paths=NULL;return 0;}
    *paths=calloc(3,sizeof(**paths));
    (*paths)[0]=strdup("udpfs:/APP_ONE.psu");(*paths)[1]=strdup("mass:/APP_TWO.psu");(*paths)[2]=strdup("ata0:/APP_THREE.psu");return 3;
}
static void dashModalMessage(int icon,const char*t,const char*b,const char*d){
    (void)icon;(void)d;assert(!loading&&t&&b);messages++;
    if(strstr(b,"apps installed")){summary++;snprintf(summary_text,sizeof(summary_text),"%s",b);}
}
static int ensurePathDeviceStackReady(const char*p){assert(loading&&frames>0&&releases==loaded);loaded++;source_ready=1;strcpy(current,p);return 1;}
static int dashHttpsCaPath(char*p,int size){assert(loading&&source_ready&&snaps==loaded);snprintf(p,size,"ca.pem");return 1;}
int hddInstallLoadCa(HddInstall*in){assert(in&&loading&&source_ready);certificates++;return scenario==4&&loaded==2?-EIO:0;}
static void dashSoundShutdown(void){assert(loading);}
int dashHddInstallPrepareStorage(HddInstall*in){assert(loading&&in->jail&&source_ready&&certificates==loaded&&snaps==loaded);networks++;source_ready=0;return 0;}
static int dashModalChoice(const char*t,const char*const*names,int n,int initial){assert(t&&names&&n==2&&!initial&&!loading);return 1;}
int hddAppChooseBoot(const SaveArchive*s,HddApp*a,unsigned i){assert(s&&a&&i==1);return 0;}
static int dashModalConfirmTable(const char*t,const DashTableRow*r,int n,const char*ok,int danger){assert(t&&r&&n==8&&ok&&!danger&&!loading);reviews++;return !(scenario==3&&loaded==2);}
static void filerSizeCacheInvalidate(const char*p){assert(p);}
void saveArchiveFree(SaveArchive*s){assert(s);}
void hddInstallRelease(HddInstall*in){assert(in&&!loading);releases++;}
static void dashSoundMaintain(void){}
static void dashBgmStart(void){}
'''
code = code.replace("@@ICONS@@", icon_enum)
code += source[source.index('typedef struct {'):source.index('static void context(')]
code += r'''
static void context(AppInstall*in,const char*op,const char*path){in->operation=op;snprintf(in->location,sizeof(in->location),"%s",path);}
static void snapshot(void*arg){
    AppInstall*in=arg;assert(loading&&source_ready&&!strcmp(current,in->source));snaps++;
    static SaveArchiveEntry files[2]={{.name="BOOT.ELF"},{.name="ALT.ELF"}};
    in->save.files=files;in->save.count=2;in->app.elf[1]=1;
    strcpy(in->app.title,"App fixture");strcpy(in->app.boot,"BOOT.ELF");in->app.elf_count=2;in->result=1;
}
static void prepare(void*arg){AppInstall*in=arg;assert(!source_ready&&networks==loaded);in->result=scenario==1&&loaded==2?-EIO:0;}
static void apply(void*arg){AppInstall*in=arg;assert(reviews==loaded);in->result=scenario==2&&loaded==2?-EIO:0;if(!in->result)applied++;}
'''
code += function('static void appLoading(')
code += r'''
static int run(AppInstall*in,const char*t,void(*work)(void*)){
    appLoading(in,t);work(in);dashLoadingEnd();return in->result>=0;
}
'''
code += function('static int installApp(') + function('void dashHddInstallApp(')
code += r'''
int main(int argc,char**argv){
    assert(argc==2);scenario=atoi(argv[1]);
    if(scenario==5)console_is_PSX=1;
    if(scenario==6)ftp=1;
    dashHddInstallApp(scenario==7?"udpfs:/APP_ONE.psu":NULL);
    assert(!loading&&releases==loaded);
    if(scenario==5||scenario==6){assert(!loaded&&!picks&&messages==1);return 0;}
    if(scenario==8){assert(picks==1&&!loaded);return 0;}
    if(scenario==7){assert(!picks&&loaded==1&&applied==1&&!summary);return 0;}
    assert(summary==1&&picks==1);
    if(!scenario)assert(loaded==3&&applied==3&&reviews==3&&strstr(summary_text,"3 of 3"));
    else assert(loaded==2&&applied==1&&strstr(summary_text,"1 of 3")&&strstr(summary_text,"Queue stopped"));
}
'''
with tempfile.TemporaryDirectory(prefix='hdd-app-queue-') as tmp:
    p = Path(tmp)
    (p / 'test.c').write_text(code)
    subprocess.run(['gcc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
                    '-I', str(root / 'include'), str(p / 'test.c'), '-o', str(p / 'test')], check=True)
    for scenario in range(9):
        subprocess.run([str(p / 'test'), str(scenario)], check=True, timeout=10)
    print("PASS: app queue shows progress before device I/O, reopens each source across network resets, retains per-app reviews, stops on failure/cancellation and reports completed count")
