"""Minimal ELF32/ELF64 little-endian reader (sections + symbols) and ar archive walker.

Pure Python so the evidence scripts do not need one binutils process per object.
"""
import struct

SHT_SYMTAB = 2
SHT_NOBITS = 8
SHF_ALLOC = 2
SHF_WRITE = 1
SHF_EXEC = 4
SHN_UNDEF = 0
SHN_ABS = 0xFFF1
SHN_COMMON = 0xFFF2
STB = {0: 'LOCAL', 1: 'GLOBAL', 2: 'WEAK'}
STT = {0: 'NOTYPE', 1: 'OBJECT', 2: 'FUNC', 3: 'SECTION', 4: 'FILE', 5: 'COMMON', 6: 'TLS'}


def _cstr(buf, off):
    end = buf.find(b'\0', off)
    return buf[off:end].decode('latin-1')


class Elf:
    def __init__(self, data, name=''):
        self.name = name
        self.data = data
        if data[:4] != b'\x7fELF':
            raise ValueError('not ELF: ' + name)
        self.is64 = data[4] == 2
        if data[5] != 1:
            raise ValueError('big endian not supported')
        if self.is64:
            (self.e_type, self.e_machine, _v, self.e_entry, self.e_phoff, self.e_shoff, self.e_flags,
             _ehs, self.e_phentsize, self.e_phnum, self.e_shentsize, self.e_shnum, self.e_shstrndx) = \
                struct.unpack_from('<HHIQQQIHHHHHH', data, 16)
        else:
            (self.e_type, self.e_machine, _v, self.e_entry, self.e_phoff, self.e_shoff, self.e_flags,
             _ehs, self.e_phentsize, self.e_phnum, self.e_shentsize, self.e_shnum, self.e_shstrndx) = \
                struct.unpack_from('<HHIIIIIHHHHHH', data, 16)
        self.sections = []
        for i in range(self.e_shnum):
            off = self.e_shoff + i * self.e_shentsize
            if self.is64:
                n, t, fl, addr, o, sz, link, info, al, es = struct.unpack_from('<IIQQQQIIQQ', data, off)
            else:
                n, t, fl, addr, o, sz, link, info, al, es = struct.unpack_from('<IIIIIIIIII', data, off)
            self.sections.append(dict(idx=i, name_off=n, type=t, flags=fl, addr=addr, offset=o, size=sz,
                                      link=link, info=info, align=al, entsize=es))
        if self.sections and self.e_shstrndx < len(self.sections):
            st = self.sections[self.e_shstrndx]
            strtab = data[st['offset']:st['offset'] + st['size']]
            for s in self.sections:
                s['name'] = _cstr(strtab, s['name_off'])
        self.segments = []
        for i in range(self.e_phnum):
            off = self.e_phoff + i * self.e_phentsize
            if self.is64:
                t, fl, o, va, pa, fs, ms, al = struct.unpack_from('<IIQQQQQQ', data, off)
            else:
                t, o, va, pa, fs, ms, fl, al = struct.unpack_from('<IIIIIIII', data, off)
            self.segments.append(dict(type=t, offset=o, vaddr=va, paddr=pa, filesz=fs, memsz=ms, flags=fl, align=al))
        self._symbols = None

    @property
    def symbols(self):
        if self._symbols is not None:
            return self._symbols
        out = []
        for s in self.sections:
            if s['type'] != SHT_SYMTAB:
                continue
            strs = self.sections[s['link']]
            strtab = self.data[strs['offset']:strs['offset'] + strs['size']]
            ent = s['entsize'] or (24 if self.is64 else 16)
            for i in range(s['size'] // ent):
                off = s['offset'] + i * ent
                if self.is64:
                    n, info, other, shndx, val, sz = struct.unpack_from('<IBBHQQ', self.data, off)
                else:
                    n, val, sz, info, other, shndx = struct.unpack_from('<IIIBBH', self.data, off)
                out.append(dict(name=_cstr(strtab, n), value=val, size=sz, bind=STB.get(info >> 4, str(info >> 4)),
                                type=STT.get(info & 15, str(info & 15)), shndx=shndx, other=other))
        self._symbols = out
        return out

    def read_vaddr(self, vaddr, length):
        for s in self.sections:
            if s['type'] != SHT_NOBITS and s['flags'] & SHF_ALLOC and s['addr'] <= vaddr < s['addr'] + s['size']:
                o = s['offset'] + (vaddr - s['addr'])
                return self.data[o:o + length]
        return None


def ar_members(path):
    """Yield (member_name, bytes) for a System V / GNU ar archive."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != b'!<arch>\n':
        raise ValueError('not an ar archive: ' + path)
    pos = 8
    longnames = b''
    while pos + 60 <= len(data):
        hdr = data[pos:pos + 60]
        name = hdr[:16].decode('latin-1').rstrip()
        size = int(hdr[48:58].decode().strip())
        body = data[pos + 60:pos + 60 + size]
        pos += 60 + size + (size & 1)
        if name == '/' or name == '/SYM64/':
            continue
        if name == '//':
            longnames = body
            continue
        if name.startswith('/') and name[1:].isdigit():
            o = int(name[1:])
            end = longnames.find(b'\n', o)
            name = longnames[o:end].decode('latin-1').rstrip('/')
        elif name.startswith('#1/'):
            n = int(name[3:])
            name = body[:n].rstrip(b'\0').decode('latin-1')
            body = body[n:]
        else:
            name = name.rstrip('/')
        yield name, body
