"""Production APA cache/journal/allocator on a sparse disk, including lost writes."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
apa = (root / 'iop/ps2hdd_osd/apa.c').read_text()
hdd = (root / 'iop/ps2hdd_osd/hdd.c').read_text()


def function(source, signature):
    start = source.index(signature)
    depth = 0
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', source[start:], re.S):
        depth += (token.group() == '{') - (token.group() == '}')
        if not depth and token.group() == '}':
            return source[start:start + token.end()] + '\n'
    raise AssertionError(signature)


code = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libapa.h"
#include "hdd_blkio.h"
#define MB 2048u
#define MINI (8*MB)
#define BLOCKS 8192
static apa_header_t disk[BLOCKS];
static unsigned char reserved[264*512] __attribute__((aligned(64)));
static apa_device_t hddDevices[1];
const char apaMBRMagic[]="Sony Computer Entertainment Inc.";
static u32 fail_lba;
static int failures, fail_read;
static unsigned writes;
void *apaAllocMem(int n){return malloc(n);}
void apaFreeMem(void*p){free(p);}
int apaGetTime(apa_ps2time_t*t){memset(t,0,sizeof(*t));return 0;}
int blkIoFlushCache(int device){assert(!device);return 0;}
int blkIoDmaTransfer(int device,void*data,u32 lba,u32 count,int writing){
    assert(!device&&count&&count<=2);
    if(lba==fail_lba && failures && writing!=fail_read){failures--;return -EIO;}
    void *p;
    if(lba<264 && lba>=2){assert(lba+count<=264);p=reserved+lba*512;}
    else {assert(!(lba%MINI)&&count==2&&lba/MINI<BLOCKS);p=&disk[lba/MINI];}
    if(writing){memcpy(p,data,count*512);writes++;}else memcpy(data,p,count*512);
    return 0;
}
'''
for sig in ('int apaCheckSum(', 'void apaSaveError(', 'int apaReadHeader(', 'int apaWriteHeader(',
            'apa_cache_t *apaFillHeader(', 'apa_cache_t *apaRemovePartition(',
            'apa_cache_t *apaInsertPartition(', 'void apaAddEmptyBlock(',
            'apa_cache_t *apaGetNextHeader(', 'apa_cache_t *apaFindPartition('):
    code += function(apa, sig)
code += function(hdd, 'apa_cache_t *hddAddPartitionHere(')
code += r'''
static void restart(void){assert(!apaCacheDeinit()&&!apaCacheInit(20));}
static apa_cache_t *find(const char*name,int*err){char id[32]={0};strcpy(id,name);*err=0;return apaFindPartition(0,id,err);}
static apa_cache_t *add(const char*name,unsigned mb,int*err){
    u32 bins[32]={0},tail=0;apa_cache_t*c=apaCacheGetHeader(0,0,0,err);assert(c);
    do{tail=c->header->start;apaAddEmptyBlock(c->header,bins);c=apaGetNextHeader(c,err);}while(c);
    assert(!*err);apa_params_t p={0};strcpy(p.id,name);p.size=mb*MB;p.type=0x100;
    return hddAddPartitionHere(0,&p,bins,tail,err);
}
static u32 setup(void){
    memset(disk,0,sizeof(disk));memset(reserved,0,sizeof(reserved));failures=0;fail_read=0;
    assert(!apaCacheInit(20)&&!apaJournalReset(0));
    hddDevices[0].totalLBA=BLOCKS*MINI;
    apa_params_t m={0};m.type=1;strcpy(m.id,"__mbr");int err=0;
    apa_cache_t*c=apaFillHeader(0,&m,0,0,0,128*MB,&err);assert(c);
    memcpy(c->header->mbr.magic,apaMBRMagic,sizeof(apaMBRMagic));
    assert(!apaCacheFlushAllDirty(0));apaCacheFree(c);
    u32 tail=0;
    for(unsigned i=0;i<223;i++){
        char name[32];snprintf(name,sizeof(name),"PP.APP%03u",i);
        c=add(name,128,&err);assert(c&&!err);tail=c->header->start;apaCacheFree(c);
    }
    restart();return tail;
}
static apa_journal_t *journal(void){return (apa_journal_t*)(reserved+(APA_SECTOR_APAL)*512);}
int main(void){
    int err=0;u32 tail=setup();
    fail_lba=tail;failures=1;
    apa_cache_t*c=add("+LSAPP.9c5459c3",8,&err);
    int reported_success=c!=NULL && !err;
    if(c)apaCacheFree(c);
    assert(!reported_success&&!c&&err==-EIO);
    assert(journal()->magic==APAL_MAGIC&&journal()->num>0);
    /* Failed dirty headers must survive eviction pressure until recovery. */
    for(unsigned i=0;i<222;i++){
        char name[32];snprintf(name,sizeof(name),"PP.APP%03u",i);c=find(name,&err);assert(c);apaCacheFree(c);
    }
    c=apaCacheGetHeader(0,tail,0,&err);assert(c&&(c->flags&APA_CACHE_FLAG_DIRTY));apaCacheFree(c);
    restart();failures=1;
    assert(apaJournalRestore(0)==-EIO&&journal()->num>0); /* failed replay keeps the log */
    assert(!apaJournalRestore(0)&&!journal()->num);
    c=find("+LSAPP.9c5459c3",&err);assert(c&&!err&&c->header->length==MINI);apaCacheFree(c);
    restart();c=find("+LSAPP.9c5459c3",&err);assert(c&&!err);apaCacheFree(c);
    assert(!apaCacheDeinit());
    /* Failure before the journal commits must never update the APA chain. */
    setup();fail_lba=APA_SECTOR_APAL_HEADERS;failures=1;
    c=add("+LSAPP.FAILED",8,&err);assert(!c&&err==-EIO&&!journal()->num);
    restart();assert(!apaJournalRestore(0));c=find("+LSAPP.FAILED",&err);assert(!c&&err==-ENOENT);
    /* A read error is not a successful stat/lookup of an absent partition. */
    restart();fail_lba=128*MB;fail_read=1;failures=1;
    c=find("missing",&err);assert(!c&&err==-EIO);
    /* Invalid journal counts cannot read beyond the protected journal area. */
    journal()->num=127;unsigned before=writes;
    assert(apaJournalRestore(0)==-EIO&&journal()->num==127&&writes==before);
    assert(!apaCacheDeinit());
    puts("PASS: production 20-entry APA cache, 223 partitions, lost link writes, durable journal replay/retry, eviction protection, failed journal writes and lookup error propagation");
}
'''
with tempfile.TemporaryDirectory(prefix='apa-metadata-') as tmp:
    tmp=Path(tmp)
    (tmp/'types.h').write_text('#include <stdint.h>\ntypedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef int32_t s32;typedef uint64_t u64;\n')
    (tmp/'hdd-ioctl.h').write_text('#define APA_IDMAX 32\n#define APA_PASSMAX 8\n#define APA_MAXSUB 64\n#define APA_FLAG_SUB 1\n#define APA_TYPE_FREE 0\n#define APA_IO_MODE_READ 0\n#define APA_IO_MODE_WRITE 1\n')
    (tmp/'iomanX.h').write_text('')
    (tmp/'test.c').write_text(code)
    # Legacy tracing uses the IOP's 32-bit long ABI; it is not under test.
    subprocess.run(['gcc','-std=c99','-Wall','-Wextra','-Werror','-Wno-format','-fsanitize=address,undefined',
                    '-fno-sanitize-recover=undefined','-DAPA_OSD_VER','-I',str(tmp),'-I',str(root/'iop/ps2hdd_osd'),
                    str(tmp/'test.c'),str(root/'iop/ps2hdd_osd/cache.c'),str(root/'iop/ps2hdd_osd/journal.c'),
                    '-o',str(tmp/'test')],check=True)
    result = subprocess.run([str(tmp/'test')], capture_output=True, text=True, timeout=30)
    if result.returncode:
        print(result.stdout, end='')
        print(result.stderr, end='')
        result.check_returncode()
    print(result.stdout.splitlines()[-1])
