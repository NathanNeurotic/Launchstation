"""Execute the MMCE presence-first root probe with deterministic devctl replies."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / "src/dash_devices.c").read_text()
a = src.index("static int probeMmceRoot(")
b = src.index("\n}\n", a) + 3
body = src[a:b]
c = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MMCE_CMD_PING 1
static int replies[2], ping_calls, root_calls, frames, active=1, root_ok=1;
static const char *expected;
static int dashLoadingActive(void) { return active; }
static void dashLoadingFrame(const char *s,int done,int total) {
  assert(s && done==0 && total==0); frames++;
}
static int fileXioDevctl(const char *path,int cmd,void *arg,int len,void *out,int size) {
  assert(!strcmp(path,expected) && cmd==MMCE_CMD_PING && !arg && !len && !out && !size);
  assert(ping_calls<2); return replies[ping_calls++];
}
static int probeRoot(const char *path,int wait) {
  assert(!strcmp(path,expected) && wait==0 && ping_calls>0 && replies[ping_calls-1]>=0);
  root_calls++; return root_ok;
}
""" + body + r"""
static void reset(void) { ping_calls=root_calls=frames=0; replies[0]=replies[1]=-1; root_ok=1; }
int main(void) {
 for(int unit=0;unit<2;unit++) {
  expected=unit?"mmce1:/":"mmce0:/";
  reset(); assert(probeMmceRoot(expected)==0 && ping_calls==2 && root_calls==0 && frames==2);
  for(int success=0;success<2;success++) {
   reset(); replies[success]=0x010100;
   assert(probeMmceRoot(expected)==1 && ping_calls==success+1 && root_calls==1);
  }
  reset(); replies[0]=0; root_ok=0;
  assert(probeMmceRoot(expected)==0 && root_calls==1);
  reset(); assert(probeMmceRoot(expected)==0);
  reset(); replies[0]=0x010100;
  assert(probeMmceRoot(expected)==1);
  reset(); active=0; replies[0]=0x010100;
  assert(probeMmceRoot(expected)==1 && frames==0); active=1;
 }
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="mmce-presence-") as folder:
    p=Path(folder)
    (p / "test.c").write_text(c)
    exe=p / "test.exe"
    subprocess.run([shutil.which("gcc") or shutil.which("cc"),"-std=c11","-Wall","-Wextra","-Werror",str(p / "test.c"),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=10)
print("PASS: MMCE absence, valid cards, second-response, failed mount and retry")
