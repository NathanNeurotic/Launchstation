"""PS3 .psv / .psx save exports and the PS1 card containers (.gme .vgs .vmp .mcx).

The C code in src/mc_save_export.c runs on the host through
tests/mc_save_export_host.c, against the host's Mbed TLS. Every file it writes
is checked here by a separate Python implementation of the published layouts
(Apollo Save Tool / psv-save-converter): AES from the `cryptography` package,
HMAC-SHA1 and SHA-256 from the standard library. The harness also reads each
export back with Launchstation's own readers and requires the same save.
"""
import atexit
from pathlib import Path
import hashlib
import hmac
import random
import shutil
import struct
import subprocess
import tempfile

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

root = Path(__file__).resolve().parents[1]
workflow = (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")
assert "python3 tests/test_mc_save_export.py" in workflow
assert "src/mc_save_export.c" in (root / "ps2.yaml").read_text(encoding="utf-8")
tmp = Path(tempfile.mkdtemp(prefix="mc-export-"))
atexit.register(shutil.rmtree, tmp, ignore_errors=True)
exe = tmp / "x"
subprocess.run(["gcc", "-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-fno-sanitize-recover=all", f"-I{root/'include'}", str(root / "tests/mc_save_export_host.c"),
                str(root / "src/mc_save_export.c"), str(root / "src/mc_ps1_card.c"),
                str(root / "src/mc_save_formats.c"), str(root / "src/mc_card_write.c"),
                str(root / "src/mc_card_view.c"), str(root / "src/mc_sha256.c"),
                "-lmbedcrypto", "-lz", "-o", str(exe)], check=True)

BLOCK, CARD = 8192, 131072
RAW, GME, VGS, VMP, MCX = range(5)
rng = random.Random(4851)
PS1_KEY = bytes.fromhex("ab5abc9fc1f49de6a051dbaefa518859")
PS2_KEY = bytes.fromhex("ea02ceef5bb4d2998f611910d77f51c6")
SIGN_IV = bytes.fromhex("b30ffeedb7dc5eb7133da60d1b6b2cdc")
MCX_KEY = bytes.fromhex("81d9cce971a9499b04addc48307f0792")
MCX_IV = bytes.fromhex("13c2e7694bec696d52cf00092ac1f272")


def run(*a):
    out = subprocess.run([str(exe), *map(str, a)], capture_output=True, text=True, timeout=120)
    assert out.returncode == 0, (a, out.returncode, out.stderr[-800:])
    return dict(kv.split("=", 1) for kv in out.stdout.split())


def aes(key, data, enc, iv=None):
    c = Cipher(algorithms.AES(key), modes.CBC(iv) if iv else modes.ECB())
    op = c.encryptor() if enc else c.decryptor()
    return op.update(data) + op.finalize()


def signature(buf, seed, sig, ps1):
    """The documented key derivation, then HMAC-SHA1 with the signature zeroed."""
    s = buf[seed:seed + 20]
    if ps1:
        a = bytes(x ^ y for x, y in zip(aes(PS1_KEY, s[:16], False), SIGN_IV))
        b = aes(PS1_KEY, s[:16], True)
        key = a + bytes(x ^ y for x, y in zip(b[:4], s[16:20]))
    else:
        key = aes(PS2_KEY, s + bytes(44), False, SIGN_IV)[:20]
    body = buf[:sig] + bytes(20) + buf[sig + 20:]
    return hmac.new(key, body, hashlib.sha1).digest()


def ps1_save(name, blocks):
    data = bytearray(rng.randbytes(blocks * BLOCK))
    data[0:2] = b"SC"
    frame = bytearray(128)
    frame[0] = 0x51
    frame[4:8] = struct.pack("<I", blocks * BLOCK)
    frame[8:10] = b"\xff\xff"
    frame[10:10 + len(name)] = name.encode()
    for b in frame[:127]:
        frame[127] ^= b
    return bytes(frame) + bytes(data), bytes(data)


# ---- PS1 save exports
for name, blocks in (("BASCUS-94163FF7", 1), ("BESLES-01234ABCDEFGH", 3), ("BISLPS-00001", 15)):
    mcs, data = ps1_save(name, blocks)
    (tmp / "s.mcs").write_bytes(mcs)
    assert run("ps1", tmp / "s.mcs", "-", tmp / "s.psv", tmp / "s.psx") == {"r": "0"}
    psv = (tmp / "s.psv").read_bytes()
    assert len(psv) == 0x84 + len(data) and psv[:4] == b"\0VSP"
    assert struct.unpack_from("<10I", psv, 0x38) == (0x14, 1, len(data), 0x84, 0x200, 0, 0, 0, 0, len(data))
    assert struct.unpack_from("<I", psv, 0x60)[0] == 0x9003
    assert psv[0x64:0x84].rstrip(b"\0") == name.encode() and psv[0x84:] == data
    assert psv[0x1C:0x30] == signature(psv, 0x08, 0x1C, True), name
    psx = (tmp / "s.psx").read_bytes()
    assert psx[:54].rstrip(b"\0") == name.encode() and psx[54:] == data
# a name the BIOS would not take is refused before anything is written
bad, _ = ps1_save("BASCUS-94163FF7", 1)
bad = bytearray(bad)
bad[10:30] = b"\x01" * 20
(tmp / "bad.mcs").write_bytes(bad)
(tmp / "n.psv").unlink(missing_ok=True)
assert run("ps1", tmp / "bad.mcs", "-", tmp / "n.psv", tmp / "n.psx") == {"r": "-8"}
assert not (tmp / "n.psv").exists()


# ---- PS2 .psv
def entry(mode, length, name, created, modified):
    e = bytearray(512)
    struct.pack_into("<HxxI", e, 0, mode, length)
    e[8:16] = created
    e[0x18:0x20] = modified
    e[0x40:0x40 + len(name)] = name.encode()
    return bytes(e)


def psu(folder, files):
    t0, t1 = bytes([0, 1, 2, 3, 4, 5, 0xE9, 7]), bytes([0, 9, 8, 7, 6, 5, 0xEA, 7])
    out = entry(0x8427, len(files) + 2, folder, t0, t1) + entry(0x8427, 0, ".", t0, t1) + entry(0xA426, 0, "..", t0, t1)
    for i, (n, d) in enumerate(files):
        out += entry(0x8497, len(d), n, bytes([i] * 8), bytes([i + 1] * 8)) + d + b"\xff" * (-len(d) % 1024)
    return out, t0, t1


icon_sys = bytearray(964)
icon_sys[0:4] = b"PS2D"
for j, n in enumerate((b"list.ico", b"copy.ico", b"del.ico")):
    icon_sys[0x104 + j * 64:0x104 + j * 64 + len(n)] = n
files = [("icon.sys", bytes(icon_sys)), ("list.ico", rng.randbytes(3000)), ("copy.ico", rng.randbytes(1500)),
         ("del.ico", rng.randbytes(700)), ("DATA", rng.randbytes(40000)), ("EMPTY", b"")]
p, t0, t1 = psu("BASLUS-21386TALES", files)
(tmp / "s.psu").write_bytes(p)
assert run("ps2", tmp / "s.psu", tmp / "s2.psv") == {"r": "0"}
v = (tmp / "s2.psv").read_bytes()
off = 0xA0 + 60 * len(files)
assert v[:4] == b"\0VSP" and struct.unpack_from("<II", v, 0x38) == (0x2C, 2)
assert len(v) == off + sum(len(d) for _, d in files)
assert struct.unpack_from("<I", v, 0x40)[0] == sum(len(d) for _, d in files)
assert struct.unpack_from("<II", v, 0x44) == (off, len(icon_sys))
at = {}
pos = off
for i, (n, d) in enumerate(files):
    r = v[0xA0 + 60 * i:0xA0 + 60 * (i + 1)]
    assert r[0:8] == bytes([i] * 8) and r[8:16] == bytes([i + 1] * 8)
    assert struct.unpack_from("<II", r, 16) == (len(d), 0x8497) and r[24:56].rstrip(b"\0") == n.encode()
    assert struct.unpack_from("<I", r, 56)[0] == pos and v[pos:pos + len(d)] == d
    at[n] = (pos, len(d))
    pos += len(d)
assert struct.unpack_from("<6I", v, 0x4C) == (*at["list.ico"], *at["copy.ico"], *at["del.ico"])
assert struct.unpack_from("<I", v, 0x64)[0] == len(files) and v[0x68:0x70] == t0 and v[0x70:0x78] == t1
assert struct.unpack_from("<II", v, 0x78) == (len(files) + 2, 0x8427)
assert v[0x80:0xA0].rstrip(b"\0") == b"BASLUS-21386TALES"
assert v[0x1C:0x30] == signature(v, 0x08, 0x1C, False)

# ---- PS2 .zip: read by Python's own zipfile (CRCs checked), the folder first, data and DOS times kept
import zipfile
assert run("zip", tmp / "s.psu", tmp / "s.zip") == {"r": "0"}
with zipfile.ZipFile(tmp / "s.zip") as z:
    assert z.testzip() is None
    infos = z.infolist()
    assert [i.filename for i in infos] == ["BASLUS-21386TALES/"] + ["BASLUS-21386TALES/" + n for n, _ in files]
    assert infos[0].is_dir() and all(i.compress_type == zipfile.ZIP_STORED for i in infos)
    assert infos[0].date_time == (2026, 5, 6, 7, 8, 8)                  # the folder's modified time, seconds even
    assert all(i.date_time == (1980, 1, 1, 0, 0, 0) for i in infos[1:])  # these test times are not real dates
    for n, d in files:
        assert z.read("BASLUS-21386TALES/" + n) == d
(tmp / "e.psu").write_bytes(psu("EMPTYSAVE", [])[0])
assert run("zip", tmp / "e.psu", tmp / "e.zip") == {"r": "0"}
with zipfile.ZipFile(tmp / "e.zip") as z:
    assert z.namelist() == ["EMPTYSAVE/"]

assert run("name", "BASLUS-21386TALES") == {"name": "BASLUS-21386" + b"TALES".hex().upper() + ".PSV"}
assert run("name", "BASCUS-9416") == {"name": "BASCUS-9416.PSV"}


# ---- PS1 card containers
def raw_card():
    c = bytearray(rng.randbytes(CARD))
    c[0:2] = b"MC"
    return bytes(c)


def sealed(kind, raw, head=None):
    (tmp / "raw").write_bytes(raw)
    if head is not None:
        (tmp / "head").write_bytes(head)
    assert run("seal", kind, tmp / "raw", tmp / "head" if head is not None else "-", tmp / "card") == {"r": "0"}
    return (tmp / "card").read_bytes()


def opened(data, ext):
    (tmp / "in").write_bytes(data)
    return run("open", tmp / "in", ext, tmp / "o.raw", tmp / "o.head")


for kind, ext in ((RAW, "mcd"), (GME, "gme"), (VGS, "vgs"), (VMP, "vmp"), (MCX, "mcx")):
    raw = raw_card()
    f = sealed(kind, raw)
    if kind == RAW:
        assert f == raw
    elif kind == GME:
        assert len(f) == 3904 + CARD and f[:11] == b"123-456-STD" and f[18] == 1 and f[20] == 1 and f[21] == ord("M")
        assert f[0x16:0x25] == bytes(raw[(i + 1) * 128] for i in range(15))
        assert f[0x26:0x35] == bytes(raw[(i + 1) * 128 + 8] for i in range(15)) and f[3904:] == raw
    elif kind == VGS:
        assert len(f) == 64 + CARD and f[:4] == b"VgsM" and struct.unpack_from("<4I", f, 4) == (1, 1, 1, 128)
        assert f[64:] == raw
    elif kind == VMP:
        assert len(f) == 128 + CARD and f[:4] == b"\0PMV" and struct.unpack_from("<I", f, 4)[0] == 128
        assert f[128:] == raw and f[0x20:0x34] == signature(f, 0x0C, 0x20, True)
    else:
        plain = aes(MCX_KEY, f, False, MCX_IV)
        assert len(f) == 128 + CARD + 32 and plain[128:128 + CARD] == raw
        assert plain[128 + CARD:] == hashlib.sha256(plain[:128 + CARD]).digest()
    assert opened(f, ext) == {"kind": str(kind), "r": str((0, 3904, 64, 128, 128)[kind])}
    assert (tmp / "o.raw").read_bytes() == raw
    # rewriting keeps the header it came with; the GME copy, VMP signature and MCX hash follow the card
    head = (tmp / "o.head").read_bytes()
    if kind != RAW:
        head = bytes(head[:-1]) + bytes([head[-1] ^ 0x5A])
        raw2 = raw_card()
        g = sealed(kind, raw2, head)
        assert opened(g, ext)["kind"] == str(kind) and (tmp / "o.raw").read_bytes() == raw2
        got = (tmp / "o.head").read_bytes()
        assert got[-1] == head[-1]
        if kind == VMP:
            assert g[0x20:0x34] == signature(g, 0x0C, 0x20, True)
        if kind == GME:
            assert got[0x16:0x25] == bytes(raw2[(i + 1) * 128] for i in range(15))

# refused: a damaged MCX (any 131232-byte file is tried as one; its hash decides), a wrong size, no "MC" frame
m = bytearray(sealed(MCX, raw_card()))
m[5000] ^= 1
assert opened(bytes(m), "mcx") == {"kind": "4", "r": "-1"}
assert opened(rng.randbytes(128 + CARD + 32), "-") == {"kind": "4", "r": "-1"}
assert opened(sealed(VGS, raw_card())[:-1], "vgs") == {"kind": "-1", "r": "-1"}
nomc = bytearray(sealed(GME, raw_card()))
nomc[3904] = 0
assert opened(bytes(nomc), "gme")["r"] == "-1"
print("ok")
