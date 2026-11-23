"""Exercise archive-wide progress separately from individual installed files."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/hdd_install_psbbn.c").read_text()


def function(signature):
    a = source.index(signature)
    depth = 0
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', source[a:], re.S):
        depth += (token.group() == "{") - (token.group() == "}")
        if not depth and token.group() == "}":
            return source[a:a + token.end()] + "\n"
    raise AssertionError(signature)


code = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hdd_install.h"
int hddInstallRecoveryFault(void){return 0;}
void hddInstallRecoveryObserve(int rc){(void)rc;}
typedef int64_t s64;
typedef struct PreflightPath PreflightPath;
#include "install_ext2.h"
#define MAX_PATH 1025
#define FIO_O_RDONLY 1
'''
a = source.index("typedef struct {\n    HddInstall *in;")
code += source[a:source.index("} Payload;", a) + len("} Payload;")]
code += r'''
static HddInstall settings;
static uint64_t overall;
static unsigned pos[HDD_INSTALL_ASSETS],opened,closed,current_updates,live_done,live_total;
static int read_error,write_error,close_error,ext2_error;
static int genOpen(const char *path,int mode){
 assert(mode==FIO_O_RDONLY);opened++;
 for(unsigned i=HI_SYSTEM;i<HDD_INSTALL_ASSETS;i++)if(strstr(path,hddInstallAssetName(i))){pos[i]=0;return 100+i;}
 assert(0);return -1;
}
static int64_t genLseek(int fd,int offset,int whence){assert(fd>=100&&whence==SEEK_SET);pos[fd-100]=offset;return offset;}
static int genRead(int fd,void *data,unsigned size){
 unsigned asset=fd-100;assert(asset<HDD_INSTALL_ASSETS&&pos[asset]+size<=settings.sizes[asset]);
 if(read_error&&asset==HI_LANGUAGE)return -EIO;
 memset(data,1,size);pos[asset]+=size;return size;
}
static int genWrite(int fd,void *data,unsigned size){assert(fd==9&&data);if(write_error)return -EIO;return size>4?4:(int)size;}
static int genClose(int fd){assert(fd==9||fd>=100);closed++;return close_error;}
int installExt2Data(InstallExt2 *fs,const void *data,unsigned size){assert(fs&&data&&size);return ext2_error;}
int installExt2End(InstallExt2 *fs){assert(fs);return ext2_error;}
void hddInstallLog(const char *s,...){assert(s);}
void hddInstallLogFlush(HddInstall *in){assert(in);}
void hddInstallStatus(const char *s,int done,int total){assert(s&&done>=0&&done<=total);}
void hddInstallProgress(const char *s,int done,int total){
 assert(s&&done>=0&&done<=total);current_updates++;live_done=done;live_total=total;
}
void hddInstallOverall(uint64_t done,uint64_t total){
 assert(total&&done<=total);uint64_t value=done*1000/total;assert(value>=overall);overall=value;
}
static int begin(void *context,const InstallTarEntry *entry){(void)context;(void)entry;assert(0);return -EINVAL;}
int installTarGzip(const InstallArchiveIo *io,uint32_t size,const InstallTarSink *sink){
 unsigned char bytes[128];assert(sink&&size>1&&size<=sizeof(bytes));
 int ret=io->read(io->context,0,bytes,1);
 return ret<0?ret:io->read(io->context,1,bytes,size-1);
}
'''
code += function("static int data(") + function("static int end(") + function("static int readArchive(") + function("static int archives(")
code += r'''
int main(void){
 strcpy(settings.stage,"ata0:/stage");settings.sizes[HI_SYSTEM]=100;settings.sizes[HI_LANGUAGE]=60;settings.sizes[HI_PS2LINUX]=40;
 settings.sizes[HI_CHANNELS]=30;
 for(unsigned japanese=0;japanese<2;japanese++)for(unsigned linux=0;linux<2;linux++){
  settings.flags=7|(linux?HDD_INSTALL_PS2LINUX:0);
  strcpy(settings.language.code,japanese?"jpn":"eng");
  Payload p={.in=&settings,.output=-1};
  overall=opened=closed=current_updates=0;
  assert(archives(&p)==0&&opened==2+linux+japanese&&closed==opened);
  assert(p.archive_done==160+40*linux+30*japanese&&p.archive_total==p.archive_done);
  assert(overall==900);
  /* Reading compressed input must not replace the current installed file. */
  assert(current_updates==0);
 }
 Payload p={.in=&settings,.output=-1};overall=0;read_error=1;
 assert(archives(&p)==-EIO&&p.archive_done==100&&overall<1000);read_error=0;
 /* ext2 and PFS sinks report only the current file's uncompressed bytes. */
 unsigned char bytes[12]={0};p.file_size=12;p.file_done=0;p.route=0;p.fs[0]=(InstallExt2*)1;
 strcpy(p.current,"__linux.4/bn/example");
 assert(data(&p,bytes,5)==0&&live_done==5&&live_total==12);
 ext2_error=-EIO;assert(data(&p,bytes,7)==-EIO&&live_done==5);ext2_error=0;
 assert(data(&p,bytes,7)==0&&live_done==12&&end(&p)==0);
 p.route=HDD_INSTALL_LINUX_PARTITIONS;p.output=9;p.file_done=0;
 assert(data(&p,bytes,12)==0&&live_done==12&&end(&p)==0&&p.output==-1);
 p.output=9;p.file_done=0;write_error=1;
 assert(data(&p,bytes,12)==-EIO&&p.file_done==0);write_error=0;close_error=-EIO;
 assert(end(&p)==-EIO&&p.output==-1);
 puts("PASS: cumulative archive progress across optional Linux, separate per-file installation bytes, and read/write/close errors");
}
'''
with tempfile.TemporaryDirectory(prefix="hdd-installer-progress-") as directory:
    d = Path(directory)
    (d / "test.c").write_text(code)
    subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined", "-I", str(root / "include"),
                    str(d / "test.c"), str(root / "src/hdd_install_plan.c"), "-o", str(d / "test")], check=True)
    subprocess.run([str(d / "test")], check=True, timeout=10)
