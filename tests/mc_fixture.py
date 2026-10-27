"""Writes PS2 memory card images for the card viewer tests.

Layout follows Ridge's format description and the constants in mymc's
public-domain format() (8 MB: indirect FAT cluster 8, 32 FAT clusters from 9,
alloc_offset 41, alloc_end 8135, free FAT entries 0x7FFFFFFF, chain end
0xFFFFFFFF, root "." mode 0x8427 length 2). The reader under test is a
separate implementation in C; this file is never linked into it.
"""
import struct

CL = 1024
EPC = 256
MODE_DIR = 0x8427        # rwx | dir | 0x0400 | exists
MODE_FILE = 0x8417       # rwx | file | 0x0400 | exists
MODE_PS1 = 0x1000
END = 0xFFFFFFFF
FREE = 0x7FFFFFFF
MAGIC = b"Sony PS2 Memory Card Format "


def dirent(mode, length, cluster, name):
    raw = struct.pack("<HHL8sLL8sL28x32s", mode, 0, length, bytes(8), cluster, 0, bytes(8), 0,
                      name.encode("ascii"))
    return raw + bytes(512 - len(raw))


def icon_sys(title_sjis: bytes, brk: int) -> bytes:
    b = bytearray(964)
    b[0:4] = b"PS2D"
    b[6:8] = struct.pack("<H", brk)
    b[192:192 + len(title_sjis)] = title_sjis
    b[260:260 + 8] = b"icon.icn"
    return bytes(b)


class Card:
    def __init__(self, mb=8, ecc=False):
        self.mb, self.ecc = mb, ecc
        self.n = mb * 1024
        first_ifc = 8
        alloc = self.n - (first_ifc + 2)
        fat_clusters = -(-alloc // EPC)
        ind = -(-fat_clusters // EPC)
        assert ind <= 32
        self.first_ifc, self.fat_clusters, self.ind = first_ifc, fat_clusters, ind
        self.alloc_offset = first_ifc + ind + fat_clusters
        blocks = self.n // 8
        self.good1, self.good2 = blocks - 1, blocks - 2
        self.alloc_end = self.good2 * 8 - self.alloc_offset
        self.fat = [END] * (fat_clusters * EPC)
        for i in range(1, self.alloc_end):
            self.fat[i] = FREE
        self.data = {}          # relative cluster -> 1024 bytes
        self.next = 1
        self.root = []          # (mode, length, cluster, name)

    def alloc_chain(self, count, first=None):
        cl = [first] if first is not None else []
        while len(cl) < count:
            cl.append(self.next)
            self.next += 1
        for a, b in zip(cl, cl[1:]):
            self.fat[a] = 0x80000000 | b
        self.fat[cl[-1]] = END
        return cl

    def write_chain(self, clusters, payload):
        for i, c in enumerate(clusters):
            chunk = payload[i * CL:(i + 1) * CL]
            self.data[c] = chunk + bytes(CL - len(chunk))

    def add_file(self, payload):
        if not payload:
            return END
        cl = self.alloc_chain(-(-len(payload) // CL))
        self.write_chain(cl, payload)
        return cl[0]

    def add_save(self, name, files, ps1=False):
        ents = [(MODE_DIR, 2, 0, "."), (MODE_DIR, 0, 0, "..")]
        for fname, payload in files.items():
            ents.append((MODE_FILE, len(payload), self.add_file(payload), fname))
        n = len(ents)
        dcl = self.alloc_chain(-(-n // 2))
        ents[0] = (MODE_DIR, 2, dcl[0], ".")
        raw = b"".join(dirent(*e) for e in ents)
        self.write_chain(dcl, raw)
        mode = MODE_DIR | (MODE_PS1 if ps1 else 0)
        self.root.append((mode, n, dcl[0], name))
        return dcl[0]

    def finish(self):
        ents = [(MODE_DIR, 2 + len(self.root), 0, "."), (MODE_DIR, 0, 0, "..")] + self.root
        count = -(-len(ents) // 2)
        rcl = self.alloc_chain(count, first=0)
        self.write_chain(rcl, b"".join(dirent(*e) for e in ents))
        return self

    def image(self, **kw):
        sb = bytearray(512)
        ifc = [self.first_ifc + i for i in range(self.ind)] + [0] * (32 - self.ind)
        bad = [0xFFFFFFFF] * 32
        hdr = struct.pack("<28s12sHHHHLLLLLL8x128s128sbbxx", MAGIC, b"1.2.0.0", 512, 2, 16, 0xFF00,
                          self.n, self.alloc_offset, self.alloc_end, 0, self.good1, self.good2,
                          struct.pack("<32L", *ifc), struct.pack("<32L", *bad), 2, 0x52)
        hdr = bytearray(hdr)
        for k, v in kw.get("patch", {}).items():     # offset -> bytes, for hostile images
            hdr[k:k + len(v)] = v
        sb[:len(hdr)] = hdr
        clusters = [bytes(CL)] * self.n
        clusters[0] = bytes(sb) + bytes(CL - 512)
        ind_buf = [self.first_ifc + self.ind + i for i in range(self.fat_clusters)]
        ind_buf += [END] * (EPC - len(ind_buf))
        clusters[self.first_ifc] = struct.pack("<256L", *ind_buf[:EPC])
        for i in range(self.fat_clusters):
            clusters[self.first_ifc + self.ind + i] = struct.pack(
                "<256L", *self.fat[i * EPC:(i + 1) * EPC])
        for rel, blob in self.data.items():
            clusters[self.alloc_offset + rel] = blob
        raw = b"".join(clusters)
        if self.ecc:
            raw = b"".join(raw[i:i + 512] + bytes(16) for i in range(0, len(raw), 512))
        return raw


def standard_card(mb=8, ecc=False):
    sjis = "セーブデータ".encode("shift_jis")
    c = Card(mb, ecc)
    c.add_save("BESLES-12345GAME", {
        "icon.sys": icon_sys(b"Game Slot 1", 4), "icon.icn": bytes(range(256)) * 12,
        "BESLES-12345GAME": b"x" * 3000})
    c.add_save("BASLUS-99999", {"icon.sys": icon_sys(sjis, 6), "data.bin": b"y" * 1500})
    c.add_save("BISLPS-00001", {"empty.dat": b"", "note.txt": b"z" * 10}, ps1=True)
    c.add_save("NOICON", {"a": b"a" * 1024, "b": b"b" * 1025})
    return c.finish(), {
        "saves": [
            ("BESLES-12345GAME", 3, 964 + 3072 + 3000, True, False),
            ("BASLUS-99999", 2, 964 + 1500, True, False),
            ("BISLPS-00001", 2, 10, False, True),
            ("NOICON", 2, 2049, False, False)],
        "titles": {"BESLES-12345GAME": (b"Game Slot 1", 4), "BASLUS-99999": (sjis, 6)}}
