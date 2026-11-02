"""Run scripts/check_stack_realign.py on small hand-built MIPS ELF files.

The shipped copy() prologue (Launchstation ca04deb, ps2build Clang 24) must
fail: it stores $7 (a3, bytes 8-15 of the by-value FILEINFO name) to the stack
just before `and $sp,$sp,$1` realigns it, so the reload reads another slot.
The fixed prologue, plain frames, $fp-relative stores and callee-saved saves
must pass. Linked, relocatable and stripped files are all read.
"""
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
script = root / 'scripts/check_stack_realign.py'
spec = importlib.util.spec_from_file_location('check_stack_realign', script)
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)

SP, FP, RA = 29, 30, 31


def r_type(rs, rt, rd, funct):
    return (rs << 21) | (rt << 16) | (rd << 11) | funct


def i_type(op, rs, rt, imm):
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)


def addiu(rt, rs, imm): return i_type(0x09, rs, rt, imm)
def daddiu(rt, rs, imm): return i_type(0x19, rs, rt, imm)
def lui(rt, imm): return i_type(0x0F, 0, rt, imm)
def beq(rs, rt, offset): return i_type(0x04, rs, rt, offset)
def sd(rt, offset, base): return i_type(0x3F, base, rt, offset)
def sq(rt, offset, base): return i_type(0x1F, base, rt, offset)  # R5900 128-bit store
def sw(rt, offset, base): return i_type(0x2B, base, rt, offset)
def swc1(ft, offset, base): return i_type(0x39, base, ft, offset)
def subu(rd, rs, rt): return r_type(rs, rt, rd, 0x23)
def dsubu(rd, rs, rt): return r_type(rs, rt, rd, 0x2F)
def addu(rd, rs, rt): return r_type(rs, rt, rd, 0x21)
def daddu(rd, rs, rt): return r_type(rs, rt, rd, 0x2D)
def move(rd, rs): return r_type(rs, 0, rd, 0x25)
def and_(rd, rs, rt): return r_type(rs, rt, rd, 0x24)
def sll(rd, rt, shift): return r_type(0, rt, rd, 0) | (shift << 6)


RETURN = [r_type(RA, 0, 0, 0x08), 0]  # jr $ra; nop
REALIGN = [addiu(1, 0, -64), and_(SP, SP, 1)]

# copy() as shipped in ca04deb (load address 0x1889a0 onward is its body).
SHIPPED_COPY = [
    0x3c01000e, 0x24212600, 0x03a1e823, 0x3c01000e, 0x03a10821, 0xfc3f25f8, 0x3c01000e, 0x03a10821,
    0xfc3e25f0, 0x3c01000e, 0x03a10821, 0xfc3c25e8, 0x3c01000e, 0x03a10821, 0xfc3725e0, 0x3c01000e,
    0x03a10821, 0xfc3625d8, 0x3c01000e, 0x03a10821, 0xfc3525d0, 0x3c01000e, 0x03a10821, 0xfc3425c8,
    0x3c01000e, 0x03a10821, 0xfc3325c0, 0x3c01000e, 0x03a10821, 0xfc3225b8, 0x3c01000e, 0x03a10821,
    0xfc3125b0, 0x3c01000e, 0x03a10821, 0xfc3025a8, 0x03a0f025, 0x2401ffc0, 0xafa701a0, 0x03a1e824,
    0x3c01000e, 0xffa502f0, 0xffa402f8, 0x3c07000e, 0x03c10821, 0x03a73821, 0x24212780, 0x78210000,
] + RETURN
# The fixed copy(): a 9 KB frame, nothing but callee-saved registers before the realignment.
FIXED_COPY = [
    0x27bddc40, 0xffbf23b8, 0xffbe23b0, 0xffbc23a8, 0xffb723a0, 0xffb62398, 0xffb52390, 0xffb42388,
    0xffb32380, 0xffb22378, 0xffb12370, 0xffb02368, 0x03a0f025, 0x2401ffc0, 0x03a1e824, 0x00c00825,
] + RETURN

FUNCTIONS = [
    ('shipped_copy', SHIPPED_COPY),
    ('fixed_copy', FIXED_COPY),
    # A spill through a temporary that holds $sp plus an offset.
    ('derived_spill', [lui(1, 1), subu(SP, SP, 1), lui(2, 1), addu(2, SP, 2), sd(4, -8, 2),
                       move(FP, SP)] + REALIGN + RETURN),
    # A float argument register stored before the realignment.
    ('float_spill', [addiu(SP, SP, -128), sd(RA, 120, SP), swc1(12, 0, SP)] + REALIGN + RETURN),
    # `and $sp,$1,$sp` is the same realignment with its operands swapped.
    ('swapped_and', [addiu(SP, SP, -64), sw(5, 8, SP), addiu(1, 0, -64), and_(SP, 1, SP)] + RETURN),
    # Stores through $fp keep the entry frame on both sides of the realignment.
    ('fp_store', [addiu(SP, SP, -128), sd(RA, 120, SP), sd(FP, 112, SP), move(FP, SP),
                  sd(4, 0, FP)] + REALIGN + RETURN),
    # No realignment: spilling arguments to the stack is ordinary.
    ('plain_frame', [addiu(SP, SP, -32), sd(4, 0, SP), sd(5, 8, SP)] + RETURN),
    # A spill after the realignment is ordinary too.
    ('after_realign', [addiu(SP, SP, -128), sd(RA, 120, SP), move(FP, SP)] + REALIGN
     + [sd(4, 0, SP)] + RETURN),
    # With alloca or a variable-length array Clang reaches the frame through $s7,
    # set up only after the realignment, so this spill writes through the caller's $s7.
    ('bp_spill', [addiu(SP, SP, -128), sd(RA, 120, SP), sd(23, 112, SP), move(FP, SP), addiu(1, 0, -64),
                  sw(7, 0, 23), and_(SP, SP, 1), move(23, SP)] + RETURN),
    # A 128-bit spill.
    ('sq_spill', [addiu(SP, SP, -512), sq(7, 0x1A0, SP)] + REALIGN + RETURN),
    # `and $sp` in a branch delay slot (drainGENERIC and others have it): the
    # window still runs up to the `and`, past the branch.
    ('delay_slot', [addiu(SP, SP, -512), sw(7, 0x1A0, SP), addiu(1, 0, -64), sll(16, 4, 0),
                    beq(16, 0, 2), and_(SP, SP, 1)] + RETURN),
    # A temporary that held $sp and was then overwritten is no stack address.
    ('redefined_temp', [addiu(SP, SP, -512), addu(2, SP, 0), lui(2, 0x10), sd(16, 0, 2)] + REALIGN + RETURN),
    # Callee-saved saves pass through every kind of $sp-derived temporary, as do
    # callee-saved float registers saved through $sp.
    ('derived_saves', [lui(1, 1), subu(SP, SP, 1), lui(1, 1), addu(1, SP, 1), sd(16, -8, 1),
                       move(2, SP), sd(17, 0, 2), addiu(3, SP, 16), sd(18, 0, 3), daddu(4, SP, 0),
                       sd(19, 8, 4), swc1(20, 32, SP), swc1(22, 36, SP), move(FP, SP)] + REALIGN + RETURN),
    # Frames allocated by daddiu, dsubu and addu of a negative constant are measured.
    ('daddiu_frame', [daddiu(SP, SP, -96), sd(RA, 88, SP), move(FP, SP)] + REALIGN + RETURN),
    ('dsubu_frame', [lui(1, 1), dsubu(SP, SP, 1), move(FP, SP)] + REALIGN + RETURN),
    ('addu_frame', [lui(1, 0xFFFF), addu(SP, SP, 1), move(FP, SP)] + REALIGN + RETURN),
]
FLAGGED = {'shipped_copy': ('sw $7,416($29)', 0x98), 'derived_spill': ('sd $4,-8($2)', 0x10),
           'float_spill': ('swc1 $f12,0($29)', 0x8), 'swapped_and': ('sw $5,8($29)', 0x4),
           'bp_spill': ('sw $7,0($23)', 0x14), 'sq_spill': ('sq $7,416($29)', 0x4),
           'delay_slot': ('sw $7,416($29)', 0x4), 'redefined_temp': ('sd $16,0($2)', 0xC)}
FRAMES = {'shipped_copy': 0xE2600, 'fixed_copy': 0x23C0, 'derived_saves': 0x10000,
          'daddiu_frame': 96, 'dsubu_frame': 0x10000, 'addu_frame': 0x10000}


def elf(functions, relocatable=False, stripped=False, base=0x100000):
    """A minimal ELF32 little-endian MIPS file with one .text section."""
    text = b''.join(struct.pack('<%dI' % len(words), *words) for _, words in functions)
    address = 0 if relocatable else base
    strtab, symbols, offset = b'\0', [bytes(16)], 0
    for name, words in functions:
        symbols.append(struct.pack('<IIIBBH', len(strtab), address + offset, 4 * len(words), 0x12, 0, 1))
        strtab += name.encode() + b'\0'
        offset += 4 * len(words)
    symtab = b''.join(symbols)
    shstrtab = b'\0.text\0.symtab\0.strtab\0.shstrtab\0'
    blobs = [text] if stripped else [text, symtab, strtab]
    blobs.append(shstrtab)
    body, offsets = b'', []
    for blob in blobs:
        offsets.append(52 + len(body))
        body += blob + bytes(-len(blob) % 4)
    headers = [bytes(40), struct.pack('<10I', 1, 1, 6, address, offsets[0], len(text), 0, 0, 4, 0)]
    if not stripped:
        headers.append(struct.pack('<10I', 7, 2, 0, 0, offsets[1], len(symtab), 3, 1, 4, 16))
        headers.append(struct.pack('<10I', 15, 3, 0, 0, offsets[2], len(strtab), 0, 0, 1, 0))
    headers.append(struct.pack('<10I', 23, 3, 0, 0, offsets[-1], len(shstrtab), 0, 0, 1, 0))
    header = b'\x7fELF\x01\x01\x01' + bytes(9) + struct.pack(
        '<HHIIIIIHHHHHH', 1 if relocatable else 2, 8, 1, base, 0, 52 + len(body), 0, 52, 0, 0, 40,
        len(headers), len(headers) - 1)
    return header + body + b''.join(headers)


def starts(functions, relocatable=False):
    address, found = 0 if relocatable else 0x100000, {}
    for name, words in functions:
        found[name] = address
        address += 4 * len(words)
    return found


with tempfile.TemporaryDirectory(prefix='stack-realign-') as folder:
    folder = Path(folder)
    for relocatable in (False, True):
        path = folder / ('relocatable.o' if relocatable else 'linked.elf')
        path.write_bytes(elf(FUNCTIONS, relocatable=relocatable))
        functions, hazards, realigned = check.scan(path)
        assert len(functions) == len(FUNCTIONS), functions
        assert len(hazards) == len(FLAGGED), hazards
        address = starts(FUNCTIONS, relocatable)
        for name, (store, offset) in FLAGGED.items():
            line = '%s at %#x: %s before' % (name, address[name] + offset, store)
            assert any(hazard.startswith(line) for hazard in hazards), (line, hazards)
        for name, size in FRAMES.items():  # shipped_copy is the 905 KB frame
            assert (name, size) in realigned, (name, realigned)
        assert not any(name == 'plain_frame' for name, _ in realigned)

    # A stripped file is scanned as one stream and still fails.
    stripped = folder / 'stripped.elf'
    stripped.write_bytes(elf(FUNCTIONS, stripped=True))
    functions, hazards, _ = check.scan(stripped)
    assert len(functions) == 1 and len(hazards) == len(FLAGGED), hazards
    assert hazards[0].startswith('<0x100000> at %#x: sw $7,416($29)' % (0x100000 + 0x98)), hazards

    # The command line: 1 with any hazard, 0 when every function is clean.
    clean = [item for item in FUNCTIONS if item[0] not in FLAGGED]
    (folder / 'clean.elf').write_bytes(elf(clean))
    (folder / 'broken.elf').write_bytes(elf([('shipped_copy', SHIPPED_COPY)]))
    (folder / 'not-elf.bin').write_bytes(b'not an ELF file')
    run = lambda *names: subprocess.run([sys.executable, str(script)] + [str(folder / n) for n in names],
                                        capture_output=True, text=True)
    result = run('clean.elf')
    assert result.returncode == 0 and 'PASS' in result.stdout and not result.stderr, result
    result = run('clean.elf', 'broken.elf')
    assert result.returncode == 1 and 'FAIL' in result.stderr and 'shipped_copy' in result.stderr, result
    assert 'note' in result.stdout and 'shipped_copy (905 KB)' in result.stdout, result
    result = run('not-elf.bin')
    assert result.returncode == 1 and 'not a 32-bit little-endian ELF file' in result.stderr, result

    # A file with nothing to check fails instead of passing: no section headers at all
    # (e_shnum = 0), or a .text section that is not executable.
    no_sections = bytearray(elf(clean))
    struct.pack_into('<HH', no_sections, 48, 0, 0)
    (folder / 'no-sections.elf').write_bytes(no_sections)
    no_code = bytearray(elf(clean))
    text_header = struct.unpack_from('<I', no_code, 32)[0] + 40  # e_shoff, then section 1 (.text)
    assert struct.unpack_from('<I', no_code, text_header + 8)[0] == 6  # SHF_ALLOC | SHF_EXECINSTR
    struct.pack_into('<I', no_code, text_header + 8, 2)  # SHF_ALLOC only
    (folder / 'no-code.elf').write_bytes(no_code)
    # An empty .text whose only function symbol has size 0 is scanned as one
    # empty stream, which is no code either.
    (folder / 'empty-code.elf').write_bytes(elf([('empty', [])]))
    assert [function.words for function in check.scan(folder / 'empty-code.elf')[0]] == [()]
    for name in ('no-sections.elf', 'no-code.elf', 'empty-code.elf'):
        if name != 'empty-code.elf':
            assert check.scan(folder / name)[0] == [], name
        result = run(name)
        assert result.returncode == 1 and 'PASS' not in result.stdout, result
        assert 'no code found, so nothing was checked' in result.stderr, result

print('PASS: the shipped copy() prologue and %d other pre-realignment stores fail (including a '
      '$s7 base-pointer spill and an `and` in a delay slot); the fixed prologue, callee-saved saves '
      'through $sp-derived temporaries, $fp stores, plain and later spills pass; frames of every '
      'allocation form are measured; linked, relocatable and stripped ELF; a file with no code to '
      'check fails' % (len(FLAGGED) - 1))
