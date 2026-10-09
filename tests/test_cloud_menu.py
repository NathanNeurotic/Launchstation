"""Execute production cloud menus with host mocks; no network or PS2 proof."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_tools.c").read_text(encoding="utf-8")


def function(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


functions = function("static int cloudSetupGui(void)") + function("void dashToolsCloudBackup(void)")
icons = sorted(set(re.findall(r"\bDI_\w+", functions)))
code = "#include <assert.h>\n#include <stdio.h>\n#include <string.h>\n"
code += "enum {" + ",".join(icons) + "};\n"
code += r'''
#define MAX_PATH 1024
#define DASH_PICK_FILE 1
#define DASH_TOAST_OK 1
typedef struct { int icon; const char *label, *detail; int disabled; } DashMenuItem;
static struct { char igr_backup_path[MAX_PATH]; int igr_cloud_sync; } dashCfg;
static char LaunchElfBootDir[MAX_PATH], LaunchElfDir[MAX_PATH];
static int configured, linked, token, ca, client;
static int scenario, calls, backup_calls, upload_calls, disconnects, saves;
static void dashStrCopy(char *d, const char *s, int n) { snprintf(d,n,"%s",s); }
static int cloudGoogleGetClientConfig(char *id,int ni,char *secret,int ns,int *present,char *err,int ne) {
    (void)err; (void)ne; snprintf(id,ni,"%s",client?"client":"");
    snprintf(secret,ns,"%s",""); *present=ca; return 0;
}
static int cloudGoogleConfigured(char *e,int n) { (void)e;(void)n;return configured; }
static int cloudGoogleIsLinked(void) { return linked; }
static int cloudGoogleHasLocalToken(void) { return token; }
static int cloudGoogleDisconnect(char *e,int n) { (void)e;(void)n;disconnects++;return 0; }
static int cloudGoogleSaveClientConfig(const char *a,const char *b,char *e,int n) {
    (void)a;(void)b;(void)e;(void)n;saves++;return 0;
}
static int cloudGoogleInstallCaBundle(const char *p,char *e,int n) { (void)p;(void)e;(void)n;return 0; }
static void dashStoreSaveSettings(void) {}
static void dashStoreFlush(int n) { (void)n; }
static void dashModalMessage(int i,const char *t,const char *b,const char *d) { (void)i;(void)t;(void)b;(void)d; }
static int dashModalConfirm(const char *t,const char *b,const char *a,int n) {
    (void)t;(void)b;(void)a;(void)n; assert(0 && "Unexpected destructive confirmation"); return 0;
}
static int dashModalKeyboard(const char *t,char *b,int n) { (void)t;(void)b;(void)n;return 1; }
static void dashToast(int i,const char *t) { (void)i;(void)t; }
static int dashPickPath(const char *t,const char *s,int m,const char *e,char *p,int n) {
    (void)t;(void)s;(void)m;(void)e;(void)p;(void)n;return 0;
}
static void cloudLink(void) {}
static void cloudSyncNow(void) { upload_calls++; }
static void dashToolsSyncAllMmceCards(void) { backup_calls++; }
static int dashModalMenu(const char *title,const char *sub,const DashMenuItem *items,int n,int initial) {
    (void)sub;
    calls++;
    if (scenario <= 2) {
        assert(!strcmp(title,"Google Drive setup") && n==4);
        if (scenario==0) { assert(initial==0); return -1; }
        if (scenario==1) { assert(initial==2); return -1; }
        /* Accept unchanged credentials on a linked account, then leave. */
        assert(initial== (calls==1?3:0)); return calls==1?0:-1;
    }
    assert(!strcmp(title,"Google Drive backups") && n==5);
    assert(!strcmp(items[0].label,"Back up cards & upload"));
    assert(!strcmp(items[3].label,"Upload existing backups"));
    assert(items[0].disabled==!linked);
    assert(items[3].disabled==(!linked || !dashCfg.igr_backup_path[0]));
    assert(items[4].disabled==!token);
    if (scenario==3) { assert(initial==1); return -1; }
    if (scenario==4) { assert(initial==2); return -1; }
    if (scenario==5) { assert(initial==0); return -1; }
    if (scenario==6) { assert(initial==0); return calls==1?0:-1; }
    assert(initial==(calls==1?0:3)); return calls==1?3:-1;
}
'''
code += functions
code += r'''
int main(void) {
    scenario=0; cloudSetupGui();
    scenario=1; calls=0; client=1; cloudSetupGui();
    scenario=2; calls=0; ca=linked=1; cloudSetupGui();
    assert(!disconnects && !saves);
    scenario=3; calls=0; configured=linked=0; dashToolsCloudBackup();
    scenario=4; calls=0; configured=1; dashToolsCloudBackup();
    scenario=5; calls=0; linked=token=1; dashToolsCloudBackup();
    scenario=6; calls=0; dashToolsCloudBackup(); assert(backup_calls==1 && !upload_calls);
    scenario=7; calls=0; strcpy(dashCfg.igr_backup_path,"mass0:/backups/");
    dashToolsCloudBackup(); assert(backup_calls==1 && upload_calls==1);
    assert(!disconnects && !saves);
    puts("PASS: cloud readiness, action dispatch, retained focus and unchanged credentials");
}
'''
compiler = shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("Host gcc or clang required")
with tempfile.TemporaryDirectory(prefix="wle-cloud-menu-") as tmp:
    harness = Path(tmp) / "cloud.c"
    exe = Path(tmp) / "cloud.exe"
    harness.write_text(code, encoding="utf-8")
    subprocess.run([compiler, "-std=gnu11", str(harness), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
