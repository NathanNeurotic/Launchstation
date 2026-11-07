"""Run the production GUI SMB save logic with independently mocked file destinations."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_settings.c").read_text()
start = source.index("static int smbedReadText(const char *path, char **text)\n{")
end = source.index("\n}\n\nstatic void smbedSave(void)", start) + 2
save_function = source[start:end]

code = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "smb_shares.h"
#define MAX_PATH 1024
#define FIO_O_RDONLY 1
#define stricmp strcasecmp
typedef struct { unsigned int size, hisize; } iox_stat_t;
#define PRIMARY "mass:/SMB2.CNF"
#define MIRROR "mc0:/SYS-CONF/SMB2.CNF"

static struct { char orig[SMB_NAME_LEN]; char name[SMB_NAME_LEN]; int pick, partial_pending; char partial_mirror_name[SMB_NAME_LEN]; SmbConfig snap; } smbed;
static char primary[4096], mirror[4096];
static int primary_exists, mirror_exists, primary_read_error, mirror_read_error;
static int primary_stat_error, mirror_stat_error;
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
static int genGetStat(const char *path, iox_stat_t *st)
{
    int is_primary = !strcmp(path, PRIMARY);
    int exists = is_primary ? primary_exists : mirror_exists;
    int failed = is_primary ? primary_stat_error : mirror_stat_error;
    if (failed) return -EIO;
    if (!exists) return -ENOENT;
    st->size = (unsigned int)strlen(is_primary ? primary : mirror);
    st->hisize = 0;
    return 0;
}
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
    primary_stat_error = mirror_stat_error = 0;
    primary_write_error = mirror_write_error = 0;
    primary_writes = mirror_writes = reloads = 0;
    strcpy(smbed.orig, "ONE");
    strcpy(smbed.name, "ONE");
    smbed.pick = 1;
    smbed.partial_pending = 0;
    smbed.partial_mirror_name[0] = '\0';
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

    /* A failed stat after failed read is not evidence a file is absent. */
    init();
    mirror_read_error = mirror_stat_error = 1;
    ret = smbedWriteFile(changed);
    assert(ret == -7 && primary_writes == 0 && mirror_writes == 0);
    init();
    primary_read_error = primary_stat_error = 1;
    ret = smbedWriteFile(changed);
    assert(ret == -4 && primary_writes == 0 && mirror_writes == 0);

    /* A real zero-byte existing file can accept a new share. */
    init();
    primary[0] = mirror[0] = '\0';
    smbed.orig[0] = '\0';
    ret = smbedWriteFile(changed);
    assert(ret == 0 && primary_writes == 1 && mirror_writes == 1);
    assert(strstr(primary, "NAME = ONE") != NULL);
    assert(strstr(mirror, "NAME = ONE") != NULL);

    /* A same-NAME secondary pointing at a different share is not a mirror. */
    init();
    strcpy(mirror, "NAME = OTHER\nURL = smb://10.0.0.7/other\n"
                   "NAME = ONE\nURL = smb://10.0.0.8/different\n");
    ret = smbedWriteFile(changed);
    assert(ret == -10 && primary_writes == 0 && mirror_writes == 0);
    ret = smbedWriteFile(NULL);
    assert(ret == -10 && primary_writes == 0 && mirror_writes == 0);

    /* A same-NAME, same-URL share with different credentials is not identical. */
    init();
    strcpy(mirror, "NAME = ONE\nUSERNAME = outsider\nURL = smb://10.0.0.1/old\n");
    ret = smbedWriteFile(changed);
    assert(ret == -10 && primary_writes == 0 && mirror_writes == 0);

    /* A stale primary selection may not silently append a replacement. */
    init();
    strcpy(smbed.orig, "MISSING");
    ret = smbedWriteFile(changed);
    assert(ret == -9 && primary_writes == 0 && mirror_writes == 0);

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
    assert(smbed.partial_pending && !strcmp(smbed.orig, "ONE"));
    assert(!strcmp(smbed.partial_mirror_name, "ONE"));
    ret = smbedWriteFile(changed);
    assert(ret == -12 && primary_writes == 1 && mirror_writes == 1);

    init();
    primary_write_error = 1;
    ret = smbedWriteFile(changed);
    assert(ret == -11 && primary_writes == 1 && mirror_writes == 0 && reloads == 0);
    assert(smbed.partial_pending);
    ret = smbedWriteFile(changed);
    assert(ret == -12 && primary_writes == 1 && mirror_writes == 0);

    /* After partial rename, retain old mirror identity and lock GUI writes. */
    init();
    strcpy(smbed.name, "RENAMED");
    assert(smbCnfBuildBlock(changed, sizeof(changed), "RENAMED", 2, NULL, NULL,
                            "10.0.0.9", 445, "new", 0) > 0);
    mirror_write_error = 1;
    ret = smbedWriteFile(changed);
    assert(ret == -8 && !strcmp(smbed.orig, "RENAMED"));
    assert(!strcmp(smbed.partial_mirror_name, "ONE") && smbed.partial_pending);
    assert(strstr(primary, "NAME = RENAMED") != NULL);
    assert(strstr(mirror, "NAME = ONE") != NULL);
    ret = smbedWriteFile(changed);
    assert(ret == -12 && primary_writes == 1 && mirror_writes == 1);

    assert(smbCnfBuildBlock(changed, sizeof(changed), "ONE", 2, NULL, NULL,
                            "10.0.0.9", 445, "new", 0) > 0);

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

    /* Deleting the last mirrored share must truncate to zero bytes, not skip. */
    init();
    strcpy(primary, "NAME = ONE\nURL = smb://10.0.0.1/old\n");
    strcpy(mirror, "NAME = ONE\nURL = smb://10.0.0.1/old\n");
    ret = smbedWriteFile(NULL);
    assert(ret == 0 && primary_writes == 1 && mirror_writes == 1);
    assert(primary_exists && mirror_exists && !primary[0] && !mirror[0]);

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
