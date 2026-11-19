"""Verify LAUNCHSTATION.CNF share sections outrank legacy SMB2.CNF fallback."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
shares = (root / "src/smb_shares.c").read_text()
cnf = (root / "src/smb_cnf.c").read_text()

code = r"""
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_PATH 1024
#define SMB_CNF_SECTION "# SMB shares"
#define SMB_CNF_NAME "SMB2.CNF"
#define SMB_CNF_MAX_SIZE 16384
static char LoadedConfigPath[MAX_PATH], LaunchElfDir[64], smb_cfg_path[MAX_PATH];
static const char *primary_text, *fallback_text;
static int mc1AccessRefused(void) { return 0; }
static char *dashLoadTextFile(const char *path, int max_size, int *len) {
    const char *text = NULL;
    (void)max_size;
    if (!strcmp(path, LoadedConfigPath)) text = primary_text;
    else if (!strcmp(path, "mass:/SMB2.CNF")) text = fallback_text;
    if (len != NULL) *len = text != NULL ? (int)strlen(text) : 0;
    return text != NULL ? strdup(text) : NULL;
}
"""
start = cnf.index("const char *smbCnfSection(")
end = cnf.index("\n}\n", start) + 3
code += cnf[start:end] + "\n"
start = shares.index("static char *smb_blocks;")
end = shares.index("static unsigned int smb_cfg_text_hash;")
code += shares[start:end]
code += r"""
static void reset_case(void) {
    free(smbSharesTakeBlocks(NULL));
    strcpy(LoadedConfigPath, "mc0:/SYS-CONF/LAUNCHSTATION.CNF");
    strcpy(LaunchElfDir, "mass:/");
    primary_text = fallback_text = NULL;
}
static char *read_source(void) {
    int refused = -1;
    char *text = smbReadConfigText(&refused);
    assert(refused == 0);
    return text;
}
int main(void) {
    char *text;
    reset_case();
    primary_text = "CNF_version=4\n# SMB shares\nNAME = NEW\n";
    fallback_text = "NAME = OLD\n";
    text = read_source();
    assert(text && strstr(text, "NEW") && !strcmp(smb_cfg_path, LoadedConfigPath));
    free(text);

    primary_text = "CNF_version=4\n";
    text = read_source();
    assert(text && strstr(text, "OLD") && !strcmp(smb_cfg_path, "mass:/SMB2.CNF"));
    free(text);

    primary_text = "# SMB shares\nNAME = PREF\n";
    text = read_source();
    assert(text && strstr(text, "PREF") && !strcmp(smb_cfg_path, LoadedConfigPath));
    free(text);

    primary_text = NULL;
    assert(smbSharesSetBlocks("NAME = MEMORY\n"));
    text = read_source();
    assert(text && strstr(text, "MEMORY") && !strcmp(smb_cfg_path, LoadedConfigPath));
    free(text);

    free(smbSharesTakeBlocks(NULL));
    primary_text = "CNF_version=4\n";
    text = read_source();
    assert(text && strstr(text, "OLD") && !strcmp(smb_cfg_path, "mass:/SMB2.CNF"));
    free(text);
    free(smbSharesTakeBlocks(NULL));
    puts("PASS: LAUNCHSTATION.CNF sections and in-memory edits outrank SMB2.CNF");
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="smb-config-source-") as tmp:
    source = Path(tmp) / "source.c"
    exe = Path(tmp) / "source"
    source.write_text(code)
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "A C host compiler is required"
    subprocess.run([compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
