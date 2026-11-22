"""Legacy smb1: paths migrate in every saved launch-key and bookmark slot."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


code = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smb_shares.h"
#define MAX_PATH 64
#define MAX_PATH_PAD 2
#define SETTING_LK_COUNT 3
#define DASH_PATH_MAX MAX_PATH
#define DASH_MAX_SCAN 2
#define DASH_MAX_FAV 2
#define DASH_MAX_RECENT 2
#define DASH_MAX_APPS 2
typedef struct {
    char igr_backup_path[DASH_PATH_MAX], igr_after_path[DASH_PATH_MAX];
    int nscan;
    char scan[DASH_MAX_SCAN][DASH_PATH_MAX];
} DashSettings;
typedef struct {
    int nfav; char fav[DASH_MAX_FAV][DASH_PATH_MAX];
    int npin; char pin[DASH_MAX_FAV][DASH_PATH_MAX];
    int nrecent; char recent[DASH_MAX_RECENT][DASH_PATH_MAX];
} DashUserLists;
static struct { char LK_Path[SETTING_LK_COUNT][MAX_PATH]; } config, *setting = &config;
static char PathPad[MAX_PATH_PAD][MAX_PATH];
static DashSettings dashCfg;
static DashUserLists dashLists;
static char *hidden_apps[DASH_MAX_APPS];
static int nhidden;
'''
code += function((root / "src/smb_cnf.c").read_text(), "int smbLegacyPath(")
code += function((root / "src/config.c").read_text(), "static void configLegacySmbPaths(")
code += function((root / "src/dash_store.c").read_text(), "static void dashStoreLegacySmbPaths(")
code += r'''
int main(void) {
    strcpy(setting->LK_Path[0], "smb1:/auto/BOOT.ELF");
    strcpy(setting->LK_Path[1], "smb1:/button/BOOT.ELF");
    strcpy(setting->LK_Path[2], "smb:/ordinary/BOOT.ELF");
    strcpy(PathPad[0], "smb1:/bookmark/folder");
    strcpy(PathPad[1], "mass:/unchanged");
    strcpy(dashCfg.igr_backup_path, "smb1:/backup");
    strcpy(dashCfg.igr_after_path, "smb1:/after/BOOT.ELF");
    dashCfg.nscan = 2;
    strcpy(dashCfg.scan[0], "smb1:/scan0");
    strcpy(dashCfg.scan[1], "smb1:/scan1");
    dashLists.nfav = dashLists.npin = dashLists.nrecent = 1;
    strcpy(dashLists.fav[0], "smb1:/favorite");
    strcpy(dashLists.pin[0], "smb1:/pinned");
    strcpy(dashLists.recent[0], "smb1:/recent");
    hidden_apps[0] = malloc(14);
    assert(hidden_apps[0] != NULL);
    strcpy(hidden_apps[0], "smb1:/hidden");
    nhidden = 1;
    configLegacySmbPaths();
    dashStoreLegacySmbPaths();
    assert(!strcmp(setting->LK_Path[0], "smbold:/auto/BOOT.ELF"));
    assert(!strcmp(setting->LK_Path[1], "smbold:/button/BOOT.ELF"));
    assert(!strcmp(setting->LK_Path[2], "smb:/ordinary/BOOT.ELF"));
    assert(!strcmp(PathPad[0], "smbold:/bookmark/folder"));
    assert(!strcmp(PathPad[1], "mass:/unchanged"));
    assert(!strcmp(dashCfg.igr_backup_path, "smbold:/backup"));
    assert(!strcmp(dashCfg.igr_after_path, "smbold:/after/BOOT.ELF"));
    assert(!strcmp(dashCfg.scan[0], "smbold:/scan0") && !strcmp(dashCfg.scan[1], "smbold:/scan1"));
    assert(!strcmp(dashLists.fav[0], "smbold:/favorite"));
    assert(!strcmp(dashLists.pin[0], "smbold:/pinned"));
    assert(!strcmp(dashLists.recent[0], "smbold:/recent"));
    assert(!strcmp(hidden_apps[0], "smbold:/hidden"));
    free(hidden_apps[0]);
    nhidden = 0;

    {
        char path[MAX_PATH] = "smb1:/";
        size_t used = strlen(path);
        memset(path + used, 'x', MAX_PATH - used - 1);
        path[MAX_PATH - 1] = '\0';
        {
            char original[MAX_PATH];
            strcpy(original, path);
            assert(smbLegacyPath(path, sizeof(path)) == -1);
            assert(!strcmp(path, original));
        }
    }
    assert(smbLegacyPath(NULL, MAX_PATH) == 0);
    puts("PASS: legacy SMB1 paths migrate in every slot; oversized input stays unchanged");
    return 0;
}
'''
compiler = shutil.which("cc") or shutil.which("gcc")
assert compiler, "a host C compiler is needed"
with tempfile.TemporaryDirectory(prefix="smb-legacy-") as tmp:
    source = Path(tmp) / "legacy.c"
    exe = Path(tmp) / "legacy"
    source.write_text(code)
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I", str(root / "include"), str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
