"""Dynamic upstream localization discovery and cancellation before disk erase."""
from pathlib import Path
import json
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
native = (root / 'src/hdd_install_ps2.c').read_text()
ui = (root / 'src/dash_hdd_install.c').read_text()


def function(source, signature):
    a = source.index(signature)
    depth = 0
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', source[a:], re.S):
        depth += (token.group() == '{') - (token.group() == '}')
        if not depth and token.group() == '}':
            return source[a:a + token.end()] + '\n'
    raise AssertionError(signature)


script = '\nURL="https://archive.org/download/psbbn-test-v5"\ncase "$LANG_FILE" in\n'
for code, key in [('eng', 2), ('fre', 4), ('jpn', 3), ('zzz', 19), ('rus', 18)]:
    script += f'    {code})\n        lang="{code}"\n        LANG_DISPLAY="${{UI_TEXT[CHANGE_LANGUAGE_{key}]}}"\n        ;;\n'
script += '    *)\n        lang="eng"\n        ;;\nesac\n'
english = 'CHANGE_LANGUAGE_2=English\nCHANGE_LANGUAGE_4=French\nCHANGE_LANGUAGE_3=Japanese\nCHANGE_LANGUAGE_19=New language\nCHANGE_LANGUAGE_18=Russian\n'
metadata = {'files': [
    {'name': f'language-pak-{code}-v2.0.1.tar.gz', 'sha1': 'b' * 40, 'size': '7000000'}
    for code in ('eng', 'fre', 'jpn', 'zzz')
] + [{'name': 'channels-jpn-v1.1.0.tar.gz', 'sha1': 'c' * 40, 'size': '979061613'}]}
code = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hdd_install.h"
#include "cloud_http.h"
static const unsigned char *bundle;
static unsigned requests,fail_request;
void cloudHttpSetCaBundle(const unsigned char *data,unsigned size){assert((data!=NULL)==(size!=0));bundle=data;}
void hddInstallStatus(const char *s,int a,int b){assert(s&&a>=0&&b>=0);}
void hddInstallLog(const char *s,...){assert(s);}
void hddInstallLogFlush(HddInstall *in){assert(in);}
int hddInstallResolve(HddInstall *in){assert(in->language.code[0]);in->download_bytes=123;return 0;}
'''
code += '\nstatic const char script[] = ' + json.dumps(script) + ';\n'
code += 'static const char english[] = ' + json.dumps(english) + ';\n'
code += 'static const char metadata[] = ' + json.dumps(json.dumps(metadata)) + ';\n'
code += r'''
int cloudHttpRequest(const char *method,const char *host,const char *path,const char *headers,
 const void *body,int bytes,const char *ca,CloudHttpResponse *r,char *error,int error_size){
 const char *text=NULL;(void)headers;(void)error;(void)error_size;
 assert(bundle&&!strcmp(method,"GET")&&!body&&!bytes&&ca&&*ca);requests++;
 if(requests==fail_request)return -EIO;
 if(!strcmp(host,"api.github.com")){
  assert(!strcmp(path,HDD_INSTALL_API "/git/ref/heads/main"));
  text="{\"object\":{\"type\":\"commit\",\"sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}";
 }else if(!strcmp(host,"raw.githubusercontent.com")){
  assert(strstr(path,"/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/"));
  if(strstr(path,"/scripts/PSBBN-Installer.sh"))text=script;
  else{assert(strstr(path,"/scripts/assets/lang/eng.txt"));text=english;}
 }else{assert(!strcmp(host,"archive.org")&&!strcmp(path,"/metadata/psbbn-test-v5"));text=metadata;}
 r->status=200;r->body=strdup(text);r->body_len=strlen(text);return 0;
}
void cloudHttpResponseFree(CloudHttpResponse *r){free(r->body);memset(r,0,sizeof(*r));}
'''
for sig in ['static int request(', 'static int upstreamText(', 'static int archiveMetadata(', 'int hddInstallLanguageChoices(']:
    code += function(native, sig)
code += r'''
#define ETH
#define WLE_NATIVE_CLOUD
#define DI_WARNING 1
#define DASH_JOB_VISIBLE 1
static int ready=1,worker_error,worker_runs,cancel_choice,releases,modal_errors;
static int dashWorkerAwaitAll(const char *s){assert(s);return 1;}
static int dashHttpsCaPath(char *p,int size){assert(size>10);strcpy(p,"pfs0:/CA");return 1;}
int hddInstallLoadCa(HddInstall *in){in->ca_data=(unsigned char*)"CA";in->ca_size=2;return 0;}
void hddInstallRelease(HddInstall *in){in->ca_data=NULL;in->ca_size=0;releases++;}
static int ensureDashboardIpNetworkReady(void){return ready;}
static const char *dashboardIpNetworkError(void){return "offline";}
static void dashModalMessage(int icon,const char *title,const char *body,const char *detail){assert(icon==DI_WARNING&&title&&body&&!detail);modal_errors++;}
static int dashWorkerSubmit(const char *s,int flags,void(*run)(void*),void(*done)(void*),void *arg){
 assert(s&&flags==1&&run&&!done);if(worker_error)return -1;run(arg);worker_runs++;return 1;
}
int dashHddInstallAwait(int id,const char *title){assert(id==1&&title);return 1;}
static int dashModalSelect(const char *title,const char *const *items,int count,int initial){
 assert(!strcmp(title,"PSBBN language")&&count==4&&initial==0&&!strcmp(items[0],"English")&&!strcmp(items[3],"New language"));
 return cancel_choice?-1:3;
}
void hddInstallOverall(uint64_t a,uint64_t b){assert(a==1&&b==1);}
'''
a = ui.index('typedef struct {\n    HddInstall *install;')
code += ui[a:ui.index('#endif', a)]
code += function(ui, 'int dashHddInstallPrepare(')
code += r'''
int main(void){
 HddInstall in={.flags=7,.jail=1};HddInstallLanguage langs[32];char item[96],out[4096],name[128],hash[41];uint32_t size;
 assert(hddInstallLanguages(script,english,langs,32)==5&&!strcmp(langs[3].code,"zzz"));
 assert(hddInstallLanguages(script,english,langs,4)<0);
 assert(hddInstallLanguages("case \"$OTHER\" in\n",english,langs,32)<0);
 assert(hddInstallLanguages(script,"",langs,32)<0);
 assert(hddInstallArchiveItem(script,item)&&!strcmp(item,"psbbn-test-v5"));
 assert(!hddInstallArchiveItem("\nURL=\"https://archive.org/download/../bad\"",item));
 assert(!hddInstallArchiveItem("\nURL=\"https://elsewhere/download/item\"",item));
 assert(hddInstallArchiveMetadata(metadata,"channels-jpn-v",name,hash,&size)&&size==979061613);
 strcpy(in.ca_path,"pfs0:/CA");in.ca_data=(unsigned char*)"CA";in.ca_size=2;
 assert(hddInstallLanguageChoices(&in,langs,32)==4&&requests==4&&!bundle);
 assert(!strcmp(in.commit,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")&&!strcmp(in.archive_item,"psbbn-test-v5"));
 assert(!strcmp(langs[2].name,"Japanese")&&!strcmp(langs[3].code,"zzz"));
 for(fail_request=1;fail_request<=4;fail_request++){
  requests=0;in.error[0]=0;assert(hddInstallLanguageChoices(&in,langs,32)<0&&!bundle&&in.error[0]);
 }
 fail_request=0;requests=0;
 assert(dashHddInstallPrepare(&in)==1&&worker_runs==2&&!strcmp(in.language.code,"zzz")&&in.ca_data&&in.download_bytes==123);
 cancel_choice=1;assert(dashHddInstallPrepare(&in)==-ECANCELED&&!in.ca_data&&!modal_errors);cancel_choice=0;
 worker_error=1;assert(dashHddInstallPrepare(&in)==0&&!in.ca_data&&modal_errors==1);worker_error=0;
 ready=0;assert(dashHddInstallPrepare(&in)==0&&!in.ca_data&&modal_errors==2);ready=1;
 strcpy(in.language.code,"jpn");
 assert(hddInstallSelectedAsset(&in,HI_CHANNELS)&&hddInstallSelectedAsset(&in,HI_KERNEL_JPN));
 assert(hddInstallConfig(&in,1,out,sizeof(out))>0&&strstr(out,"osd_language = jap\n"));
 assert(hddInstallConfig(&in,0,out,sizeof(out))>0&&strstr(out,"osd_language = jap\n"));
 strcpy(in.language.code,"fre");
 assert(!hddInstallSelectedAsset(&in,HI_CHANNELS)&&!hddInstallSelectedAsset(&in,HI_KERNEL_JPN));
 assert(hddInstallConfig(&in,1,out,sizeof(out))>0&&strstr(out,"osd_language = fre\n"));
 assert(hddInstallConfig(&in,0,out,sizeof(out))>0&&strstr(out,"osd_language = fre\n"));
 strcpy(in.language.code,"hun");assert(hddInstallConfig(&in,1,out,sizeof(out))>0&&strstr(out,"osd_language = eng\n"));
 in.flags=0;requests=worker_runs=0;assert(dashHddInstallPrepare(&in)==1&&!requests&&!worker_runs);
 puts("PASS: dynamic language additions, unavailable-pack filtering, Japanese dependencies, pinned metadata, network failure, cancellation and matching OSD configuration");
}
'''
with tempfile.TemporaryDirectory(prefix='hdd-installer-languages-') as directory:
    d = Path(directory)
    (d / 'test.c').write_text(code)
    subprocess.run(['gcc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                    '-I', str(root / 'include'), str(d / 'test.c'), str(root / 'src/hdd_install_plan.c'),
                    '-o', str(d / 'test')], check=True)
    subprocess.run([str(d / 'test')], check=True)
