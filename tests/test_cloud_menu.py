"""Execute production cloud menus with host mocks; no network or PS2 proof."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_tools.c").read_text(encoding="utf-8")

# The menu action map and rendered menu must have identical capacity:
# seven current entries, including cloud-history browsing.
menu = source[source.index("void dashToolsCloudBackup(void)"):source.index("static void runTool", source.index("void dashToolsCloudBackup(void)"))]
sizes = re.search(r"DashMenuItem\s+items\[(\d+)\];\s*int\s+ids\[(\d+)\]", menu)
assert sizes is not None and sizes.group(1) == sizes.group(2), \
    "cloud menu action IDs must not write beyond the per-item action array"
assert int(sizes.group(1)) >= 7, "all seven cloud menu commands need capacity"



def function(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


functions = (function("static int chooseMmceBackupRoot(") +
             function("static int cloudDisconnectForClientChange(int linked)") +
             function("static void cloudSetupGui(void)") +
             function("void dashToolsCloudBackup(void)"))
icons = sorted(set(re.findall(r"\bDI_\w+", functions)))
code = "#include <assert.h>\n#include <stdio.h>\n#include <string.h>\n"
code += "enum {" + ",".join(icons) + "};\n"
code += r'''
#define MAX_PATH 1024
#define DASH_PICK_FILE 1
#define DASH_PICK_FOLDER 2
#define DASH_TOAST_OK 1
typedef struct { int icon; const char *label, *detail; int disabled; } DashMenuItem;
static struct { char igr_backup_path[MAX_PATH]; int igr_cloud_sync; } dashCfg;
static char LaunchElfBootDir[MAX_PATH], LaunchElfDir[MAX_PATH];
static int configured, linked, token, ca, client, builtin;
static int scenario, calls, backup_calls, upload_calls, browse_calls, disconnects, saves, clears, settings_saves, picks, accept_change;
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
static int cloudGoogleClearClientConfig(char *e,int n) { (void)e;(void)n;clears++;client=0;return 0; }
static int cloudGoogleHasBuiltinClient(void) { return builtin; }
static int cloudGoogleInstallCaBundle(const char *p,char *e,int n) { (void)p;(void)e;(void)n;return 0; }
static void dashStoreSaveSettings(void) { settings_saves++; }
static int dashWorkerAwaitAll(const char *s) { (void)s;return 1; }
static void dashToolsRefreshMmceStatus(void) {}
static void dashStoreFlush(int n) { (void)n; }
static void dashModalMessage(int i,const char *t,const char *b,const char *d) { (void)i;(void)t;(void)b;(void)d; }
static int dashModalConfirm(const char *t,const char *b,const char *a,int n) {
    (void)b;(void)a;(void)n; assert(!strcmp(t,"Change backup folder?")); return accept_change;
}
static int dashModalKeyboard(const char *t,char *b,int n) { (void)t;(void)b;(void)n;return 1; }
static void dashToast(int i,const char *t) { (void)i;(void)t; }
static int dashPickPath(const char *t,const char *s,int m,const char *e,char *p,int n) {
    (void)t;(void)s;(void)e;picks++;assert(m==DASH_PICK_FOLDER);
    if (scenario==9) return 0;
    snprintf(p,n,"mass0:/new/");return 1;
}
static void cloudLink(void) {}
static void cloudSyncNow(void) { upload_calls++; }
static void cloudBrowseHistory(void) { browse_calls++; }
static void dashToolsSyncAllMmceCards(void) { backup_calls++; }
static int dashModalMenu(const char *title,const char *sub,const DashMenuItem *items,int n,int initial) {
    (void)sub;
    calls++;
    if (scenario <= 2) {
        assert(!strcmp(title,"Advanced Google Drive setup"));
        if (scenario==0) {
            /* Built-in client: nothing required, secret needs a custom ID first. */
            assert(n==4 && initial==3 && items[1].disabled);
            assert(!strcmp(items[0].detail,"Not set (built-in client in use)"));
            assert(!strcmp(items[2].detail,"Not needed (built-in Google roots in use)"));
            return -1;
        }
        if (scenario==1) {
            /* A custom client can be removed to go back to the built-in one. */
            if (calls==1) {
                assert(n==5 && !strcmp(items[2].label,"Use built-in Launchstation client"));
                return 2;
            }
            assert(n==4 && !client); return -1;
        }
        /* Accept unchanged credentials on a linked account, then leave. */
        assert(n==4 && initial==(calls==1?3:0)); return calls==1?0:-1;
    }
    assert(!strcmp(title,"Google Drive backups") && n==7);
    assert(!strcmp(items[0].label,"Back up cards & upload"));
    assert(!strcmp(items[1].label,linked?"Relink Google Drive":"Link Google Drive"));
    assert(items[1].disabled==!configured);
    assert(!strcmp(items[2].label,"Upload existing backups"));
    assert(items[0].disabled==!linked);
    assert(items[2].disabled==(!linked || !dashCfg.igr_backup_path[0]));
    assert(!strcmp(items[3].label,"Browse cloud history..."));
    assert(items[3].disabled==!linked);
    assert(!strcmp(items[4].label,"Backup folder...") && !items[4].disabled);
    assert(items[5].disabled==!token);
    assert(!strcmp(items[6].label,"Advanced setup...") && !items[6].disabled);
    if (scenario==3) { assert(initial==6); return -1; }
    if (scenario==4) { assert(initial==1); return -1; }
    if (scenario==5) { assert(initial==0); return -1; }
    if (scenario==6) { assert(initial==0); return calls==1?0:-1; }
    if (scenario==12) { assert(initial==(calls==1?0:3)); return calls==1?3:-1; }
    if (scenario>=8) { assert(initial==(calls==1?0:4)); return calls==1?4:-1; }
    assert(initial==(calls==1?0:2)); return calls==1?2:-1;
}
'''
code += functions
code += r'''
int main(void) {
    builtin=1;
    scenario=0; cloudSetupGui();
    scenario=1; calls=0; client=1; cloudSetupGui();
    assert(clears==1 && !disconnects && !saves);
    scenario=2; calls=0; client=1; builtin=0; ca=linked=1; cloudSetupGui();
    assert(!disconnects && !saves);
    builtin=1; client=0;
    scenario=3; calls=0; configured=linked=0; dashToolsCloudBackup();
    scenario=4; calls=0; configured=1; dashToolsCloudBackup();
    scenario=5; calls=0; linked=token=1; dashToolsCloudBackup();
    scenario=6; calls=0; dashToolsCloudBackup(); assert(backup_calls==1 && !upload_calls);
    scenario=7; calls=0; strcpy(dashCfg.igr_backup_path,"mass0:/backups/");
    dashToolsCloudBackup(); assert(backup_calls==1 && upload_calls==1);
    assert(!disconnects && !saves);
    scenario=8; calls=0; accept_change=0; dashToolsCloudBackup();
    assert(!strcmp(dashCfg.igr_backup_path,"mass0:/backups/") && !settings_saves);
    scenario=9; calls=0; dashToolsCloudBackup(); assert(!settings_saves);
    scenario=10; calls=0; accept_change=1; dashToolsCloudBackup();
    assert(!strcmp(dashCfg.igr_backup_path,"mass0:/new/") && settings_saves==1);
    calls=0; dashToolsCloudBackup(); assert(settings_saves==1); /* Same folder is a no-op. */
    scenario=11; calls=0; dashCfg.igr_backup_path[0]=0; accept_change=0;
    dashToolsCloudBackup(); assert(settings_saves==2 && picks==5);
    scenario=12; calls=0; linked=1; dashToolsCloudBackup();
    assert(browse_calls==1 && !disconnects);
    puts("PASS: cloud readiness, read-only browse, action dispatch and retained focus");
}
'''
compiler = shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("Host gcc or clang required")
with tempfile.TemporaryDirectory(prefix="wle-cloud-menu-") as tmp:
    harness = Path(tmp) / "cloud.c"
    exe = Path(tmp) / "cloud.exe"
    harness.write_text("#define localizeDashboardText(s) (s)\n" + code, encoding="utf-8")
    subprocess.run([compiler, "-std=gnu11", str(harness), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
