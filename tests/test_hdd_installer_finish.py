"""Exercise production installer commit ordering and failures without disk I/O."""
from pathlib import Path
import hashlib
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/hdd_format_ps2.c').read_text()
zero = source[source.index('static int zeroDisk('):source.index('static int flushDisk(')]
function = zero + source[source.index('int hddInstallFinish('):]
code = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hdd_format.h"
#include "save_sign.h"
#include "hdd_install.h"
typedef int64_t s64;
typedef struct { uint32_t start, size; } hddSetOsdMBR_t;
#define FIO_O_RDONLY 1
#define HDIOC_SETOSDMBR 2
#define HF_BEGIN 3
#define HF_FINISH 4
#define HF_ZERO 5
#define MBR_DIGEST @@MBR_DIGEST@@
#define HF_INSTALL_BEGIN 6
static struct { uint32_t sectors, write_limit; } packet;
static unsigned char payload[66048], disk[66048], header[1024];
static unsigned offset, activated, recovered, populated, flushed, flushed_writes, reads, writes;
static unsigned zeroed, zero_calls;
static int zero_error;
static int session, begun, finished, bad_read, bad_flush, bad_limit, populate_error;
static HddFormatPlan plan;
static int genOpen(const char *name, int mode) { assert(name && mode == FIO_O_RDONLY); offset = 0; return 9; }
static s64 genLseek(int fd, int pos, int whence) { assert(fd == 9 && pos == 0); if(whence == SEEK_END) return sizeof(payload); offset = 0; return 0; }
static int genRead(int fd, void *buf, unsigned n) {
    assert(fd == 9); if(n > 37) n = 37; assert(offset + n <= sizeof(payload)); memcpy(buf, payload + offset, n); offset += n; return n;
}
static int genClose(int fd) { assert(fd == 9); return 0; }
static int call(int cmd, uint32_t lba, unsigned count, unsigned send, unsigned receive) {
    assert(send == 64 && receive == 64);
    if(cmd == HF_ZERO) { assert(count && count <= 2048); zero_calls++; if(zero_error) return -EIO; zeroed += count; return 0; }
    assert(!lba && !count);
    if(cmd == HF_BEGIN || cmd == HF_INSTALL_BEGIN) { assert(cmd == HF_INSTALL_BEGIN && packet.write_limit == plan.exfat_start); assert(!session && packet.sectors == plan.sectors); session = 1; begun++; }
    else { assert(cmd == HF_FINISH && session); session = 0; finished++; }
    return 0;
}
static int readDisk(void *ctx, uint32_t lba, unsigned count, void *data) {
    (void)ctx; assert(session); reads++;
    if(lba == 0) { assert(count == 2); memcpy(data, header, 1024); return 0; }
    assert(lba >= 8192 && (lba - 8192 + count) * 512 <= sizeof(disk));
    memcpy(data, disk + (lba - 8192) * 512, count * 512);
    if(bad_read) ((char *)data)[0] ^= 1;
    return 0;
}
static int writeDisk(void *ctx, uint32_t lba, unsigned count, const void *data) {
    (void)ctx; assert(session && !activated && lba >= 8192 && (lba - 8192 + count) * 512 <= sizeof(disk)); writes++;
    memcpy(disk + (lba - 8192) * 512, data, count * 512); return 0;
}

static int flushDisk(void *ctx) { (void)ctx; assert(session); flushed++; if(!bad_flush)flushed_writes=writes; return bad_flush ? -EIO : 0; }
static int apaDisk(void *ctx) { (void)ctx; assert(0); return 0; }
static void progress(void *ctx, const char *s, uint32_t n, uint32_t total) { (void)ctx; (void)s; (void)n; (void)total; }
static void dashWorkerStatus(const char *s, uint32_t n, uint32_t total) { assert(s && n <= total); }
static int readPhysicalSector(void *ctx, uint32_t lba, void *data) { return readDisk(ctx, lba, 1, data); }
int hddApaLimit(HddFsReadSector read, void *ctx, uint32_t total, uint32_t *limit) {
    (void)read; (void)ctx; assert(total == plan.sectors && session); *limit = plan.kind == HDD_FORMAT_APA_JAIL ? plan.apa_sectors : plan.sectors; return bad_limit ? -EINVAL : 0;
}
static int fileXioDevctl(const char *path, int cmd, const void *data, unsigned size, void *out, unsigned outsize) {
    const hddSetOsdMBR_t *info = data;
    assert(!strcmp(path,"hdd0:") && cmd == HDIOC_SETOSDMBR && size == sizeof(*info) && !out && !outsize);
    assert(flushed && writes && flushed_writes==writes && !memcmp(disk,payload,sizeof(disk)));
    memcpy(header + 304,&info->start,4); memcpy(header + 308,&info->size,4); activated++; return 0;
}
int hddFormatRecovery(const HddFormatPlan *p, const HddFormatIo *io) { assert(p == &plan && io && session); recovered++; return 0; }
static int populate(void *ctx, const HddFormatIo *io) { assert(ctx == &plan && io && !activated && !writes && session); populated++; return populate_error; }
'''
code += r'''

void hddInstallLog(const char *format, ...) { (void)format; }
int hddInstallLogEnabled(void) { return 0; }
void hddInstallLogFlush(HddInstall *in) { (void)in; }
void hddInstallStatus(const char *s,int done,int total) { dashWorkerStatus(s,done,total); }
void hddInstallProgress(const char *s,int done,int total) { dashWorkerStatus(s,done,total); }
void hddInstallOverall(uint64_t done,uint64_t total) { assert(total&&done<=total); }
void hddInstallPhase(unsigned phase) { assert(phase==HDD_INSTALL_PHASE_FINISH&&!activated); }
'''
code += function
code += r'''
static void clear(void) {
    memset(header,0,sizeof(header)); memset(disk,0,sizeof(disk)); memset(payload,0x5a,sizeof(payload));
    memcpy(payload,"\x01\x00\x00\x04",4);
    offset=activated=recovered=populated=flushed=flushed_writes=reads=writes=0;
    session=begun=finished=bad_read=bad_flush=bad_limit=populate_error=0;
    assert(hddFormatPlan(&plan,HDD_FORMAT_APA_JAIL,100u*1024*2048,8192,1)==0);
}
int main(void) {
    int started = 8;
    assert(zeroDisk(NULL,100,5000)==0 && zero_calls==3 && zeroed==5000);
    zero_error=1; assert(zeroDisk(NULL,100,5000)==-EIO && zero_calls==4 && zeroed==5000);
    zero_error=0;
    clear(); assert(hddInstallFinish(&plan,"mbr",MBR_DIGEST,&started,populate,&plan)==0);
    assert(started && begun==1 && finished==1 && activated==1 && recovered==0 && populated==1 && !session);
    clear(); assert(hddInstallFinish(&plan,"mbr","0000000000000000000000000000000000000000",&started,populate,&plan)==-EIO);
    assert(started && writes && !activated && !recovered && finished==1);
    clear(); populate_error=-EIO; assert(hddInstallFinish(&plan,"mbr",MBR_DIGEST,&started,populate,&plan)==-EIO);
    assert(started && populated==1 && !writes && !activated && !recovered && finished==1);
    clear(); bad_read=1; assert(hddInstallFinish(&plan,"mbr",MBR_DIGEST,&started,NULL,NULL)==-EIO);
    assert(started && writes==1 && !activated && !recovered && finished==1);
    clear(); bad_flush=1; assert(hddInstallFinish(&plan,"mbr",MBR_DIGEST,&started,NULL,NULL)==-EIO);
    assert(started && writes && !activated && !recovered && finished==1);
    clear(); bad_limit=1; assert(hddInstallFinish(&plan,"mbr",MBR_DIGEST,&started,NULL,NULL)<0);
    assert(started && !writes && !activated && finished==1);
    clear(); payload[0]=0; assert(hddInstallFinish(&plan,"mbr",MBR_DIGEST,&started,NULL,NULL)<0);
    assert(started && !writes && !activated && finished==1);
    clear(); plan.exfat_start++; assert(hddInstallFinish(&plan,"mbr",MBR_DIGEST,&started,NULL,NULL)==-EINVAL);
    assert(!started && !begun && !writes && !activated);
    clear(); assert(hddInstallFinish(&plan,NULL,NULL,&started,NULL,NULL)==0);
    assert(started && !writes && !activated && recovered==0 && finished==1);
    puts("PASS: software before MBR, short reads, readback/flush checks, failed population never activates boot, recovery preservation and session cleanup");
}
'''
payload = b'\x01\x00\x00\x04' + b'\x5a' * (66048 - 4)
digest = hashlib.sha1(f'blob {len(payload)}\0'.encode() + payload).hexdigest()
code = code.replace('@@MBR_DIGEST@@', '"' + digest + '"')
with tempfile.TemporaryDirectory(prefix='hdd-installer-finish-') as temp:
    p = Path(temp); (p / 'test.c').write_text(code)
    subprocess.run(['gcc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'include'),
                    str(p / 'test.c'), str(root / 'src/hdd_format.c'), str(root / 'src/hdd_format_upcase.c'),
                    str(root / 'src/save_sign.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True, timeout=10)
