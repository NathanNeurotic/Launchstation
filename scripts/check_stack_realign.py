#!/usr/bin/env python3
"""Fail if a function stores a register to its stack before it realigns $sp.

A function with a local aligned beyond 16 bytes (FILEINFO embeds libmc's
64-byte-aligned sceMcTblGetDir) starts like this:

    allocate the frame      addiu/daddiu $sp,$sp,-N  or  subu/dsubu $sp,$sp,$rX
    save callee-saved regs  sd $16..$23/$gp/$fp/$ra, off($sp)
    keep the entry frame    move $fp,$sp
    realign                 and $sp,$sp,$rX          ($rX holds -64)

A store relative to $sp in that window, or through a register made from $sp
(lui $1; addu $1,$sp,$1), lands at the address before the realignment, while
the code after it finds the frame from the realigned $sp. Saving a
callee-saved register there is fine: the epilogue does `move $sp,$fp` before
it reloads them. Anything else stored there is lost, including a callee-saved
register that the window changed first, since that spills the function's own
value instead of saving the caller's. In a function that also uses alloca or
a variable-length array, Clang reaches the frame through $s7, which it sets up
only after the realignment, so every store through $s7 (or a register made
from it) in the window fails too: it writes wherever the caller's $s7 points.
Stores through $fp use the entry frame on both sides, and stores through any
other register do not address this frame (an argument pointer, say, when the
delay-slot filler moved `and $sp` past the start of the body), so they pass.

In release builds since #210, the ps2build Clang register scavenger put such
a store in copy() (src/filer_copy.c): it saved the argument register holding
bytes 8-15 of the by-value FILEINFO name there and reloaded it from the
realigned frame, so a pasted "RIPTOPL.ELF" was opened as "RIPTOPL.".

This checks every function in the given ELF files, linked or relocatable, and
exits 1 if any has such a store, or if a file has no code to check (no
executable section, or no section headers at all). It needs no toolchain: it
decodes the ELF and the few MIPS instructions involved itself.

With --max-frame NAME=BYTES it also fails unless every file has a function
NAME whose stack frame is at most BYTES. copy() calls itself once per folder
level, up to MAX_RECURSE (16) frames deep, and delete() and getFileSize() once
per level of the folder tree, on the EE main thread's 128 KB stack. A listing
of MAX_ENTRY entries is about 900 KB, so each level keeps its listing on the
heap; a budget catches a listing or other large buffer put back in a frame.

With --max-chain NAME[*COUNT],...=BYTES it adds up the frames of the functions
of a call chain, COUNT frames for NAME*COUNT, and fails unless every file has
them all and the total is at most BYTES. For the deepest known way into a
16-level copy(), from _main through the file browser to the innermost
listing, that catches a frame that grows anywhere on the way, not only in the
functions given a budget. The names are not checked to call each other.
"""

import argparse
import pathlib
import struct
import sys

SP, FP, BP = 29, 30, 23  # BP: the base pointer Clang uses with alloca, $s7
CALLEE_SAVED = frozenset(range(16, 24)) | {28, 30, 31}  # $s0-$s7, $gp, $fp, $ra
CALLEE_SAVED_FPR = frozenset(range(20, 32))  # widest set any MIPS ABI saves
BIG_FRAME = 0x8000  # offsets past this need a scratch register
ADDS = (0x20, 0x21, 0x25, 0x2C, 0x2D)  # add, addu, or (move), dadd, daddu
ADD_IMMEDIATES = (0x08, 0x09, 0x18, 0x19)  # addi, addiu, daddi, daddiu

GPR_STORES = {0x28: "sb", 0x29: "sh", 0x2A: "swl", 0x2B: "sw", 0x2C: "sdl",
              0x2D: "sdr", 0x2E: "swr", 0x3F: "sd", 0x1F: "sq"}  # 0x1F is sq on the R5900
FPR_STORES = {0x39: "swc1", 0x3D: "sdc1"}
# Opcodes whose rt is a destination register.
WRITES_RT = {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x18, 0x19,  # ALU immediate
             0x1A, 0x1B, 0x1E, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,  # loads
             0x27, 0x37}

# `and $sp` with -64 after a 16-byte aligned allocation lowers $sp by up to 48 bytes.
REALIGN_SLACK = 48

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
        frame = {SP}  # $sp and temporaries made from it, e.g. lui $1; addu $1,$sp,$1
        base = {BP}  # the caller's $s7 and temporaries made from it
        changed = set()  # general registers the window has written
        for position in range(start, index):
            item = words[position]
            op, rs, rt, rd, funct, imm = fields(item)
            store = GPR_STORES.get(op) or FPR_STORES.get(op)
            if store:
                if op in FPR_STORES:
                    # Float registers are not tracked as changed: the register
                    # scavenger borrows only general ones, and the prologue saves
                    # float registers before anything uses them.
                    saves = rt in CALLEE_SAVED_FPR
                else:
                    saves = rt in CALLEE_SAVED and rt not in changed
                if rs in base or (rs in frame and not saves):
                    register = ("$f%d" if op in FPR_STORES else "$%d") % rt
                    hazards.append(
                        "%s at %#x: %s %s,%d($%d) before `and $sp` at %#x (frame %d bytes)"
                        % (function.name, function.address + 4 * position, store, register, imm, rs,
                           function.address + 4 * index, size))
                continue
            target = destination(item)
            if target in (None, 0, SP):
                continue
            sources = {rs, rt} if op == 0 and funct in ADDS else {rs} if op in ADD_IMMEDIATES else set()
            from_frame, from_base = bool(sources & frame), bool(sources & base)
            changed.add(target)
            frame.discard(target)
            base.discard(target)
            if target != FP:  # $fp holds the entry frame, which stays valid
                if from_frame:
                    frame.add(target)
                if from_base:
                    base.add(target)
    return hazards, realignments


def stack_frame(function):
    """The most stack the function's own frame takes: its largest allocation,
    wherever it is (shrink-wrapping can put it after an early return), plus
    REALIGN_SLACK if it realigns $sp."""
    constants, size, realigns = {}, 0, False
    for index, word in enumerate(function.words):
        allocated = frame_size(function.words, index, constants)
        if allocated is not None:
            size = max(size, allocated)
        track_constant(word, constants)
        realigns = realigns or realigns_sp(word)
    return size + (REALIGN_SLACK if realigns else 0)


def frame_budget(text):
    name, separator, size = text.partition("=")
    try:
        if not name or not separator:
            raise ValueError
        return name, int(size, 0)
    except ValueError:
        raise argparse.ArgumentTypeError("expected NAME=BYTES, got %r" % text)


def check_frames(functions, budgets):
    """Return (failures, passes) as printable lines for each NAME=BYTES budget."""
    failures, passes = [], []
    for name, budget in budgets:
        frames = [stack_frame(function) for function in functions if function.name == name]
        if not frames:
            failures.append("%s not found, so its frame was not checked" % name)
        elif max(frames) > budget:
            failures.append("%s has a %d-byte stack frame, more than its budget of %d" % (name, max(frames), budget))
        else:
            passes.append("%s %d <= %d" % (name, max(frames), budget))
    return failures, passes


def chain_budget(text):
    names, separator, size = text.partition("=")
    try:
        if not names or not separator:
            raise ValueError
        chain = []
        for item in names.split(","):
            name, star, count = item.partition("*")
            chain.append((name, int(count) if star else 1))
            if not name or chain[-1][1] < 1:
                raise ValueError
        return chain, int(size, 0)
    except ValueError:
        raise argparse.ArgumentTypeError("expected NAME[*COUNT],...=BYTES, got %r" % text)


def check_chains(functions, budgets):
    """Return (failures, passes) as printable lines for each chain budget: the
    frames of the functions of a call chain, COUNT of them for NAME*COUNT (a
    function that calls itself), added up."""
    failures, passes = [], []
    for chain, budget in budgets:
        label = " > ".join(name if count == 1 else "%s*%d" % (name, count) for name, count in chain)
        frames, missing = {}, []
        for name, _ in chain:
            sizes = [stack_frame(function) for function in functions if function.name == name]
            if sizes:
                frames[name] = max(sizes)
            elif name not in missing:
                missing.append(name)
        if missing:
            failures.append("%s not found, so the chain %s was not checked" % (", ".join(missing), label))
            continue
        total = sum(count * frames[name] for name, count in chain)
        if total > budget:
            failures.append("the chain %s takes %d bytes of stack, more than its budget of %d" % (label, total, budget))
        else:
            passes.append("chain %s %d <= %d" % (label, total, budget))
    return failures, passes


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
    parser.add_argument("--max-frame", action="append", default=[], type=frame_budget, metavar="NAME=BYTES",
                        help="also fail unless function NAME is present and its stack frame, with "
                             "%d bytes for a realignment, is at most BYTES" % REALIGN_SLACK)
    parser.add_argument("--max-chain", action="append", default=[], type=chain_budget,
                        metavar="NAME[*COUNT],...=BYTES",
                        help="also fail unless every function of the call chain is present and their "
                             "frames, COUNT of them for NAME*COUNT, add up to at most BYTES")
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
        if not any(function.words for function in functions):
            print("FAIL %s: no code found, so nothing was checked" % path, file=sys.stderr)
            failed = True
        elif hazards:
            failed = True
        else:
            print("PASS %s: %d functions, %d realign $sp, none stores to the stack before realigning"
                  % (path, len(functions), len({name for name, _ in realigned})))
        if args.max_frame or args.max_chain:
            too_big, within = check_frames(functions, args.max_frame)
            too_deep, chains_within = check_chains(functions, args.max_chain)
            for line in too_big + too_deep:
                print("FAIL %s: %s" % (path, line), file=sys.stderr)
            if too_big or too_deep:
                failed = True
            else:
                print("PASS %s: stack frames within budget: %s" % (path, ", ".join(within + chains_within)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
