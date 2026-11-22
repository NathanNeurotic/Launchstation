"""Exercise production launcher discovery/downloads with a local HTTPS facade."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
native = (root / "src/hdd_install_ps2.c").read_text()


def function(signature, source=native):
    start = source.index(signature)
    depth = 0
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', source[start:], re.S):
        depth += (token.group() == "{") - (token.group() == "}")
        if not depth and token.group() == "}":
            return source[start:start + token.end()] + "\n"
    raise AssertionError(signature)


code = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "hdd_install.h"
#include "cloud_http.h"
#include "save_sign.h"
#define MAX_PATH 1025
#define FIO_O_RDONLY O_RDONLY
#define FIO_O_WRONLY O_WRONLY
#define FIO_O_CREAT O_CREAT
#define FIO_O_EXCL O_EXCL
typedef int64_t s64;
typedef uint64_t u64;
static const char *work, *failure;
static unsigned requests, downloads, read_bytes;
static uint64_t overall_bytes, overall_total;
void hddInstallLog(const char *fmt, ...) { assert(fmt); }
void hddInstallLogFlush(HddInstall *in) { assert(in); }
void cloudHttpSetCaBundle(const unsigned char *data,unsigned size) { (void)data;(void)size; }
void hddInstallStatus(const char *s, int done, int total) { assert(s && done >= 0 && total >= 0); }
void hddInstallProgress(const char *s, int done, int total) { hddInstallStatus(s,done,total); }
void hddInstallOverall(uint64_t done,uint64_t total) { assert(total&&done>=overall_bytes&&done<=total);overall_bytes=done;overall_total=total; }
static int genOpen(const char *p, int mode) {
    char path[2048]; assert(!strncmp(p,"ata0:/stage/",12));
    snprintf(path,sizeof(path),"%s/stage/%s",work,p+12); return open(path,mode,0600);
}
static int genRead(int fd, void *data, unsigned n) { int r=read(fd,data,n > 97 ? 97 : n);if(r>0)read_bytes+=r;return r; }
static int genWrite(int fd, const void *data, unsigned n) { return write(fd,data,n > 31 ? 31 : n); }
#define genLseek lseek
#define genClose close
static char *load(const char *path, int *size) {
    FILE *f=fopen(path,"rb");char *data;long n;
    if(!f)return NULL;assert(!fseek(f,0,SEEK_END));n=ftell(f);rewind(f);assert(n>=0&&n<65536);
    data=malloc(n+1);assert(data&&fread(data,1,n,f)==(size_t)n);data[n]=0;fclose(f);*size=n;return data;
}
int cloudHttpRequest(const char *method,const char *host,const char *url,const char *headers,
                 const void *body,int body_size,const char *ca,CloudHttpResponse *response,char *error,int error_size) {
    const char *begin=strstr(url,"/contents/"),*end=strchr(url,'?');char path[2048];int n;
    (void)error;(void)error_size;
    assert(!strcmp(method,"GET")&&!strcmp(host,"api.github.com")&&headers&&!body&&!body_size&&ca&&begin&&end);
    assert(!strcmp(end,"?ref=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));requests++;
    if(!strcmp(failure,"request"))return -EIO;
    begin+=10;n=end-begin;snprintf(path,sizeof(path),"%s/api/%.*s.json",work,n,begin);
    response->body=load(path,&response->body_len);response->status=response->body?200:404;return 0;
}
void cloudHttpResponseFree(CloudHttpResponse *r) { free(r->body);memset(r,0,sizeof(*r)); }
int cloudHttpDownloadAlignedFd(const char *url,int fd,const char *ca,int maximum,int *size,unsigned char digest[32],
                    void (*progress)(int, int, void *),void *context,char *error,int error_size) {
    const char prefix[]="https://raw.githubusercontent.com/" HDD_INSTALL_REPOSITORY "/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/";
    char path[2048];unsigned char buffer[71];FILE*f;size_t n;
    assert(!strncmp(url,prefix,sizeof(prefix)-1)&&ca&&maximum>0&&!digest&&progress);(void)error;(void)error_size;
    downloads++;*size=0;
    if(!strcmp(failure,"write"))return -EIO;
    snprintf(path,sizeof(path),"%s/remote/%s",work,url+sizeof(prefix)-1);f=fopen(path,"rb");assert(f);
    while((n=fread(buffer,1,sizeof(buffer),f))){
        for(unsigned offset=0;offset<n;){int w=genWrite(fd,buffer+offset,n-offset);assert(w>0);offset+=w;}
        *size+=n;progress(*size,maximum,context);
    }
    fclose(f);if(!strcmp(failure,"truncated"))(*size)--;return 0;
}
'''
start = native.index("static const char *const launcher_required[]")
code += native[start:native.index("static int launcherManifest(", start)]
for signature in ("static int pathJoin(", "static int readAll(", "static int closeResult(",
                  "static int checkStageHash(", "static void launcherStageName(", "static int findLauncher(",
                  "static const char *launcherIcon(", "static const char *launcherArt(", "static int launcherManifest(", "static int checkLaunchers("):
    code += function(signature)
start = native.index("typedef struct { const char *name;")
end = native.index("static void installerHttpsStatus(", start)
code += native[start:end]
code += r'''
int main(int argc,char **argv) {
    HddInstall in={.flags=7,.jail=1};const char *phase="metadata";int ret;
    assert(argc==4);work=argv[1];failure=argv[2];in.game_launcher=atoi(argv[3]);
    strcpy(in.stage,"ata0:/stage");strcpy(in.ca_path,"fixture.pem");memset(in.commit,'a',40);
    ret=launcherMetadata(&in);
    if(ret>=0)ret=downloadLaunchers(&in,&phase,0,downloadBytes(&in));
    assert((ret==0)==!strcmp(failure,"ok"));
    unsigned nonempty=0;for(unsigned i=0;i<in.launcher_count;i++)nonempty+=!!in.launcher_files[i].size;
    if(!ret){assert(in.launcher_count>=15&&downloads==nonempty&&requests>=8&&(uint64_t)read_bytes==downloadBytes(&in)+3*52);
             assert(overall_bytes==downloadBytes(&in)&&overall_total==overall_bytes);
             assert(findLauncher(&in,launcherIcon(&in))>=0&&findLauncher(&in,launcherArt(&in))>=0);}
    free(in.launcher_files);return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="hdd-installer-launchers-") as directory:
    d = Path(directory)
    (d / "test.c").write_text(code)
    subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation",
                    "-fsanitize=undefined", "-I", str(root / "include"), str(d / "test.c"),
                    str(root / "src/hdd_install_plan.c"), str(root / "src/save_sign.c"), "-o", str(d / "test")], check=True)
    elf = bytearray(120)
    elf[:6] = b"\x7fELF\x01\x01"
    elf[18] = 8
    files = {"scripts/assets/OPL/OPNPS2LD.ELF": bytes(elf), "scripts/assets/NHDDL/nhddl.elf": bytes(elf),
             "scripts/assets/neutrino/neutrino.elf": bytes(elf), "scripts/assets/OPL/version.txt": b"OPL",
             "scripts/assets/NHDDL/version.txt": b"NHDDL", "scripts/assets/neutrino/version.txt": b"Neutrino",
             "scripts/assets/OPL/LNG/lang_French.lng": b"translation", "scripts/assets/neutrino/config/system.toml": b"config",
             "scripts/assets/neutrino/modules/ata_bd.irx": b"IRX runtime", "scripts/assets/neutrino/udpfs_server/compressed_iso/__init__.py": b"helper",
             "scripts/assets/neutrino/udpfs_server/compressed_iso/empty.py": b"",
             **{f"scripts/licenses/{name}": b"license" for name in ("OPL", "nhddl", "neutrino")},
             **{f"icons/{kind}/APP_{app}{suffix}": b"icon" for app in ("OPL", "NHDDL")
                for kind, suffix in (("art", ".png"), ("ico", "_LST.ico"))}}
    for choice in (0, 1):
        for failure in ("ok", "bad-hash", "truncated", "request", "write", "missing", "duplicate", "symlink", "traversal", "oversize", "file-limit", "directory-limit"):
            work = d / f"{choice}-{failure}"
            (work / "stage").mkdir(parents=True)
            listings = {path: [] for path in ("scripts/assets/OPL", "scripts/assets/NHDDL", "scripts/assets/neutrino", "icons/art", "icons/ico", "scripts/licenses")}
            def ensure_directory(parent):
                if parent in listings:
                    return
                ancestor = str(Path(parent).parent)
                ensure_directory(ancestor)
                listings[parent] = []
                listings[ancestor].append({"name": Path(parent).name, "type": "dir", "size": 0, "sha": "a" * 40})
            for remote, data in files.items():
                if failure == "missing" and remote.endswith("neutrino.elf"):
                    continue
                path = work / "remote" / remote
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
                parent = str(Path(remote).parent)
                ensure_directory(parent)
                listings[parent].append({"name": Path(remote).name, "type": "file", "size": len(data),
                                         "sha": "b"*40 if failure=="bad-hash" else hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()})
            if failure in ("duplicate", "symlink", "traversal", "oversize"):
                entry = dict(listings["scripts/assets/OPL"][0])
                if failure == "symlink": entry["type"] = "symlink"
                if failure == "traversal": entry["name"] = "../escape"
                if failure == "oversize": entry["size"] = 16 * 1024 * 1024 + 1
                listings["scripts/assets/OPL"].append(entry)
            if failure in ("file-limit", "directory-limit"):
                count = 161 if failure == "file-limit" else 16
                listings["scripts/assets/OPL"].extend({"name": f"extra-{i}", "type": "file" if failure == "file-limit" else "dir",
                                                       "size": 1, "sha": "c" * 40} for i in range(count))
            for parent, entries in listings.items():
                path = work / "api" / (parent + ".json")
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(json.dumps(entries))
            subprocess.run([str(d / "test"), str(work), failure, str(choice)], check=True, timeout=10)
    print("PASS: pinned launcher discovery, both defaults, staged digest and ELF header checks, short I/O, download errors and bounded/restricted metadata")
    # Run the actual PATINFO writer, including the optional NHDDL argument,
    # against a raw-write facade that only inspects memory.
    header = (root / "src/hdd_header_injector.c").read_text()
    header_code = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include "hdd_header_injector.h"
typedef uint32_t u32;
typedef struct { unsigned mode,private_5; } iox_stat_t;
#define MAX_NAME 32
#define APA_TYPE_PFS 0x100
#define HDIOC_FLUSH 1
'''
    header_code += "\n".join(re.findall(r"^#define APA_HEADER_.*$", header, re.M)) + "\n"
    a = header.index("enum HddHeaderFileKind {")
    b = header.index("} HddHeaderFileSpec;", a) + len("} HddHeaderFileSpec;")
    header_code += header[a:b] + "\n"
    a = header.index("static const HddHeaderFileSpec hdd_header_files[")
    header_code += header[a:header.index("};", a) + 2] + "\n"
    header_code += r'''
static int writes,flushes,write_error;
static const char *expected;
static int hddHeaderBuildPartitionPath(char *p,unsigned size,const char *name){return snprintf(p,size,"hdd0:%s",name)>=(int)size?-EINVAL:0;}
static int fileXioGetStat(const char*p,iox_stat_t*s){assert(!strcmp(p,"hdd0:PP.LAUNCHER"));s->mode=APA_TYPE_PFS;s->private_5=262144;return 0;}
static int hddHeaderWriteRawRange(u32 lba,u32 offset,const void *data,u32 size){
 const unsigned char *attributes = data;
 assert(lba==262144&&offset==0x1000&&size==0x800+4);writes++;
 assert((uintptr_t)data%64==0);
 assert(!memcmp(attributes,"PS2ICON3D",9));
 u32 bytes;memcpy(&bytes,attributes+0x14,4);assert(bytes<512);
 assert(!memcmp(attributes+0x200,expected,bytes)&&bytes==strlen(expected));
 assert(!memcmp(attributes+0x800,"ICON",4));return write_error;
}
static int fileXioDevctl(const char*p,int command,const void*in,int size,void*out,int outsize){
 assert(!strcmp(p,"hdd0:")&&command==HDIOC_FLUSH&&!in&&!size&&!out&&!outsize);flushes++;return 0;
}
'''
    header_code += function("int InjectHddAppLauncher(", header) + function("int InjectHddAppLauncherWithArg(", header)
    header_code += r'''
int main(void){
 const char*elf="hdd0:__system:pfs:/launcher/nhddl.elf";
 expected="BOOT2 = PATINFO\nHDDUNITPOWER = NICHDD\npath = hdd0:__system:pfs:/launcher/nhddl.elf\narg = -mode=ata\n";
 assert(!InjectHddAppLauncherWithArg("PP.LAUNCHER",elf,"NHDDL","ICON",4,"-mode=ata")&&writes==1&&flushes==1);
 expected="BOOT2 = PATINFO\nHDDUNITPOWER = NICHDD\npath = hdd0:__system:pfs:/launcher/nhddl.elf\n";
 assert(!InjectHddAppLauncher("PP.LAUNCHER",elf,"NHDDL","ICON",4)&&writes==2&&flushes==2);
 assert(InjectHddAppLauncherWithArg("PP.LAUNCHER",elf,"NHDDL","ICON",4,"-mode=ata\npath=bad")==-EINVAL&&writes==2);
 char large[600];memset(large,'a',599);large[599]=0;
 assert(InjectHddAppLauncherWithArg("PP.LAUNCHER",elf,"NHDDL","ICON",4,large)==-ENAMETOOLONG&&writes==2);
 write_error=-EIO;assert(InjectHddAppLauncher("PP.LAUNCHER",elf,"NHDDL","ICON",4)==-EIO&&writes==3&&flushes==2);
 puts("PASS: production PATINFO includes the NHDDL ATA argument, preserves icon metadata and rejects invalid/oversized arguments and raw-write failures");
}
'''
    (d / "header.c").write_text(header_code)
    subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined", "-I", str(root / "include"),
                    str(d / "header.c"), "-o", str(d / "header")], check=True)
    subprocess.run([str(d / "header")], check=True, timeout=10)
