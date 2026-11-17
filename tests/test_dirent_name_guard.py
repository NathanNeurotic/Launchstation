#!/usr/bin/env python3
"""Every directory reader that copies a driver's entry name checks that the
name is terminated first, so a driver that fills the whole 256-byte name
cannot overrun FILEINFO.name (saildot4k's c627ecc hardening)."""
import re
from pathlib import Path

src = (Path(__file__).resolve().parents[1] / "src/filer.c").read_text(encoding="utf-8")
loops = list(re.finditer(r"while \(\(?(?:\w+ = )?fileXioDread\(\w+, &(\w+)\)\)? > 0\) \{\n(.*?)\n\t\}\n", src, re.S))
copying = [m for m in loops if "strcpy(info[" in m.group(2) and m.group(1) + ".name" in m.group(2)]
assert len(copying) >= 7, len(copying)
for m in copying:
    var, body = m.group(1), m.group(2)
    guard = "if (!memchr(%s.name, 0, sizeof(%s.name)))" % (var, var)
    assert guard in body, var
    assert body.index(guard) < body.index("strcpy(info["), var
print("PASS: %d directory readers reject unterminated entry names before copying them" % len(copying))
