"""Card viewer: valid cards (no-ECC, ECC, 16 MB) and hostile images."""
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).parent))
import mc_fixture as fx

root = Path(__file__).resolve().parents[1]
tmp = Path(tempfile.mkdtemp())
exe = tmp / "host"
subprocess.run(["gcc", "-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-fno-sanitize-recover=all", f"-I{root/'include'}", str(root / "tests/mc_card_view_host.c"),
                str(root / "src/mc_card_view.c"), str(root / "src/mc_sha256.c"), "-o", str(exe)], check=True)


def run(raw, name):
    p = tmp / name
    p.write_bytes(raw)
    out = subprocess.run([str(exe), str(p)], capture_output=True, text=True)
    assert out.returncode == 0, (name, out.returncode, out.stderr[-400:])
    return out.stdout.splitlines()


def parse(lines):
    d = {"saves": []}
    for l in lines:
        if l.startswith("save|"):
            p = l.split("|")
            d["saves"].append((p[1], int(p[2]), int(p[3]), bool(int(p[4])), bool(int(p[5])),
                               int(p[6]), bytes.fromhex(p[7])))
        else:
            k, v = l.split("=", 1)
            d[k] = v
            if k == "nsaves":
                d["total"] = v.split("total=")[1]
    return d


def check_valid(raw, expect, label, ecc):
    d = parse(run(raw, label))
    assert d["open"] == "0", (label, d)
    assert d["ecc"].split()[0] == str(int(ecc)), (label, d["ecc"])
    assert [s[:5] for s in d["saves"]] == expect["saves"], (label, d["saves"])
    for name, files, size, icon, ps1, brk, title in d["saves"]:
        if name in expect["titles"]:
            want, wbrk = expect["titles"][name]
            assert title.rstrip(b"\0")[:len(want)] == want and brk == wbrk, (name, title, brk)
    return d


# 1. standard 8 MB, no ECC and ECC, must agree exactly
card, expect = fx.standard_card(8, False)
a = check_valid(card.image(), expect, "plain", False)
card_e, _ = fx.standard_card(8, True)
b = check_valid(card_e.image(), expect, "ecc", True)
assert a["saves"] == b["saves"]
assert len(card.image()) == 8388608 and len(card_e.image()) == 8650752

# free-space accounting: 8135 allocatable, used = root + 4 dirs/files chains
free_expected = sum(1 for i in range(card.alloc_end) if not (card.fat[i] & 0x80000000))
free_line = a["free"].split()
assert (free_line[0], int(free_line[1]), int(free_line[2])) == ("0", free_expected, 8135), a["free"]
assert free_expected < 8135 - 5

# 2. 16 MB card (more FAT clusters, same code path)
c16, e16 = fx.standard_card(16, False)
d16 = check_valid(c16.image(), e16, "mb16", False)
assert d16["ecc"].split()[1] == "clusters=16384", d16["ecc"]

# 3. many saves: root directory spans many clusters
many = fx.Card(8, False)
for i in range(60):
    many.add_save(f"SAVE{i:03d}", {"d": bytes([i]) * (i * 40)})
many.finish()
dm = parse(run(many.image(), "many"))
assert dm["open"] == "0" and dm["total"] == "60", dm
dm2 = parse(run(many.image(), "many2"))
assert len(dm2["saves"]) == 60 and dm2["saves"][59][0] == "SAVE059"

# 4. hostile images must be rejected or tolerated, never crash (ASan/UBSan are on)
good = card.image()
assert parse(run(good[:-512], "trunc"))["open"] == "-2"                       # size
assert parse(run(b"\0" * 8388608, "zeros"))["open"] == "-3"                   # magic
bad_magic = bytearray(good); bad_magic[0] ^= 1
assert parse(run(bytes(bad_magic), "magic"))["open"] == "-3"
patched = lambda patch: card.image(patch=patch)
assert parse(run(patched({0x3C: b"\x01\0\0\0"}), "rootdir"))["open"] == "-4"  # rootdir != 0
assert parse(run(patched({0x28: b"\0\x04"}), "pagesize"))["open"] == "-4"     # 1024-byte pages
assert parse(run(patched({0x30: b"\x00\x10\0\0"}), "clusters"))["open"] == "-4"
assert parse(run(patched({0x34: b"\xff\xff\xff\x7f"}), "alloc"))["open"] == "-4"
r = parse(run(patched({0x50: b"\0\0\0\0"}), "noifc"))["open"]                 # missing indirect FAT
assert r in ("-5", "-4"), r

# 5. a FAT chain that loops must terminate: point NOICON's directory chain at itself
loop = fx.Card(8, False)
dcl = loop.add_save("LOOP", {"a": b"a" * 5000})
loop.finish()
loop.fat[dcl] = 0x80000000 | dcl         # directory cluster links to itself
dl = parse(run(loop.image(), "loop"))
assert dl["open"] == "0" and dl["total"] == "1", dl

# 6. directory claims more entries than its chain holds
short = fx.Card(8, False)
dcl = short.add_save("SHORT", {"a": b"a" * 100})
short.finish()
short.root[0] = (short.root[0][0], 4000, short.root[0][2], short.root[0][3])
ds = parse(run(short.image(), "short"))
assert ds["open"] == "0"

# 7. a save whose chain ends in a free cluster is listed with zero contents
broken = fx.Card(8, False)
dcl = broken.add_save("BROKEN", {"a": b"a" * 100})
broken.finish()
broken.fat[dcl] = 0x7FFFFFFF
db = parse(run(broken.image(), "broken"))
assert db["open"] == "0" and db["saves"][0][1:3] == (0, 0), db
# 8. mutation fuzz: random damage to the superblock, FAT and directory clusters must never
# crash, hang or trip a sanitizer (the harness aborts on any ASan/UBSan finding)
import random
rng = random.Random(20261026)
base = bytearray(card.image())
span = card.alloc_offset * 1024 + 64 * 1024        # superblock + ifc + FAT + first data clusters
for n in range(150):
    img = bytearray(base)
    for _ in range(rng.choice((1, 4, 16, 64))):
        img[rng.randrange(span)] = rng.randrange(256)
    p = tmp / "fuzz.bin"
    p.write_bytes(bytes(img))
    out = subprocess.run([str(exe), str(p)], capture_output=True, text=True, timeout=20)
    assert out.returncode == 0, (n, out.returncode, out.stderr[-600:])
print("PS2 card viewer: valid cards, ECC/no-ECC parity, 16 MB, 60-save root, hostile inputs under ASan/UBSan: OK")
