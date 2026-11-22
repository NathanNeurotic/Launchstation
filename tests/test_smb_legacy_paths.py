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
#include <string.h>
#include "smb_shares.h"
#define MAX_PATH 64
#define MAX_PATH_PAD 2
#define SETTING_LK_COUNT 3
static struct { char LK_Path[SETTING_LK_COUNT][MAX_PATH]; } config, *setting = &config;
static char PathPad[MAX_PATH_PAD][MAX_PATH];
'''
code += function((root / "src/smb_cnf.c").read_text(), "int smbLegacyPath(")
code += function((root / "src/config.c").read_text(), "static void configLegacySmbPaths(")
code += r'''
int main(void) {
    strcpy(setting->LK_Path[0], "smb1:/auto/BOOT.ELF");
    strcpy(setting->LK_Path[1], "smb1:/button/BOOT.ELF");
    strcpy(setting->LK_Path[2], "smb:/ordinary/BOOT.ELF");
    strcpy(PathPad[0], "smb1:/bookmark/folder");
    strcpy(PathPad[1], "mass:/unchanged");
    configLegacySmbPaths();
    assert(!strcmp(setting->LK_Path[0], "smbold:/auto/BOOT.ELF"));
    assert(!strcmp(setting->LK_Path[1], "smbold:/button/BOOT.ELF"));
    assert(!strcmp(setting->LK_Path[2], "smb:/ordinary/BOOT.ELF"));
    assert(!strcmp(PathPad[0], "smbold:/bookmark/folder"));
    assert(!strcmp(PathPad[1], "mass:/unchanged"));

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
