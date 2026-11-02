#!/usr/bin/env python3
"""Fail if a function stores a register to its stack before it realigns $sp.

A function with a local aligned beyond 16 bytes (FILEINFO embeds libmc's
64-byte-aligned sceMcTblGetDir) starts like this:

    allocate the frame      addiu/daddiu $sp,$sp,-N  or  subu/dsubu $sp,$sp,$rX
    save callee-saved regs  sd $16..$23/$gp/$fp/$ra, off($sp)
    keep the entry frame    move $fp,$sp
    realign                 and $sp,$sp,$rX          ($rX holds -64)

A store relative to $sp in that window lands at the address before the
realignment, while the code after it finds the frame from the realigned $sp.
Callee-saved registers are fine there: the epilogue does `move $sp,$fp`
before it reloads them. Anything else is lost. In release builds since #210,
the ps2build Clang register scavenger put such a store in copy()
(src/filer_copy.c): it saved the argument register holding bytes 8-15 of the
by-value FILEINFO name there and reloaded it from the realigned frame, so a
pasted "RIPTOPL.ELF" was opened as "RIPTOPL.".

This checks every function in the given ELF files, linked or relocatable, and
exits 1 if any has such a store. It needs no toolchain: it decodes the ELF
and the few MIPS instructions involved itself.
"""

import argparse
import pathlib
import struct
import sys

SP, FP = 29, 30
CALLEE_SAVED = frozenset(range(16, 24)) | {28, 30, 31}  # $s0-$s7, $gp, $fp, $ra
CALLEE_SAVED_FPR = frozenset(range(20, 32))  # widest set any MIPS ABI saves
BIG_FRAME = 0x8000  # offsets past this need a scratch register

GPR_STORES = {0x28: "sb", 0x29: "sh", 0x2A: "swl", 0x2B: "sw", 0x2C: "sdl",
              0x2D: "sdr", 0x2E: "swr", 0x3F: "sd", 0x1F: "sq"}  # 0x1F is sq on the R5900
FPR_STORES = {0x39: "swc1", 0x3D: "sdc1"}
# Opcodes whose rt is a destination register.
WRITES_RT = {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x18, 0x19,  # ALU immediate
             0x1A, 0x1B, 0x1E, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,  # loads
             0x27, 0x37}

SHT_PROGBITS, SHT_SYMTAB = 1, 2
SHF_EXECINSTR = 0x4
STT_FUNC = 2
ET_REL = 1
EM_MIPS = 8


def fields(word):
    """op, rs, rt, rd, funct and the sign-extended 16-bit immediate."""
    imm = word & 0xFFFF
    return (word >> 26, (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31,
            word & 0x3F, imm - 0x10000 if imm & 0x8000 else imm)


def destination(word):
    """The general register an instruction writes, or None."""
    op, rs, rt, rd, funct, _ = fields(word)
    if op == 0:
        return None if funct in (0x08, 0x0C, 0x0D, 0x0F) else rd  # jr, syscall, break, sync
    if op == 0x1C:  # R5900 MMI: all write rd
        return rd
    if op == 0x03:  # jal
        return 31
    if op in (0x10, 0x11) and rs in (0, 1, 2):  # mfc0, mfc1/dmfc1, cfc1
        return rt
    if op in WRITES_RT:
        return rt
    return None


def realigns_sp(word):
    op, rs, rt, rd, funct, _ = fields(word)
    return op == 0 and funct == 0x24 and rd == SP and SP in (rs, rt)


class Function:
    def __init__(self, name, address, words):
        self.name, self.address, self.words = name, address, words


def read_functions(path):
    """Yield the functions of an ELF32 little-endian MIPS file."""
    data = path.read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise ValueError("not a 32-bit little-endian ELF file")
    (e_type, e_machine, _, _, _, e_shoff, _, _, _, _, e_shentsize, e_shnum,
     _) = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    if e_machine != EM_MIPS:
        raise ValueError("not a MIPS ELF file")
    sections = [struct.unpack_from("<10I", data, e_shoff + i * e_shentsize) for i in range(e_shnum)]

    def code(index):
        section = sections[index] if 0 < index < len(sections) else None
        if section and section[1] == SHT_PROGBITS and section[2] & SHF_EXECINSTR:
            return section
        return None

    def words(section, start, size):
        offset = section[4] + start - (0 if e_type == ET_REL else section[3])
        if start < (0 if e_type == ET_REL else section[3]) or offset + size > section[4] + section[5]:
            raise ValueError("function outside its section")
        return struct.unpack_from("<%dI" % (size // 4), data, offset)

    found = {}
    for symtab in (s for s in sections if s[1] == SHT_SYMTAB):
        strtab = sections[symtab[6]]
        for offset in range(symtab[4], symtab[4] + symtab[5], 16):
            name, value, size, info, _, shndx = struct.unpack_from("<IIIBBH", data, offset)
            section = code(shndx)
            if info & 15 != STT_FUNC or not size or section is None:
                continue
            key = (shndx, value, size)
            if key in found:
                continue
            end = data.index(b"\0", strtab[4] + name)
            label = data[strtab[4] + name:end].decode("ascii", "replace")
            found[key] = Function(label, value & ~3, words(section, value & ~3, size & ~3))
    if found:
        return list(found.values())
    # Stripped: scan each code section as one stream.
    return [Function("<%s>" % hex(s[3]), s[3] if e_type != ET_REL else 0,
                     words(s, s[3] if e_type != ET_REL else 0, s[5] & ~3))
            for i, s in enumerate(sections) if code(i)]


def frame_size(words, index, constants):
    """Bytes allocated by words[index] if it allocates the stack frame, else None."""
    op, rs, rt, rd, funct, imm = fields(words[index])
    if op in (0x08, 0x09, 0x18, 0x19) and rs == SP and rt == SP and imm < 0:
        return -imm
    if op == 0 and rd == SP and rs == SP and funct in (0x22, 0x23, 0x2E, 0x2F):  # sub(u)/dsub(u)
        return constants.get(rt, 0)
    if op == 0 and rd == SP and SP in (rs, rt) and funct in (0x20, 0x21, 0x2C, 0x2D):  # add(u)/dadd(u)
        value = constants.get(rt if rs == SP else rs)
        return -value if value is not None and value < 0 else None
    return None


def track_constant(word, constants):
    op, rs, rt, rd, _, imm = fields(word)
    value = None
    if op == 0x0F:  # lui
        value = ((imm & 0xFFFF) << 16) - (1 << 32 if imm & 0x8000 else 0)
    elif op in (0x09, 0x19) and (rs == 0 or rs in constants):  # addiu/daddiu
        value = constants.get(rs, 0) + imm
    elif op == 0x0D and (rs == 0 or rs in constants):  # ori
        value = constants.get(rs, 0) | (imm & 0xFFFF)
    target = destination(word)
    if target:
        constants.pop(target, None)
        if value is not None:
            constants[target] = value


def check(function):
    """Return (hazards, realignments): each hazard is a printable line, each
    realignment the (address, frame bytes) of an `and $sp` in this function."""
    hazards, realignments = [], []
    words, start, size = function.words, 0, 0
    constants = {}
    for index, word in enumerate(words):
        allocated = frame_size(words, index, constants)
        if allocated is not None:
            start, size = index, allocated
        track_constant(word, constants)
        if not realigns_sp(word):
            continue
        realignments.append((function.address + 4 * index, size))
        derived = set()  # temporaries holding $sp plus an offset, e.g. lui $1; addu $1,$sp,$1
        for position in range(start, index):
            item = words[position]
            op, rs, rt, rd, funct, imm = fields(item)
            store = GPR_STORES.get(op) or FPR_STORES.get(op)
            if store and (rs == SP or rs in derived):
                saved = CALLEE_SAVED_FPR if op in FPR_STORES else CALLEE_SAVED
                if rt not in saved:
                    register = ("$f%d" if op in FPR_STORES else "$%d") % rt
                    hazards.append(
                        "%s at %#x: %s %s,%d($%d) before `and $sp` at %#x (frame %d bytes)"
                        % (function.name, function.address + 4 * position, store, register, imm, rs,
                           function.address + 4 * index, size))
                continue
            target = destination(item)
            if target is None:
                continue
            derived.discard(target)
            from_sp = ((op == 0 and funct in (0x20, 0x21, 0x25, 0x2C, 0x2D) and SP in (rs, rt))
                       or (op in (0x08, 0x09, 0x18, 0x19) and rs == SP))
            if from_sp and target not in (0, SP, FP):
                derived.add(target)
    return hazards, realignments


def scan(path):
    functions = read_functions(path)
    hazards, realigned = [], []
    for function in functions:
        found, realignments = check(function)
        hazards += found
        # A stripped file has no names, so name each realignment by its address.
        realigned += [(function.name if not function.name.startswith("<") else hex(address), size)
                      for address, size in realignments]
    return functions, hazards, realigned


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("elves", nargs="+", type=pathlib.Path)
    args = parser.parse_args(argv)
    failed = False
    for path in args.elves:
        try:
            functions, hazards, realigned = scan(path)
        except (OSError, ValueError, struct.error) as error:
            print("FAIL %s: %s" % (path, error), file=sys.stderr)
            failed = True
            continue
        for line in hazards:
            print("FAIL %s: %s" % (path, line), file=sys.stderr)
        big = sorted({(size, name) for name, size in realigned if size >= BIG_FRAME}, reverse=True)
        if big:
            print("note %s: realigned frames of 32 KB or more: %s" % (
                path, ", ".join("%s (%d KB)" % (name, size // 1024) for size, name in big)))
        if hazards:
            failed = True
        else:
            print("PASS %s: %d functions, %d realign $sp, none stores to the stack before realigning"
                  % (path, len(functions), len({name for name, _ in realigned})))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
