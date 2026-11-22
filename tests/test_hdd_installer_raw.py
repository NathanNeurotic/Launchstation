"""Exercise production IOP install sessions and bounds without a physical disk."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
s = (root / 'iop/hdd_format/main.c').read_text()
functions = s[s.index('static int transfer('):s.index('static void serve(')]
code = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hdd_format_rpc.h"
#define EXFAT
#define ATA_DIR_READ 0
#define ATA_DIR_WRITE 1
#define HDIOC_READSECTOR 1
#define HDIOC_WRITESECTOR 2
#define HDIOC_FLUSH 3
typedef struct { unsigned lba,size;unsigned char data[]; } hddAtaTransfer_t;
typedef struct { unsigned total_sectors;int exists,has_packet; } ata_devinfo_t;
struct block_device {const char *name; unsigned devNr,parNr,sectorSize,sectorOffset,sectorCount;};
static unsigned capacity,write_limit;
static int active,installing,detached,synchronized,disk_error;
static unsigned reads,writes,flushes,disconnects;
static unsigned char root_header[1024];
static ata_devinfo_t info={100u*1024*2048,1,0};
static struct block_device bd={"ata",0,0,512,0,100u*1024*2048};
static ata_devinfo_t *ata_get_devinfo(int unit){assert(!unit);return &info;}
static int ata_device_sector_io(int unit,void*p,unsigned lba,unsigned n,int dir){
    assert(!unit&&n&&lba<info.total_sectors&&n<=info.total_sectors-lba);
    if(installing)assert(synchronized); /* all install I/O uses HDD/PFS locking */
    if(disk_error)return -1;
    if(dir==ATA_DIR_READ){reads++;if(lba==0&&n==2)memcpy(p,root_header,1024);else memset(p,0,n*512);}
    else {assert(dir==ATA_DIR_WRITE);writes++;}
    return 0;
}
static int ata_device_flush_cache(int unit){assert(!unit);if(installing)assert(synchronized);flushes++;return disk_error?-1:0;}
static int iomanX_devctl(const char*dev,int op,void*arg,unsigned size,void*out,unsigned outsize){
    hddAtaTransfer_t *t=arg;int ret;assert(!strcmp(dev,"hdd0:")&&!synchronized);synchronized=1;
    if(op==HDIOC_FLUSH){assert(!arg&&!size&&!out&&!outsize);ret=ata_device_flush_cache(0);}
    else if(op==HDIOC_READSECTOR){assert(size==8&&out&&outsize==t->size*512);ret=ata_device_sector_io(0,out,t->lba,t->size,ATA_DIR_READ);}
    else {assert(op==HDIOC_WRITESECTOR&&size==8+t->size*512&&!out&&!outsize);ret=ata_device_sector_io(0,t->data,t->lba,t->size,ATA_DIR_WRITE);}
    synchronized=0;return ret<0?-EIO:0;
}
static void bdm_get_bd(struct block_device **list,unsigned n){assert(n==32);list[0]=&bd;}
static void bdm_disconnect_bd(struct block_device *p){assert(p==&bd);detached=1;disconnects++;}
'''
code += functions
code += r'''
static HddFormatPacket packet;
static void put(unsigned at,unsigned v){memcpy(root_header+at,&v,4);}
static void checksum(void){unsigned sum=0;for(unsigned i=4;i<1024;i+=4){unsigned v;memcpy(&v,root_header+i,4);sum+=v;}put(0,sum);}
static void setup(int jail){
    unsigned apa=10u*1024*2048,recovery=apa+2048,exfat=recovery+65536;
    memset(root_header,0,sizeof(root_header));put(4,0x00415041);
    if(jail){root_header[510]=0x55;root_header[511]=0xaa;root_header[450]=root_header[466]=0x17;root_header[482]=7;
        put(454,2048);put(458,apa);put(470,recovery);put(474,65536);put(486,exfat);put(490,info.total_sectors-exfat-33);}
    checksum();memset(&packet,0,sizeof(packet));packet.sectors=info.total_sectors;packet.write_limit=jail?exfat:0;
    active=installing=detached=synchronized=disk_error=0;reads=writes=flushes=disconnects=0;
}
static int request(int op,unsigned lba,unsigned n){packet.lba=lba;packet.count=n;command(op,&packet,op==HF_WRITE?64+n*512:64);return packet.result;}
int main(void){
    unsigned limit;
    setup(1);limit=packet.write_limit;assert(!request(HF_INSTALL_BEGIN,0,0)&&active&&!detached&&write_limit==limit);
    assert(!request(HF_READ,info.total_sectors-1,1)&&!detached); /* archive/recovery tail reads */
    assert(!request(HF_WRITE,8192,1)&&writes==1);
    assert(!request(HF_ZERO,limit-64,64)&&writes==2);
    assert(request(HF_WRITE,limit-1,2)==-EINVAL&&writes==2);
    assert(request(HF_ZERO,limit,1)==-EINVAL&&writes==2);
    assert(!request(HF_FINISH,0,0)&&!active&&!detached&&disconnects==0);
    assert(request(HF_WRITE,8192,1)==-EPERM);
    setup(1);packet.write_limit++;assert(request(HF_INSTALL_BEGIN,0,0)<0&&!active&&!detached&&!writes);
    setup(1);root_header[500]^=1;assert(request(HF_INSTALL_BEGIN,0,0)<0&&!active&&!detached);
    setup(1);put(474,65535);checksum();assert(request(HF_INSTALL_BEGIN,0,0)<0&&!active&&!detached);
    setup(1);packet.write_limit=0;assert(request(HF_INSTALL_BEGIN,0,0)<0&&!active&&!detached);
    setup(0);assert(!request(HF_INSTALL_BEGIN,0,0)&&!detached&&write_limit==info.total_sectors);
    assert(!request(HF_WRITE,8192,1));disk_error=1;assert(request(HF_ZERO,8192,64)==-EIO);disk_error=0;assert(!request(HF_FINISH,0,0));
    setup(1);assert(!request(HF_BEGIN,0,0)&&detached&&disconnects==1&&write_limit==info.total_sectors);
    assert(!request(HF_WRITE,packet.write_limit,1)); /* full formatting still owns all sectors */
    assert(!request(HF_FINISH,0,0)&&!active);
    puts("PASS: internal ATA source stays mounted; install I/O shares HDD/PFS locking; raw writes cannot enter exFAT; format sessions still detach ATA");
}
'''
with tempfile.TemporaryDirectory(prefix='hdd-installer-raw-') as directory:
    d = Path(directory); (d/'check.c').write_text(code)
    subprocess.run(['gcc','-std=c99','-Wall','-Wextra','-Werror','-I',str(root/'include'),str(d/'check.c'),'-o',str(d/'check')],check=True)
    subprocess.run([str(d/'check')],check=True,timeout=10)
