"""Installer HTTPS phases are visible and observers cannot outlive a job."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
s = (root / 'src/hdd_install_ps2.c').read_text()
def function(signature):
    a=s.index(signature);b=s.index('{',a)+1;depth=1
    while depth:
        depth+=(s[b]=='{')-(s[b]=='}');b+=1
    return s[a:b]+'\n'
code = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "hdd_install.h"
#include "cloud_http.h"
static void (*observer)(const char *, void *);
void hddInstallLog(const char *format,...) { (void)format; }
void hddInstallLogFlush(HddInstall *in) { (void)in; }
static void *observer_context;
static const unsigned char *ca_snapshot;
void cloudHttpSetCaBundle(const unsigned char *pem, unsigned bytes){ca_snapshot=pem;assert((pem!=NULL)==(bytes!=0));}
static char text[160];
static int failure, changes;
void cloudHttpSetStatusCallback(void (*callback)(const char *, void *),void *context){observer=callback;observer_context=context;changes++;}
void hddInstallProgress(const char *s,int done,int total){assert(!done&&!total);snprintf(text,sizeof(text),"%s",s);}
void hddInstallPhase(unsigned phase){assert(phase==HDD_INSTALL_PHASE_DOWNLOAD);}
static int downloadInstaller(HddInstall *in,const char **phase){
    assert(in&&observer&&observer_context&&ca_snapshot==in->ca_data);
    observer("Resolving server address",observer_context);assert(strstr(text,"PSBBN DP repository")&&strstr(text,"Resolving"));
    *phase="system.tar.gz";observer("Verifying secure connection",observer_context);assert(strstr(text,"system.tar.gz")&&strstr(text,"Verifying"));
    observer("Receiving download headers",observer_context);assert(strstr(text,"Receiving download headers"));
    return failure;
}
'''
code += function('static void installerHttpsStatus(')+function('int hddInstallDownload(')
code += r'''
int main(void){unsigned char ca[]={0};HddInstall in={.ca_data=ca,.ca_size=1};assert(!hddInstallDownload(&in)&&!observer&&!observer_context&&!ca_snapshot&&changes==2);
 failure=-EIO;assert(hddInstallDownload(&in)==-EIO&&!observer&&!observer_context&&!ca_snapshot&&changes==4);
 puts("PASS: installer displays DNS/TLS/header phases and clears the observer after success/failure");}
'''
with tempfile.TemporaryDirectory(prefix='hdd-installer-transport-') as directory:
    d=Path(directory);(d/'check.c').write_text(code)
    subprocess.run(['gcc','-std=c99','-Wall','-Wextra','-Werror','-I',str(root/'include'),str(d/'check.c'),'-o',str(d/'check')],check=True)
    subprocess.run([str(d/'check')],check=True,timeout=10)
