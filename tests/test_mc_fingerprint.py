"""Card fingerprint (index-only change detection) and the SHA-256 it uses.

The fingerprint lets the MMCE backup skip a card without reading all of it.
It must change for every save change made through the filesystem, stay the
same when only B?DATA-SYSTEM's contents change (the PS2 rewrites its history
file on every game start), agree between ECC and no-ECC images, and never
crash on damaged images.
"""
from pathlib import Path
import hashlib
import random
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).parent))
import mc_fixture as fx

root = Path(__file__).resolve().parents[1]
workflow = (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")
assert "python3 tests/test_mc_fingerprint.py" in workflow
tmp = Path(tempfile.mkdtemp())
exe = tmp / "fp"
subprocess.run(["gcc", "-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-fno-sanitize-recover=all", f"-I{root/'include'}", str(root / "tests/mc_fingerprint_host.c"),
                str(root / "src/mc_card_view.c"), str(root / "src/mc_sha256.c"), "-o", str(exe)], check=True)


def run(*args):
    out = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True, timeout=30)
    assert out.returncode == 0, (args, out.returncode, out.stderr[-600:])
    return dict(l.split("=", 1) for l in out.stdout.splitlines())


def fp(raw, name="card.bin"):
    p = tmp / name
    p.write_bytes(raw)
    d = run(p)
    assert d["open"] == "0", d
    assert d["fpr"] == "0", d
    return d["fp"]


# 1. SHA-256 against Python's hashlib, across block boundaries
rng = random.Random(20261028)
for n in list(range(0, 130)) + [1000, 4096, 65537, 1 << 20]:
    blob = bytes(rng.randrange(256) for _ in range(n)) if n < 70000 else rng.randbytes(n)
    p = tmp / "blob"
    p.write_bytes(blob)
    out = subprocess.run([str(exe), "sha", str(p)], capture_output=True, text=True, check=True)
    assert out.stdout.strip() == hashlib.sha256(blob).hexdigest(), n


def entry_offset(img, name, nth=0, ecc=False):
    """Byte offset of the directory entry called name (raw no-ECC image)."""
    assert not ecc
    key = name.encode("ascii") + b"\0"
    pos, seen = 0, 0
    while True:
        pos = img.find(key, pos)
        assert pos >= 0, name
        if (pos - 0x40) % 512 == 0 and pos >= 0x40:
            if seen == nth:
                return pos - 0x40
            seen += 1
        pos += 1


def patch(img, off, data):
    b = bytearray(img)
    b[off:off + len(data)] = data
    return bytes(b)


def card_with_system(history=b"h" * 1000):
    c = fx.Card(8, False)
    c.add_save("BADATA-SYSTEM", {"history": history, "icon.sys": fx.icon_sys(b"System", 0)})
    c.add_save("BASLUS-11111GAME", {"icon.sys": fx.icon_sys(b"Game", 0), "SAVE.DAT": b"s" * 5000})
    c.add_save("BESLES-22222", {"DATA": b"d" * 300})
    return c.finish()


# 2. stable, and the same for ECC and no-ECC images of one card
card, _ = fx.standard_card(8, False)
card_e, _ = fx.standard_card(8, True)
a = fp(card.image())
assert a == fp(card.image(), "again.bin")
assert a == fp(card_e.image(), "ecc.bin"), "ECC and no-ECC images must fingerprint alike"
c16, _ = fx.standard_card(16, False)
assert fp(c16.image(), "c16.bin") != a  # different geometry

base_card = card_with_system()
base = base_card.image()
f0 = fp(base)

# 3. every index change shows
o = entry_offset(base, "SAVE.DAT")
assert fp(patch(base, o + 4, struct.pack("<L", 4999))) != f0, "file length"
assert fp(patch(base, o + 0x18, bytes([0, 1, 2, 3, 4, 5, 0xE9, 0x07]))) != f0, "file modified time"
assert fp(patch(base, o + 0x10, struct.pack("<L", 900))) != f0, "file first cluster"
assert fp(patch(base, o + 0x40, b"SAVE.DAX")) != f0, "file rename"
assert fp(patch(base, o, struct.pack("<H", 0x0417))) != f0, "file deleted (exists bit cleared)"
o = entry_offset(base, "BESLES-22222")
assert fp(patch(base, o, struct.pack("<H", 0x0427))) != f0, "save deleted"
assert fp(patch(base, o + 0x40, b"BESLES-22223")) != f0, "save renamed"
added = fx.Card(8, False)
added.add_save("BADATA-SYSTEM", {"history": b"h" * 1000, "icon.sys": fx.icon_sys(b"System", 0)})
added.add_save("BASLUS-11111GAME", {"icon.sys": fx.icon_sys(b"Game", 0), "SAVE.DAT": b"s" * 5000})
added.add_save("BESLES-22222", {"DATA": b"d" * 300})
added.add_save("NEWSAVE", {"x": b"x"})
assert fp(added.finish().image()) != f0, "save added"
assert fp(patch(base, 0x151, b"\x53")) != f0, "superblock flags"

# 4. what the fingerprint deliberately ignores
o = entry_offset(base, "history")
same = patch(base, o + 4, struct.pack("<L", 1200))                      # history grew
same = patch(same, o + 0x18, bytes([0, 9, 9, 9, 9, 9, 0xEA, 0x07]))     # and was rewritten
same = patch(same, o + 0x10, struct.pack("<L", 2000))                   # elsewhere
o2 = entry_offset(base, "BADATA-SYSTEM")
same = patch(same, o2 + 4, struct.pack("<L", 5))                        # folder gained an entry
same = patch(same, o2 + 0x18, bytes([0, 9, 9, 9, 9, 9, 0xEA, 0x07]))
assert fp(same) == f0, "B?DATA-SYSTEM contents and times must not count as a change"
dot = entry_offset(base, ".", 0)                                       # root "." entry
assert dot == base_card.alloc_offset * 1024
assert fp(patch(base, dot + 0x18, bytes([0, 1, 1, 1, 1, 1, 0xEA, 0x07]))) == f0, "root '.' time"
# data bytes without an index change are invisible (documented limit)
sd = entry_offset(base, "SAVE.DAT")
first = struct.unpack_from("<L", base, sd + 0x10)[0]
raw_write = patch(base, (base_card.alloc_offset + first) * 1024, b"RAWPAGEWRITE")
assert fp(raw_write) == f0
# but a system folder appearing for the first time is a change
nosys = fx.Card(8, False)
nosys.add_save("BASLUS-11111GAME", {"icon.sys": fx.icon_sys(b"Game", 0), "SAVE.DAT": b"s" * 5000})
withsys = fx.Card(8, False)
withsys.add_save("BASLUS-11111GAME", {"icon.sys": fx.icon_sys(b"Game", 0), "SAVE.DAT": b"s" * 5000})
withsys.add_save("BIDATA-SYSTEM", {"history": b"h"})
assert fp(nosys.finish().image()) != fp(withsys.finish().image())
# the system folder's name is still hashed: another region letter is a change
assert fp(patch(base, entry_offset(base, "BADATA-SYSTEM") + 0x41, b"Z")) != f0
o3 = entry_offset(base, "BADATA-SYSTEM")
renamed = patch(base, o3 + 0x40, b"BADATA-SYSTEX")
assert fp(renamed) != f0

# 5. damaged images: errors, never crashes or hangs (ASan/UBSan abort the harness)
loop = fx.Card(8, False)
dcl = loop.add_save("LOOP", {"a": b"a" * 5000})
loop.finish()
loop.fat[dcl] = 0x80000000 | dcl
p = tmp / "loop.bin"
p.write_bytes(loop.image())
d = run(p)
assert d["open"] == "0" and d["fpr"] != "0", d
# a save directory that claims to contain itself recursively must stop at the depth limit
selfref = fx.Card(8, False)
dcl = selfref.add_save("SELF", {"a": b"a"})
selfref.finish()
img = selfref.image()
o = entry_offset(img, "a")
img = patch(img, o, struct.pack("<HHL", 0x8427, 0, 3))
img = patch(img, o + 0x10, struct.pack("<L", dcl))
p.write_bytes(img)
d = run(p)
assert d["fpr"] == "-5", d

fuzz_base = bytearray(base)
span = base_card.alloc_offset * 1024 + 64 * 1024
for n in range(200):
    img = bytearray(fuzz_base)
    for _ in range(rng.choice((1, 4, 16, 64))):
        img[rng.randrange(span)] = rng.randrange(256)
    p.write_bytes(bytes(img))
    run(p)

print("Card fingerprint: SHA-256 == hashlib, ECC/no-ECC agree, every index change detected, "
      "B?DATA-SYSTEM ignored, damaged images fail safely: OK")
