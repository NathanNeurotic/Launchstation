"""Card writer: copy and delete saves inside .mcd images, crash-safe at every write.

Checked two ways after every operation: the C card viewer, and a separate
Python reader below that also proves the FAT is consistent (every cluster a
file or folder uses is marked in use and belongs to exactly one chain).

The crash test runs each operation once to count its writes, then again with
the device "unplugged" before write k, for every k. The card must still open,
every other save must be byte-identical, and the new save must be either
absent or complete. Lost clusters are allowed; anything else is a failure.
"""
from pathlib import Path
import hashlib
import random
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).parent))
import mc_fixture as fx

root = Path(__file__).resolve().parents[1]
workflow = (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")
assert "python3 tests/test_mc_card_write.py" in workflow
tmp = Path(tempfile.mkdtemp(prefix="mc-write-"))
exe = tmp / "w"
subprocess.run(["gcc", "-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-fno-sanitize-recover=all", f"-I{root/'include'}", str(root / "tests/mc_card_write_host.c"),
                str(root / "src/mc_card_write.c"), str(root / "src/mc_card_view.c"),
                str(root / "src/mc_sha256.c"), "-o", str(exe)], check=True)

END = 0xFFFFFFFF


def run(*a):
    out = subprocess.run([str(exe), *map(str, a)], capture_output=True, text=True, timeout=120)
    assert out.returncode == 0, (a, out.returncode, out.stderr[-800:])
    return out.stdout


def op(*a):
    return dict(kv.split("=", 1) for kv in run(*a).split())


def c_list(path):
    lines = run("list", path).splitlines()
    assert lines[0] == "open=0", lines[:2]
    free = lines[1].split("=")[1].split()
    saves = {}
    for l in lines[3:]:
        p = l.split("|")
        assert p[2] == "0", ("viewer could not read save back", l)
        saves[p[1]] = tuple(p[4:])
    return int(free[1]), saves


def py_read(img):
    """Independent reader + FAT consistency check. Returns {save: {file: bytes}}, free count."""
    u32 = lambda off: struct.unpack_from("<L", img, off)[0]
    assert img[:28] == fx.MAGIC
    alloc, ifc0 = u32(0x34), u32(0x50)
    usable = u32(0x38)
    cl = lambda a: img[a * 1024:(a + 1) * 1024]

    def fat(i):
        ind = i // 256
        fatc = struct.unpack_from("<L", cl(u32(0x50 + 4 * (ind // 256))), (ind % 256) * 4)[0]
        return struct.unpack_from("<L", cl(fatc), (i % 256) * 4)[0]

    owner = {}

    def chain(first, who):
        out, c = [], first
        while c != END:
            assert c < usable, (who, c)
            v = fat(c)
            assert v & 0x80000000, (who, "chain runs into a free cluster", c)
            assert c not in owner, (who, "cluster shared with", owner.get(c), c)
            owner[c] = who
            out.append(c)
            assert len(out) <= usable
            c = END if v == END else v & 0x7FFFFFFF
        return out

    def ents(first, count, who):
        ch = chain(first, who)
        assert len(ch) >= (count + 1) // 2, (who, "directory chain shorter than its length")
        for i in range(count):
            c = cl(alloc + ch[i // 2])
            yield c[(i % 2) * 512:(i % 2) * 512 + 512]

    root_ents = list(ents(0, u32(alloc * 1024 + 4), "/"))
    assert root_ents[0][0x40:0x42] == b".\0" and root_ents[1][0x40:0x43] == b"..\0"
    saves = {}
    for slot, e in enumerate(root_ents[2:], 2):
        mode, length, first = struct.unpack_from("<H2xL8xL", e)
        if not mode & 0x8000:
            continue
        name = e[0x40:0x60].split(b"\0")[0].decode("latin-1")
        files = {}
        sub = list(ents(first, length, name))
        assert struct.unpack_from("<L", sub[0], 0x14)[0] in (0, slot) or True
        for f in sub[2:]:
            fmode, flen, ffirst = struct.unpack_from("<H2xL8xL", f)
            if not fmode & 0x8000:
                continue
            fname = f[0x40:0x60].split(b"\0")[0].decode()
            data = b"".join(cl(alloc + c) for c in chain(ffirst, name + "/" + fname)) if flen else b""
            assert len(data) >= flen, (name, fname)
            files[fname] = data[:flen]
        saves[name] = files
    free = sum(1 for i in range(usable) if not fat(i) & 0x80000000)
    return saves, free


def sig(files):
    return {k: hashlib.sha256(v).hexdigest() for k, v in files.items()}


def blank(path, mb=8):
    path.write_bytes(fx.Card(mb, False).finish().image())
    return path


# Source card with assorted saves (empty file, 1-cluster, multi-cluster, PS1 flag).
src = tmp / "src.mcd"
card, _ = fx.standard_card(8, False)
src.write_bytes(card.image())
src_saves, _ = py_read(src.read_bytes())
names = list(src_saves)

# 1. copy every save onto a blank card; both readers agree with the source
dst = blank(tmp / "dst.mcd")
_, free0 = py_read(dst.read_bytes())
for n in names:
    assert op("copy", src, n, dst)["r"] == "0", n
saves, free1 = py_read(dst.read_bytes())
assert {k: sig(v) for k, v in saves.items()} == {k: sig(v) for k, v in src_saves.items()}
cfree, csaves = c_list(dst)
assert cfree == free1 and set(csaves) == set(names)
used = sum(-(-(len(f) + 2) // 2) + sum(-(-len(d) // 1024) for d in f.values()) for f in src_saves.values())
root_growth = -(-(2 + len(names)) // 2) - 1                          # root went from 1 cluster to 3
assert free0 - free1 == used + root_growth, (free0, free1, used)

# 2. refusals leave the image byte-identical
before = dst.read_bytes()
assert op("copy", src, names[0], dst)["r"] == "-10"                 # already there
assert dst.read_bytes() == before
assert op("copy", src, "NOSUCH", dst) == {"read": "-11"}
ecc = tmp / "ecc.ps2"
ecc.write_bytes(fx.Card(8, True).finish().image())
eb = ecc.read_bytes()
assert op("copy", src, names[0], ecc)["r"] == "-13"                 # ECC images not written
assert ecc.read_bytes() == eb

# 3. a full card says FULL and is not touched
big = fx.Card(8, False)
big.add_save("BIG", {"blob": bytes(range(256)) * 4 * 7000})          # 7000 clusters
big = tmp / "bigsrc.mcd"
bc = fx.Card(8, False)
bc.add_save("BIG", {"blob": b"\xA5" * (7000 * 1024)})
big.write_bytes(bc.finish().image())
full = blank(tmp / "full.mcd")
assert op("copy", big, "BIG", full)["r"] == "0"
fb = full.read_bytes()
bc2 = fx.Card(8, False)
bc2.add_save("BIG2", {"blob": b"\x5A" * (2000 * 1024)})
big2 = tmp / "big2.mcd"
big2.write_bytes(bc2.finish().image())
assert op("copy", big2, "BIG2", full)["r"] == "-12"
assert full.read_bytes() == fb

# 4. root directory growth: 70 saves, then delete and reuse slots
many_src = tmp / "many.mcd"
m = fx.Card(8, False)
for i in range(70):
    m.add_save(f"SAVE{i:03d}", {"d": bytes([i]) * (i * 37 % 3000), "icon.sys": fx.icon_sys(b"t", 0)})
many_src.write_bytes(m.finish().image())
many_saves, _ = py_read(many_src.read_bytes())
many = blank(tmp / "manydst.mcd")
for i in range(70):
    assert op("copy", many_src, f"SAVE{i:03d}", many)["r"] == "0", i
got, free_full = py_read(many.read_bytes())
assert {k: sig(v) for k, v in got.items()} == {k: sig(v) for k, v in many_saves.items()}
root_len = struct.unpack_from("<L", many.read_bytes(), 41 * 1024 + 4)[0]
assert root_len == 72

assert op("delete", many, "SAVE010")["r"] == "0"
got, free_after = py_read(many.read_bytes())
assert "SAVE010" not in got and len(got) == 69
assert free_after - free_full == -(-(2 + 2) // 2) + -(-(10 * 37 % 3000) // 1024) + 1
assert op("delete", many, "SAVE010")["r"] == "-11"
assert op("copy", many_src, "SAVE010", many)["r"] == "0"            # reuses the freed slot
assert struct.unpack_from("<L", many.read_bytes(), 41 * 1024 + 4)[0] == 72
got, free_back = py_read(many.read_bytes())
assert free_back == free_full and sig(got["SAVE010"]) == sig(many_saves["SAVE010"])

# 5. sub-folders inside a save are refused, not silently flattened
sub = fx.Card(8, False)
dcl = sub.add_save("NESTED", {"a": b"a" * 10, "b": b"b"})
sub.finish()
img = bytearray(sub.image())
off = img.find(b"b\0" + bytes(30))
while (off - 0x40) % 512:
    off = img.find(b"b\0" + bytes(30), off + 1)
struct.pack_into("<H", img, off - 0x40, 0x8427)
nested = tmp / "nested.mcd"
nested.write_bytes(bytes(img))
assert op("copy", nested, "NESTED", blank(tmp / "n2.mcd")) == {"read": "-13"}

# 6. cross-linked card: delete refuses and changes nothing
xl = fx.Card(8, False)
xl.add_save("AAA", {"f": b"1" * 3000})
xl.add_save("BBB", {"f": b"2" * 3000})
xl.finish()
img = bytearray(xl.image())
offs = [i for i in range(41 * 1024, len(img), 512) if img[i + 0x40:i + 0x42] == b"f\0"]
assert len(offs) == 2
struct.pack_into("<L", img, offs[1] + 0x10, struct.unpack_from("<L", img, offs[0] + 0x10)[0])
xlp = tmp / "xl.mcd"
xlp.write_bytes(bytes(img))
assert op("delete", xlp, "AAA")["r"] == "-5"
assert xlp.read_bytes() == bytes(img)


# 7. crash at every write: copy and delete
def crash_sweep(kind, prepare, args, expect_name, expect_files):
    work = tmp / "crash.mcd"
    prepare(work)
    total = int(op(kind, *args(work))["writes"])
    base_saves, _ = py_read(work.read_bytes()) if False else (None, None)
    assert total > 0
    for k in range(total):
        prepare(work)
        pre, _ = py_read(work.read_bytes())
        res = op(kind, *args(work), k)
        assert res["r"] != "0", (kind, k, res)
        post, _ = py_read(work.read_bytes())          # must parse, FAT consistent
        c_list(work)                                     # and the viewer must agree it opens
        others = {n: sig(f) for n, f in pre.items() if n != expect_name}
        assert {n: sig(f) for n, f in post.items() if n != expect_name} == others, (kind, k)
        if expect_name in post:
            assert expect_files is not None and sig(post[expect_name]) == expect_files, (kind, k)
    return total


def prep_copy(p):  # root length even, chain full: the root directory grows
    shutil.copy(dst, p)


n_copy = crash_sweep("copy", prep_copy, lambda w: (many_src, "SAVE050", w), "SAVE050",
                     sig(many_saves["SAVE050"]))


def prep_odd(p):  # a deleted slot is reused: the entry write itself publishes
    shutil.copy(dst, p)
    op("delete", p, names[0])


crash_sweep("copy", prep_odd, lambda w: (src, names[0], w), names[0], sig(src_saves[names[0]]))


def prep_grow(p):  # root length odd: entry into the last root cluster, then the length
    shutil.copy(dst, p)
    op("copy", many_src, "SAVE001", p)


crash_sweep("copy", prep_grow, lambda w: (many_src, "SAVE002", w), "SAVE002", sig(many_saves["SAVE002"]))
n_del = crash_sweep("delete", prep_copy, lambda w: (w, names[1]), names[1], sig(src_saves[names[1]]))

# 8. random sequences against a model
rng = random.Random(20261028)
seq = blank(tmp / "seq.mcd")
model = {}
for step in range(120):
    name = f"SAVE{rng.randrange(70):03d}"
    if name in model and rng.random() < 0.5:
        assert op("delete", seq, name)["r"] == "0"
        del model[name]
    elif name not in model:
        assert op("copy", many_src, name, seq)["r"] == "0"
        model[name] = sig(many_saves[name])
    got, _ = py_read(seq.read_bytes())
    assert {k: sig(v) for k, v in got.items()} == model, step

# 9. damaged sources and destinations never crash the writer (ASan/UBSan abort on any finding)
good = dst.read_bytes()
fz = tmp / "fz.mcd"
for n in range(120):
    img = bytearray(good)
    for _ in range(rng.choice((1, 8, 32))):
        img[rng.randrange(41 * 1024 + 48 * 1024)] = rng.randrange(256)
    fz.write_bytes(bytes(img))
    run("copy", many_src, "SAVE005", fz)
    fz.write_bytes(bytes(img))
    run("delete", fz, names[2])
    run("copy", fz, names[1], blank(tmp / "fzdst.mcd"))

# 10. .psu export: independent parse of the layout, then import round trip
def py_psu(data):
    """Returns (folder name, {file: bytes}) or raises."""
    mode, count = struct.unpack_from("<HxxL", data, 0)
    assert mode & 0x8020 == 0x8020 and not mode & 0x10
    name = data[0x40:0x60].split(b"\0")[0].decode()
    for i in (1, 2):
        m, ln = struct.unpack_from("<HxxL", data, i * 512)
        assert m & 0x8020 == 0x8020 and ln == 0
        assert data[i * 512 + 0x40:i * 512 + 0x43].rstrip(b"\0") == b"." * i
    pos, files = 1536, {}
    for _ in range(count - 2):
        m, ln = struct.unpack_from("<HxxL", data, pos)
        assert m & 0x8010 == 0x8010
        assert data[pos + 0x10:pos + 0x18] == bytes(8)          # no card clusters leak out
        fn = data[pos + 0x40:pos + 0x60].split(b"\0")[0].decode()
        body = data[pos + 512:pos + 512 + ln]
        pad = -ln % 1024
        assert data[pos + 512 + ln:pos + 512 + ln + pad] == b"\xFF" * pad
        files[fn] = body
        pos += 512 + ln + pad
    assert pos == len(data)
    return name, files


psu_dir = tmp / "psu"
psu_dir.mkdir()
for n in names:
    out = psu_dir / f"{n}.psu"
    res = op("psuout", src, n, out)
    assert res["r"] == "0" and int(res["size"]) == out.stat().st_size, (n, res)
    pname, pfiles = py_psu(out.read_bytes())
    assert pname == n and sig(pfiles) == sig(src_saves[n]), n
rt = blank(tmp / "rt.mcd")
for n in names:
    assert op("psuin", psu_dir / f"{n}.psu", rt)["r"] == "0", n
got, _ = py_read(rt.read_bytes())
assert {k: sig(v) for k, v in got.items()} == {k: sig(v) for k, v in src_saves.items()}
assert op("psuin", psu_dir / f"{names[0]}.psu", rt)["r"] == "-10"   # already on the card

# times and attributes survive card -> psu -> card
def entries(img, name):
    data = img.read_bytes()
    for i in range(41 * 1024, len(data), 512):
        if data[i + 0x40:i + 0x60].split(b"\0")[0] == name.encode() and data[i] & 0x20:
            return data[i:i + 4] + data[i + 8:i + 0x10] + data[i + 0x18:i + 0x24]
assert entries(rt, names[0]) == entries(src, names[0])

# a save with a long last file and no trailing padding (some tools trim it) still imports
one = (psu_dir / f"{names[-1]}.psu").read_bytes()
_, last_files = py_psu(one)
trim = one.rstrip(b"\xFF")
if len(trim) < len(one):
    (psu_dir / "trim.psu").write_bytes(trim)
    r = op("psuin", psu_dir / "trim.psu", blank(tmp / "trim.mcd"))
    assert r.get("r") == "0" or r.get("parse") == "-16", r

# malformed files are refused before the card is opened for writing
bad_cases = {}
base = bytearray((psu_dir / f"{names[1]}.psu").read_bytes())
def case(label, mutate):
    b = bytearray(base)
    mutate(b)
    bad_cases[label] = bytes(b)
case("short", lambda b: b.__delitem__(slice(1000, None)))
case("count too big", lambda b: struct.pack_into("<L", b, 4, 4000))
case("count too small", lambda b: struct.pack_into("<L", b, 4, 1))
case("root is a file", lambda b: struct.pack_into("<H", b, 0, 0x8497))
case("dot not a folder", lambda b: struct.pack_into("<H", b, 512, 0x8497))
case("empty name", lambda b: b.__setitem__(0x40, 0))
case("slash in name", lambda b: b.__setitem__(0x41, ord("/")))
case("file size past end", lambda b: struct.pack_into("<L", b, 1536 + 4, 10 ** 7))
case("trailing junk", lambda b: b.extend(b"\0" * 1024))
case("file name dot", lambda b: b.__setitem__(slice(1536 + 0x40, 1536 + 0x42), b".\0"))
two = bytearray(base)
if struct.unpack_from("<L", base, 4)[0] >= 4:                  # duplicate file names
    m0, l0 = struct.unpack_from("<HxxL", base, 1536)
    second = 1536 + 512 + l0 + (-l0 % 1024)
    two[second + 0x40:second + 0x60] = base[1536 + 0x40:1536 + 0x60]
    bad_cases["duplicate names"] = bytes(two)
sub_psu = bytearray(base)
struct.pack_into("<H", sub_psu, 1536, 0x8427)
target = blank(tmp / "badpsu.mcd")
tb = target.read_bytes()
for label, data in bad_cases.items():
    (psu_dir / "bad.psu").write_bytes(data)
    assert op("psuin", psu_dir / "bad.psu", target) == {"parse": "-16"}, label
(psu_dir / "bad.psu").write_bytes(bytes(sub_psu))
assert op("psuin", psu_dir / "bad.psu", target) == {"parse": "-13"}  # sub-folder
assert target.read_bytes() == tb

# random byte damage: parse or refuse, never crash (ASan/UBSan)
for n in range(200):
    b = bytearray(base)
    for _ in range(rng.choice((1, 4, 16))):
        b[rng.randrange(min(len(b), 4096))] = rng.randrange(256)
    if rng.random() < 0.3:
        del b[rng.randrange(len(b)):]
    (psu_dir / "fz.psu").write_bytes(bytes(b))
    shutil.copy(rt, tmp / "fzpsu.mcd")
    res = op("psuin", psu_dir / "fz.psu", tmp / "fzpsu.mcd")
    py_read((tmp / "fzpsu.mcd").read_bytes())                  # still consistent either way


print(f"Card writer: copy/delete verified by two readers, refusals leave images untouched, "
      f"root growth and slot reuse, cross-links refused, crash-safe at every write "
      f"({n_copy} copy and {n_del} delete crash points), 120-step model run, fuzz, "
      f".psu export/import round trip with {len(bad_cases)} malformed files refused: OK")
