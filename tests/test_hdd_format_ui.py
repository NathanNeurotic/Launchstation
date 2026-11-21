"""Exercise the actual HDD Manager format action without touching a drive."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_hdd.c").read_text()
start = source.index("static void actFormat(void)")
end = source.index("\nstatic void actStatus(void)", start)
action = source[start:end]

code = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hdd_format.h"
#include "hdd_install.h"
typedef uint32_t u32;
typedef struct { HddFormatPlan format_plan; HddInstall *installer; int format_started, ret, op; } HddJob;
#define ETH
#define DI_WARNING 1
#define DASH_TOAST_OK 2
#define HOP_FORMAT 3
#define HDIOC_TOTALSECTOR 4
#define HOP_DOWNLOAD_INSTALLER 5
#define HOP_INSTALL_SOFTWARE 6
#define HDIOC_MAXSECTOR 7
static int console_is_PSX, ftp, selected, reserve, total;
static int confirms, cancel_confirm, loaded, load_error, jobs, started, job_error, queue_error;
static unsigned install_flags;
static int downloads, installed, download_error, install_error, cache_kept;
static int messages, toasts, reset, reloaded, maintained, bgm, stopped, changed, freed;
static int sel, top;
static float top_anim;
static HddFormatPlan actual;
static int isFtpServerRunning(void) { return ftp; }
static int isSmbServerRunning(void) { return 0; }
static const char *localizeDashboardText(const char *text) { return text; }
static int fileXioDevctl(const char *dev, int op, void *a, int alen, void *b, int blen) {
    assert(!strcmp(dev, "hdd0:") && (op == HDIOC_TOTALSECTOR || op == HDIOC_MAXSECTOR) && !a && !alen && !b && !blen);
    return op == HDIOC_TOTALSECTOR ? total : 2048 * 2048;
}
static int dashModalChoice(const char *title, const char *const *options, int count, int current) {
    assert(!strcmp(title, "Format HDD") && count == 3 && current == 0);
    assert(!strcmp(options[0], "APA") && !strcmp(options[1], "exFAT") && strstr(options[2], "APA Jail"));
    return selected;
}
static int pickSize(const char *title, int min, int max, int initial) {
    assert(strstr(title, "Additional APA") && min == (install_flags & HDD_INSTALL_PSBBN ? 6400 : install_flags & HDD_INSTALL_APPS ? 256 : 0) && max % 128 == 0 && initial <= max);
    return reserve;
}
static void dashModalMessage(int icon, const char *title, const char *body, const char *detail) {
    assert(icon == DI_WARNING && title && body); if(detail) cache_kept++; messages++;
}
static int dashModalConfirm(const char *title, const char *body, const char *button, int danger) {
    assert(title && body && button && danger == 1);
    confirms++;
    if (confirms == 1 && selected == HDD_FORMAT_EXFAT)
        assert(strstr(body, "PS2") && strstr(body, "32 KiB") && strstr(body, "one MBR"));
    if (confirms == 1 && selected == HDD_FORMAT_APA_JAIL)
        assert(strstr(body, "PS2") && strstr(body, "Recovery: 32 MiB") && strstr(body, "additional"));
    return confirms != cancel_confirm;
}
int dashHddInstallChoose(HddInstall *in, int jail) { in->jail = jail; in->flags = install_flags; return 1; }
int dashHddInstallPrepare(HddInstall *in) { assert(!confirms); strcpy(in->stage, "usb0:/cache"); return 1; }
int hddInstallCleanup(HddInstall *in) { in->stage[0] = 0; return 0; }
static int prepareTransferDeviceStacks(const char *a, const char *b) { assert(a && b); return 0; }
static HddJob *jobNew(int operation) { HddJob *j = calloc(1, sizeof(HddJob)); j->op = operation; return j; }
static void hddJobFree(HddJob *j) { freed++; free(j); }
int hddFormatLoad(void) { assert(confirms == 2); loaded++; return load_error; }
static void dashSoundShutdown(void) { assert(loaded); stopped++; }
static int runJob(HddJob *j, const char *title, int cancelable) {
    assert(title && cancelable == 0);
    if (j->op == HOP_DOWNLOAD_INSTALLER) { assert(!confirms && !stopped); downloads++; j->ret = download_error; return 1; }
    if (j->op == HOP_INSTALL_SOFTWARE) { assert(reset && confirms == 2); installed++; j->ret = install_error; j->format_started = 1; return 1; }
    assert(stopped); jobs++;
    actual = j->format_plan;
    if (queue_error) { hddJobFree(j); return 0; }
    j->format_started = started; j->ret = job_error;
    return 1;
}
static void rebootIopAndReloadCoreStackSilent(void) { assert(jobs && started); reset++; }
static void loadHddModules(void) { assert(reset || downloads); reloaded++; }
static void dashSoundMaintain(void) { maintained++; }
static void dashBgmStart(void) { bgm++; }
static void dashToast(int icon, const char *text) { assert(icon == DASH_TOAST_OK && text); toasts++; }
static void afterChange(void) { changed++; }
int hddFormatRecovery(const HddFormatPlan *p, const HddFormatIo *io) { (void)p; (void)io; return 0; }
'''
code += action
code += r'''
static void clear(void) {
    install_flags = 0; downloads = installed = download_error = install_error = cache_kept = 0;
    console_is_PSX = ftp = 0; selected = HDD_FORMAT_APA_JAIL; reserve = 512; total = 8 * 1024 * 2048;
    confirms = cancel_confirm = loaded = load_error = jobs = job_error = queue_error = 0; started = 1;
    messages = toasts = reset = reloaded = maintained = bgm = stopped = changed = freed = 0;
    sel = top = 2; top_anim = 2;
}
int main(void) {
    clear(); console_is_PSX = 1; actFormat(); assert(messages == 1 && !confirms && !loaded && !jobs);
    clear(); ftp = 1; actFormat(); assert(messages == 1 && !confirms && !loaded && !jobs);
    for (int i = 0; i < 3; i++) {
        clear(); total = i == 0 ? -EIO : i == 1 ? 0 : -1; actFormat();
        assert(messages == 1 && !confirms && !loaded && !jobs);
    }
    clear(); selected = -1; actFormat(); assert(!confirms && !loaded && !jobs);
    clear(); reserve = -1; actFormat(); assert(!confirms && !loaded && !jobs);
    for (int i = 1; i <= 2; i++) {
        clear(); cancel_confirm = i; actFormat(); assert(confirms == i && !loaded && !jobs && !stopped);
    }
    clear(); load_error = -EIO; actFormat(); assert(confirms == 2 && loaded == 1 && !jobs && !stopped && freed == 1);
    clear(); queue_error = 1; actFormat(); assert(jobs == 1 && freed == 1 && !reset && maintained && bgm && !changed);
    for (int kind = HDD_FORMAT_APA; kind <= HDD_FORMAT_APA_JAIL; kind++) {
        clear(); selected = kind; actFormat();
        assert(confirms == 2 && loaded == 1 && jobs == 1 && freed == 1);
        assert(reset == 1 && reloaded == 1 && maintained == 1 && bgm == 1 && toasts == 1 && !messages);
        assert(actual.kind == (unsigned)kind && actual.reserve_mb == (kind == HDD_FORMAT_APA_JAIL ? 512 : 0));
        assert(sel == 0 && top == 0 && top_anim == -1 && changed == 1);
    }
    clear(); job_error = -EIO; actFormat();
    assert(messages == 1 && !toasts && reset == 1 && reloaded == 1 && changed == 1);
    clear(); job_error = -EBUSY; started = 0; actFormat();
    assert(messages == 1 && !toasts && !reset && !reloaded && changed == 1 && maintained && bgm);
    clear(); total = 100 * 1024 * 2048; reserve = 8192; install_flags = 7; actFormat();
    assert(downloads == 1 && installed == 1 && confirms == 2 && jobs == 1 && reset == 2 && !messages && toasts == 1);
    clear(); total = 100 * 1024 * 2048; reserve = 8192; install_flags = 7; download_error = -EIO; actFormat();
    assert(downloads == 1 && !confirms && !jobs && !stopped && messages == 1);
    clear(); total = 100 * 1024 * 2048; reserve = 8192; install_flags = 7; install_error = -EIO; actFormat();
    assert(downloads == 1 && installed == 1 && jobs == 1 && reset == 2 && !toasts && cache_kept == 1 && messages == 1);
    puts("PASS: pre-erase download, failed-install cache retention, three format choices, PSX/FTP/capacity guards, two confirmations, cancellation, queue/load failures and storage reload after success/failure");
}
'''
compiler = shutil.which("gcc") or shutil.which("cc")
assert compiler
with tempfile.TemporaryDirectory(prefix="hdd-format-ui-") as temp:
    cfile, executable = Path(temp) / "check.c", Path(temp) / "check"
    cfile.write_text(code)
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(root / "include"),
                    str(cfile), str(root / "src/hdd_format.c"), str(root / "src/hdd_format_upcase.c"),
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=10)

# Every allocation path in the common EE library must check the predicted
# destination before invoking the existing driver's allocation command.
library = (root / "ee_libs/hdd/libhdd.c").read_text()
assert library.count("hddCheckAllocation((u32)partSize * 2048)") == 3
assert "ee_libs/hdd/libhdd.c" in (root / "ps2.yaml").read_text()
assert source.index("filerUnmountVmc(i)", source.index("case HOP_FORMAT:")) < source.index("hddFormatDisk(&j->format_plan")
print("PASS: shared create/expand guards in both builds; VMC unmount precedes formatting")
