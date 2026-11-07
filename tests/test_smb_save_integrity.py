"""Run the production GUI SMB save logic with independently mocked file destinations."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_settings.c").read_text()
start = source.index("static int smbedWriteFile(const char *block)\n{")
end = source.index("\n}\n\nstatic void smbedSave(void)", start) + 2
save_function = source[start:end]

code = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "smb_shares.h"
#define MAX_PATH 1024
#define FIO_O_RDONLY 1
#define stricmp strcasecmp
#define PRIMARY "mass:/SMB2.CNF"
#define MIRROR "mc0:/SYS-CONF/SMB2.CNF"

static struct { char orig[SMB_NAME_LEN]; char name[SMB_NAME_LEN]; SmbConfig snap; } smbed;
static char primary[4096], mirror[4096];
static int primary_exists, mirror_exists, primary_read_error, mirror_read_error;
static int primary_write_error, mirror_write_error;
static int primary_writes, mirror_writes, reloads, reload_ok;

static void smbedConfigPath(char *out, int n) { snprintf(out, n, "%s", PRIMARY); }
static void configBuildSysconfPath(char *out, int n, const char *file)
{
    assert(!strcmp(file, "SMB2.CNF"));
    snprintf(out, n, "%s", MIRROR);
}
static char *dashLoadTextFile(const char *path, int cap, int *len)
{
    int is_primary = !strcmp(path, PRIMARY);
    const char *value = is_primary ? primary : mirror;
    int exists = is_primary ? primary_exists : mirror_exists;
    int read_error = is_primary ? primary_read_error : mirror_read_error;
    (void)len;
    return exists && !read_error && strlen(value) > 0 &&
           strlen(value) <= (size_t)cap ? strdup(value) : NULL;
}
static int genOpen(const char *path, int flags)
{
    assert(flags == FIO_O_RDONLY);
    return (!strcmp(path, PRIMARY) ? primary_exists : mirror_exists) ? 3 : -1;
}
static int genClose(int fd) { assert(fd == 3); return 0; }
static int dashWriteFile(const char *path, const void *data, int n)
{
    int is_primary = !strcmp(path, PRIMARY);
    char *dst = is_primary ? primary : mirror;
    assert(n >= 0 && n < 4096);
    if (is_primary) primary_writes++;
    else mirror_writes++;
    if (is_primary ? primary_write_error : mirror_write_error)
        return -1;
    memcpy(dst, data, n);
    dst[n] = '\0';
    if (is_primary) primary_exists = 1;
    else mirror_exists = 1;
    return 0;
}
int smbSharesReload(void) { reloads++; return reload_ok; }
int smbSharesSnapshot(SmbConfig *out) { memset(out, 0, sizeof(*out)); return 0; }
'''
code += save_function
code += r'''
static void init(void)
{
    strcpy(primary, "# primary\nNAME = ONE\nURL = smb://10.0.0.1/old\n"
                    "NAME = TWO\nURL = smb://10.0.0.2/two\n");
    strcpy(mirror, "# mirror\nNAME = OTHER\nURL = smb://10.0.0.7/other\n"
                   "NAME = ONE\nURL = smb://10.0.0.1/old\n");
    primary_exists = mirror_exists = reload_ok = 1;
    primary_read_error = mirror_read_error = 0;
    primary_write_error = mirror_write_error = 0;
    primary_writes = mirror_writes = reloads = 0;
    strcpy(smbed.orig, "ONE");
    strcpy(smbed.name, "ONE");
}

int main(void)
{
    char changed[512];
    int ret;
    assert(smbCnfBuildBlock(changed, sizeof(changed), "ONE", 2, NULL, NULL,
                            "10.0.0.9", 445, "new", 0) > 0);

    init();
    ret = smbedWriteFile(changed);
    assert(ret == 0 && primary_writes == 1 && mirror_writes == 1 && reloads == 1);
    assert(strstr(primary, "smb://10.0.0.9/new") != NULL);
    assert(strstr(primary, "NAME = TWO\nURL = smb://10.0.0.2/two") != NULL);
    assert(strstr(mirror, "# mirror\nNAME = OTHER\nURL = smb://10.0.0.7/other") != NULL);
    assert(strstr(mirror, "smb://10.0.0.9/new") != NULL);
    assert(strstr(mirror, "smb://10.0.0.2/two") == NULL);

    /* A secondary file with independent shares must not be overwritten. */
    init();
    strcpy(mirror, "NAME = OTHER\nURL = smb://10.0.0.7/other\n");
    ret = smbedWriteFile(changed);
    assert(ret == 0 && strstr(mirror, "NAME = OTHER\nURL = smb://10.0.0.7/other") != NULL);
    assert(strstr(mirror, "NAME = ONE\nURL = smb://10.0.0.9/new") != NULL);

    /* An absent SYS-CONF copy is created from the validated primary config. */
    init();
    mirror_exists = 0;
    ret = smbedWriteFile(changed);
    assert(ret == 0 && mirror_writes == 1 && mirror_exists);
    assert(strcmp(primary, mirror) == 0);

    /* An unreadable existing mirror rejects edits before primary write. */
    init();
    mirror_read_error = 1;
    ret = smbedWriteFile(changed);
    assert(ret == -7 && primary_writes == 0 && mirror_writes == 0 && reloads == 0);

    /* A mirrored rename that would introduce duplicate NAME must fail closed. */
    init();
    strcpy(smbed.name, "OTHER");
    assert(smbCnfBuildBlock(changed, sizeof(changed), "OTHER", 2, NULL, NULL,
                            "10.0.0.9", 445, "new", 0) > 0);
    ret = smbedWriteFile(changed);
    assert(ret == -7 && primary_writes == 0 && mirror_writes == 0);

    assert(smbCnfBuildBlock(changed, sizeof(changed), "ONE", 2, NULL, NULL,
                            "10.0.0.9", 445, "new", 0) > 0);
    init();
    mirror_write_error = 1;
    ret = smbedWriteFile(changed);
    assert(ret == -8 && primary_writes == 1 && mirror_writes == 1 && reloads == 1);
    assert(strstr(primary, "smb://10.0.0.9/new") != NULL);
    assert(strstr(mirror, "smb://10.0.0.1/old") != NULL);

    init();
    primary_write_error = 1;
    ret = smbedWriteFile(changed);
    assert(ret == -3 && primary_writes == 1 && mirror_writes == 0 && reloads == 0);

    /* Delete does not create a missing mirror or modify unrelated entries. */
    init();
    mirror_exists = 0;
    ret = smbedWriteFile(NULL);
    assert(ret == 0 && primary_writes == 1 && mirror_writes == 0);
    assert(!strstr(primary, "NAME = ONE"));
    assert(strstr(primary, "NAME = TWO"));
    init();
    strcpy(mirror, "NAME = OTHER\nURL = smb://10.0.0.7/other\n");
    ret = smbedWriteFile(NULL);
    assert(ret == 0 && mirror_writes == 0);
    assert(strstr(mirror, "NAME = OTHER"));

    init();
    primary_read_error = 1;
    ret = smbedWriteFile(changed);
    assert(ret == -4 && primary_writes == 0 && mirror_writes == 0);

    puts("PASS: GUI SMB save preflights and preserves independent files and reports partial writes");
    return 0;
}
'''

compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="smb-save-integrity-") as tmp:
    src = Path(tmp) / "test_smb_save_integrity.c"
    exe = Path(tmp) / "test_smb_save_integrity"
    src.write_text(code)
    subprocess.run([compiler, "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-DSMB",
                    "-I", str(root / "include"), str(src), str(root / "src/smb_cnf.c"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=5)
