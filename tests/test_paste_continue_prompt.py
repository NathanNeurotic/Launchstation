"""The paste's "Continue transfer?" prompt must not repeat while a button is held (UDPFS report)."""
from pathlib import Path
import shutil, subprocess, tempfile

src = (Path(__file__).resolve().parents[1] / "src/filer_copy.c").read_text()
a = src.index("static int copyAskedToStop(")
end = src.index("{", a) + 1
depth = 1
while depth:
    depth += (src[end] == "{") - (src[end] == "}")
    end += 1
func = src[a:end]

q = src.index("ynDialog(LNG(Continue_transfer))")
assert "copyAskedToStop(&pad_armed)" in src[q - 200:q], "the prompt must be gated by the fresh-press check"
assert "readpad()" not in src[q - 200:q], "never the auto-repeating readpad()"
assert "copyAskedToStop(&pad_armed)" in src

code = r'''
#include <assert.h>
#include <stdio.h>
typedef unsigned int u32;
u32 paddata, new_pad;
static int fresh_report;   /* what readpad_noRepeat reports this poll */
static int readpad_noRepeat(void) { return fresh_report; }
#define PAD_CROSS 0x4000
#define PAD_L3_H1 0x10000
''' + func + r'''
/* One poll: held = buttons down now, fresh = bits newly pressed (0 for none). */
static int poll(int *armed, u32 held, u32 fresh) {
    paddata = held; new_pad = fresh; fresh_report = fresh != 0;
    return copyAskedToStop(armed);
}
int main(void) {
    int armed = 0, asked = 0, i;
    /* X from the paste confirm is still held when the copy starts: never asks, however long it is held */
    for (i = 0; i < 500; i++) asked += poll(&armed, PAD_CROSS, 0);
    assert(asked == 0);
    /* the driver's auto-repeat reports the held X as "new" again and again: still ignored */
    for (i = 0; i < 500; i++) asked += poll(&armed, PAD_CROSS, PAD_CROSS);
    assert(asked == 0 && armed == 0);
    /* released, then one real press asks exactly once, however long it stays held afterwards */
    assert(poll(&armed, 0, 0) == 0 && armed == 1);
    assert(poll(&armed, PAD_CROSS, PAD_CROSS) == 1);
    for (i = 0; i < 500; i++) asked += poll(&armed, PAD_CROSS, PAD_CROSS);
    assert(asked == 0);
    /* mashing X (press, release, press...) asks once per press, not per chunk */
    for (i = 0; i < 3; i++) {
        assert(poll(&armed, 0, 0) == 0);
        assert(poll(&armed, PAD_CROSS, PAD_CROSS) == 1);
        assert(poll(&armed, PAD_CROSS, 0) == 0);
    }
    /* stick pseudo-buttons (drift) never ask */
    assert(poll(&armed, 0, 0) == 0);
    for (i = 0; i < 50; i++) assert(poll(&armed, PAD_L3_H1, PAD_L3_H1) == 0);
    puts("ok");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    d = Path(d); (d / "t.c").write_text(code)
    subprocess.run([shutil.which("cc") or shutil.which("gcc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror", str(d / "t.c"), "-o", str(d / "t")], check=True)
    subprocess.run([str(d / "t")], check=True, timeout=10)
print("PASS: paste asks 'Continue transfer?' once per fresh press, never for a held or auto-repeating button")
