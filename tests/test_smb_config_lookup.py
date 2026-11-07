"""Exercise the production SMB2.CNF location lookup with failing/invalid candidates.

The test extracts the real reader-choice functions from smb_shares.c. It never
uses a PS2 driver, and is not a substitute for hardware testing.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/smb_shares.c").read_text()
start = source.index("static int smbLoadConfigCandidate(const char *path, char **text)\n{")
end = source.index("\n}\n\nstatic unsigned int smb_cfg_text_hash;", start) + 2
reader = source[start:end]

code = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_PATH 1024
#define SMB_CNF_NAME "SMB2.CNF"
#define SMB_CNF_MAX_SIZE 16384
typedef struct { unsigned int size, hisize; } iox_stat_t;

static char LaunchElfDir[MAX_PATH] = "mass:/APPS/";
static char smb_cfg_path[MAX_PATH];
static int smb_cfg_read_error, mc1_refused;
static int read_counts[3], stat_counts[3];
static struct {
    int exists, unreadable, stat_error, len;
    char bytes[256];
} files[3];

static int which(const char *path)
{
    if (!strcmp(path, "mass:/APPS/SMB2.CNF") || !strcmp(path, "mc1:/APPS/SMB2.CNF")) {
        if (!strncmp(path, "mc1:", 4)) assert(!mc1_refused);
        return 0;
    }
    if (!strcmp(path, "mc0:/SYS-CONF/SMB2.CNF")) return 1;
    if (!strcmp(path, "mc1:/SYS-CONF/SMB2.CNF")) { assert(!mc1_refused); return 2; }
    assert(!"unrecognized SMB config candidate");
    return -1;
}
static int mc1AccessRefused(void) { return mc1_refused; }
static char *dashLoadTextFile(const char *path, int limit, int *out_len)
{
    int i = which(path);
    char *result;
    read_counts[i]++;
    if (out_len) *out_len = 0;
    if (!files[i].exists || files[i].unreadable || files[i].len <= 0 ||
        files[i].len > limit)
        return NULL;
    result = (char *)malloc((size_t)files[i].len + 1);
    assert(result);
    memcpy(result, files[i].bytes, (size_t)files[i].len);
    result[files[i].len] = '\0';
    if (out_len) *out_len = files[i].len;
    return result;
}
static int genGetStat(const char *path, iox_stat_t *st)
{
    int i = which(path);
    stat_counts[i]++;
    if (files[i].stat_error) return -EIO;
    if (!files[i].exists) return -ENOENT;
    st->size = (unsigned int)files[i].len;
    st->hisize = 0;
    return 0;
}
'''
code += reader
code += r'''
static void reset(void)
{
    memset(files, 0, sizeof(files));
    memset(read_counts, 0, sizeof(read_counts));
    memset(stat_counts, 0, sizeof(stat_counts));
    snprintf(LaunchElfDir, sizeof(LaunchElfDir), "mass:/APPS/");
    smb_cfg_path[0] = '\0';
    smb_cfg_read_error = mc1_refused = 0;
}
static void set(int index, const char *text)
{
    assert(index >= 0 && index < 3 && strlen(text) < sizeof(files[index].bytes));
    files[index].exists = 1;
    files[index].len = (int)strlen(text);
    memcpy(files[index].bytes, text, (size_t)files[index].len);
}
int main(void)
{
    char *text;
    int refused;

    reset();
    set(0, "NAME = CWD\nURL = smb://10.0.0.1/cwd\n");
    set(1, "NAME = OTHER\nURL = smb://10.0.0.2/other\n");
    text = smbReadConfigText(&refused);
    assert(text && strstr(text, "CWD") && !smb_cfg_read_error && !refused);
    assert(!strcmp(smb_cfg_path, "mass:/APPS/SMB2.CNF"));
    assert(read_counts[0] == 1 && read_counts[1] == 0 && read_counts[2] == 0);
    free(text);

    reset();
    set(1, "NAME = CARD\nURL = smb://10.0.0.2/card\n");
    text = smbReadConfigText(&refused);
    assert(text && strstr(text, "CARD") && !smb_cfg_read_error);
    assert(!strcmp(smb_cfg_path, "mc0:/SYS-CONF/SMB2.CNF"));
    free(text);

    /* An existing unreadable CWD file must not select an unrelated card share. */
    reset();
    set(0, "NAME = WRONG\nURL = smb://10.0.0.1/wrong\n");
    files[0].unreadable = 1;
    set(1, "NAME = CARD\nURL = smb://10.0.0.2/card\n");
    text = smbReadConfigText(&refused);
    assert(!text && smb_cfg_read_error && !strcmp(smb_cfg_path, "mass:/APPS/SMB2.CNF"));
    assert(read_counts[1] == 0 && read_counts[2] == 0);

    /* Fail on an oversized higher-priority config rather than falling back. */
    reset();
    files[0].exists = 1;
    files[0].len = SMB_CNF_MAX_SIZE + 1;
    set(1, "NAME = CARD\nURL = smb://10.0.0.2/card\n");
    text = smbReadConfigText(&refused);
    assert(!text && smb_cfg_read_error && read_counts[1] == 0);

    /* Embedded NUL concealing a later share must never be parsed as success. */
    reset();
    set(0, "NAME = ONE\nURL = smb://10.0.0.1/one\n");
    files[0].bytes[files[0].len++] = '\0';
    memcpy(files[0].bytes + files[0].len, "NAME = HIDDEN\n", 14);
    files[0].len += 14;
    set(1, "NAME = CARD\nURL = smb://10.0.0.2/card\n");
    text = smbReadConfigText(&refused);
    assert(!text && smb_cfg_read_error && read_counts[1] == 0);

    /* Explicitly empty CWD configuration suppresses any card fallback. */
    reset();
    files[0].exists = 1; files[0].len = 0;
    set(1, "NAME = CARD\nURL = smb://10.0.0.2/card\n");
    text = smbReadConfigText(&refused);
    assert(text && !*text && !smb_cfg_read_error && read_counts[1] == 0);
    assert(stat_counts[0] == 1);
    free(text);

    /* A candidate with a non-ENOENT stat error is not evidence of absence. */
    reset();
    files[0].stat_error = 1;
    set(1, "NAME = CARD\nURL = smb://10.0.0.2/card\n");
    text = smbReadConfigText(&refused);
    assert(!text && smb_cfg_read_error && read_counts[1] == 0);

    /* An invalid mc0 file prevents fallback to a valid mc1 share. */
    reset();
    files[0].exists = 0;
    files[1].exists = 1; files[1].unreadable = 1; files[1].len = 90;
    set(2, "NAME = CARD2\nURL = smb://10.0.0.3/card\n");
    text = smbReadConfigText(&refused);
    assert(!text && smb_cfg_read_error && !strcmp(smb_cfg_path, "mc0:/SYS-CONF/SMB2.CNF"));
    assert(read_counts[2] == 0);

    /* MX4SIO ownership avoids implicit mc1 IO even when booted from mc1. */
    reset();
    snprintf(LaunchElfDir, sizeof(LaunchElfDir), "mc1:/APPS/");
    mc1_refused = 1;
    set(1, "NAME = CARD\nURL = smb://10.0.0.2/card\n");
    text = smbReadConfigText(&refused);
    assert(text && refused && !smb_cfg_read_error);
    assert(!strcmp(smb_cfg_path, "mc0:/SYS-CONF/SMB2.CNF"));
    assert(!read_counts[0] && !read_counts[2] && !stat_counts[0] && !stat_counts[2]);
    free(text);

    puts("PASS: SMB config lookup preserves precedence and refuses unreadable/corrupt candidates");
    return 0;
}
'''
compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="smb-config-lookup-") as tmp:
    src = Path(tmp) / "test_smb_config_lookup.c"
    binary = Path(tmp) / "test_smb_config_lookup"
    src.write_text(code)
    subprocess.run([compiler, "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                    "-I", str(root / "include"), str(src), "-o", str(binary)],
                   check=True)
    subprocess.run([str(binary)], check=True, timeout=5)
