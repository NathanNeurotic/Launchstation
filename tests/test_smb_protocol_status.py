"""Network (smb:/) names the protocol family or families actually connected."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_devices.c").read_text()


def function(text, signature):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
enum { SMB_PATH_V1 = 1, SMB_PATH_V2 = 2 };
enum { DASH_DEV_PROBE = 1, DASH_DEV_READY = 2, DASH_DEV_ABSENT = 3 };
typedef struct { int state; char status[64]; } DashDevice;
static int connected[4];
static int smbSharesConnectedCount(int version) { return connected[version]; }
static void dashStrCopy(char *out, const char *in, int size) { snprintf(out, (size_t)size, "%s", in); }
'''
code += function(source, "static const char *smbProtocolLabel(void)")
code += function(source, "static void smbProtocolStatus(DashDevice *d)")
code += r'''
static void expect(int v1, int v2, const char *label) {
    connected[1] = v1; connected[2] = v2;
    assert(!strcmp(smbProtocolLabel(), label));
}
int main(void) {
    expect(0, 0, "No SMB Enabled");
    expect(1, 0, "SMB1 Enabled");
    expect(0, 1, "SMB2/3 Enabled");
    expect(1, 1, "SMB1 + SMB2/3 Enabled");
    {
        DashDevice row;
        row.state = DASH_DEV_PROBE;
        strcpy(row.status, "Not connected");
        smbProtocolStatus(&row);
        assert(!strcmp(row.status, "SMB1 + SMB2/3 Enabled"));
        row.state = DASH_DEV_ABSENT;
        strcpy(row.status, "Server did not answer");
        smbProtocolStatus(&row);
        assert(!strcmp(row.status, "Server did not answer"));
    }
    puts("PASS: Network row reports active SMB protocol families");
    return 0;
}
'''
compiler = shutil.which("cc") or shutil.which("gcc")
assert compiler, "a host C compiler is needed"
with tempfile.TemporaryDirectory(prefix="smb-protocol-status-") as tmp:
    c = Path(tmp) / "status.c"
    exe = Path(tmp) / "status"
    c.write_text(code)
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
