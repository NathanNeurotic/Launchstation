"""PS1 card images and PS1 single saves (.mcs, PS1 .psv, .psx, raw).

The C code in src/mc_ps1_card.c runs on the host through
tests/mc_ps1_card_host.c. Every card it writes is checked with a separate
Python reader below that follows the PS1 BIOS layout on its own: frame 0
"MC", one 128-byte directory frame per block with an XOR checksum, links as
0-based block numbers, 0xFFFF at the end. Refusals must leave the card file
byte-identical.
"""
from pathlib import Path
import hashlib
import random
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
workflow = (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")
assert "python3 tests/test_mc_ps1_card.py" in workflow
tmp = Path(tempfile.mkdtemp(prefix="ps1card-"))
exe = tmp / "p"
subprocess.run(["gcc", "-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-fno-sanitize-recover=all", f"-I{root/'include'}", str(root / "tests/mc_ps1_card_host.c"),
                str(root / "src/mc_ps1_card.c"), "-o", str(exe)], check=True)

BLOCK, FRAME, CARD = 8192, 128, 131072
OK, NOT_CARD, INVALID, UNSUPPORTED, FULL, EXISTS, DAMAGED, NOMEM, NAME = 0, -1, -2, -3, -4, -5, -6, -7, -8
MCS, PSV, PSX, RAW = 1, 2, 3, 4
rng = random.Random(1307)


def run(*a):
    out = subprocess.run([str(exe), *map(str, a)], capture_output=True, text=True, timeout=60)
    assert out.returncode == 0, (a, out.returncode, out.stderr[-800:])
    return out.stdout


def kv(*a):
    return dict(p.split("=", 1) for p in run(*a).split())


def xor(frame):
    x = 0
    for b in frame[:127]:
        x ^= b
    return x


def py_card(path):
    """Independent reader: checks every frame's checksum and every link; returns
    ({name: (first, [blocks], data)}, free_count, raw bytes)."""
    c = Path(path).read_bytes()
    assert len(c) == CARD and c[:2] == b"MC"
    for i in range(36):
        f = c[i * FRAME:(i + 1) * FRAME]
        assert f[127] == xor(f), ("checksum", i)
    assert c[63 * FRAME:64 * FRAME] == c[:FRAME]
    saves, used, free = {}, set(), 0
    for i in range(1, 16):
        f = c[i * FRAME:(i + 1) * FRAME]
        st = struct.unpack_from("<I", f)[0]
        if st & 0xF0 == 0xA0:
            free += 1
        if st != 0x51:
            continue
        name = f[10:30].split(b"\0")[0].decode()
        chain, b = [], i
        while True:
            assert b not in used and b not in chain
            chain.append(b)
            fb = c[b * FRAME:(b + 1) * FRAME]
            nxt = struct.unpack_from("<H", fb, 8)[0]
            stb = struct.unpack_from("<I", fb)[0]
            if nxt == 0xFFFF:
                assert stb == (0x51 if len(chain) == 1 else 0x53)
                break
            assert stb == (0x51 if len(chain) == 1 else 0x52)
            assert struct.unpack_from("<I", fb, 4)[0] == 0 or len(chain) == 1
            b = nxt + 1
        used.update(chain)
        assert struct.unpack_from("<I", f, 4)[0] == len(chain) * BLOCK
        saves[name] = (i, chain, b"".join(c[k * BLOCK:(k + 1) * BLOCK] for k in chain))
    return saves, free, c


def c_list(path):
    lines = run("list", path).splitlines()
    free = int(lines[0].split("=")[1])
    rows = [l.split("|") for l in lines[1:]]
    return free, {r[3]: (int(r[0]), int(r[1]), int(r[2]), bytes.fromhex(r[4])) for r in rows}


def save_data(blocks, title):
    d = bytearray(rng.randbytes(blocks * BLOCK))
    d[0:4] = b"SC\x11" + bytes([blocks])
    t = title.ljust(64, b"\0")
    d[4:68] = t
    return bytes(d)


def mcs(name, data):
    f = bytearray(FRAME)
    struct.pack_into("<IIH", f, 0, 0x51, len(data), 0xFFFF)
    f[10:10 + len(name)] = name.encode()
    f[127] = xor(f)
    return bytes(f) + data


def psv_ps1(name, data, kind=1):
    h = bytearray(0x84)
    h[0:4] = b"\0VSP"
    h[0x3C] = kind
    h[0x64:0x64 + len(name)] = name.encode()
    return bytes(h) + data


def psx(name, data):
    h = bytearray(54)
    h[0:len(name)] = name.encode()
    h[21:21 + 5] = b"title"
    return bytes(h) + data


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def write(name, b):
    p = tmp / name
    p.write_bytes(b)
    return p


# --- a blank card
card = tmp / "card.mcd"
assert kv("format", card)["r"] == "0"
saves, free, raw = py_card(card)
assert saves == {} and free == 15
for i in range(1, 16):
    assert raw[i * FRAME:i * FRAME + 4] == b"\xa0\0\0\0" and raw[i * FRAME + 8:i * FRAME + 10] == b"\xff\xff"
assert c_list(card) == (15, {})

# --- every input format
A = save_data(3, "ＡＢＣ".encode("shift_jis"))
B = save_data(1, b"Second save")
C = save_data(2, b"Action Replay")
D = save_data(1, b"Raw one")
E = save_data(2, b"Raw with extension")
cases = [
    ("a.mcs", mcs("BASLUS-00001AAAA", A), "a.mcs", MCS, "BASLUS-00001AAAA", A),
    ("b.psv", psv_ps1("BESLES-00002BBBB", B), "b.psv", PSV, "BESLES-00002BBBB", B),
    ("c.psx", psx("BISLPS-00003CCCC", C), "c.psx", PSX, "BISLPS-00003CCCC", C),
    ("BASLUS-00004DDDD", D, "BASLUS-00004DDDD", RAW, "BASLUS-00004DDDD", D),
    ("BASLUS-00005EEEE.bin", E, "BASLUS-00005EEEE.bin", RAW, "BASLUS-00005EEEE", E),
]
for fname, body, hint, fmt, name, data in cases:
    p = write(fname, body)
    got = kv("parse", p, hint)
    assert got == {"r": "0", "fmt": str(fmt), "name": name, "blocks": str(len(data) // BLOCK)}, (fname, got)
    assert kv("import", card, p, hint) == {"r": "0", "fmt": str(fmt)}, fname
saves, free, raw = py_card(card)
assert free == 15 - 9
assert {n: (s[1], s[2]) for n, s in saves.items()} == {
    "BASLUS-00001AAAA": ([1, 2, 3], A), "BESLES-00002BBBB": ([4], B), "BISLPS-00003CCCC": ([5, 6], C),
    "BASLUS-00004DDDD": ([7], D), "BASLUS-00005EEEE": ([8, 9], E)}
cfree, listed = c_list(card)
assert cfree == 6 and listed["BASLUS-00001AAAA"] == (1, 3, 0, "ＡＢＣ".encode("shift_jis"))
assert listed["BISLPS-00003CCCC"][:3] == (5, 2, 0)

# --- refusals leave the card untouched
before = sha(card)
refusals = [
    (write("dup.mcs", mcs("BESLES-00002BBBB", B)), "dup.mcs", EXISTS),
    (write("ps2.psv", psv_ps1("BASLUS-20000PS2", B, kind=2)), "ps2.psv", UNSUPPORTED),
    (write("short.mcs", mcs("BASLUS-00009XXXX", B)[:-1]), "short.mcs", INVALID),
    (write("nosc.mcs", mcs("BASLUS-00009XXXX", b"XX" + B[2:])), "nosc.mcs", INVALID),
    (write("big.mcs", mcs("BASLUS-00009XXXX", save_data(16, b"x"))), "big.mcs", INVALID),
    (write("BASLUS-00009-NAME-TOO-LONG", D), "BASLUS-00009-NAME-TOO-LONG", NAME),
    (write("empty.mcs", mcs("", B)), "empty.mcs", NAME),
    (write("junk.bin", b"\x00" * BLOCK), "junk.bin", INVALID),
    (write("short54.psx", b"\x01" * 54), "short54.psx", INVALID),   # .psx magic sits at 0x36: never read past the end
    (write("short55.psx", b"\x01" * 55), "short55.psx", INVALID),
    (write("full.mcs", mcs("BASLUS-00010FULL", save_data(7, b"too big"))), "full.mcs", FULL),
]
for p, hint, want in refusals:
    assert int(kv("import", card, p, hint)["r"]) == want, (p.name, want)
    assert sha(card) == before, p.name
notcard = write("notcard.mcd", b"\0" * CARD)
assert kv("list", notcard)["r"] == str(NOT_CARD)
assert kv("import", write("small.mcd", b"MC" + b"\0" * 100), tmp / "a.mcs", "a.mcs")["r"] == str(NOT_CARD)

# --- export: an .mcs built independently, and it parses back to the same save
out = tmp / "out.mcs"
assert kv("export", card, 1, out)["r"] == "0"
assert out.read_bytes() == mcs("BASLUS-00001AAAA", A)
assert kv("export", card, 2, out)["r"] == str(DAMAGED)   # a middle block is not a save

# --- delete the BIOS way, then reuse the freed blocks in a fragmented chain
other = {n: s for n, s in py_card(card)[0].items() if n != "BASLUS-00001AAAA"}
assert kv("delete", card, 1)["r"] == "0"
saves, free, raw = py_card(card)
assert [raw[i * FRAME] for i in (1, 2, 3)] == [0xA1, 0xA2, 0xA3] and free == 9
assert saves == other
assert "BASLUS-00001AAAA" not in c_list(card)[1]
F = save_data(4, b"Fragmented")
assert kv("import", card, write("f.mcs", mcs("BASLUS-00011FFFF", F)), "f.mcs")["r"] == "0"
saves, free, raw = py_card(card)
assert saves["BASLUS-00011FFFF"][1:] == ([1, 2, 3, 10], F) and free == 5
assert {n: s for n, s in saves.items() if n != "BASLUS-00011FFFF"} == other
assert raw[3 * FRAME + 8:3 * FRAME + 10] == b"\x09\x00"   # block 3 links to block 10 (0-based 9)

# --- broken links: listed as damaged, never read, deleted or overwritten
bad = bytearray(card.read_bytes())
f = 4 * FRAME                       # BESLES (one block at 4): point it at free block 11
struct.pack_into("<H", bad, f + 8, 10)
bad[f + 127] = xor(bad[f:f + FRAME])
loop = bytearray(card.read_bytes())
for blk, nxt in ((5, 5),):          # BISLPS: block 5 links to itself
    struct.pack_into("<H", loop, blk * FRAME + 8, nxt - 1)
    loop[blk * FRAME + 127] = xor(loop[blk * FRAME:(blk + 1) * FRAME])
for img, first, name in ((bad, 4, "BESLES-00002BBBB"), (loop, 5, "BISLPS-00003CCCC")):
    p = write("bad.mcd", bytes(img))
    rows = c_list(p)[1]
    assert rows[name][2] == 1, rows[name]
    h = sha(p)
    assert kv("export", p, first, out)["r"] == str(DAMAGED)
    assert kv("delete", p, first)["r"] == str(DAMAGED)
    assert sha(p) == h

# --- the card UI: PS1 images open their own tool, every write is verified
ui = (root / "src/dash_ps1_card.c").read_text(encoding="utf-8")
tool = (root / "src/dash_card_tool.c").read_text(encoding="utf-8")
browser = (root / "src/dash_browser.c").read_text(encoding="utf-8")
yaml = (root / "ps2.yaml").read_text(encoding="utf-8")
assert "src/mc_ps1_card.c" in yaml and "src/dash_ps1_card.c" in yaml
assert "if (dashPs1CardIsImage(image)) {" in tool and "dashPs1CardTool(image);" in tool
assert 'genCmpFileExt(f->name, "MCR") || genCmpFileExt(f->name, "VM1")' in browser
w = ui[ui.index("static int p1Commit("):]
w = w[:w.index("\n}\n")]
# order: re-read and compare, safety copy, write, read back
assert w.index("memcmp(now, before, PS1_CARD_SIZE)") < \
    w.index("p1Safety(path, before") < w.index("p1WriteAll(path, after)") < w.index("memcmp(now, after, PS1_CARD_SIZE)")
assert "FIO_O_TRUNC" not in w                       # never truncates the card it rewrites
assert 'dashPickPath("Import which PS1 save?", dir, DASH_PICK_FILE, NULL, file, sizeof(file))' in ui   # raw saves have no extension
assert "ent->stat.size == PS1_CARD_SIZE" in tool                                      # "All saves" skips PS1 cards
assert '"128 KB PS1 card"' in tool and "dashPs1CardNewBlank(folder);" in tool
assert "Open a PS1 card image with Card contents and use Import save... there." in tool  # a PS1 .psv refused by a PS2 card points here
print("PASS: PS1 card images: list, import (.mcs/.psv/.psx/raw), export .mcs, delete; refusals leave the card unchanged")
