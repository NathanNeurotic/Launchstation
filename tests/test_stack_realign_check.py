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
def lui(rt, imm): return i_type(0x0F, 0, rt, imm)
def sd(rt, offset, base): return i_type(0x3F, base, rt, offset)
def sw(rt, offset, base): return i_type(0x2B, base, rt, offset)
def swc1(ft, offset, base): return i_type(0x39, base, ft, offset)
def subu(rd, rs, rt): return r_type(rs, rt, rd, 0x23)
def addu(rd, rs, rt): return r_type(rs, rt, rd, 0x21)
def move(rd, rs): return r_type(rs, 0, rd, 0x25)
def and_(rd, rs, rt): return r_type(rs, rt, rd, 0x24)


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
]
FLAGGED = {'shipped_copy': ('sw $7,416($29)', 0x98), 'derived_spill': ('sd $4,-8($2)', 0x10),
           'float_spill': ('swc1 $f12,0($29)', 0x8), 'swapped_and': ('sw $5,8($29)', 0x4)}


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
        assert ('shipped_copy', 0xE2600) in realigned, realigned  # the 905 KB frame
        assert ('fixed_copy', 0x23C0) in realigned, realigned
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

print('PASS: the shipped copy() prologue and three other pre-realignment stores fail; '
      'the fixed prologue, $fp stores, plain and later spills pass; linked, relocatable and stripped ELF')
