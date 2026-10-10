"""Exercise the actual updater comparison/confirmation code with mocked GitHub responses.

This covers stale, current, newer, divergent, malformed and unreachable releases.
It is a host-side regression test, not an HTTPS or console integration test.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / "src/dash_tools.c").read_text()


def function(signature):
    start = src.index(signature)
    pos = src.index("{", start) + 1
    depth = 1
    while depth:
        depth += (src[pos] == "{") - (src[pos] == "}")
        pos += 1
    return src[start:pos] + "\n"


sources = "".join(function(signature) for signature in (
    "static int releaseJsonString(",
    "static int releaseAssetDigest(",
    "static int updateIsCommitId(",
    "static int updateCommitRelation(",
    "void dashToolsCheckUpdates(void)\n{",
))
enums = src[src.index("enum { UPDATE_RELATION_UNKNOWN"):src.index("static int updateIsCommitId(")]

program = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_PATH 256
#define GIT_HASH "e70d903"
#define SELF_UPDATE_ASSET "APP_LAUNCHSTATION.psu"
#define DI_CHECK 1
#define DI_WARNING 2
typedef struct {
    int status;
    char *body;
    int body_len;
} CloudHttpResponse;
static char comparison[32] = "ahead";
static char release_target[80] = "51e1e0c7bc1efcb5b51c6cc9de056ee858694a03";
static const char *last_title;
static int requests, compares, confirmations, installs, response_status = 200;
static int launchstationUpdateCaPath(char *path, int n) {
    snprintf(path,n,"ca.pem");
    return 1;
}
static int ensureDashboardIpNetworkReady(void) { return 1; }
static void dashLoadingBegin(const char *s) { (void)s; }
static void dashLoadingEnd(void) {}
static void dashModalMessage(int kind,const char *title,const char *body,const char *detail) {
    (void)kind;(void)body;(void)detail;
    last_title=title;
}
static int dashModalConfirm(const char *title,const char *body,const char *action,int danger) {
    (void)title;(void)body;(void)action;(void)danger;
    confirmations++; return 1;
}
static int selfUpdateInstall(const char *ca,const char *digest,char *result,int cap) {
    (void)ca;(void)digest;
    installs++;
    snprintf(result,cap,"installed");
    return 0;
}
static int cloudHttpRequest(const char *method,const char *host,const char *path,
                            const char *headers,const void *body,int body_len,
                            const char *ca,CloudHttpResponse *out,char *err,int err_size) {
    char json[512];
    (void)headers;(void)body;(void)body_len;(void)ca;(void)err;(void)err_size;
    assert(!strcmp(method,"GET") && !strcmp(host,"api.github.com"));
    requests++;
    if (strstr(path,"/compare/")) {
        compares++;
        assert(strstr(path,"?per_page=1&page=2"));
        assert(strstr(path,"/compare/e70d903..."));
        snprintf(json,sizeof(json),"{\"status\":\"%s\"}",comparison);
        out->status=response_status;
    } else {
        assert(strstr(path,"/releases/tags/latest"));
        snprintf(json,sizeof(json),"{\"target_commitish\":\"%s\",\"assets\":[{\"name\":\"APP_LAUNCHSTATION.psu\",\"digest\":\"sha256:%064d\"}]}",
                 release_target,0);
        out->status=200;
    }
    out->body=malloc(strlen(json)+1);
    assert(out->body != NULL);
    strcpy(out->body,json);
    out->body_len=(int)strlen(json);
    return 0;
}
static void cloudHttpResponseFree(CloudHttpResponse *r) {
    free(r->body);r->body=NULL;
}
'''
program += enums + sources
program += r'''
static void reset(const char *status,const char *release,int code) {
    requests=compares=confirmations=installs=0;
    last_title=NULL;
    snprintf(comparison,sizeof(comparison),"%s",status);
    snprintf(release_target,sizeof(release_target),"%s",release);
    response_status=code;
}
int main(void) {
    const char *old="51e1e0c7bc1efcb5b51c6cc9de056ee858694a03";
    reset("ahead",old,200);
    dashToolsCheckUpdates();
    assert(compares==1 && requests==2 && confirmations==1 && installs==1);
    reset("behind",old,200);
    dashToolsCheckUpdates();
    assert(compares==1 && confirmations==0 && installs==0);
    assert(!strcmp(last_title,"No newer release available"));
    reset("identical",old,200);
    dashToolsCheckUpdates();
    assert(compares==1 && confirmations==0 && installs==0);
    reset("diverged",old,200);
    dashToolsCheckUpdates();
    assert(compares==1 && confirmations==0 && installs==0);
    assert(!strcmp(last_title,"Release is on another commit history"));
    reset("unrecognized",old,200);
    dashToolsCheckUpdates();
    assert(!strcmp(last_title,"Could not verify update order") && installs==0);
    reset("ahead",old,404);
    dashToolsCheckUpdates();
    assert(!strcmp(last_title,"Could not verify update order") && installs==0);
    reset("ahead","not-a-commit",200);
    dashToolsCheckUpdates();
    assert(compares==0 && confirmations==0 && installs==0);
    reset("ahead","e70d903c7811a1b95b95771fa6fd813f48f5ecd4",200);
    dashToolsCheckUpdates();
    assert(compares==0 && confirmations==0 && installs==0);
    assert(!strcmp(last_title,"Launchstation is up to date"));
    assert(!updateIsCommitId("local") && !updateIsCommitId("../malformed"));
    assert(updateIsCommitId("e70d903"));
    puts("PASS: updater only installs a proven descendant; stale/divergent/unknown builds are blocked");
}
'''
compiler=shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="launchstation-update-order-") as temp:
    path=Path(temp)/"check.c"
    binary=Path(temp)/"check"
    path.write_text(program)
    subprocess.run([compiler,"-std=c11","-Wall","-Wextra","-Werror",
                    str(path),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=5)
