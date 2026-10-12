"""Google Drive link screen: QR code + automatic polling (host checks).

Encodes Google's device URL with the vendored Nayuki qrcodegen exactly as the
dashboard does and checks the symbol's structure, then pins the polling rules
(RFC 8628 interval/slow_down, expiry, Back cancels, bounded network retries).
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
tools = (root / "src/dash_tools.c").read_text(encoding="utf-8")
google = (root / "src/cloud_google.c").read_text(encoding="utf-8")
yaml = (root / "ps2.yaml").read_text(encoding="utf-8")
vendored = (root / "src/qrcodegen.c").read_text(encoding="utf-8")

assert "src/qrcodegen.c" in yaml
assert "Copyright (c) Project Nayuki. (MIT License)" in vendored

harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "qrcodegen.h"
static int finder(const uint8_t *qr, int ox, int oy) {
    int x, y;
    for (y = 0; y < 7; y++)
        for (x = 0; x < 7; x++) {
            int ring = (x == 0 || x == 6 || y == 0 || y == 6);
            int core = (x >= 2 && x <= 4 && y >= 2 && y <= 4);
            if (qrcodegen_getModule(qr, ox + x, oy + y) != (ring || core)) return 0;
        }
    return 1;
}
int main(void) {
    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(6)], tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
    int size;
    assert(qrcodegen_encodeText("https://www.google.com/device", tmp, qr, qrcodegen_Ecc_MEDIUM,
                                qrcodegen_VERSION_MIN, 6, qrcodegen_Mask_AUTO, true));
    size = qrcodegen_getSize(qr);
    assert(size == 29); /* version 3: fits the 150 px box at 4 px per module */
    assert(finder(qr, 0, 0) && finder(qr, size - 7, 0) && finder(qr, 0, size - 7));
    assert((int)(150 / (size + 8)) >= 4);
    printf("PASS: device URL encodes as a %dx%d QR (4 px modules)\n", size, size);
    return 0;
}
'''
compiler = shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("Host gcc or clang required")
with tempfile.TemporaryDirectory(prefix="wle-qr-") as tmp:
    c = Path(tmp) / "qr.c"
    exe = Path(tmp) / "qr.exe"
    c.write_text(harness, encoding="utf-8")
    subprocess.run([compiler, "-std=c99", "-I", str(root / "include"), str(c),
                    str(root / "src/qrcodegen.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

link = tools[tools.index("static void cloudLink(void)"):tools.index("static void cloudSyncNow(void)")]
assert "qrcodegen_encodeText(code.verification_url" in link
assert "cloudDrawQr(qr" in link
assert "next_poll = Timer() + (u64)interval * 1000;" in link
assert "if (r == 2)\n\t\t\tinterval += 5;" in link
assert "now >= deadline" in link and "CLOUD_LINK_EXPIRED" in link
assert "in.pressed & DASH_IN_BACK" in link
assert '!strncmp(error, "Google OAuth:", 13) || ++failures >= CLOUD_LINK_NET_RETRIES' in link
assert "dashModalConfirm" not in link  # no more manual "Check" prompts
assert 'return strcmp(oauth_error, "slow_down") ? 1 : 2;' in google
print("PASS: link screen polls automatically, honours slow_down/expiry and Back cancels")
