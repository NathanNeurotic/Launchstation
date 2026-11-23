"""Exercise untrusted PSU metadata, HDD template extraction and durable records."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
header_source = (root / "src/hdd_header_injector.c").read_text()
start = header_source.index("int InjectHddAppAssets(")
header_function = header_source[start:]  # Last function in this translation unit.
code = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hdd_app.h"
#define MAX_NAME 64
#define APA_TYPE_PFS 0x100
#define APA_HEADER_MAGIC "PS2ICON3D"
#define HDIOC_FLUSH 1
typedef uint32_t u32;
typedef struct {unsigned mode,private_5;} iox_stat_t;
static unsigned char hdd_header_buffer[2*1024*1024],disk[2*1024*1024];
static unsigned flushes,wrong_partition,write_failure;
static int hddHeaderBuildPartitionPath(char*out,unsigned size,const char*name){return snprintf(out,size,"hdd0:%s",name)>0?0:-EINVAL;}
static int fileXioGetStat(const char*path,iox_stat_t*stat){assert(!strcmp(path,"hdd0:PP.APP_MY_APP"));stat->mode=wrong_partition?0x1337:APA_TYPE_PFS;stat->private_5=262144;return 0;}
static int hddHeaderWriteRawRange(u32 lba,u32 offset,const void*data,u32 n){assert(lba==262144&&offset==0x1000&&n<0x111000);if(write_failure)return -EIO;memcpy(disk+offset,data,n);return 0;}
static int fileXioDevctl(const char*dev,int cmd,void*a,int b,void*c,int d){assert(!strcmp(dev,"hdd0:")&&cmd==HDIOC_FLUSH&&!a&&!b&&!c&&!d);flushes++;return 0;}
'''
code += header_function
code += r'''
static unsigned get32(unsigned pos){uint32_t n;memcpy(&n,disk+pos,4);return n;}
static void headerTest(const char*cnf,const char*settings){
    memset(disk,0x5a,sizeof(disk));unsigned char list[111],del[77];memset(list,0x37,sizeof(list));memset(del,0x41,sizeof(del));
    assert(!InjectHddAppAssets("PP.APP_MY_APP",cnf,settings,list,sizeof(list),del,sizeof(del))&&flushes==1);
    for(unsigned i=0;i<0x1000;i++)assert(disk[i]==0x5a); /* no APA/journal write */
    assert(!memcmp(disk+0x1000,"PS2ICON3D",9));assert(get32(0x1010)==0x200&&get32(0x1014)==strlen(cnf));
    assert(get32(0x1018)==0x400&&get32(0x101c)==strlen(settings));
    assert(get32(0x1020)==0x800&&get32(0x1024)==sizeof(list));assert(!memcmp(disk+0x1800,list,sizeof(list)));
    unsigned del_pos=get32(0x1028)+0x1000;assert(del_pos==((0x1800+sizeof(list)+63)&~63u)&&get32(0x102c)==sizeof(del));assert(!memcmp(disk+del_pos,del,sizeof(del)));
    assert(get32(0x1030)==0&&get32(0x1034)==0); /* no stale KELF pointer */
    assert(InjectHddAppAssets("PP.APP_MY_APP",cnf,"PS2D",list,sizeof(list),del,sizeof(del))==-EINVAL);
    assert(InjectHddAppAssets("PP.APP_MY_APP",cnf,settings,list,0x80001,del,sizeof(del))==-EINVAL);
    wrong_partition=1;assert(InjectHddAppAssets("PP.APP_MY_APP",cnf,settings,list,sizeof(list),del,sizeof(del))==-EINVAL);wrong_partition=0;
    write_failure=1;assert(InjectHddAppAssets("PP.APP_MY_APP",cnf,settings,list,sizeof(list),del,sizeof(del))==-EIO&&flushes==1);
}

static const unsigned char elf[52] = {0x7f,'E','L','F',1,1,1,[16]=2,[18]=8};
static void entry(SaveArchiveEntry *e,const char *name,const void *data,unsigned size) {
    memset(e,0,sizeof(*e));strcpy(e->name,name);e->data=data;e->size=size;e->mode=0x8497;
}
int main(void) {
    HddApp app;SaveArchiveEntry files[8];SaveArchive save={0};save.files=files;
    strcpy(save.folder.name,"APP_MY_APP");save.count=1;entry(files,"BOOT.ELF",elf,sizeof(elf));
    assert(hddAppInspect(&save,&app)==0 && !strcmp(app.boot,"BOOT.ELF") && !strcmp(app.partition,"PP.APP_MY_APP"));
    char cnf[512],info[1024],settings[1024];
    assert(hddAppSystemCnf(&app,cnf)>0 && strstr(cnf,"path = ata0:/APPS/APP_MY_APP/BOOT.ELF\n"));
    assert(hddAppInfo(&app,info)>0 && strstr(info,"title = [APP] APP_MY_APP\n") && strstr(info,"content_type = 255\n"));
    assert(strstr(info,"title_id = APP-MY-APP\n"));
    save.count=2;entry(files+1,"OTHER.ELF",elf,sizeof(elf));assert(hddAppInspect(&save,&app)==1);
    assert(hddAppChooseBoot(&save,&app,1)==0 && !strcmp(app.boot,"OTHER.ELF"));
    assert(hddAppChooseBoot(&save,&app,2)==-EINVAL);
    /* The supplied PBT must not choose an ELF or inject shell instructions. */
    const char pbt[]="SET \"BOOT\" \"OTHER.ELF\"\n$(touch /should-never-exist)";
    save.count=3;entry(files+2,"APPINFO.PBT",pbt,sizeof(pbt)-1);assert(hddAppInspect(&save,&app)==1);
    const char cfg[]="title = My app\r\nboot = other.elf\r\n";
    save.count=4;entry(files+3,"title.cfg",cfg,sizeof(cfg)-1);
    assert(hddAppInspect(&save,&app)==0 && !strcmp(app.boot,"OTHER.ELF") && !strcmp(app.title,"My app"));
    const char script[]="#!/bin/bash\ncreate_icon_sys() {\n    cat > \"$icon_sys_filename\" <<EOL\nPS2X\ntitle0=$title\ntitle1=$publisher\nbgcola=58\nEOL\n}\n";
    assert(hddAppIconSettings(script,&app,settings)>0 && !strcmp(settings,"PS2X\ntitle0=My app\ntitle1=Homebrew\nbgcola=58\n"));
    headerTest(cnf,settings);
    assert(hddAppIconSettings("create_icon_sys() {\ncat <<EOL\nPS2X\ntitle0=$(evil)\nEOL\n}\n",&app,settings)<0);
    assert(hddAppIconSettings("create_icon_sys() {\ncat <<EOL\nPS2D\nEOL\n}\n",&app,settings)<0);
    assert(hddAppIconSettings("PS2D",&app,settings)<0); /* memory-card metadata */
    entry(files+1,"boot.elf",elf,sizeof(elf));assert(hddAppInspect(&save,&app)==-EINVAL);
    entry(files+1,"../boot.elf",elf,sizeof(elf));assert(hddAppInspect(&save,&app)==-EINVAL);
    entry(files+1,"LSAPP.ID",elf,sizeof(elf));assert(hddAppInspect(&save,&app)==-EINVAL);
    save.count=1;files[0].mode=0x8427;assert(hddAppInspect(&save,&app)==-EINVAL);
    files[0].mode=0x8497;files[0].data=(const unsigned char *)cfg;files[0].size=sizeof(cfg)-1;
    assert(hddAppInspect(&save,&app)==-ENOEXEC);
    files[0].data=elf;files[0].size=sizeof(elf);save.ps1=1;assert(hddAppInspect(&save,&app)==-EINVAL);save.ps1=0;
    strcpy(save.folder.name,"APP_1234567890123456789012345678");
    assert(!hddAppInspect(&save,&app) && strlen(app.partition)==31);
    assert(!hddAppLeaf("../../bad") && !hddAppLeaf("file:") && !hddAppLeaf("file.") && !hddAppLeaf("file "));
    HddAppJournal j={.sequence=17,.phase=HA_MOVE_OLD,.update=1,.package_crc=0x12345678,.package_size=8192},decoded;
    strcpy(j.boot,"BOOT.ELF");unsigned char bytes[HDD_APP_JOURNAL_SIZE];
    assert(!hddAppJournalEncode(&j,bytes) && !hddAppJournalDecode(&decoded,bytes));
    assert(decoded.sequence==j.sequence && decoded.phase==j.phase && decoded.package_crc==j.package_crc && !strcmp(decoded.boot,j.boot));
    for(unsigned i=0;i<sizeof(bytes);i++) {bytes[i]^=1;assert(hddAppJournalDecode(&decoded,bytes)<0);bytes[i]^=1;}
    j.phase=HA_DONE+1;assert(hddAppJournalEncode(&j,bytes)<0);
    puts("PASS: app PSU validation, ELF choice, PBT ignored, DP HDD template, ATA PATINFO metadata and journal corruption rejection");
}
'''
with tempfile.TemporaryDirectory(prefix="hdd-app-") as tmp:
    tmp = Path(tmp)
    (tmp / "test.c").write_text(code)
    subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(root / "include"),
                    str(root / "src/hdd_app.c"), str(tmp / "test.c"), "-lz", "-o", str(tmp / "test")], check=True)
    subprocess.run([str(tmp / "test")], check=True)
