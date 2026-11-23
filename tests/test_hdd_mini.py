"""Run the production IOP allocator/split/coalescing code on an in-memory APA disk."""
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


apa = (root / "iop/ps2hdd_osd/apa.c").read_text()
hdd = (root / "iop/ps2hdd_osd/hdd.c").read_text()
code = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libapa.h"
#include "hdd_format.h"
#define APA_IO_MODE_READ 0
#define APA_IO_MODE_WRITE 1
#define MB 2048u
#define BLOCKS 512
static apa_header_t headers[BLOCKS];
static apa_cache_t cache[BLOCKS];
static apa_device_t hddDevices[1];
static unsigned flushes;
int apaGetTime(apa_ps2time_t *time) { memset(time,0,sizeof(*time));return 0; }
apa_cache_t *apaCacheGetHeader(s32 device,u32 lba,u32 mode,int *error) {
    assert(device == 0 && lba % (8 * MB) == 0 && lba / (8 * MB) < BLOCKS);
    unsigned i=lba/(8*MB);*error=0;
    if(mode == APA_IO_MODE_READ) assert(headers[i].magic == APA_MAGIC);
    cache[i].header=&headers[i];cache[i].sector=lba;cache[i].device=0;
    return &cache[i];
}
void apaCacheFree(apa_cache_t *c) { assert(c); }
int apaCacheFlushAllDirty(s32 device) {
    assert(!device);flushes++;
    for(unsigned i=0;i<BLOCKS;i++) if(cache[i].flags & APA_CACHE_FLAG_DIRTY) {
        headers[i].checksum=apaCheckSum(&headers[i],1);cache[i].flags=0;
    }
    return 0;
}
'''
for name in ("int apaCheckSum(", "apa_cache_t *apaFillHeader(", "apa_cache_t *apaRemovePartition(",
             "void apaMakeEmpty(", "apa_cache_t *apaInsertPartition(", "void apaAddEmptyBlock(",
             "apa_cache_t *apaDeleteFixPrev(", "apa_cache_t *apaDeleteFixNext(", "int apaDelete("):
    code += function(apa, name)
code += function(hdd, "apa_cache_t *hddAddPartitionHere(")
code += r'''
static int readSector(void *ctx,uint32_t lba,void *bytes) {
    (void)ctx;unsigned i=(lba/ (8*MB)),offset=lba%(8*MB);
    if(i>=BLOCKS || offset>1 || headers[i].magic!=APA_MAGIC)return -EIO;
    memcpy(bytes,(unsigned char*)&headers[i]+offset*512,512);return 0;
}
static void validate(void) {
    unsigned lba=0,prev=0,count=0;
    do {
        apa_header_t *h=&headers[lba/(8*MB)];
        assert(h->magic==APA_MAGIC && h->checksum==(u32)apaCheckSum(h,1));
        assert(h->start==lba && h->length>=8*MB && !(h->length&(h->length-1)) && !(lba%h->length));
        assert((uint64_t)lba+h->length<=hddDevices[0].totalLBA && ++count<=BLOCKS);
        if(lba)assert(h->prev==prev);
        if(h->next)assert(h->next==lba+h->length);
        prev=lba;lba=h->next;
    }while(lba);
    assert(headers[0].prev==prev);
}
static unsigned add(unsigned size) {
    unsigned bins[32]={0},lba=0,prev=0;int error=0;
    int guard=hddApaCanAllocate(readSector,NULL,hddDevices[0].totalLBA,size*MB);
    if(guard<0){assert(guard==-ENOSPC);return 0;}
    do {
        apa_header_t *h=&headers[lba/(8*MB)];apaAddEmptyBlock(h,bins);prev=lba;lba=h->next;
    }while(lba);
    apa_params_t p={0};p.size=size*MB;p.type=0x100;strcpy(p.id,"app");
    apa_cache_t *c=hddAddPartitionHere(0,&p,bins,prev,&error);
    assert(c && !error);unsigned start=c->header->start;apaCacheFree(c);validate();return start;
}
static void del(unsigned lba) {
    int error;assert(lba);apa_cache_t*c=apaCacheGetHeader(0,lba,APA_IO_MODE_READ,&error);
    assert(c->header->type==0x100 && !apaDelete(c));validate();
}
int main(void) {
    assert(sizeof(apa_header_t)==1024);
    hddDevices[0].totalLBA=4096*MB;
    apa_params_t m={0};m.type=1;int error;
    apa_cache_t*c=apaFillHeader(0,&m,0,0,0,128*MB,&error);apaCacheFlushAllDirty(0);apaCacheFree(c);
    assert(add(128)==128*MB);assert(add(256)==256*MB);assert(add(512)==512*MB);assert(add(1024)==1024*MB);
    unsigned a=add(8),b=add(128),d=add(8);
    assert(a==2048*MB && b==2176*MB && d==2056*MB);
    assert(headers[2064/8].length==16*MB && headers[2080/8].length==32*MB && headers[2112/8].length==64*MB);
    del(a);del(d);assert(headers[2048/8].type==0 && headers[2048/8].length==128*MB);
    assert(headers[b/(8*MB)].type==0x100 && headers[b/(8*MB)].length==128*MB); /* occupied neighbor never split */
    assert(add(8)==2048*MB);assert(add(8)==2056*MB);assert(add(8)==2064*MB);
    assert(add(8)==2072*MB);assert(add(8)==2080*MB); /* split the free 32 MiB extent */
    assert(headers[2088/8].type==0 && headers[2088/8].length==8*MB);
    assert(headers[2096/8].type==0 && headers[2096/8].length==16*MB);
    /* A2 hybrid boundary: physically 4 GiB, only 2.5 GiB reserved for APA. */
    unsigned char *raw=(unsigned char*)&headers[0];
    raw[510]=0x55;raw[511]=0xaa;raw[450]=0x17;
    uint32_t start=2048,length=2560*MB;memcpy(raw+454,&start,4);memcpy(raw+458,&length,4);
    raw[466]=7;start+=length;length=4096*MB-start;memcpy(raw+470,&start,4);memcpy(raw+474,&length,4);
    cache[0].flags|=APA_CACHE_FLAG_DIRTY;apaCacheFlushAllDirty(0);
    unsigned live[400],n=0;
    srand(23);
    for(unsigned i=0;i<2000;i++) {
        if(n && rand()%3==0) { unsigned j=rand()%n;del(live[j]);live[j]=live[--n]; }
        else { unsigned pos=add(8u<<(rand()%6));if(pos)live[n++]=pos; }
        assert(n<400);validate();
        assert(hddApaCanAllocate(readSector,NULL,hddDevices[0].totalLBA,4*MB)==-EINVAL);
        for(unsigned lba=0;;){apa_header_t*h=&headers[lba/(8*MB)];assert(lba+h->length<=2560*MB);if(!h->next)break;lba=h->next;}
    }
    /* Malformed chain and tail links must fail before a mutation. */
    headers[0].prev^=8*MB;cache[0].flags|=APA_CACHE_FLAG_DIRTY;apaCacheFlushAllDirty(0);
    assert(hddApaCanAllocate(readSector,NULL,hddDevices[0].totalLBA,8*MB)==-EIO);
    assert(flushes>1000);
    puts("PASS: production mini allocator, mixed 8/128/8 chain, padding, free splits/coalescing, 2000 fragmented operations and APA Jail bounds");
}
'''
with tempfile.TemporaryDirectory(prefix="apa-mini-") as tmp:
    tmp = Path(tmp)
    (tmp / "types.h").write_text("#include <stdint.h>\ntypedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef int32_t s32;typedef uint64_t u64;\n")
    (tmp / "hdd-ioctl.h").write_text("#define APA_IDMAX 32\n#define APA_PASSMAX 8\n#define APA_MAXSUB 64\n#define APA_FLAG_SUB 1\n#define APA_TYPE_FREE 0\n")
    (tmp / "test.c").write_text(code)
    subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined", "-fno-sanitize-recover=undefined", "-DAPA_OSD_VER", "-I", str(tmp),
                    "-I", str(root / "include"), "-I", str(root / "iop/ps2hdd_osd"), str(tmp / "test.c"),
                    str(root / "src/hdd_apa_boundary.c"), "-o", str(tmp / "test")], check=True)
    subprocess.run([str(tmp / "test")], check=True, timeout=20)
