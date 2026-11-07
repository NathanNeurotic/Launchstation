"""Run the production GUI SMB save logic with independently mocked file destinations."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_settings.c").read_text()
helper_start = source.index("static void smbedBuildSysconfPath(char *path, int size)\n{")
helper_end = source.index("\n}\n\nstatic void smbedConfigPath(", helper_start) + 2
target_helper = source[helper_start:helper_end]
start = source.index("static int smbedReadText(const char *path, char **text)\n{")
end = source.index("\n}\n\nstatic void smbedSave(void)", start) + 2
save_function = source[start:end]
store_source = (root / "src/dash_store.c").read_text()
load_start = store_source.index("char *dashLoadTextFile(const char *path, int max_size, int *out_len)\n{")
load_end = store_source.index("\n}\n\nint dashWriteFile(", load_start) + 2
loader_function = store_source[load_start:load_end]

code = r'''
#include <assert.h>
#include <errno.h>
#include <malloc.h>
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
static char primary[SMB_CNF_MAX_SIZE + 2048], mirror[SMB_CNF_MAX_SIZE + 2048];
static int primary_length = -1, mirror_length = -1;
static long long primary_reported_size = -1;
static int primary_exists, mirror_exists, primary_read_error, mirror_read_error;
static int primary_stat_error, mirror_stat_error;
static int primary_read_limit, mirror_read_limit;
static int primary_write_error, mirror_write_error;
static int primary_writes, mirror_writes, reloads, reload_ok;

static void dashStrCopy(char *out, const char *value, int size)
{
    assert(size > 0);
    snprintf(out, (size_t)size, "%s", value);
}
static int mx4sio_owns_slot2, ordinary_sysconf_probes;
static int mc1AccessRefused(void) { return mx4sio_owns_slot2; }
static void smbedConfigPath(char *out, int n) { snprintf(out, n, "%s", PRIMARY); }
static void configBuildSysconfPath(char *out, int n, const char *file)
{
    /* This mock represents the generic helper probing both card slots.
     * It must not run if the SMB-specific guard says slot 2 is owned. */
    assert(!mx4sio_owns_slot2);
    ordinary_sysconf_probes++;
    assert(!strcmp(file, "SMB2.CNF"));
    snprintf(out, n, "%s", MIRROR);
}
/* Exercise the production loader, including short positive reads and premature EOF. */
static int read_offset;
static int genFixPath(const char *path, char *fixed)
{
    snprintf(fixed, MAX_PATH, "%s", path);
    return 0;
}
static int genOpen(const char *path, int flags)
{
    int first = !strcmp(path, PRIMARY);
    assert(flags == FIO_O_RDONLY);
    if (!(first ? primary_exists : mirror_exists) ||
        (first ? primary_read_error : mirror_read_error))
        return -ENOENT;
    read_offset = 0;
    return first ? 1 : 2;
}
static long long genLseek(int fd, int amount, int whence)
{
    int size = fd == 1 ? (primary_length >= 0 ? primary_length : (int)strlen(primary))
                       : (mirror_length >= 0 ? mirror_length : (int)strlen(mirror));
    if (whence == SEEK_END) {
        read_offset = size + amount;
        if (fd == 1 && primary_reported_size >= 0) return primary_reported_size;
    }
    else if (whence == SEEK_SET) read_offset = amount;
    else assert(0);
    return read_offset;
}
static int genRead(int fd, void *buf, int size)
{
    const char *src = fd == 1 ? primary : mirror;
    int limit = fd == 1 ? primary_read_limit : mirror_read_limit;
    int actual = fd == 1 ? (primary_length >= 0 ? primary_length : (int)strlen(primary))
                         : (mirror_length >= 0 ? mirror_length : (int)strlen(mirror));
    int remaining = actual - read_offset;
    if (limit >= 0 && limit - read_offset < remaining)
        remaining = limit - read_offset;
    if (remaining <= 0) return 0;
    if (size > remaining) size = remaining;
    if (size > 7) size = 7;
    memcpy(buf, src + read_offset, (size_t)size);
    read_offset += size;
    return size;
}
static int genClose(int fd) { (void)fd; return 0; }
static int genGetStat(const char *path, iox_stat_t *st)
{
    int is_primary = !strcmp(path, PRIMARY);
    int exists = is_primary ? primary_exists : mirror_exists;
    int failed = is_primary ? primary_stat_error : mirror_stat_error;
    if (failed) return -EIO;
    if (!exists) return -ENOENT;
    st->size = (unsigned int)(is_primary ? (primary_length >= 0 ? primary_length : (int)strlen(primary))
                                       : (mirror_length >= 0 ? mirror_length : (int)strlen(mirror)));
    st->hisize = is_primary && primary_reported_size > 0
               ? (unsigned int)((unsigned long long)primary_reported_size >> 32) : 0;
    return 0;
}
static int dashWriteFile(const char *path, const void *data, int n)
{
    int is_primary = !strcmp(path, PRIMARY);
    char *dst = is_primary ? primary : mirror;
    assert(n >= 0 && n < (int)sizeof(primary));
    if (is_primary) primary_writes++;
    else mirror_writes++;
    if (is_primary ? primary_write_error : mirror_write_error)
        return -1;
    memcpy(dst, data, n);
    dst[n] = '\0';
    if (is_primary) { primary_exists = 1; primary_length = n; }
    else { mirror_exists = 1; mirror_length = n; }
    return 0;
}
int smbSharesReload(void) { reloads++; return reload_ok; }
int smbSharesSnapshot(SmbConfig *out) { memset(out, 0, sizeof(*out)); return 0; }
'''
code += target_helper + '\n' + loader_function + '\n' + save_function
code += r'''
static void init(void)
{
    strcpy(primary, "# primary\nNAME = ONE\nURL = smb://10.0.0.1/old\n"
                    "NAME = TWO\nURL = smb://10.0.0.2/two\n");
    strcpy(mirror, "# mirror\nNAME = OTHER\nURL = smb://10.0.0.7/other\n"
                   "NAME = ONE\nURL = smb://10.0.0.1/old\n");
    primary_exists = mirror_exists = reload_ok = 1;
    primary_read_error = mirror_read_error = 0;
    primary_length = mirror_length = -1;
    primary_reported_size = -1;
    mx4sio_owns_slot2 = ordinary_sysconf_probes = 0;
    primary_stat_error = mirror_stat_error = 0;
    primary_read_limit = mirror_read_limit = -1;
    primary_write_error = mirror_write_error = 0;
    primary_writes = mirror_writes = reloads = 0;
    strcpy(smbed.orig, "ONE");
    strcpy(smbed.name, "ONE");
    smbed.pick = 1;
    smbed.partial_pending = 0;
    smbed.partial_mirror_name[0] = '\0';
    /* The real editor populates this snapshot when the user picks a share. */
    {
        char copy[SMB_CNF_MAX_SIZE + 2048];
        memcpy(copy, primary, strlen(primary) + 1);
        smbConfigParse(copy, &smbed.snap);
    }
}

static void extend_with_comment(char *path, int desired_length)
{
    int n = (int)strlen(path);
    assert(desired_length < (int)sizeof(primary) && desired_length > n + 2);
    path[n++] = '#';
    while (n < desired_length - 1) path[n++] = 'x';
    path[n++] = '\n';
    path[n] = '\0';
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

    /* A partial primary read must never overwrite its unseen later share. */
    init();
    primary_read_limit = (int)(strstr(primary, "NAME = TWO") - primary);
    ret = smbedWriteFile(changed);
    assert(ret == -4 && primary_writes == 0 && mirror_writes == 0 && reloads == 0);
    assert(strstr(primary, "NAME = TWO") != NULL);

    /* A partial mirror read must refuse the primary write during preflight. */
    init();
    mirror_read_limit = (int)(strstr(mirror, "NAME = ONE") - mirror);
    ret = smbedWriteFile(changed);
    assert(ret == -7 && primary_writes == 0 && mirror_writes == 0 && reloads == 0);
    assert(strstr(mirror, "NAME = ONE") != NULL);

    /* A real zero-byte existing file can accept a new share. */
    init();
    primary[0] = mirror[0] = '\0';
    smbed.orig[0] = '\0';
    ret = smbedWriteFile(changed);
    assert(ret == 0 && primary_writes == 1 && mirror_writes == 1);
    assert(strstr(primary, "NAME = ONE") != NULL);
    assert(strstr(mirror, "NAME = ONE") != NULL);

    /* Selecting an automatic SYS-CONF mirror may not probe mc1 or reset the
     * IOP while MX4SIO owns slot 2. Neither may an implicit mc1 read. */
    init();
    mx4sio_owns_slot2 = 1;
    {
        char target[MAX_PATH], *data = NULL;
        smbedBuildSysconfPath(target, sizeof(target));
        assert(!strcmp(target, MIRROR) && ordinary_sysconf_probes == 0);
        assert(smbedReadText("mc1:/SYS-CONF/SMB2.CNF", &data) < 0 && !data);
    }
    ret = smbedWriteFile(changed);
    assert(ret == 0 && ordinary_sysconf_probes == 0);
    assert(primary_writes == 1 && mirror_writes == 1);

    /* 64-bit seek values must not wrap into a convincing short text file. */
    init();
    primary_reported_size = (1LL << 32) + 63;
    ret = smbedWriteFile(changed);
    assert(ret == -4 && primary_writes == 0 && mirror_writes == 0);
    assert(strstr(primary, "NAME = TWO") != NULL);

    /* A NUL hides later shares from strlen() and must reject all writes. */
    init();
    {
        int n = (int)strlen(primary);
        const char hidden[] = "NAME = HIDDEN\nURL = smb://10.0.0.3/hidden\n";
        memcpy(primary + n + 1, hidden, sizeof(hidden));
        primary_length = n + 1 + (int)strlen(hidden);
        primary[n] = '\0';
    }
    ret = smbedWriteFile(changed);
    assert(ret == -4 && primary_writes == 0 && mirror_writes == 0);
    init();
    {
        int n = (int)strlen(mirror);
        const char hidden[] = "NAME = HIDDEN\nURL = smb://10.0.0.3/hidden\n";
        memcpy(mirror + n + 1, hidden, sizeof(hidden));
        mirror_length = n + 1 + (int)strlen(hidden);
        mirror[n] = '\0';
    }
    ret = smbedWriteFile(changed);
    assert(ret == -7 && primary_writes == 0 && mirror_writes == 0);

    /* A save must not exceed the loader's 16-KiB maximum, even when the
     * upsert's scratch buffer could hold the resulting larger text. */
    init();
    smbed.orig[0] = '\0';
    strcpy(smbed.name, "THREE");
    assert(smbCnfBuildBlock(changed, sizeof(changed), "THREE", 2, NULL, NULL,
                            "10.0.0.9", 445, "new", 0) > 0);
    extend_with_comment(primary, SMB_CNF_MAX_SIZE - 8);
    ret = smbedWriteFile(changed);
    assert(ret == -1 && primary_writes == 0 && mirror_writes == 0);

    init();
    smbed.orig[0] = '\0';
    strcpy(smbed.name, "THREE");
    extend_with_comment(mirror, SMB_CNF_MAX_SIZE - 8);
    ret = smbedWriteFile(changed);
    assert(ret == -7 && primary_writes == 0 && mirror_writes == 0);
    assert(strstr(mirror, "NAME = ONE") != NULL);

    assert(smbCnfBuildBlock(changed, sizeof(changed), "ONE", 2, NULL, NULL,
                            "10.0.0.9", 445, "new", 0) > 0);

    /* The text editor may seed its example file only if stat proves the
     * target does not exist. Failed read/stat must not clobber credentials. */
    init();
    primary_read_error = 1;
    ret = smbedSeedTemplateIfAbsent(PRIMARY, "# template\n", 11);
    assert(ret == 0 && primary_writes == 0);
    assert(strstr(primary, "NAME = ONE") != NULL);
    init();
    primary_stat_error = 1;
    ret = smbedSeedTemplateIfAbsent(PRIMARY, "# template\n", 11);
    assert(ret == 0 && primary_writes == 0);
    assert(strstr(primary, "NAME = ONE") != NULL);
    init();
    primary_exists = 0;
    ret = smbedSeedTemplateIfAbsent(PRIMARY, "# template\n", 11);
    assert(ret == 0 && primary_writes == 1 && primary_exists);
    assert(strcmp(primary, "# template\n") == 0);
    init();
    primary_exists = 0;
    primary_write_error = 1;
    ret = smbedSeedTemplateIfAbsent(PRIMARY, "# template\n", 11);
    assert(ret < 0 && primary_writes == 1);

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

    /* Editing a same-NAME mirror with a different declared SMB dialect
     * marker must not silently replace that file's choice. */
    init();
    strcpy(primary, "NAME = ONE\nVERSION = 3\nURL = smb://10.0.0.1/old\n"
                    "NAME = TWO\nURL = smb://10.0.0.2/two\n");
    strcpy(mirror, "NAME = ONE\nVERSION = 2\nURL = smb://10.0.0.1/old\n");
    ret = smbedWriteFile(changed);
    assert(ret == -10 && primary_writes == 0 && mirror_writes == 0);

    /* A valid on-disk share may have been edited by a second editor after
     * it was selected in the GUI; do not overwrite the newer credentials. */
    init();
    strcpy(primary, "# primary\nNAME = ONE\nUSERNAME = somebody_else\n"
                    "URL = smb://10.0.0.1/old\nNAME = TWO\n"
                    "URL = smb://10.0.0.2/two\n");
    ret = smbedWriteFile(changed);
    assert(ret == -9 && primary_writes == 0 && mirror_writes == 0);
    assert(strstr(primary, "USERNAME = somebody_else") != NULL);

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

    /* Both files can be written even though the old IOP connection
     * refuses to retire. A subsequent edit or delete must remain locked. */
    init();
    reload_ok = 0;
    ret = smbedWriteFile(changed);
    assert(ret == -6 && smbed.partial_pending);
    assert(primary_writes == 1 && mirror_writes == 1 && reloads == 1);
    assert(strstr(primary, "smb://10.0.0.9/new") != NULL);
    ret = smbedWriteFile(changed);
    assert(ret == -12 && primary_writes == 1 && mirror_writes == 1);
    init();
    reload_ok = 0;
    ret = smbedWriteFile(NULL);
    assert(ret == -6 && smbed.partial_pending);
    assert(primary_writes == 1 && mirror_writes == 1 && reloads == 1);
    ret = smbedWriteFile(NULL);
    assert(ret == -12 && primary_writes == 1 && mirror_writes == 1);

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
