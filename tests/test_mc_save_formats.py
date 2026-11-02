"""Save formats other tools export (Blaze .mcm, MAX Drive .max, CodeBreaker .cbs, SharkPort/X-Port
.sps/.xps, PS3 .psv) read into the card writer's save structure and written out as .psu.

Each fixture is built here from the format description (Ross Ridge's public-domain mymc for
.max/.cbs/.sps, bucanero's psv-save-converter notes for .psv, Berion's Blaze sample for .mcm);
the .psu that comes out is parsed independently and must hold the same files, names, modes and
times. Malformed files are refused whole. A real Blaze export is also run when BLAZE_MCM_SAMPLE
names one (it is not committed: it holds someone else's binaries)."""
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
import zlib

root = Path(__file__).resolve().parents[1]
workflow = (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")
assert "python3 tests/test_mc_save_formats.py" in workflow
assert "src/mc_save_formats.c" in (root / "ps2.yaml").read_text(encoding="utf-8")
tmp = Path(tempfile.mkdtemp(prefix="mc-formats-"))
exe = tmp / "f"
subprocess.run(["gcc", "-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-fno-sanitize-recover=all", f"-I{root/'include'}", str(root / "tests/mc_save_formats_host.c"),
                str(root / "src/mc_save_formats.c"), str(root / "src/mc_card_write.c"),
                str(root / "src/mc_card_view.c"), str(root / "src/mc_sha256.c"), "-lz", "-o", str(exe)], check=True)


def run(*a):
    out = subprocess.run([str(exe), *map(str, a)], capture_output=True, text=True, timeout=120)
    assert out.returncode == 0, (a, out.returncode, out.stderr[-800:])
    return dict(kv.split("=", 1) for kv in out.stdout.split())


def conv(src, name_hint="-", ext_hint="-", stamp=True):
    out = tmp / (Path(src).name + ".psu")
    if out.exists():
        out.unlink()
    r = run("conv", src, out, name_hint, ext_hint, *(["stamp"] if stamp else []))
    return r, out


def py_psu(data):
    """Independent .psu parser: (dir entry, [(entry, data)])."""
    ent = data[:512]
    count = struct.unpack_from("<L", ent, 4)[0]
    assert data[512 + 0x40:512 + 0x42] == b".\0" and data[1024 + 0x40:1024 + 0x43] == b"..\0"
    files, pos = [], 1536
    for _ in range(count - 2):
        e = data[pos:pos + 512]
        size = struct.unpack_from("<L", e, 4)[0]
        files.append((e, data[pos + 512:pos + 512 + size]))
        pos += 512 + (size + 1023) // 1024 * 1024
    assert pos == len(data), (pos, len(data))
    return ent, files


def name_of(e):
    return e[0x40:0x60].split(b"\0", 1)[0]


STAMP = bytes([0, 56, 34, 12, 29, 10, 0xEA, 0x07])
T1 = bytes([0, 1, 2, 3, 4, 5, 0xE5, 0x07])
T2 = bytes([0, 7, 8, 9, 10, 11, 0xE6, 0x07])
MODE_DIR, MODE_FILE = 0x8427, 0x8497

FOLDER = b"BASLUS-21050SAVE"
FILES = [(b"icon.sys", bytes(range(256)) * 3 + b"\0" * 196), (b"BASLUS-21050", bytes(i * 7 & 0xFF for i in range(12345))),
         (b"tiny.dat", b"x"), (b"empty", b"")]


def check(out, folder, files, dir_mode=MODE_DIR, times=None):
    ent, got = py_psu(out.read_bytes())
    assert name_of(ent) == folder, name_of(ent)
    assert struct.unpack_from("<H", ent)[0] == dir_mode
    assert [(name_of(e), d) for e, d in got] == list(files)
    if times is not None:
        created, modified = times
        assert ent[8:16] == created and ent[0x18:0x20] == modified, (ent[8:16], ent[0x18:0x20])
        for e, _ in got:
            assert e[8:16] == created and e[0x18:0x20] == modified


# ---------------------------------------------------------------- Blaze .mcm
def make_mcm(folder_unused, files, title=b"wLaunchELF R3Z"):
    raw = b"".join(n.ljust(32, b"\0") + struct.pack("<L", len(d)) + d for n, d in files)
    body = zlib.compress(raw, 9)
    clusters = sum((len(d) + 1023) // 1024 for _, d in files) + ((len(files) + 2) * 512 + 1023) // 1024
    hdr = struct.pack("<LL", clusters * 1024, 80 + len(body)) + title.ljust(72, b"\0")
    return hdr + body


mcm = tmp / "BASLUS-21050SAVE.mcm"
mcm.write_bytes(make_mcm(FOLDER, FILES))
r, out = conv(mcm, "BASLUS-21050SAVE", "mcm")
assert r == {"format": "2", "r": "0", "name": "BASLUS-21050SAVE", "files": "4", "psu": "0"}, r
check(out, FOLDER, FILES, times=(STAMP, STAMP))                      # no times in the format: the console clock
r, out = conv(mcm, "BASLUS-21050SAVE", "mcm", stamp=False)
check(out, FOLDER, FILES, times=(bytes(8), bytes(8)))                # or zero without a clock
assert run("detect", mcm, "MCM")["format"] == "2" and run("detect", mcm, "psu")["format"] == "0"
assert conv(mcm, "-", "mcm")[0]["r"] == "-16"                       # the folder name comes from the file name
assert conv(mcm, "BAD/NAME", "mcm")[0]["r"] == "-16"
bad = bytearray(make_mcm(FOLDER, FILES))
struct.pack_into("<L", bad, 4, len(bad) + 1)
(tmp / "bad.mcm").write_bytes(bad)
assert conv(tmp / "bad.mcm", "X", "mcm")[0]["r"] == "-16"            # size field disagrees with the file
(tmp / "cut.mcm").write_bytes(make_mcm(FOLDER, FILES)[:-40])
assert conv(tmp / "cut.mcm", "X", "mcm")[0]["r"] == "-16"            # cut zlib stream
raw = zlib.compress(b"name".ljust(32, b"\0") + struct.pack("<L", 999) + b"short", 9)
(tmp / "over.mcm").write_bytes(struct.pack("<LL", 1024, 80 + len(raw)) + b"\0" * 72 + raw)
assert conv(tmp / "over.mcm", "X", "mcm")[0]["r"] == "-16"           # record longer than the data
dup = make_mcm(FOLDER, [(b"same", b"1"), (b"same", b"2")])
(tmp / "dup.mcm").write_bytes(dup)
assert conv(tmp / "dup.mcm", "X", "mcm")[0]["r"] == "-16"            # duplicate names

sample = os.environ.get("BLAZE_MCM_SAMPLE")
if sample and Path(sample).exists():
    r, out = conv(sample, Path(sample).stem, "mcm")
    assert r["r"] == "0", r
    ent, got = py_psu(out.read_bytes())
    d = zlib.decompress(Path(sample).read_bytes()[80:])
    print(f"  real Blaze sample: {name_of(ent).decode()} with {len(got)} files, {len(d)} bytes, OK")


# ---------------------------------------------------------------- MAX Drive .max (LZARI)
class Lzari:
    """Okumura's LZARI encoder (LZARI.C), greedy matching; just enough to make test input."""
    N, F, THRESHOLD = 4096, 60, 2
    NCHAR = 256 - THRESHOLD + F
    Q1 = 1 << 15
    Q2, Q3, Q4 = 2 * Q1, 3 * Q1, 4 * Q1
    MAX_CUM = Q1 - 1

    def __init__(self):
        n = self.NCHAR
        self.sym_cum = [0] * (n + 1)
        self.sym_freq = [0] * (n + 1)
        self.char_to_sym = [0] * n
        self.sym_to_char = [0] * (n + 1)
        for sym in range(n, 0, -1):
            ch = sym - 1
            self.char_to_sym[ch] = sym
            self.sym_to_char[sym] = ch
            self.sym_freq[sym] = 1
            self.sym_cum[sym - 1] = self.sym_cum[sym] + 1
        self.position_cum = [0] * (self.N + 1)
        for i in range(self.N, 0, -1):
            self.position_cum[i - 1] = self.position_cum[i] + 10000 // (i + 200)
        self.low, self.high, self.shifts = 0, self.Q4, 0
        self.bits = []

    def put(self, bit):
        self.bits.append(bit)

    def output(self, bit):
        self.put(bit)
        for _ in range(self.shifts):
            self.put(1 - bit)
        self.shifts = 0

    def update(self, sym):
        if self.sym_cum[0] >= self.MAX_CUM:
            c = 0
            for i in range(self.NCHAR, 0, -1):
                self.sym_cum[i] = c
                self.sym_freq[i] = (self.sym_freq[i] + 1) >> 1
                c += self.sym_freq[i]
            self.sym_cum[0] = c
        i = sym
        while self.sym_freq[i] == self.sym_freq[i - 1]:
            i -= 1
        if i < sym:
            ci, cs = self.sym_to_char[i], self.sym_to_char[sym]
            self.sym_to_char[i], self.sym_to_char[sym] = cs, ci
            self.char_to_sym[ci], self.char_to_sym[cs] = sym, i
        self.sym_freq[i] += 1
        i -= 1
        while i >= 0:
            self.sym_cum[i] += 1
            i -= 1

    def narrow(self):
        while True:
            if self.high <= self.Q2:
                self.output(0)
            elif self.low >= self.Q2:
                self.output(1)
                self.low -= self.Q2
                self.high -= self.Q2
            elif self.low >= self.Q1 and self.high <= self.Q3:
                self.shifts += 1
                self.low -= self.Q1
                self.high -= self.Q1
            else:
                break
            self.low += self.low
            self.high += self.high

    def encode_char(self, ch):
        sym = self.char_to_sym[ch]
        rng = self.high - self.low
        self.high = self.low + rng * self.sym_cum[sym - 1] // self.sym_cum[0]
        self.low += rng * self.sym_cum[sym] // self.sym_cum[0]
        self.narrow()
        self.update(sym)

    def encode_position(self, pos):
        rng = self.high - self.low
        self.high = self.low + rng * self.position_cum[pos] // self.position_cum[0]
        self.low += rng * self.position_cum[pos + 1] // self.position_cum[0]
        self.narrow()

    def encode(self, data):
        N, F = self.N, self.F
        ring = bytearray(b" " * (N - F) + b"\0" * F)
        r, cur = N - F, 0
        while cur < len(data):
            best_len, best_d = 0, 0
            limit = min(F, len(data) - cur)
            for d in range(1, N):
                k = 0
                while k < limit:
                    src = data[cur + k - d] if k - d >= 0 else ring[(r - d + k) & (N - 1)]
                    if src != data[cur + k]:
                        break
                    k += 1
                if k > best_len:
                    best_len, best_d = k, d
                    if k == limit:
                        break
            if best_len > self.THRESHOLD:
                self.encode_char(255 - self.THRESHOLD + best_len)
                self.encode_position(best_d - 1)
                n = best_len
            else:
                self.encode_char(data[cur])
                n = 1
            for k in range(n):
                ring[r] = data[cur + k]
                r = (r + 1) & (N - 1)
            cur += n
        self.shifts += 1
        self.output(0 if self.low < self.Q1 else 1)
        bits = self.bits + [0] * (-len(self.bits) % 8)
        return bytes(int("".join(map(str, bits[i:i + 8])), 2) for i in range(0, len(bits), 8))


def make_max(folder, files, iconsys=b"Test save", clen_is_length=False):
    raw = b""
    for n, d in files:
        raw += struct.pack("<L", len(d)) + n.ljust(32, b"\0") + d
        raw += b"\0" * (((len(raw) + 8 + 15) & ~15) - 8 - len(raw))
    comp = Lzari().encode(raw)
    clen = len(raw) if clen_is_length else len(comp) + 4
    hdr = struct.pack("<12sL32s32sLLL", b"Ps2PowerSave", 0, folder, iconsys, clen, len(files), len(raw))
    return hdr + comp, raw


# the decoder alone, on the encoder's output, including long repeats and the ring wrapping
sample_text = (b"The quick brown fox jumps over the lazy dog. " * 40 + bytes(range(256)) * 20 + b"A" * 5000
               + b"".join(bytes([(i * 37) & 0xFF, (i // 3) & 0xFF]) for i in range(3000)))
comp = Lzari().encode(sample_text)
(tmp / "s.lz").write_bytes(comp)
assert len(comp) < len(sample_text)
assert run("lzari", tmp / "s.lz", len(sample_text), tmp / "s.out")["r"] == "0"
assert (tmp / "s.out").read_bytes() == sample_text
assert run("lzari", tmp / "s.lz", len(sample_text) + 2000, tmp / "s.out2")["r"] == "-1"   # asks for more than there is

mx, raw = make_max(FOLDER, FILES)
(tmp / "t.max").write_bytes(mx)
r, out = conv(tmp / "t.max")
assert r == {"format": "3", "r": "0", "name": "BASLUS-21050SAVE", "files": "4", "psu": "0"}, r
check(out, FOLDER, FILES, times=(STAMP, STAMP))
mx2, _ = make_max(FOLDER, FILES, clen_is_length=True)                 # the "uncompressed size in clen" variant
(tmp / "t2.max").write_bytes(mx2)
check(conv(tmp / "t2.max")[1], FOLDER, FILES)
assert run("detect", tmp / "t.max")["format"] == "3"
(tmp / "cut.max").write_bytes(mx[:len(mx) // 2])
assert conv(tmp / "cut.max")[0]["r"] == "-16"                         # cut stream
bad = bytearray(mx)
struct.pack_into("<L", bad, 0x54, 9)                                  # claims more files than there are
(tmp / "bad.max").write_bytes(bad)
assert conv(tmp / "bad.max")[0]["r"] == "-16"
bad = bytearray(mx)
bad[0x10:0x30] = b"BAD*NAME".ljust(32, b"\0")
(tmp / "badname.max").write_bytes(bad)
assert conv(tmp / "badname.max")[0]["r"] == "-16"
# the last record's padding stripped and one record too many claimed: alignment must not step past the data
raw_np = b""
for n, d in FILES:
    rec = struct.pack("<L", len(d)) + n.ljust(32, b"\0") + d
    raw_np += rec
    if (n, d) != FILES[-1]:
        raw_np += b"\0" * (((len(raw_np) + 8 + 15) & ~15) - 8 - len(raw_np))
assert ((len(raw_np) + 8 + 15) & ~15) - 8 > len(raw_np)                # padding really is missing at the end
comp_np = Lzari().encode(raw_np)
hdr = struct.pack("<12sL32s32sLLL", b"Ps2PowerSave", 0, FOLDER, b"Test save", len(comp_np) + 4, len(FILES) + 1, len(raw_np))
(tmp / "nopad.max").write_bytes(hdr + comp_np)
assert conv(tmp / "nopad.max")[0]["r"] == "-16"


# ---------------------------------------------------------------- CodeBreaker .cbs
# mymc's PS2SAVE_CBS_RC4S (ps2save.py, Ross Ridge, public domain), copied independently of the C table
RC4S = [0x5f, 0x1f, 0x85, 0x6f, 0x31, 0xaa, 0x3b, 0x18, 0x21, 0xb9, 0xce, 0x1c, 0x07, 0x4c, 0x9c, 0xb4,
        0x81, 0xb8, 0xef, 0x98, 0x59, 0xae, 0xf9, 0x26, 0xe3, 0x80, 0xa3, 0x29, 0x2d, 0x73, 0x51, 0x62,
        0x7c, 0x64, 0x46, 0xf4, 0x34, 0x1a, 0xf6, 0xe1, 0xba, 0x3a, 0x0d, 0x82, 0x79, 0x0a, 0x5c, 0x16,
        0x71, 0x49, 0x8e, 0xac, 0x8c, 0x9f, 0x35, 0x19, 0x45, 0x94, 0x3f, 0x56, 0x0c, 0x91, 0x00, 0x0b,
        0xd7, 0xb0, 0xdd, 0x39, 0x66, 0xa1, 0x76, 0x52, 0x13, 0x57, 0xf3, 0xbb, 0x4e, 0xe5, 0xdc, 0xf0,
        0x65, 0x84, 0xb2, 0xd6, 0xdf, 0x15, 0x3c, 0x63, 0x1d, 0x89, 0x14, 0xbd, 0xd2, 0x36, 0xfe, 0xb1,
        0xca, 0x8b, 0xa4, 0xc6, 0x9e, 0x67, 0x47, 0x37, 0x42, 0x6d, 0x6a, 0x03, 0x92, 0x70, 0x05, 0x7d,
        0x96, 0x2f, 0x40, 0x90, 0xc4, 0xf1, 0x3e, 0x3d, 0x01, 0xf7, 0x68, 0x1e, 0xc3, 0xfc, 0x72, 0xb5,
        0x54, 0xcf, 0xe7, 0x41, 0xe4, 0x4d, 0x83, 0x55, 0x12, 0x22, 0x09, 0x78, 0xfa, 0xde, 0xa7, 0x06,
        0x08, 0x23, 0xbf, 0x0f, 0xcc, 0xc1, 0x97, 0x61, 0xc5, 0x4a, 0xe6, 0xa0, 0x11, 0xc2, 0xea, 0x74,
        0x02, 0x87, 0xd5, 0xd1, 0x9d, 0xb7, 0x7e, 0x38, 0x60, 0x53, 0x95, 0x8d, 0x25, 0x77, 0x10, 0x5e,
        0x9b, 0x7f, 0xd8, 0x6e, 0xda, 0xa2, 0x2e, 0x20, 0x4f, 0xcd, 0x8f, 0xcb, 0xbe, 0x5a, 0xe0, 0xed,
        0x2c, 0x9a, 0xd4, 0xe2, 0xaf, 0xd0, 0xa9, 0xe8, 0xad, 0x7a, 0xbc, 0xa8, 0xf2, 0xee, 0xeb, 0xf5,
        0xa6, 0x99, 0x28, 0x24, 0x6c, 0x2b, 0x75, 0x5d, 0xf8, 0xd3, 0x86, 0x17, 0xfb, 0xc0, 0x7b, 0xb3,
        0x58, 0xdb, 0xc7, 0x4b, 0xff, 0x04, 0x50, 0xe9, 0x88, 0x69, 0xc9, 0x2a, 0xab, 0xfd, 0x5b, 0x1b,
        0x8a, 0xd9, 0xec, 0x27, 0x44, 0x0e, 0x33, 0xc8, 0x6b, 0x93, 0x32, 0x48, 0xb6, 0x30, 0x43, 0xa5]
src_c = (root / "src/mc_save_formats.c").read_text(encoding="utf-8")
a = src_c.index("cbs_rc4[256] = {")
table = [int(x, 16) for x in __import__("re").findall(r"0x[0-9a-f]{2}", src_c[a:src_c.index("};", a)])]
assert len(table) == 256 and len(set(table)) == 256
assert table == RC4S, "cbs_rc4 in src/mc_save_formats.c differs from mymc's table"


def rc4(data):
    s = list(RC4S)
    out = bytearray(data)
    j = 0
    for ii in range(len(out)):
        i = (ii + 1) % 256
        j = (j + s[i]) % 256
        s[i], s[j] = s[j], s[i]
        out[ii] ^= s[(s[i] + s[j]) % 256]
    return bytes(out)


def make_cbs(folder, files, created=T1, modified=T2, dirmode=MODE_DIR, filemode=MODE_FILE, flen_total=True,
             title=b"Test title"):
    body = b""
    for n, d in files:
        body += struct.pack("<8s8sLHHLL32s", created, modified, len(d), filemode, 0, 0, 0, n) + d
    comp = rc4(zlib.compress(body, 9))
    hlen = max(92 + 32, 92 + len(title) + 1)                          # the title field is at least 32 bytes
    hlen += -hlen % 16
    total = hlen + len(comp)
    hdr = struct.pack("<4sLLLL32s8s8sLLLLLL", b"CFU\0", 0x1D, hlen, len(body), total if flen_total else len(comp),
                      folder, created, modified, 0, 0, dirmode, 0, 0, 0) + title.ljust(hlen - 92, b"\0")
    return hdr + comp


(tmp / "t.cbs").write_bytes(make_cbs(FOLDER, FILES))
r, out = conv(tmp / "t.cbs")
assert r == {"format": "4", "r": "0", "name": "BASLUS-21050SAVE", "files": "4", "psu": "0"}, r
check(out, FOLDER, FILES, times=(T1, T2))                              # the format's own times survive
(tmp / "t2.cbs").write_bytes(make_cbs(FOLDER, FILES, flen_total=False))
check(conv(tmp / "t2.cbs")[1], FOLDER, FILES, times=(T1, T2))
(tmp / "t3.cbs").write_bytes(make_cbs(FOLDER, FILES, created=bytes(8), modified=bytes(8), dirmode=0))
check(conv(tmp / "t3.cbs")[1], FOLDER, FILES, times=(STAMP, STAMP))    # zero times and a bad mode: defaults
(tmp / "sub.cbs").write_bytes(make_cbs(FOLDER, FILES, filemode=MODE_DIR))
assert conv(tmp / "sub.cbs")[0]["r"] == "-13"                          # a sub-folder: unsupported, as in .psu
cbs = make_cbs(FOLDER, FILES)
(tmp / "cut.cbs").write_bytes(cbs[:-20])
assert conv(tmp / "cut.cbs")[0]["r"] == "-16"
bad = bytearray(cbs)
bad[-1] ^= 0xFF
(tmp / "flip.cbs").write_bytes(bad)
assert conv(tmp / "flip.cbs")[0]["r"] == "-16"                         # a flipped byte breaks the zlib stream
assert run("detect", tmp / "t.cbs")["format"] == "4"


# ---------------------------------------------------------------- SharkPort / X-Port .sps
def make_sps(folder, files, created=T1, modified=T2, dirmode=MODE_DIR, filemode=MODE_FILE, pad=0):
    def swap(m):
        return ((m & 0xFF) << 8) | (m >> 8)

    def lstr(s):
        return struct.pack("<L", len(s)) + s

    def ent(name, length, mode, c, m):
        return struct.pack("<H64sL8xH2x8s8s", 98 + pad, name, length, swap(mode), c, m) + b"\0" * pad

    body = ent(folder, len(files) + 2, dirmode, created, modified)
    for n, d in files:
        body += ent(n, len(d), filemode, created, modified) + d
    body += b"\0\0\0\0"                                                 # the checksum, ignored
    return (b"\x0d\0\0\0SharkPortSave" + struct.pack("<L", 2) + lstr(folder) + lstr(b"2026/10/29") + lstr(b"a comment")
            + struct.pack("<L", len(body)) + body)


(tmp / "t.sps").write_bytes(make_sps(FOLDER, FILES))
r, out = conv(tmp / "t.sps")
assert r == {"format": "5", "r": "0", "name": "BASLUS-21050SAVE", "files": "4", "psu": "0"}, r
check(out, FOLDER, FILES, times=(T1, T2))
(tmp / "t.xps").write_bytes(make_sps(FOLDER, FILES, pad=6))            # longer entry headers are skipped
check(conv(tmp / "t.xps")[1], FOLDER, FILES, times=(T1, T2))
(tmp / "sub.sps").write_bytes(make_sps(FOLDER, FILES, filemode=MODE_DIR))
assert conv(tmp / "sub.sps")[0]["r"] == "-13"
sps = make_sps(FOLDER, FILES)
(tmp / "cut.sps").write_bytes(sps[:-300])
assert conv(tmp / "cut.sps")[0]["r"] == "-16"
assert run("detect", tmp / "t.sps")["format"] == "5"


# ---------------------------------------------------------------- PS3 .psv
def make_psv(folder, files, created=T1, modified=T2, dirmode=MODE_DIR, filemode=MODE_FILE, save_type=2):
    hdr = struct.pack("<4sL20s20sLLLL", b"\0VSP", 0, b"salt" * 5, b"\0" * 20, 0, 0, 0x2C, save_type)
    table_end = 0xA0 + 60 * len(files)
    data, recs, pos = b"", b"", table_end
    icons = {}
    for n, d in files:
        recs += struct.pack("<8s8sLL32sL", created, modified, len(d), filemode, n, pos)
        icons[n] = (pos, len(d))
        data += d
        pos += len(d)
    sysp, syss = icons.get(b"icon.sys", (0, 0))
    ps2h = struct.pack("<10L", len(data), sysp, syss, 0, 0, 0, 0, 0, 0, len(files))
    maindir = struct.pack("<8s8sLL32s", created, modified, len(files) + 2, dirmode, folder)
    out = hdr + ps2h + maindir + recs + data
    assert len(hdr) == 0x40 and len(hdr + ps2h) == 0x68 and len(hdr + ps2h + maindir) == 0xA0
    return out


(tmp / "t.psv").write_bytes(make_psv(FOLDER, FILES))
r, out = conv(tmp / "t.psv")
assert r == {"format": "6", "r": "0", "name": "BASLUS-21050SAVE", "files": "4", "psu": "0"}, r
check(out, FOLDER, FILES, times=(T1, T2))
(tmp / "ps1.psv").write_bytes(make_psv(FOLDER, FILES, save_type=1))
assert conv(tmp / "ps1.psv")[0] == {"format": "6", "r": "-13"}       # a PS1 save is recognised and declined
psv = make_psv(FOLDER, FILES)
(tmp / "cut.psv").write_bytes(psv[:-5])
assert conv(tmp / "cut.psv")[0]["r"] == "-16"                          # a file's data runs past the end
bad = bytearray(psv)
struct.pack_into("<L", bad, 0x64, 5)
(tmp / "bad.psv").write_bytes(bad)
assert conv(tmp / "bad.psv")[0]["r"] == "-16"
assert run("detect", tmp / "t.psv")["format"] == "6"

# ---------------------------------------------------------------- .psu passes straight through
psu = out.read_bytes()
(tmp / "t.psu").write_bytes(psu)
r, out2 = conv(tmp / "t.psu")
assert r["format"] == "1" and r["r"] == "0" and out2.read_bytes() == psu
assert run("detect", tmp / "t.psu")["format"] == "1"
(tmp / "junk.bin").write_bytes(b"\xFF" * 4000)
assert conv(tmp / "junk.bin")[0] == {"format": "0", "r": "-16"}
assert run("detect", tmp / "junk.bin", "mcm")["format"] == "0"

print("Save formats: Blaze .mcm, MAX Drive .max (LZARI), CodeBreaker .cbs (RC4+zlib), SharkPort/X-Port .sps/.xps "
      "and PS3 .psv read into saves and written as .psu with names, data, modes and times intact; cut, oversized, "
      "misnamed, duplicate, sub-folder and PS1 inputs refused whole: OK")
