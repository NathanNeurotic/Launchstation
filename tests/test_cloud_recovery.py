"""Compile real cloud-recovery publication helpers with fault-injected storage/HTTPS.

No console, Google account or external file writes are used. Every failure
must keep the final .mcd absent and preserve preexisting/active cards.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/cloud_google.c").read_text(encoding="utf-8")
http = (root / "src/cloud_http.c").read_text(encoding="utf-8")
header = (root / "include/cloud_google.h").read_text(encoding="utf-8")
ui = (root / "src/dash_tools.c").read_text(encoding="utf-8")
http_header = (root / "include/cloud_http.h").read_text(encoding="utf-8")


def extract(text, signature):
    start = text.index(signature)
    at = text.index("{", start) + 1
    depth = 1
    in_string = in_char = escaped = False
    while depth:
        if at >= len(text):
            raise AssertionError("Unbalanced function: " + signature)
        c = text[at]
        if in_string or in_char:
            if escaped:
                escaped = False
            elif c == "\\":
                escaped = True
            elif c == '"' and in_string:
                in_string = False
            elif c == "'" and in_char:
                in_char = False
        elif c == '"':
            in_string = True
        elif c == "'":
            in_char = True
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        at += 1
    return text[start:at] + "\n"


assert "cloudHttpDownloadGoogleFd" in http_header
assert "cloudHttpDownloadFdInternal(url, NULL," in http
assert "cloudHttpDownloadFdInternal(url, bearer_token," in http
assert 'if (bearer != NULL && !cloud_google_media_host(host))' in http or \
       'bearer != NULL && !cloud_google_media_host(host)' in http
assert '!strcmp(host, "www.googleapis.com")' in http
assert '"Authorization: Bearer %s\\r\\n"' in http
assert "cloudGoogleRecoverSnapshot(item, folder, result," in ui
assert "dashModalConfirm(\"Recover selected cloud snapshot?\"" in ui
assert "mcCardValidateNoEcc(stage)" in source
assert "fileXioRename(stage, result)" in source
assert "cloud_digest_matches(digest, snapshot->sha256)" in source
assert "wleExists(result) || wleExists(stage) || wleExists(download)" in source
assert "Drive files are not deleted" not in source or '"DELETE"' not in source

defs = header[header.index("#define CLOUD_GOOGLE_BROWSE_MAX"):
              header.index("int cloudGoogleListBackupHistory")]
c = r'''
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#define CLOUD_ID_MAX 192
#define CLOUD_RECOVERY_MAX_BYTES (96 * 1024 * 1024)
#define MAX_PATH 1024
#define FIO_O_WRONLY 1
#define FIO_O_CREAT 2
#define FIO_O_TRUNC 4
#define MC_CARD_EXPORT_OK 0
typedef struct {
    char access_token[128], ca_path[128];
} GoogleSession;
static int scenario, opens, closes, downloads, copies, imports, renames;
static int deletes, existing, network_ready, geometry_checks;
static char removed[4][MAX_PATH];
static int genCmpFileExt(const char *p, const char *ext) {
    const char *dot=strrchr(p,'.');return dot && !strcasecmp(dot+1,ext);
}
static int drive_safe_id(const char *id) {
    const char *p;
    if(!id || !id[0] || strlen(id)>=CLOUD_ID_MAX)return 0;
    for(p=id;*p;p++)if(!isalnum((unsigned char)*p)&&*p!='_'&&*p!='-')return 0;
    return 1;
}
static void set_error(char *err,int n,const char *message) {
    if(err && n>0)snprintf(err,n,"%s",message);
}
static int wleExists(const char *path) {(void)path;return existing;}
static int genOpen(const char *p,int flags) {
    assert(strstr(p,".DOWNLOAD.PART")!=NULL);
    assert(flags==(FIO_O_WRONLY|FIO_O_CREAT|FIO_O_TRUNC));
    opens++;return 5;
}
static int genClose(int fd) {assert(fd==5);closes++;return 0;}
static int genRemove(char *path) {
    assert(strncmp(path,"mmce",4)!=0);
    if(deletes<4)snprintf(removed[deletes],MAX_PATH,"%s",path);
    deletes++;return 0;
}
static int fileXioRename(char *from,char *to) {
    assert(strstr(from,".mcd.PART") && strstr(to,"RECOVER-CLOUD-"));
    renames++;return scenario==4 ? -1 : 0;
}
static int mcCardValidateNoEcc(const char *name) {
    geometry_checks++;
    if(scenario==3 && strstr(name,"DOWNLOAD.PART"))return -1;
    return 0;
}
static int mcCardValidatePcsx2(const char *name) {(void)name;geometry_checks++;return 0;}
static int mcCardBackupNoEcc(const char *from,const char *to,void *fn) {
    (void)fn;assert(strstr(from,"DOWNLOAD.PART") && strstr(to,".mcd.PART"));
    copies++;return 0;
}
static int mcCardImportPcsx2(const char *from,const char *to,void *fn) {
    (void)fn;assert(strstr(from,"DOWNLOAD.PART") && strstr(to,".mcd.PART"));
    imports++;return 0;
}
static int ensurePathDeviceStackReady(const char *dir) {
    return strncmp(dir,"mmce",4)!=0;
}
static int ensureDashboardIpNetworkReady(void) {network_ready++;return 1;}
static int load_session(GoogleSession *s,int need_token,char *err,int n) {
    (void)err;(void)n;assert(need_token);memset(s,0,sizeof(*s));
    strcpy(s->access_token,"stub-token");strcpy(s->ca_path,"CA");
    return 0;
}
static int refresh_access_token(GoogleSession *s,char *err,int n) {
    (void)s;(void)err;(void)n;return 0;
}
static int cloudHttpDownloadGoogleFd(const char *url,const char *token,int fd,
    const char *ca,int limit,int *written,unsigned char digest[32],
    char *err,int errlen) {
    (void)err;(void)errlen;assert(strstr(url,"?alt=media"));
    assert(!strcmp(token,"stub-token") && !strcmp(ca,"CA") && fd==5);
    assert(limit>8*1024*1024);downloads++;
    if(scenario==5)return -1;
    *written=8*1024*1024;
    memset(digest,scenario==2 ? 0xff : 0,32);
    return 0;
}
'''
c += defs
for sign in ("static int cloud_valid_digest(", "static int cloud_digest_matches(",
             "static int cloud_recovery_path(", "int cloudGoogleRecoverSnapshot("):
    c += extract(source, sign)

c += extract(http, "static int cloud_google_media_host(")
c += r'''
static CloudGoogleBrowseItem item;
static char result[1024], error[256];
static void reset(void) {
    scenario=opens=closes=downloads=copies=imports=renames=deletes=existing=
    network_ready=geometry_checks=0;
    memset(removed,0,sizeof(removed));
    memset(&item,0,sizeof(item));
    strcpy(item.id,"my_file_1");
    strcpy(item.name,"WLE-20261012.mcd");
    memset(item.sha256,'0',64);item.sha256[64]=0;
    memset(result,0,sizeof(result));memset(error,0,sizeof(error));
}
int main(void) {
    char short_result[8];
    reset();
    assert(cloudGoogleRecoverSnapshot(&item,"mass0:/backup",result,sizeof(result),
                                      error,sizeof(error))==0);
    assert(downloads==1 && copies==1 && imports==0 && renames==1);
    assert(opens==1 && closes==1 && !strcmp(result,"mass0:/backup/RECOVER-CLOUD-001.mcd"));
    assert(deletes==1 && strstr(removed[0],".DOWNLOAD.PART"));

    reset();strcpy(item.name,"WLE-20261012.ps2");
    assert(cloudGoogleRecoverSnapshot(&item,"usb1:/",result,sizeof(result),
                                      error,sizeof(error))==0);
    assert(imports==1 && copies==0 && renames==1);

    reset();item.sha256[0]=0;
    assert(cloudGoogleRecoverSnapshot(&item,"mass0:/",result,sizeof(result),
                                      error,sizeof(error))<0);
    assert(!opens && !downloads && !renames);
    reset();scenario=2;
    assert(cloudGoogleRecoverSnapshot(&item,"mass0:/",result,sizeof(result),
                                      error,sizeof(error))<0);
    assert(downloads==1 && !copies && !renames && deletes==2);
    reset();scenario=3;
    assert(cloudGoogleRecoverSnapshot(&item,"mass0:/",result,sizeof(result),
                                      error,sizeof(error))<0);
    assert(downloads==1 && !copies && !renames && deletes==2);
    reset();scenario=4;
    assert(cloudGoogleRecoverSnapshot(&item,"mass0:/",result,sizeof(result),
                                      error,sizeof(error))<0);
    assert(copies==1 && renames==1 && deletes==2);
    reset();scenario=5;
    assert(cloudGoogleRecoverSnapshot(&item,"mass0:/",result,sizeof(result),
                                      error,sizeof(error))<0);
    assert(!copies && !renames && closes==1 && deletes==2);

    reset();existing=1;
    assert(cloudGoogleRecoverSnapshot(&item,"mass0:/",result,sizeof(result),
                                      error,sizeof(error))<0);
    assert(!opens && !downloads);
    reset();
    assert(cloudGoogleRecoverSnapshot(&item,"mmce0:/MemoryCards",result,sizeof(result),
                                      error,sizeof(error))<0);
    assert(!opens);
    reset();
    assert(cloudGoogleRecoverSnapshot(&item,"mass0:/",short_result,sizeof(short_result),
                                      error,sizeof(error))<0);
    assert(!opens);
    assert(cloud_google_media_host("www.googleapis.com"));
    assert(cloud_google_media_host("download.googleusercontent.com"));
    assert(!cloud_google_media_host("evilgoogleusercontent.com"));
    assert(!cloud_google_media_host("googleusercontent.com.evil.test"));
    puts("PASS: cloud SHA, card geometry, staged recovery, redirect host scoping and failure cleanup");
    return 0;
}
'''
compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="wle-cloud-recovery-") as td:
    name = Path(td)/"recovery.c"
    exe = Path(td)/"recovery"
    name.write_text(c, encoding="utf-8")
    subprocess.run([compiler,"-std=gnu11","-Wall","-Wextra","-Werror",
                    "-Wno-unused-parameter",str(name),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
