"""Run the real SMB path/config conversion and config IO preflight on the host."""
from pathlib import Path
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ISO = (ROOT/'src/neutrino_iso.c').read_text(encoding='utf-8')
STOCK = '''name = "SMB driver"
depends = ["i_dev9_hidden"]
[[module]]
file = "ministack.irx"
args = ["ip=192.168.1.10"] # keep this comment
env = ["LE", "EE"]
[[module]]
file = "smb_ioman.irx"
args = ["ip=192.168.1.10", "mask=255.255.255.0", "gw=192.168.1.1", "srv=192.168.1.2", "user=guest", "pass=", "share=PS2SMB"]
env = ["LE"]
[[module]]
file = "smb_fhi.irx"
args = ["ip=192.168.1.10", "mask=255.255.255.0", "gw=192.168.1.1", "srv=192.168.1.2", "user=guest", "pass=", "share=PS2SMB"]
env = ["EE"]
'''

def function(name):
    start = ISO.index(name)
    return ISO[start:ISO.index('\n}',start)+2]

code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "neutrino_smb.h"
#define MAX_PATH 1024
#define FIO_O_RDONLY 0
#define FIO_O_WRONLY 1
#define FIO_O_CREAT 2
#define FIO_O_TRUNC 4
static const char *stock = @STOCK@;
static char disk[8192], ip[16]="10.0.0.7", netmask[16]="255.255.0.0", gw[16]="10.0.0.1";
static int pos, writes, write_opens, fail_write, missing, readonly;
static int genOpen(const char *path, int flags) {
    if (missing && strstr(path,"smb_fhi.irx")) return -1;
    if (flags) write_opens++;
    if (flags && readonly) return -1;
    pos=0;
    if (flags) { writes++; disk[0]=0; }
    return 3;
}
static int openPathForRead(const char *path, char *out, int size) { (void)out; (void)size; return genOpen(path,0); }
static int genClose(int fd) { (void)fd; return 0; }
static int genLseek(int fd,int offset,int how) { (void)fd; pos=how==SEEK_END?(int)strlen(disk):offset; return pos; }
static int genRead(int fd,void *buf,int size) {
    int left=(int)strlen(disk)-pos; (void)fd;
    if(size>left)size=left;
    memcpy(buf,disk+pos,size); pos+=size; return size;
}
static int genWrite(int fd,void *buf,int size) {
    (void)fd;
    if(fail_write) { fail_write--; size/=2; }
    memcpy(disk,buf,size); disk[size]=0; return size;
}
static void setMessage(char *msg,size_t size,const char *text,const char *path) {
    (void)path; snprintf(msg,size,"%s",text);
}
@FUNCTIONS@
static void parse(SmbConfig *cfg,const char *text) {
    char buf[4096]; strcpy(buf,text); assert(smbConfigParse(buf,cfg)==1);
    cfg->share[0].state=SMB_STATE_CONNECTED;
}
int main(void) {
    SmbConfig cfg; SmbShare sh;
    char dvd[132], out[8192], second[8192], msg[256], path[256];
    const char *why;
    parse(&cfg,"NAME=PC\nUSERNAME=player\nPASSWORD=pa\"ss\\word\nURL=smb://10.0.0.2/games/sub\n");
    assert(neutrinoSmbResolve(&cfg,"smb:/PC/DVD/game.iso",&sh,dvd,sizeof(dvd),&why));
    /* smb2man's pinned driver ignores URL subdirectories; match its browser view. */
    assert(!strcmp(dvd,"smb:DVD/game.iso") && !strcmp(sh.share,"games") && !strcmp(sh.server,"10.0.0.2"));
    assert(!neutrinoSmbResolve(&cfg,"smb:/OTHER/DVD/game.iso",&sh,dvd,sizeof(dvd),&why));
    strcpy(cfg.share[0].url,"smb://nas/games");
    assert(!neutrinoSmbResolve(&cfg,"smb:/PC/DVD/game.iso",&sh,dvd,sizeof(dvd),&why));
    strcpy(cfg.share[0].url,"smb://10.0.0.2:1445/games");
    assert(!neutrinoSmbResolve(&cfg,"smb:/PC/DVD/game.iso",&sh,dvd,sizeof(dvd),&why));
    strcpy(cfg.share[0].url,"smb://10.0.0.2:445/games");
    assert(neutrinoSmbResolve(&cfg,"smb:/PC/DVD/game.iso",&sh,dvd,sizeof(dvd),&why));
    memset(path,'a',sizeof(path)); memcpy(path,"smb:/PC/",8); path[200]=0;
    assert(!neutrinoSmbResolve(&cfg,path,&sh,dvd,sizeof(dvd),&why));
    assert(neutrinoSmbResolve(&cfg,"smb:/PC/DVD/game.iso",&sh,dvd,sizeof(dvd),&why));
    assert(neutrinoSmbConfig(stock,&sh,ip,netmask,gw,out,sizeof(out))>0);
    assert(strstr(out,"pass=pa\\\"ss\\\\word") && strstr(out,"# keep this comment"));
    assert(strstr(out,"depends = [\"i_dev9_hidden\"]") && strstr(out,"srv=10.0.0.2"));
    assert(neutrinoSmbConfig(out,&sh,ip,netmask,gw,second,sizeof(second))>0 && !strcmp(out,second));
    assert(neutrinoSmbConfig(stock,&sh,"999.0.0.1",netmask,gw,out,sizeof(out))<0);
    assert(neutrinoSmbConfig(stock,&sh,ip,netmask,gw,out,20)<0);
    strcpy(second,stock); *strstr(second,"pass=")='X';
    assert(neutrinoSmbConfig(second,&sh,ip,netmask,gw,out,sizeof(out))<0);
    strcpy(disk,stock); writes=0;
    assert(prepareSmbConfig("mass:/neutrino/neutrino.elf",&sh,msg,sizeof(msg)) && writes==1);
    assert(prepareSmbConfig("mass:/neutrino/neutrino.elf",&sh,msg,sizeof(msg)) && writes==1);
    strcpy(disk,stock); fail_write=1;
    assert(!prepareSmbConfig("mc0:/neutrino/neutrino.elf",&sh,msg,sizeof(msg)) && !strcmp(disk,stock));
    assert(!strstr(msg,"restored"));
    /* Both the update and the restore come up short: the file is damaged
     * and the message must say so rather than blame the module args. */
    strcpy(disk,stock); fail_write=2;
    assert(!prepareSmbConfig("mc0:/neutrino/neutrino.elf",&sh,msg,sizeof(msg)) && strcmp(disk,stock));
    assert(strstr(msg,"could not be restored") && strstr(msg,"bsd-smb.toml"));
    missing=1; writes=0;
    assert(!prepareSmbConfig("mass:/neutrino/neutrino.elf",&sh,msg,sizeof(msg)) && !writes); missing=0;
    /* Read-only media: the write open fails before anything is truncated, so
     * the file is intact and the generic message applies, not the restore one. */
    strcpy(disk,stock); readonly=1; write_opens=0;
    assert(!prepareSmbConfig("mass:/neutrino/neutrino.elf",&sh,msg,sizeof(msg))); readonly=0;
    assert(write_opens==1 && !strcmp(disk,stock) && strstr(msg,"writable") && !strstr(msg,"restored"));
    assert(!prepareSmbConfig("smb:/PC/neutrino/neutrino.elf",&sh,msg,sizeof(msg)));
    assert(!prepareSmbConfig("udpfs:/neutrino/neutrino.elf",&sh,msg,sizeof(msg)));
    parse(&cfg,"NAME=OLD\nVERSION=1\nURL=smb://10.0.0.2/games\n");
    assert(neutrinoSmbResolve(&cfg,"smb1:/DVD/game.iso",&sh,dvd,sizeof(dvd),&why));
    assert(!strcmp(dvd,"smb:DVD/game.iso"));
    return 0;
}
'''
functions = '\n'.join(function(name) for name in (
    'static int fileExists(', 'static const char *neutrinoBsdForPath(',
    'static int smbCoreLocal(', 'static int smbConfigWrite(', 'static int prepareSmbConfig('))
code = code.replace('@STOCK@',json.dumps(STOCK)).replace('@FUNCTIONS@',functions)
with tempfile.TemporaryDirectory() as tmp:
    src,exe = Path(tmp)/'test.c',Path(tmp)/'test.exe'
    src.write_text(code,encoding='utf-8')
    subprocess.run(['gcc','-std=c99','-Wall','-Wextra','-Werror','-DSMB','-I',str(ROOT/'include'),
        str(src),str(ROOT/'src/neutrino_smb.c'),str(ROOT/'src/smb_cnf.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

assert '!strcmp(bsd,"smb") ? "" : "-qb\\n"' in ISO
assert ISO.index('prepareSmbConfig(fullpath') < ISO.index('CleanUpForKeepIopExec(')
assert 'smbSharesNeutrino(path' in ISO
print('Neutrino SMB: share mapping, limits, TOML rewrite, no-op, rollback and handoff passed')
