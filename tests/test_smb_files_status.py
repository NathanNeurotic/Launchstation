"""Exercise the real Files/Devices SMB failure modal with mocked device state."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_files.c").read_text()
start = source.index("static void deviceNotFound(const DashDevice *d)\n{")
end = start + len("static void deviceNotFound(const DashDevice *d)\n{")
depth = 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
function = source[start:end] + "\n"

code = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
enum { DI_USB=1, DI_NETWORK=2, DI_SHARE=3, DI_WARNING=4 };
enum { SMB_PATH_NONE=0, SMB_PATH_V1=1, SMB_PATH_V2=2 };
/* Mirror the real DashDevice field sizes: avoid spurious host warnings. */
typedef struct {
    int icon, state;
    char name[32], mount[24], browse[24], status[48], detail[48];
    float fill;
} DashDevice;
static int loaded, status_calls, last_version;
static const char *error_message = "";
static char last_title[64], last_body[160];
static int smbPathKind(const char *p) {
    return !strncmp(p,"smb1:",5) ? 1 : !strncmp(p,"smb:",4) ? 2 : 0;
}
static int isSmbDriverLoaded(int v) { assert(v==1||v==2); return loaded; }
static const char *smbLoadError(int v) { assert(v==1||v==2); return error_message; }
static void smbSharesStatus(int v, char *dst, int n) {
    status_calls++; last_version=v;
    snprintf(dst,(size_t)n,"Games (line 4): Connection refused");
}
static int isPcCardCfPath(const char *p) { (void)p; return 0; }
static void dashModalMessage(int icon, const char *title, const char *body, const char *extra) {
    assert(icon==DI_WARNING && extra==NULL);
    snprintf(last_title,sizeof(last_title),"%s",title);
    snprintf(last_body,sizeof(last_body),"%s",body);
}
""" + function + r"""
int main(void) {
    DashDevice d={0};
    d.icon=DI_SHARE; snprintf(d.name,sizeof(d.name),"Network (SMB)");
    snprintf(d.mount,sizeof(d.mount),"smb:");
    error_message="The network stack could not be started.";
    deviceNotFound(&d);
    assert(!strcmp(last_title,"Device not found"));
    assert(!strcmp(last_body,error_message) && status_calls==0);
    loaded=1; error_message="";
    deviceNotFound(&d);
    assert(!strcmp(last_body,"Games (line 4): Connection refused") &&
           status_calls==1 && last_version==2);
    snprintf(d.mount,sizeof(d.mount),"smb1:");
    deviceNotFound(&d);
    assert(status_calls==2 && last_version==1);
    snprintf(d.mount,sizeof(d.mount),"udpfs:");
    deviceNotFound(&d);
    assert(strstr(last_body,"UDPFS")!=NULL && status_calls==2);
    d.icon=DI_USB;
    deviceNotFound(&d);
    assert(strstr(last_body,"USB storage device")!=NULL);
    puts("PASS: Files/Devices reports SMB-specific load and share failures");
    return 0;
}
"""
compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="smb-files-status-") as tmp:
    c_path, binary = Path(tmp) / "test.c", Path(tmp) / "test"
    c_path.write_text(code, encoding="utf-8")
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-DSMB",
                    str(c_path), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
