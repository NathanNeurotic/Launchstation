"""Exercise PSBBN partition creation against the production APA size parser."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'src/hdd_install_psbbn.c').read_text()
driver = (root/'iop/ps2hdd_osd/hdd_fio.c').read_text()
def function(text, signature):
    start=text.index(signature)
    tokens=re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]',text[start:],re.S)
    depth=0
    for token in tokens:
        depth+=(token.group()=='{')-(token.group()=='}')
        if not depth and token.group()=='}': return text[start:start+token.end()]+'\n'
    raise AssertionError(signature)

code=r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "hdd_install.h"
#define APA_PRINTF(...) ((void)0)
#define FIO_O_CREAT 1
#define FIO_O_EXCL 2
#define FIO_O_RDWR 4
#define FIO_MT_RDWR 1
#define FS_GROUP_APPLICATION 1
#define HDIOC_FLUSH 1
typedef struct {unsigned private_5,size,mode,attr,private_0;} iox_stat_t;
static unsigned expected_mb[]={512,128,512,512,128,256,2048,1024,2048,8192};
static unsigned expected_count=8,contents_mb=1024;
static const char *const expected_names[]={"__linux.1","__linux.2","__linux.4","__linux.5","__linux.6","__linux.7","__linux.9","__linux.8","__linux.3","__linux.10"};
static unsigned allocated,opened,closed,stats,contents,mounted,flushed,mkdirs,current_lba;
static unsigned starts[10];
static int fail_index,fail_phase;
'''
# Real stock-driver syntax. Its parser intentionally accepts 1G/2G, rather
# than 1024M/2048M, regardless of whether the optional mini APA mode is built.
start=driver.index('#ifndef APA_8MB_PARTITION_SIZE')
end=driver.index('///////////////////////////////////////////////////////////////////////////////',start)
code+=driver[start:end]
code+=function(driver, 'static int fioPartitionSizeLookUp(char *str)\n{')
code+=r'''
void hddInstallLog(const char *format,...){(void)format;}
void hddInstallLogFlush(HddInstall *in){assert(in->jail);}
void hddInstallStatus(const char*s,int done,int total){assert(s&&total==(int)expected_count+1&&done==(int)opened);}
int hddCheckAllocation(uint32_t sectors){
    assert(allocated<expected_count && sectors==expected_mb[allocated]*2048u);
    allocated++;return fail_index==(int)opened&&fail_phase==1?-ENOSPC:0;
}
static int fileXioOpen(const char *path,int flags,int mode){
    char name[32],size[16],type[16];unsigned i=opened++;
    assert(i<expected_count && flags==7 && mode==0);
    assert(sscanf(path,"hdd0:%31[^,],,,%15[^,],%15s",name,size,type)==3);
    assert(!strcmp(name,expected_names[i]));
    int sectors=fioPartitionSizeLookUp(size);
    if(sectors<0){fprintf(stderr,"Driver rejected %s: %s\n",name,size);return sectors;}
    assert(sectors==(int)(expected_mb[i]*2048u));
    assert(!strcmp(type,i==1?"EXT2SWAP":"EXT2"));
    if(fail_index==(int)i&&fail_phase==2)return -EIO;
    current_lba=(current_lba+sectors-1)&~(sectors-1);starts[i]=current_lba;current_lba+=sectors;
    return 100+i;
}
static int fileXioClose(int fd){assert(fd==99+(int)opened);closed++;return fail_index==(int)opened-1&&fail_phase==3?-EIO:0;}
static int fileXioGetStat(const char *path,iox_stat_t *out){
    unsigned i=opened-1;char expected[64];stats++;
    snprintf(expected,sizeof(expected),"hdd0:%s",expected_names[i]);assert(!strcmp(path,expected));
    if(fail_index==(int)i&&fail_phase==4)return -EIO;
    memset(out,0,sizeof(*out));out->private_5=starts[i];out->size=expected_mb[i]*2048u;out->mode=i==1?0x82:0x83;
    if(fail_index==(int)i&&fail_phase==5)out->private_0=1;
    return 0;
}
static int hddMakeFilesystem(int mb,char *name,int group){assert(opened==expected_count&&closed==expected_count&&stats==expected_count&&mb==(int)contents_mb&&group==FS_GROUP_APPLICATION&&!strcmp(name,"__contents"));contents++;return mb;}
static int fileXioMount(const char *mount,const char *partition,int flags){assert(contents==1&&!mounted&&!strcmp(mount,"pfs0:")&&!strcmp(partition,"hdd0:__common")&&flags==FIO_MT_RDWR);mounted++;return 0;}
static int genMkdir(const char *path,int mode){assert(mounted&&!strcmp(path,"pfs0:/Your Saves")&&mode==0777);mkdirs++;return 0;}
static int fileXioUmount(const char *mount){assert(mounted&&!strcmp(mount,"pfs0:"));mounted--;return 0;}
static int fileXioDevctl(const char *dev,int command,void*a,int n,void*b,int m){assert(!strcmp(dev,"hdd0:")&&command==HDIOC_FLUSH&&!a&&!b&&!n&&!m&&!mounted);flushed++;return 0;}
'''
code+=source[source.index('static const char *const linux_names'):source.index('static const char *const pfs_names')]
code+=function(source,'int hddInstallPsbbnPartitions(')
code+=r'''
static void reset(void){allocated=opened=closed=stats=contents=mounted=flushed=mkdirs=0;current_lba=4194304;}
int main(void){
    HddInstall in={.jail=1};
    assert(fioPartitionSizeLookUp("2048M")==-EINVAL && fioPartitionSizeLookUp("1024M")==-EINVAL);
    reset();fail_index=-1;assert(hddInstallPsbbnPartitions(&in)==0);
    assert(allocated==8&&opened==8&&closed==8&&stats==8&&contents==1&&!mounted&&mkdirs==1&&flushed==1);
    for(unsigned i=0;i<8;i++)assert(in.linux[i].start==starts[i]&&in.linux[i].sectors==expected_mb[i]*2048u);
    /* Abort at the affected 2 GiB partition, keeping earlier allocations. */
    for(int phase=1;phase<=5;phase++){
        reset();fail_index=6;fail_phase=phase;
        int ret=hddInstallPsbbnPartitions(&in);
        assert(ret==(phase==1?-ENOSPC:phase==5?-EINVAL:-EIO));
        assert(allocated==7&&opened==(phase==1?6u:7u)&&closed==(phase<3?6u:7u));
        assert(!contents&&!mounted&&!mkdirs&&!flushed);
    }
    reset();fail_index=-1;expected_count=10;contents_mb=4096;expected_mb[7]=4096;
    in.flags=15;in.music_mb=4096;in.contents_mb=4096;in.linux_home_mb=8192;
    assert(hddInstallPsbbnPartitions(&in)==0&&opened==10&&closed==10&&stats==10&&contents==1&&flushed==1);
    for(unsigned i=0;i<10;i++)assert(in.linux[i].sectors==expected_mb[i]*2048u);
    reset();in.jail=0;assert(hddInstallPsbbnPartitions(&in)==-EINVAL&&!allocated&&!opened);
    puts("PASS: all eight PSBBN partition requests use stock APA size syntax, validate extents, abort on errors and create/flush contents and saves folders");
}
'''
with tempfile.TemporaryDirectory(prefix='hdd-installer-partitions-') as directory:
    d=Path(directory);(d/'check.c').write_text(code)
    for mini in (False,True):
        subprocess.run(['gcc','-std=c99','-Wall','-Wextra','-Werror','-I',str(root/'include'),*(['-DAPA_8MB_PARTITION_SIZE'] if mini else []),str(d/'check.c'),str(root/'src/hdd_install_plan.c'),'-o',str(d/'check')],check=True)
        subprocess.run([str(d/'check')],check=True,timeout=10)
