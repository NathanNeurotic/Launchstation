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
typedef struct { const char *label, *value; } DashTableRow;
typedef struct { HddFormatPlan format_plan; HddInstall *installer; int format_started, ret, op; } HddJob;
#define ETH
#define DI_WARNING 1
#define DI_INFO 20
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
    assert((strstr(title, "Additional APA") || strstr(title, "Reserved APA")) && min == (install_flags & HDD_INSTALL_PSBBN ? 0 : install_flags & HDD_INSTALL_APPS ? 256 : 0) && max % 128 == 0 && initial <= max);
    return reserve;
}
static void dashModalMessage(int icon, const char *title, const char *body, const char *detail) {
    assert(title && body);
    if (icon == DI_INFO) return;
    assert(icon == DI_WARNING); if(detail) cache_kept++; messages++;
}
static int dashModalConfirm(const char *title, const char *body, const char *button, int danger) {
    assert(title && body && button && danger == 1);
    confirms++;
    assert(confirms == 2 && strstr(body,"Every partition") && !strcmp(button,"Format"));
    return confirms != cancel_confirm;
}
static const char *reviewValue(const DashTableRow *rows,int count,const char *label) {
    for(int i=0;i<count;i++){assert(rows[i].label&&rows[i].value);if(!strcmp(rows[i].label,label))return rows[i].value;}
    assert(0);return NULL;
}
static int dashModalConfirmTable(const char *title,const DashTableRow *rows,int count,const char *button,int danger) {
    assert(!strcmp(title,"Review HDD format") && count>2 && count<=18 && !strcmp(button,"Continue") && danger==1);
    assert(!confirms);confirms++;
    assert(!strcmp(reviewValue(rows,count,"Format"),selected==0?"APA":selected==1?"exFAT":"APA Jail (APA + exFAT)"));
    if(selected!=HDD_FORMAT_APA)assert(!strcmp(reviewValue(rows,count,"exFAT label / cluster"),"PS2 / 32 KiB"));
    if(selected==HDD_FORMAT_APA_JAIL)assert(!strcmp(reviewValue(rows,count,"Recovery"),"32 MiB"));
    if(install_flags&HDD_INSTALL_PSBBN){
        assert(!strcmp(reviewValue(rows,count,"PSBBN language"),"French"));
        assert(!strcmp(reviewValue(rows,count,"Music"),"1024 MiB"));
        assert(!strcmp(reviewValue(rows,count,"Console HDD boot"),"Enable"));
    }
    return confirms!=cancel_confirm;
}
void dashHddInstallDebugBegin(HddInstall *in,const HddFormatPlan *p) { (void)in;(void)p; }
void dashHddInstallDebugEnd(HddInstall *in,int ret) { (void)in;(void)ret; }
void hddInstallLog(const char *format,...) { (void)format; }
void hddInstallPhase(unsigned phase) { assert(phase==HDD_INSTALL_PHASE_FORMAT||phase==HDD_INSTALL_PHASE_DOWNLOAD); }
int dashHddInstallChoose(HddInstall *in, int jail) { in->jail = jail; in->flags = install_flags; return 1; }
int dashHddInstallConfigure(HddInstall *in,unsigned max) { assert(max>=2048); in->music_mb=in->contents_mb=1024; return 1; }
int dashHddInstallPrepare(HddInstall *in) { assert(!confirms && !jobs); strcpy(in->language.name,"French"); return 1; }
int dashHddInstallPrepareStorage(HddInstall *in) { assert(confirms == 2 && reset == 1 && jobs == 1); strcpy(in->stage, in->jail ? "ata0:/PSBBN_DP_TEMP" : "pfs2:/PSBBN_DP_TEMP"); return 0; }
void hddInstallRelease(HddInstall *in) { (void)in; }
int hddInstallCleanup(HddInstall *in) { in->stage[0] = 0; return 0; }
static HddJob *jobNew(int operation) { HddJob *j = calloc(1, sizeof(HddJob)); j->op = operation; return j; }
static void hddJobFree(HddJob *j) { freed++; free(j); }
int hddFormatLoad(void) { assert(confirms == 2); loaded++; return load_error; }
static void dashSoundShutdown(void) { assert(loaded); stopped++; }
static int runJob(HddJob *j, const char *title, int cancelable) {
    assert(title && cancelable == 0);
    if (j->op == HOP_DOWNLOAD_INSTALLER) { assert(confirms == 2 && stopped && reset == 1 && jobs == 1); assert(strstr(j->installer->stage, "/PSBBN_DP_TEMP")); downloads++; j->ret = download_error; return 1; }
    if (j->op == HOP_INSTALL_SOFTWARE) { assert(reset && confirms == 2); installed++; j->ret = install_error; j->format_started = 1; return 1; }
    assert(stopped); jobs++;
    actual = j->format_plan;
    if (queue_error) { hddJobFree(j); return 0; }
    j->format_started = started; j->ret = job_error;
    return 1;
}
static void rebootIopAndReloadCoreStackSilent(void) { assert(jobs && started); reset++; }
static void loadHddModules(void) { assert(reset || install_flags); reloaded++; }
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
    assert(downloads == 1 && confirms == 2 && jobs == 1 && stopped && reset == 1 && !installed && !toasts && cache_kept == 1 && messages == 1);
    clear(); total = 100 * 1024 * 2048; reserve = 8192; install_flags = 7; install_error = -EIO; actFormat();
    assert(downloads == 1 && installed == 1 && jobs == 1 && reset == 2 && !toasts && cache_kept == 1 && messages == 1);
    clear(); selected = HDD_FORMAT_APA; install_flags = 3; actFormat();
    assert(downloads == 1 && installed == 1 && confirms == 2 && toasts == 1 && !messages);
    clear(); total = 100 * 1024 * 2048; reserve = 8192; install_flags = 7; cancel_confirm = 2; actFormat();
    assert(confirms == 2 && !downloads && !jobs);
    puts("PASS: format-before-download, internal staging, failed-install cache retention, three format choices, PSX/FTP/capacity guards, two confirmations, cancellation, queue/load failures and storage reload after success/failure");
}
'''
compiler = shutil.which("gcc") or shutil.which("cc")
assert compiler
with tempfile.TemporaryDirectory(prefix="hdd-format-ui-") as temp:
    cfile, executable = Path(temp) / "check.c", Path(temp) / "check"
    cfile.write_text(code)
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(root / "include"),
                    str(cfile), str(root / "src/hdd_format.c"), str(root / "src/hdd_install_plan.c"), str(root / "src/hdd_format_upcase.c"),
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=10)

# A VMC unmount failure must survive filerUnmountVmc() and stop the real
# format job before the raw formatter is entered.
browser = (root / "src/filer_browser.c").read_text()
vmc_unmount = browser[browser.index("int filerUnmountVmc(int slot)\n{"):browser.index("/* Writes icon.sys", browser.index("int filerUnmountVmc(int slot)\n{"))]
job_start = source.index("static void hddJobRun(void *arg)\n{")
job = source[job_start:source.index("static void hddJobFree(HddJob *j)\n{", job_start)]
job_types = source[source.index("enum {\n\tHOP_SCAN"):source.index("} HddJob;") + len("} HddJob;")]
vmc_code = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hdd_format.h"
#include "hdd_install.h"
#include "hdd_header_injector.h"
#define MAX_PATH 1024
#define MAX_PART_NAME 256
#define MAX_PARTITIONS 128
#define MOUNT_LIMIT 16
#define FS_GROUP_APPLICATION 1
typedef uint32_t u32;
typedef struct { int unused; } HddPart;
static int vmcMounted[2], vmc_PartyIndex[2], Party_vmcIndex[MOUNT_LIMIT];
static char mountedParty[MOUNT_LIMIT][MAX_PATH];
static int unmount_result, format_calls;
static int fileXioUmount(const char *dev) { assert(!strcmp(dev, "vmc0:")); return unmount_result; }
static void filerSizeCacheInvalidate(const char *path) { (void)path; }
static void releaseMounts(void) {}
static void unmountParty(int i) { (void)i; }
static void scanRun(void *j) { (void)j; }
static void pfsRun(void *j) { (void)j; }
static int hddMakeFilesystem(int size, const char *name, int group) { (void)size; (void)name; (void)group; return 0; }
static int fileXioRemove(const char *path) { (void)path; return 0; }
static int fileXioRename(const char *a, const char *b) { (void)a; (void)b; return 0; }
static int HdlRenameGame(char *a, char *b) { (void)a; (void)b; return 0; }
int hddFormatDisk(const HddFormatPlan *plan, int *started) { (void)plan; (void)started; format_calls++; return 0; }
int InjectHddPartitionHeaderFromSource(const char *a, const char *b, char *dir, size_t size, int *copied, int *error) {
    (void)a; (void)b; (void)dir; (void)size; (void)copied; (void)error; return 0;
}
int HddHeaderListSourcePartitions(const char *source, HddHeaderSourcePartition *parts, int max, int *invalid) {
    (void)source; (void)parts; (void)max; (void)invalid; return 0;
}
void hddInstallLog(const char *text, ...) { (void)text; }
void hddInstallLogFlush(HddInstall *in) { (void)in; }
int hddInstallMountStage(HddInstall *in) { (void)in; return 0; }
int hddInstallUnmountStage(HddInstall *in) { (void)in; return 0; }
int hddInstallCreateStage(HddInstall *in) { (void)in; return 0; }
int hddInstallCleanup(HddInstall *in) { (void)in; return 0; }
int hddInstallEnableBoot(HddInstall *in) { (void)in; return 0; }
int hddInstallDownload(HddInstall *in) { (void)in; return 0; }
int hddInstallApply(HddInstall *in) { (void)in; return 0; }
int hddInstallPsbbnPayload(void *in, const HddFormatIo *io) { (void)in; (void)io; return 0; }
int hddInstallFinish(const HddFormatPlan *plan, const char *mbr, const char *hash, int *started,
                     int (*populate)(void *, const HddFormatIo *), void *context) {
    (void)plan; (void)mbr; (void)hash; (void)started; (void)populate; (void)context; return 0;
}
'''
vmc_code += job_types + "\n" + vmc_unmount + "\n" + job + r'''
int main(void) {
    HddJob j = {0};
    j.op = HOP_FORMAT;
    vmcMounted[0] = 1; vmc_PartyIndex[0] = 1; Party_vmcIndex[1] = 0;
    unmount_result = -EIO;
    hddJobRun(&j);
    assert(j.ret == -EIO && format_calls == 0);
    assert(vmcMounted[0] == 1 && vmc_PartyIndex[0] == 1 && Party_vmcIndex[1] == 0);
    unmount_result = 0;
    hddJobRun(&j);
    assert(j.ret == 0 && format_calls == 1);
    assert(vmcMounted[0] == 0 && vmc_PartyIndex[0] == -1 && Party_vmcIndex[1] == -1);
    puts("PASS: failed VMC unmount retains state and prevents raw formatting");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="hdd-format-vmc-") as temp:
    cfile, executable = Path(temp) / "check.c", Path(temp) / "check"
    cfile.write_text(vmc_code)
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-format-truncation",
                    "-I", str(root / "include"), str(cfile), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=10)

# Every allocation path in the common EE library must check the predicted
# destination before invoking the existing driver's allocation command.
library = (root / "ee_libs/hdd/libhdd.c").read_text()
assert library.count("hddCheckAllocation((u32)partSize * 2048)") == 3
assert "ee_libs/hdd/libhdd.c" in (root / "ps2.yaml").read_text()
assert source.index("filerUnmountVmc(i)", source.index("case HOP_FORMAT:")) < source.index("hddFormatDisk(&j->format_plan")
print("PASS: shared create/expand guards in both builds; VMC unmount precedes formatting")
