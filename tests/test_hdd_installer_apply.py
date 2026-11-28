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
launcher = (root / 'src/hdd_header_injector.c').read_text()
launcher = launcher[launcher.index('int InjectHddAppLauncher('):]
assert 'memalign(64, APA_HEADER_SIZE)' in launcher
assert 'if (scratch == NULL) return -ENOMEM;' in launcher
assert launcher.index('if (scratch == NULL) return -ENOMEM;') < launcher.index('hddHeaderWriteRawRange(')
assert 'free(scratch);\n\treturn ret;' in launcher
code = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "hdd_install.h"
int hddInstallRecoveryFault(void){return 0;}
void hddInstallRecoveryObserve(int rc){(void)rc;}
static unsigned saved_intent, saved_start, operation, crash_at;
static jmp_buf crashed;
static void crashpoint(void){if(++operation==crash_at)longjmp(crashed,1);}
int hddInstallResumeSave(HddInstall *in){
    assert(in);
    crashpoint();
    saved_intent=in->pfs_intent;saved_start=in->pfs_start[HDD_INSTALL_PFS_GAME];
    crashpoint();
    return 0;
}
int hddInstallResumeRemove(HddInstall *in){assert(in);return 0;}
#define MAX_PATH 1025
#define FIO_O_RDONLY O_RDONLY
#define FIO_O_WRONLY O_WRONLY
#define FIO_O_CREAT O_CREAT
#define FIO_O_EXCL O_EXCL
#define FIO_O_TRUNC O_TRUNC
#define FIO_MT_RDWR 1
#define FIO_CST_ATTR 1
#define FIO_S_ISDIR S_ISDIR
#define stricmp strcasecmp
#define HDIOC_FLUSH 1
typedef uint64_t u64;
typedef int64_t s64;
typedef struct { unsigned mode,attr,size,private_5,private_0; } iox_stat_t;
static char work[1024];
static const char *mounted[4];
static unsigned mounts,attrcalls,partitions,formats,injected,psbbn_parts;
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
static int fileXioGetStat(const char*p,iox_stat_t*s){
    char path[4096];struct stat st;
    if(!strncmp(p,"hdd0:",5)){
        snprintf(path,sizeof(path),"%s/%s",work,p+5);
        if(stat(path,&st)<0)return -errno;
        *s=(iox_stat_t){.mode=0x100,.size=128*2048,.private_5=262144};return 0;
    }
    int r=map(p,path,sizeof(path));if(r<0)return r;if(stat(path,&st)<0)return -errno;s->mode=st.st_mode;return 0;
}
static int fileXioChStat(const char*p,const iox_stat_t*s,int flags){assert(strstr(p,"pfs2:/APPS") && s->attr==0xc4a7 && flags==FIO_CST_ATTR);attrcalls++;return attr_error;}
static int fileXioMount(const char*m,const char*p,int flags){unsigned i=m[3]-'0';assert(i<4&&!mounted[i]&&flags==FIO_MT_RDWR);mounted[i]=p+5;mounts++;return 0;}
static int fileXioUmount(const char*m){unsigned i=m[3]-'0';assert(i<4&&mounted[i]);mounted[i]=NULL;mounts--;return 0;}
static int hddCreatePfsExtent(unsigned mb,const char*name){
    char path[4096];assert(mb==128&&!strncmp(name,"+LSIN",5));
    snprintf(path,sizeof(path),"%s/%s",work,name);
    if(mkdir(path,0700)<0)return -errno;
    partitions++;crashpoint();return 0;
}
static int hddFormatPfs(const char*path){
    iox_stat_t st;assert(!strncmp(path,"hdd0:+LSIN",10)&&!fileXioGetStat(path,&st));
    formats++;crashpoint();return 0;
}
static int fileXioRename(const char*from,const char*to){
    char a[2048],b[2048];assert(!strncmp(from,"hdd0:+LSIN",10)&&!strncmp(to,"hdd0:",5));
    snprintf(a,sizeof(a),"%s/%s",work,from+5);snprintf(b,sizeof(b),"%s/%s",work,to+5);
    if(!access(b,F_OK))return -EEXIST;
    if(rename(a,b)<0)return -errno;
    crashpoint();return 0;
}
static int InjectHddAppLauncher(const char*p,const char*elf,const char*title,const void*icon,unsigned size){assert(p&&title&&icon&&size&&strstr(elf,"ata0:/APPS/")&&partitions==injected+1);injected++;return 0;}
static int InjectHddAppLauncherWithArg(const char*p,const char*elf,const char*title,const void*icon,unsigned size,const char*arg){
    assert(!strcmp(p,"PP.LAUNCHER")&&title&&icon&&size&&partitions==injected+1);
    assert(strstr(elf,"hdd0:__system:pfs:/launcher/"));
    if(strstr(elf,"nhddl"))assert(arg&&!strcmp(arg,"-mode=ata"));else assert(!arg&&(strstr(elf,"OPNPS2LD.ELF")||strstr(elf,"RIPTOPL-RA-UNPAKT.ELF")));
    injected++;return 0;
}
int hddInstallPsbbnPartitions(HddInstall*in){assert(in->jail&&(in->flags&7)==7);psbbn_parts++;return 0;}
int hddInstallPsbbnPreflight(HddInstall*in){(void)in;return 0;}
'''
code += r'''

void hddInstallLog(const char *format, ...) { (void)format; }
int hddInstallLogEnabled(void) { return 0; }
void hddInstallLogFlush(HddInstall *in) { (void)in; }
void hddInstallStatus(const char *s,int done,int total) { dashWorkerStatus(s,done,total); }
void hddInstallProgress(const char *s,int done,int total) { assert(done>=0&&total>=0&&done<=total);dashWorkerStatus(s,done,total); }
void hddInstallOverall(uint64_t done,uint64_t total) { assert(total&&done<=total); }
void hddInstallPhase(unsigned phase) { assert(phase==HDD_INSTALL_PHASE_INSTALL); }
'''
code += native
code += r'''
static void testCreate(unsigned cut){
    static HddInstall in;
    in.apa_sectors=1048576;
    memset(in.owner,0x38,sizeof(in.owner));
    crash_at=cut;
    if(!setjmp(crashed)){
        assert(hddInstallPfsPartition(&in,128,"PP.LAUNCHER",HDD_INSTALL_PFS_GAME)==128);
        assert(!cut);
    }else{
        crash_at=0;
        in.pfs_intent=saved_intent;in.pfs_start[HDD_INSTALL_PFS_GAME]=saved_start;
        assert(hddInstallPfsPartition(&in,128,"PP.LAUNCHER",HDD_INSTALL_PFS_GAME)==128);
    }
    assert(partitions==1&&formats==(cut==4||cut==5?2:1));
    assert(in.pfs_start[HDD_INSTALL_PFS_GAME]==262144);
    iox_stat_t st;
    assert(!fileXioGetStat("hdd0:PP.LAUNCHER",&st));
    in.pfs_start[HDD_INSTALL_PFS_GAME]=0;
    assert(hddInstallPfsPartition(&in,128,"PP.LAUNCHER",HDD_INSTALL_PFS_GAME)==-EEXIST);
    char name[32],path[4096];
    hddInstallPendingPfsName(&in,HDD_INSTALL_PFS_APP,name);
    snprintf(path,sizeof(path),"%s/%s",work,name);assert(!mkdir(path,0700));
    assert(hddInstallPfsPartition(&in,128,"PP.UNRELATED",HDD_INSTALL_PFS_APP)==-EEXIST);
    assert(!access(path,F_OK)&&partitions==1);
}
int main(int argc,char**argv){
    HddInstall in={.apa_sectors=128*1024*2048u};FILE*f;char path[4096],hash[41];unsigned i,size;int ret;
    assert(argc==3||argc==5);snprintf(work,sizeof(work),"%s",argv[1]);
    if(argc==3){testCreate(atoi(argv[2]));return 0;}
    in.flags=atoi(argv[2]);in.jail=!!(in.flags&4);in.game_launcher=atoi(argv[4]);
    in.ready=in.owned=1;strcpy(in.stage,in.jail?"ata0:/PSBBN_DP_TEMP":"pfs2:/PSBBN_DP_TEMP");memset(in.commit,'a',40);
    if(in.jail)strcpy(in.language.code,"jpn");
    snprintf(path,sizeof(path),"%s/manifest",work);f=fopen(path,"r");assert(f);
    while(fscanf(f,"%u %u %40s",&i,&size,hash)==3){assert(i<HDD_INSTALL_ASSETS);in.sizes[i]=size;strcpy(in.hashes[i],hash);}fclose(f);
    if(in.jail){
        in.launcher_files=calloc(HDD_INSTALL_LAUNCHER_FILES,sizeof(*in.launcher_files));assert(in.launcher_files);
        snprintf(path,sizeof(path),"%s/launcher-manifest",work);f=fopen(path,"r");assert(f);
        char remote[192];
        while(fscanf(f,"%u %191s %u %40s",&i,remote,&size,hash)==4){
            assert(i==in.launcher_count&&i<HDD_INSTALL_LAUNCHER_FILES);
            strcpy(in.launcher_files[i].path,remote);strcpy(in.launcher_files[i].hash,hash);in.launcher_files[i].size=size;in.launcher_count++;
        }fclose(f);
    }
    if(!strcmp(argv[3],"bad-hash"))in.hashes[2][0]^=1;
    if(!strcmp(argv[3],"attr"))attr_error=-EIO;
    if(!strcmp(argv[3],"size"))in.sizes[2]--;
    if(!strcmp(argv[3],"launcher-size"))in.launcher_files[0].size--;
    ret=hddInstallApply(&in);assert(!mounts);
    if(strcmp(argv[3],"ok")){assert(ret<0);assert(!partitions&&!injected);hddInstallRelease(&in);return 0;}
    assert(ret==0);
    if(in.flags&4){
        assert(!fileXioMount("pfs0:","hdd0:__system",1));assert(!fileXioMount("pfs1:","hdd0:__sysconf",1));
        assert(hddInstallWriteConfigs(&in)==0);assert(!fileXioUmount("pfs1:")&&!fileXioUmount("pfs0:"));
    }
    assert(attrcalls==(!in.jail&&(in.flags&2)?3:0));
    assert(partitions==(in.jail?3:0)&&injected==partitions&&psbbn_parts==(unsigned)in.jail);
    if(in.jail){
        assert(in.pfs_start[HDD_INSTALL_PFS_GAME]==262144);
        in.pfs_start[HDD_INSTALL_PFS_GAME]=524288;
        assert(hddInstallPfsPartition(&in,128,"PP.LAUNCHER",HDD_INSTALL_PFS_GAME)==-EEXIST);
        in.pfs_start[HDD_INSTALL_PFS_GAME]=0;
        assert(hddInstallPfsPartition(&in,128,"PP.LAUNCHER",HDD_INSTALL_PFS_GAME)==-EEXIST);
        assert(partitions==3);
    }
    if(in.flags&2){snprintf(path,sizeof(path),"%s/%s/APPS/APP_WLE-R3Z/WLE-R3Z.ELF",work,in.jail?"ata":"__common");assert(!access(path,F_OK));}
    if(in.flags&1){snprintf(path,sizeof(path),"%s/__system/osdmenu/hosdmenu.elf",work);assert(!access(path,F_OK));}
    if(!in.jail&&(in.flags&1)){snprintf(path,sizeof(path),"%s/__sysconf/osdmenu/OSDMBR.CNF",work);assert(!access(path,F_OK));}
    hddInstallRelease(&in);return 0;
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
    for cut in range(8):
        with tempfile.TemporaryDirectory(dir=d) as work:
            subprocess.run([str(d / 'test'), work, str(cut)], check=True, timeout=10)
    extra = io.BytesIO()
    with zipfile.ZipFile(extra, 'w', zipfile.ZIP_DEFLATED) as z:
        for name in ('OSDSYS_A.XLF','FNTOSD','ICOIMAGE','JISUCS','SKBIMAGE','SNDIMAGE','TEXIMAGE'):
            z.writestr('extras/' + name, b'OSD fixture ' * 1000)
    assets = [('OSDMBR.XLF', b'\x01\x00\x00\x04' + bytes(508)), ('hosdmenu.elf', b'ELF fixture'),
              ('APP_WLE-R3Z.psu', psu('APP_WLE-R3Z','WLE-R3Z.ELF')),
              ('SYS_R3CONFIGURATOR.psu', psu('SYS_R3CONFIGURATOR','r3configurator.elf')),
              ('extras.zip', extra.getvalue()), ('version.txt', b'fixture'),
              ('system.tar.gz', b'preflight fixture'), ('language.tar.gz', b'preflight fixture'),
              ('APP_WLER3Z.png', b'jacket fixture'), ('SYS_R3CONFIG.png', b'jacket fixture'), ('PS2Linux.tar.gz', b'linux preflight fixture'), ('ps2-linux-ntsc', b'new NTSC kernel'), ('ps2-linux-vga', b'new VGA kernel'),
              ('channels.tar.gz', b'channels preflight fixture'), ('vmlinux_jpn', b'new Japanese kernel')]
    elf = bytearray(120); elf[:6] = b'\x7fELF\x01\x01'; elf[18] = 8
    launchers = [
        ('scripts/assets/OPL/OPNPS2LD.ELF', bytes(elf)), ('scripts/assets/NHDDL/nhddl.elf', bytes(elf)),
        ('scripts/assets/neutrino/neutrino.elf', bytes(elf)), ('scripts/assets/neutrino/modules/ata_bd.irx', b'ATA module'),
        ('scripts/assets/neutrino/config/system.toml', b'Neutrino configuration'), ('scripts/assets/neutrino/version.txt', b'v1.8.0'),
        ('scripts/assets/neutrino/udpfs_server/compressed_iso/__init__.py', b'UDPFS helper'),
        ('scripts/assets/OPL/version.txt', b'OPL version'), ('scripts/assets/NHDDL/version.txt', b'NHDDL version'),
        ('scripts/assets/OPL/LNG/lang_French.lng', b'French translation'),
        ('scripts/licenses/OPL', b'OPL license'), ('scripts/licenses/nhddl', b'NHDDL license'), ('scripts/licenses/neutrino', b'Neutrino license'),
    ]
    for flags, failure, launcher in ((1,'ok',0),(2,'ok',0),(3,'ok',0),(7,'ok',0),(7,'ok',1),(7,'ok',2),(15,'ok',2),(7,'ok',3),
                                    (15,'ok',0),(15,'ok',1),(3,'bad-hash',0),(3,'size',0),(3,'attr',0),(7,'launcher-size',0)):
        work = d / (str(flags) + failure + str(launcher)); work.mkdir()
        for part in ('stage','ata','__system','__sysconf','__common'): (work / part).mkdir()
        stage = work / ('ata' if flags & 4 else '__common') / 'PSBBN_DP_TEMP'; stage.mkdir()
        if flags & 12: (work / '__system/p2lboot').mkdir()
        if flags & 4: (work / '__system/p2lboot/vmlinux').write_bytes(b'older archive kernel')
        (stage / 'owner.bin').write_bytes(bytes(20))
        manifest = []
        for i, (name, data) in enumerate(assets):
            (stage / name).write_bytes(data)
            prefix = b'' if i in (4,6,7,10,13) else b'blob ' + str(len(data)).encode() + b'\0'
            manifest.append(f'{i} {len(data)} {hashlib.sha1(prefix+data).hexdigest()}\n')
        (work / 'manifest').write_text(''.join(manifest))
        if flags & 4:
            selected_launchers = launchers
            if launcher>=2:
                selected_launchers = [(remote,payload) for remote,payload in launchers if not any(x in remote for x in ('NHDDL','OPNPS2LD','scripts/licenses/'))]
                selected_launchers += [('scripts/assets/OPL/RIPTOPL-RA-UNPAKT.ELF',bytes(elf)),('scripts/assets/Ember/ember.elf',bytes(elf)),
                    ('scripts/assets/Ember/version.txt',b'0.8')]
                dep=io.BytesIO()
                with zipfile.ZipFile(dep,'w',zipfile.ZIP_DEFLATED) as z:
                    z.writestr('dep/bios.bin',b'B'*524288)
                    z.writestr('dep/ignored.bin',b'unneeded')
                (stage/'extras.zip').write_bytes(dep.getvalue())
                manifest[4]=f'4 {len(dep.getvalue())} {hashlib.sha1(dep.getvalue()).hexdigest()}\n'
                (work/'manifest').write_text(''.join(manifest))
            package = selected_launchers + [('icons/ico/APP_RIPTOPL_LST.ico' if launcher>=2 else 'icons/ico/APP_NHDDL_LST.ico' if launcher else 'icons/ico/APP_OPL_LST.ico', b'launcher icon'),
                                   ('icons/art/APP_RIPTOPL.png' if launcher>=2 else 'icons/art/APP_NHDDL.png' if launcher else 'icons/art/APP_OPL.png', b'launcher art')]
            manifest = []
            for i, (remote, data) in enumerate(package):
                (stage / f'launcher-{i:03}.bin').write_bytes(data)
                digest = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
                manifest.append(f'{i} {remote} {len(data)} {digest}\n')
            (work / 'launcher-manifest').write_text(''.join(manifest))
            (work / '__system/launcher').mkdir()
            for name in ('OPNPS2LD.ELF', 'nhddl.elf'):
                (work / '__system/launcher' / name).write_bytes(b'older archive launcher')
        subprocess.run([str(d / 'test'),str(work),str(flags),failure,str(launcher)],check=True,timeout=20)
        if flags & 8:
            assert (work / '__system/p2lboot/ps2-linux-ntsc').read_bytes() == b'new NTSC kernel'
            assert (work / '__system/p2lboot/ps2-linux-vga').read_bytes() == b'new VGA kernel'
            assert b'boot_circle = $PSBBN' in (work / '__sysconf/osdmenu/OSDMBR.CNF').read_bytes()
        if flags & 4 and failure == 'ok':
            assert (work / '__system/p2lboot/vmlinux').read_bytes() == b'new Japanese kernel'
            assert (work / 'PP.APP_WLE-R3Z/res/info.sys').exists()
            assert (work / 'PP.SYS_R3CONFIGURATOR/res/jkt_001.png').read_bytes() == b'jacket fixture'
            assert (work / 'PP.LAUNCHER/res/jkt_001.png').read_bytes() == b'launcher art'
            assert (b'APP_RIPTOPL' if launcher>=2 else b'APP_NHDDL' if launcher else b'APP_OPL') in (work / 'PP.LAUNCHER/res/info.sys').read_bytes()
            if launcher>=2:
                assert (work/'__system/launcher/RIPTOPL-RA-UNPAKT.ELF').read_bytes()==bytes(elf)
                assert (work/'ata/Ember/ember.elf').read_bytes()==bytes(elf)
                assert (work/'ata/Ember/bios.bin').read_bytes()==b'B'*524288
                assert b'Gageformer' in (work/'ata/Ember/README.txt').read_bytes()
                assert (work/'ata/Ember/games').is_dir() and not (work/'ata/Ember/ignored.bin').exists()
            else:
                assert (work / '__system/launcher/OPNPS2LD.ELF').read_bytes() == bytes(elf)
                assert (work / '__system/launcher/nhddl.elf').read_bytes() == bytes(elf)
            assert (work / 'ata/neutrino/modules/ata_bd.irx').read_bytes() == b'ATA module'
            assert (work / 'ata/neutrino/config/system.toml').read_bytes() == b'Neutrino configuration'
            assert (work / 'ata/neutrino/udpfs_server/compressed_iso/__init__.py').read_bytes() == b'UDPFS helper'
            assert (work / 'ata/LNG/lang_French.lng').read_bytes() == b'French translation'
            assert b'enable_bdm_hdd=1' in (work / 'ata' / ('settings_riptopl.cfg' if launcher>=2 else 'conf_opl.cfg')).read_bytes()
            if launcher>=2:
                assert f'default_core={int(launcher==3)}'.encode() in (work/'ata/settings_riptopl.cfg').read_bytes()
            if launcher<2:
                assert (work / '__system/launcher/licenses/OPL').read_bytes() == b'OPL license'
            else:
                assert not (work / '__system/launcher/licenses').exists()
            config = (work / '__sysconf/osdmenu/OSDMENU.CNF').read_bytes()
            assert b'name_OSDSYS_ITEM_1 = ' + (b'RiptOPL (Neutrino)' if launcher==3 else b'Open PS2 Loader: RiptOPL' if launcher>=2 else b'Neutrino (NHDDL)' if launcher else b'Open PS2 Loader') in config
    print('PASS: production PSU/app/HOSD copying, shared APPS attributes, launcher resources, staged hash checks, short I/O and failed-install unmounts')
