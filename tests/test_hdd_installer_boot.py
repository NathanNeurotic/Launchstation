"""Run console boot configuration and the real installation job with mock I/O."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
code = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hdd_install.h"
typedef uint32_t u32;
enum { OPEN, READ, WRITE, CLOSE };
static unsigned char config[15], original[15];
static int access_no, mode, opened, fault_access, fault_cmd, fault_result;
static unsigned fault_status, calls[4], delays;
static int corrupt, transient;
static int command(int op, u32 *status) {
    calls[op]++;
    *status = 0;
    if (access_no == fault_access && op == fault_cmd) {
        *status = fault_status;
        return fault_result;
    }
    if (transient) { transient--; *status = 8; return 1; }
    return 1;
}
int sceCdOpenConfig(int block, int writing, int count, u32 *status) {
    int ret;
    assert(block == 0 && count == 1 && !opened);
    ret = command(OPEN, status);
    if (ret && !(*status & 0x89)) { opened = 1; mode = writing; }
    return ret;
}
int sceCdReadConfig(void *data, u32 *status) {
    int ret;
    assert(opened && !mode && !((uintptr_t)data & 63));
    ret = command(READ, status);
    if (ret && !(*status & 0x89)) {
        memcpy(data, config, 15);
        if (access_no == 2 && corrupt) ((unsigned char *)data)[corrupt - 1] ^= 1;
    }
    return ret;
}
int sceCdWriteConfig(const void *data, u32 *status) {
    int ret;
    assert(opened && mode && access_no == 1 && !((uintptr_t)data & 63));
    ret = command(WRITE, status);
    if (ret && !(*status & 0x89)) memcpy(config, data, 15);
    return ret;
}
int sceCdCloseConfig(u32 *status) {
    int ret;
    assert(opened);
    ret = command(CLOSE, status);
    if (ret && !(*status & 0x89)) { opened = 0; access_no++; }
    return ret;
}
int DelayThread(int usec) { assert(usec == 10000); delays++; return 0; }
void hddInstallLog(const char *format, ...) { assert(format); }
void hddInstallStatus(const char *text, int done, int total) { assert(text && done <= total); }
static void reset(HddInstall *in, unsigned byte) {
    memset(in, 0, sizeof(*in)); in->flags = HDD_INSTALL_HOSD;
    for (unsigned i = 0; i < 15; i++) config[i] = (i * 19 + byte) & 255;
    config[0] = byte;
    memcpy(original, config, 15);
    access_no = mode = opened = corrupt = transient = 0;
    fault_access = fault_cmd = -1;
    fault_status = fault_result = delays = 0;
    memset(calls, 0, sizeof(calls));
}
int main(void) {
    HddInstall in;
    for (unsigned byte = 0; byte < 256; byte++) {
        reset(&in, byte);
        assert(hddReadBootFlag() == ((byte & 3) == 2));
        assert(!opened && access_no == 1 && calls[READ] == 1 && !calls[WRITE]);
        assert(!memcmp(config, original, sizeof(config)));
        reset(&in, byte);
        assert(hddInstallEnableBoot(&in) == 0 && !in.error[0]);
        assert(config[0] == ((byte & ~3u) | 2) && !memcmp(config + 1, original + 1, 14));
        assert(!opened && access_no == ((byte & 3) == 2 ? 1 : 3));
        assert(calls[WRITE] == ((byte & 3) == 2 ? 0u : 1u));
    }
    reset(&in, 0xa2);
    fault_access = 0; fault_cmd = READ; fault_status = 0x80; fault_result = 1;
    assert(hddReadBootFlag() == -EIO && !opened && !calls[WRITE]);
    reset(&in, 0xa2);
    assert(hddEnableBootFlag() == HDD_BOOT_ALREADY_ENABLED && calls[READ] == 1 && !calls[WRITE]);
    reset(&in, 0xa1);
    assert(hddEnableBootFlag() == HDD_BOOT_ENABLED && calls[READ] == 2 && calls[WRITE] == 1 && config[0] == 0xa2);
    assert(hddEnableBootFlag() == HDD_BOOT_ALREADY_ENABLED && calls[WRITE] == 1);
    for (unsigned flags = 0; flags < 16; flags++) {
        if (flags & HDD_INSTALL_HOSD) continue;
        reset(&in, 3); in.flags = flags;
        assert(hddInstallEnableBoot(&in) == 0 && !calls[OPEN] && !memcmp(config, original, 15));
    }
    /* Errors at every stage, including close/commit and verify-read, must
     * report failure. No failed initial read may lead to a write. */
    for (int a = 0; a < 3; a++) for (int op = OPEN; op <= CLOSE; op++) {
        if ((a == 1 && op == READ) || (a != 1 && op == WRITE)) continue;
        for (int failure = 0; failure < 4; failure++) {
            reset(&in, 3); fault_access = a; fault_cmd = op;
            fault_status = failure == 0 ? 0 : failure == 1 ? 8 : failure == 2 ? 1 : 0x80;
            fault_result = failure != 0;
            assert(hddInstallEnableBoot(&in) == -EIO && strstr(in.error, "console HDD boot failed"));
            assert(calls[op] <= 302 && delays <= 300);
            if (!a) assert(!calls[WRITE] && !memcmp(config, original, 15));
            if (op != CLOSE) assert(!opened);
        }
    }
    for (int byte = 1; byte <= 15; byte++) {
        reset(&in, 3); corrupt = byte;
        assert(hddInstallEnableBoot(&in) == -EIO && in.error[0] && calls[WRITE] == 1 && !opened);
    }
    reset(&in, 3); transient = 2;
    assert(hddInstallEnableBoot(&in) == 0 && calls[OPEN] == 5 && delays == 2);
    puts("PASS: canonical boot flags, all unrelated bytes preserved, no redundant writes, bounded config errors and complete readback verification");
}
'''

job_source = (root / "src/dash_hdd.c").read_text()
job = job_source[job_source.index("\t\tcase HOP_INSTALL_SOFTWARE:"):job_source.index("\t\tcase HOP_UNMOUNT:")]
job_code = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include "hdd_install.h"
int hddInstallResumeSave(HddInstall *in){assert(in);return 0;}
int hddInstallFindStage(HddInstall *in){assert(in);return 0;}
#define HOP_INSTALL_SOFTWARE 1
typedef struct { HddInstall *installer; HddFormatPlan format_plan; int ret, format_started; } HddJob;
static int apply_error, mount_error, finish_error, boot_error, applied, mounted, finished, enabled, cleaned, unmounted;
void hddInstallLog(const char *fmt, ...) { (void)fmt; }
void hddInstallLogFlush(HddInstall *in) { (void)in; }
static void filerSizeCacheInvalidate(const char *s) { assert(s); }
int hddInstallApply(HddInstall *in) { assert(in); applied++; return apply_error; }
int hddInstallMountStage(HddInstall *in) { assert(in && applied); mounted++; return mount_error; }
int hddInstallPsbbnPayload(void *in, const HddFormatIo *io) { (void)in; (void)io; return 0; }
int hddInstallFinish(const HddFormatPlan *p, const char *mbr, const char *digest, int *started,
                     int (*populate)(void *, const HddFormatIo *), void *context) {
    (void)digest;
    HddInstall *in = context;
    assert(p && started && mounted && !enabled && !cleaned);
    assert(!!mbr == !!(in->flags & HDD_INSTALL_HOSD));
    assert(!!populate == !!(in->flags & HDD_INSTALL_PSBBN));
    finished++; return finish_error;
}
int hddInstallEnableBoot(HddInstall *in) {
    assert(in && finished && !finish_error && !cleaned);
    enabled++; return boot_error;
}
int hddInstallCleanup(HddInstall *in) { assert(in && finished && enabled && !boot_error); cleaned++; return 0; }
int hddInstallUnmountStage(HddInstall *in) { assert(in && mounted); unmounted++; return 0; }
static void run(HddJob *j) {
    switch (HOP_INSTALL_SOFTWARE) {
'''
job_code += job + r'''
    }
}
int main(void) {
    HddInstall in = {0}; HddJob job = {.installer = &in};
    const unsigned profiles[] = {HDD_INSTALL_APPS, HDD_INSTALL_HOSD, HDD_INSTALL_HOSD | HDD_INSTALL_APPS,
        HDD_INSTALL_HOSD | HDD_INSTALL_APPS | HDD_INSTALL_PSBBN};
    for (unsigned i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
        in.flags = profiles[i];
        for (int failure = 0; failure <= 4; failure++) {
            in.applied = in.populated = in.committed = 0;
            applied = mounted = finished = enabled = cleaned = unmounted = 0;
            apply_error = failure == 1 ? -EIO : 0; mount_error = failure == 2 ? -EIO : 0;
            finish_error = failure == 3 ? -EIO : 0; boot_error = failure == 4 ? -EIO : 0;
            run(&job);
            assert((job.ret == 0) == !failure);
            assert(enabled == (!failure || failure == 4));
            assert(cleaned == !failure);
            assert(unmounted == (!failure || failure >= 3));
        }
    }
    puts("PASS: console boot only after successful software/MBR commit; errors retained and failed installs keep staged diagnostics");
}
'''

menu = job_source[job_source.index("static void partitionMenu("):job_source.index("// Drawing", job_source.index("static void partitionMenu("))]
assert 'if (!console_is_PSX && p && p->type == HP_SYSTEM && !strcmp(p->name, "__mbr"))' in menu
assert menu.index('"Format HDD..."') < menu.index('"Enable HDD boot flag..."') < menu.index('"Resume installation..."')
assert 'runBootFlagJob(HOP_READ_BOOT_FLAG, &ret) && ret > 0' in menu
assert 'ADD(enabled ? DI_BOOT_FLAG_ENABLED : DI_BOOT_FLAG,' in menu
assert 'enabled ? "HDD boot flag enabled..." : "Enable HDD boot flag...", NULL, enabled, A_ENABLE_BOOT_FLAG' in menu
assert 'case A_ENABLE_BOOT_FLAG:\n\t\t\tactEnableBootFlag();' in menu
worker = job_source[job_source.index("\t\tcase HOP_READ_BOOT_FLAG:"):job_source.index("\t\tcase HOP_RESUME_AVAILABLE:")]
manual = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "hdd_install.h"
enum { DI_CHECK, DI_WARNING, HOP_ENABLE_BOOT_FLAG, HOP_READ_BOOT_FLAG };
typedef struct { int op, ret; } HddJob;
static HddJob job;
static int idle=1, expected, jobs, modules, notices, reads, writes;
static int dashWorkerAwaitAll(const char *title){assert(title&&*title);return idle;}
static void loadCdModules(void){assert(idle);modules++;}
static HddJob *jobNew(int op){assert(modules>jobs);jobs++;job.op=op;return &job;}
static void hddJobFree(HddJob *j){assert(j==&job);}
int hddReadBootFlag(void){reads++;return expected;}
int hddEnableBootFlag(void){writes++;return expected;}
static int runJob(HddJob *j,const char *title,int cancelable){
    assert(j==&job&&title&&*title&&!cancelable);
    switch(j->op){
''' + worker + r'''
    }
    return 1;
}
static void dashModalMessage(int icon,const char *title,const char *body,const char *detail){
    assert(!detail&&body&&*body);
    if(expected==HDD_BOOT_ALREADY_ENABLED) assert(icon==DI_CHECK&&!strcmp(title,"Already enabled!"));
    else if(expected==HDD_BOOT_ENABLED) assert(icon==DI_CHECK&&!strcmp(title,"HDD boot is enabled"));
    else assert(icon==DI_WARNING&&strstr(title,"could not be verified")&&strstr(body,"-5"));
    notices++;
}
'''
manual += job_source[job_source.index("static int runBootFlagJob("):job_source.index("static void partitionMenu(")]
manual += r'''
int main(void){
    int result;
    idle=0;actEnableBootFlag();assert(!jobs&&!notices&&!modules);
    assert(!runBootFlagJob(HOP_READ_BOOT_FLAG,&result));
    idle=1;
    for (int state=-1; state<=1; state++) {
        expected=state;
        assert(runBootFlagJob(HOP_READ_BOOT_FLAG,&result)&&result==state);
    }
    assert(reads==3&&!writes&&!notices);
    expected=HDD_BOOT_ALREADY_ENABLED;actEnableBootFlag();
    expected=HDD_BOOT_ENABLED;actEnableBootFlag();
    expected=-EIO;actEnableBootFlag();
    assert(jobs==6&&notices==3&&modules==6&&writes==3);
    puts("PASS: __mbr reads the boot flag without writing, disables the row when enabled and reports manual results");
}
'''

with tempfile.TemporaryDirectory(prefix="hdd-installer-boot-") as temp:
    p = Path(temp)
    (p / "kernel.h").write_text("#include <stdint.h>\ntypedef uint32_t u32;\n")
    (p / "delaythread.h").write_text("int DelayThread(int);\n")
    (p / "libcdvd.h").write_text("""#include <stdint.h>
int sceCdOpenConfig(int, int, int, uint32_t *);
int sceCdReadConfig(void *, uint32_t *);
int sceCdWriteConfig(const void *, uint32_t *);
int sceCdCloseConfig(uint32_t *);
""")
    for name, body, sources in [("boot", code, [root / "src/hdd_install_boot.c"]), ("job", job_code, []), ("manual", manual, [])]:
        (p / f"{name}.c").write_text(body)
        subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(p),
                        "-I", str(root / "include"), str(p / f"{name}.c"),
                        *map(str, sources), "-o", str(p / name)], check=True)
        subprocess.run([str(p / name)], check=True, timeout=10)
