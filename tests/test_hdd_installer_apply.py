"""Run production app/HOSD installation against a host-backed PFS/ATA facade."""
from pathlib import Path
import hashlib
import io
import struct
import subprocess
import tempfile
import zipfile

root = Path(__file__).resolve().parents[1]
native = (root / 'src/hdd_install_ps2.c').read_text().replace('#include "launchelf.h"', '').replace('#include "dash_internal.h"', '')
code = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "hdd_install.h"
#define MAX_PATH 1025
#define FIO_O_RDONLY O_RDONLY
#define FIO_O_WRONLY O_WRONLY
#define FIO_O_CREAT O_CREAT
#define FIO_O_EXCL O_EXCL
#define FIO_MT_RDWR 1
#define FIO_CST_ATTR 1
#define FIO_S_ISDIR S_ISDIR
#define FS_GROUP_APPLICATION 1
#define HDIOC_FLUSH 1
typedef uint64_t u64;
typedef struct { unsigned mode,attr; } iox_stat_t;
static char work[1024];
static const char *mounted[4];
static unsigned mounts,attrcalls,partitions,injected,psbbn_parts;
static int attr_error;
static u64 Timer(void){return 1;}
static void dashWorkerStatus(const char*s,unsigned n,unsigned total){assert(s);(void)n;(void)total;}
static int map(const char*p,char*out,size_t cap) {
    const char *colon=strchr(p,':'),*base;
    char prefix[2048];
    if(!colon)return -EINVAL;
    if(!strncmp(p,"usb0:",5))base="stage";
    else if(!strncmp(p,"ata0:",5))base="ata";
    else if(!strncmp(p,"pfs",3)&&p[3]>='0'&&p[3]<='3'&&p[4]==':') {
        unsigned i=p[3]-'0';if(!mounted[i])return -ENODEV;base=mounted[i];
    } else return -ENOENT;
    if(snprintf(prefix,sizeof(prefix),"%s/%s",work,base)>=(int)sizeof(prefix))return -ENAMETOOLONG;
    return snprintf(out,cap,"%s%s",prefix,colon+1)<(int)cap?0:-ENAMETOOLONG;
}
static int genOpen(const char*p,int mode){char path[4096];int r=map(p,path,sizeof(path));return r<0?r:open(path,mode,0600);}
static int genRead(int fd,void*p,unsigned n){if(n>97)n=97;return read(fd,p,n);}
static int genWrite(int fd,const void*p,unsigned n){if(n>31)n=31;return write(fd,p,n);}
#define genLseek lseek
#define genClose close
static int genMkdir(const char*p,int mode){char path[4096];int r=map(p,path,sizeof(path));return r<0?r:mkdir(path,mode)<0?-errno:0;}
static int genRemove(char*p){char path[4096];int r=map(p,path,sizeof(path));return r<0?r:unlink(path);}
static int genRmdir(char*p){char path[4096];int r=map(p,path,sizeof(path));return r<0?r:rmdir(path);}
static int fileXioGetStat(const char*p,iox_stat_t*s){char path[4096];struct stat st;int r=map(p,path,sizeof(path));if(r<0)return r;if(stat(path,&st)<0)return -errno;s->mode=st.st_mode;return 0;}
static int fileXioChStat(const char*p,const iox_stat_t*s,int flags){assert(strstr(p,"pfs2:/APPS") && s->attr==0xc4a7 && flags==FIO_CST_ATTR);attrcalls++;return attr_error;}
static int fileXioMount(const char*m,const char*p,int flags){unsigned i=m[3]-'0';assert(i<4&&!mounted[i]&&flags==FIO_MT_RDWR);mounted[i]=p+5;mounts++;return 0;}
static int fileXioUmount(const char*m){unsigned i=m[3]-'0';assert(i<4&&mounted[i]);mounted[i]=NULL;mounts--;return 0;}
static int hddMakeFilesystem(int mb,char*name,int group){char path[4096];assert(mb==128&&group==FS_GROUP_APPLICATION);partitions++;snprintf(path,sizeof(path),"%s/%s",work,name);assert(!mkdir(path,0700));return mb;}
static int InjectHddAppLauncher(const char*p,const char*elf,const char*title,const void*icon,unsigned size){assert(p&&title&&icon&&size&&strstr(elf,"ata0:/APPS/")&&partitions==injected+1);injected++;return 0;}
int hddInstallPsbbnPartitions(HddInstall*in){assert(in->jail&&in->flags==7);psbbn_parts++;return 0;}
int hddInstallPsbbnPreflight(HddInstall*in){(void)in;return 0;}
'''
code += native
code += r'''
int main(int argc,char**argv){
    HddInstall in={0};FILE*f;char path[4096],hash[41];unsigned i,size;int ret;
    assert(argc==4);snprintf(work,sizeof(work),"%s",argv[1]);in.flags=atoi(argv[2]);in.jail=in.flags==7;
    in.ready=in.owned=1;strcpy(in.stage,"usb0:/");memset(in.commit,'a',40);
    snprintf(path,sizeof(path),"%s/manifest",work);f=fopen(path,"r");assert(f);
    while(fscanf(f,"%u %u %40s",&i,&size,hash)==3){assert(i<10);in.sizes[i]=size;strcpy(in.hashes[i],hash);}fclose(f);
    if(!strcmp(argv[3],"hash"))in.hashes[2][0]='0';
    if(!strcmp(argv[3],"attr"))attr_error=-EIO;
    ret=hddInstallApply(&in);assert(!mounts);
    if(strcmp(argv[3],"ok")){assert(ret<0);assert(!partitions&&!injected);return 0;}
    assert(ret==0);assert(attrcalls==(!in.jail&&(in.flags&2)?3:0));
    assert(partitions==(in.jail?2:0)&&injected==partitions&&psbbn_parts==(unsigned)in.jail);
    if(in.flags&2){snprintf(path,sizeof(path),"%s/%s/APPS/APP_WLE-R3Z/WLE-R3Z.ELF",work,in.jail?"ata":"__common");assert(!access(path,F_OK));}
    if(in.flags&1){snprintf(path,sizeof(path),"%s/__system/osdmenu/hosdmenu.elf",work);assert(!access(path,F_OK));}
    if(!in.jail&&(in.flags&1)){snprintf(path,sizeof(path),"%s/__sysconf/osdmenu/OSDMBR.CNF",work);assert(!access(path,F_OK));}
    return 0;
}
'''

def psu(folder, boot):
    elf = bytearray(120); elf[:6] = b'\x7fELF\x01\x01'; elf[18] = 8
    files = [(boot, bytes(elf)), ('title.cfg', b'boot=' + boot.encode()), ('list.icn', b'icon fixture' * 10)]
    def header(name, mode, size):
        h = bytearray(512); struct.pack_into('<H', h, 0, mode); struct.pack_into('<I', h, 4, size)
        h[64:64 + len(name)] = name.encode(); return bytes(h)
    out = header(folder, 0x8427, len(files) + 2) + header('.', 0x8427, 0) + header('..', 0x8427, 0)
    for n, data in files: out += header(n, 0x8497, len(data)) + data + bytes((-len(data)) % 1024)
    return out

with tempfile.TemporaryDirectory(prefix='hdd-installer-apply-') as directory:
    d = Path(directory); (d / 'test.c').write_text(code)
    subprocess.run(['gcc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-I', str(root / 'include'), str(d / 'test.c'),
                    *[str(root / 'src' / (n + '.c')) for n in ('hdd_install_plan','install_archive','save_archive','save_lzari','save_sign')],
                    '-lz', '-o', str(d / 'test')], check=True)
    extra = io.BytesIO()
    with zipfile.ZipFile(extra, 'w', zipfile.ZIP_DEFLATED) as z:
        for name in ('OSDSYS_A.XLF','FNTOSD','ICOIMAGE','JISUCS','SKBIMAGE','SNDIMAGE','TEXIMAGE'):
            z.writestr('extras/' + name, b'OSD fixture ' * 1000)
    assets = [('OSDMBR.XLF', b'\x01\x00\x00\x04' + bytes(508)), ('hosdmenu.elf', b'ELF fixture'),
              ('APP_WLE-R3Z.psu', psu('APP_WLE-R3Z','WLE-R3Z.ELF')),
              ('SYS_R3CONFIGURATOR.psu', psu('SYS_R3CONFIGURATOR','r3configurator.elf')),
              ('extras.zip', extra.getvalue()), ('version.txt', b'fixture'),
              ('system.tar.gz', b'preflight fixture'), ('language.tar.gz', b'preflight fixture'),
              ('APP_WLER3Z.png', b'jacket fixture'), ('SYS_R3CONFIG.png', b'jacket fixture')]
    for flags, failure in ((1,'ok'),(2,'ok'),(3,'ok'),(7,'ok'),(3,'hash'),(3,'attr')):
        work = d / (str(flags) + failure); work.mkdir()
        for part in ('stage','ata','__system','__sysconf','__common'): (work / part).mkdir()
        (work / 'stage/owner.bin').write_bytes(bytes(20))
        manifest = []
        for i, (name, data) in enumerate(assets):
            (work / 'stage' / name).write_bytes(data)
            prefix = b'' if i in (4,6,7) else b'blob ' + str(len(data)).encode() + b'\0'
            manifest.append(f'{i} {len(data)} {hashlib.sha1(prefix+data).hexdigest()}\n')
        (work / 'manifest').write_text(''.join(manifest))
        subprocess.run([str(d / 'test'),str(work),str(flags),failure],check=True,timeout=20)
        if flags == 7:
            assert (work / 'PP.APP_WLE-R3Z/res/info.sys').exists()
            assert (work / 'PP.SYS_R3CONFIGURATOR/res/jkt_001.png').read_bytes() == b'jacket fixture'
    print('PASS: production PSU/app/HOSD copying, shared APPS attributes, exFAT/PATINFO/browser resources, hashes, short I/O and failed-install unmounts')
