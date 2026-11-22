"""Regression tests for reviewed save archive, LZARI, and SHA-1 paths."""
from pathlib import Path
import ctypes as c
import hashlib
import re
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


class Entry(c.Structure):
    _fields_ = [("name", c.c_char * 33), ("mode", c.c_uint16),
                ("created", c.c_ubyte * 8), ("modified", c.c_ubyte * 8),
                ("data", c.c_void_p), ("size", c.c_size_t)]


class Archive(c.Structure):
    _fields_ = [("ps1", c.c_int), ("folder", Entry), ("files", c.POINTER(Entry)),
                ("count", c.c_uint), ("decoded", c.c_void_p)]


class Sha1(c.Structure):
    _fields_ = [("hash", c.c_uint32 * 5), ("bytes", c.c_uint32),
                ("used", c.c_uint), ("block", c.c_ubyte * 64)]


def parse(lib, payload, filename):
    archive = Archive()
    data = c.create_string_buffer(payload)
    ok = lib.saveArchiveParse(c.byref(archive), data, len(payload), filename.encode())
    result = None
    if ok:
        result = (archive.ps1, archive.folder.name.decode(), [
            (archive.files[i].name.decode(), c.string_at(archive.files[i].data, archive.files[i].size))
            for i in range(archive.count)
        ])
    lib.saveArchiveFree(c.byref(archive))
    return result, data


def put32(data, offset, value):
    struct.pack_into('<I', data, offset, value)


def xps_fixture(comment=b'nonempty comment', checksum=True):
    result = bytearray(21)
    result[4:20] = b'SharkPortSave\0\0\0'
    for value in (b'title', b'date', comment):
        result += struct.pack('<I', len(value)) + value
    body = bytearray(500)
    struct.pack_into('<H', body, 0, 250)
    put32(body, 66, 3)
    body[2:6] = b'root'
    body[78] = 0x84; body[79] = 0x27
    struct.pack_into('<H', body, 250, 250)
    put32(body, 250 + 66, 4)
    body[252:256] = b'file'
    body[250 + 78] = 0x84; body[250 + 79] = 0x97
    body += b'data'
    result += struct.pack('<I', len(body)) + body
    if checksum:
        checksum_value = 0
        for byte in body:
            checksum_value = (checksum_value + (byte << (checksum_value % 24))) & 0xffffffff
        result += struct.pack('<I', checksum_value)
    return bytes(result)


def cbs_fixture():
    raw = bytearray(68)
    put32(raw, 16, 4)
    put32(raw, 20, 0x8497)
    raw[32:36] = b'file'
    raw[64:68] = b'data'
    header = bytearray(300)
    header[:4] = b'CFU\0'
    put32(header, 8, len(header))
    put32(header, 12, len(raw))
    header[20:24] = b'root'
    put32(header, 72, 0x8427)
    source = (ROOT / 'src/save_archive.c').read_text()
    block = source[source.index('static const uint8_t cbs_state'):]
    state = [int(value, 16) for value in re.findall(r'0x([0-9a-f]+)', block[:block.index('};')])]
    encrypted = bytearray(zlib.compress(raw))
    a = b = 0
    for i in range(len(encrypted)):
        a = (a + 1) & 255
        b = (b + state[a]) & 255
        state[a], state[b] = state[b], state[a]
        encrypted[i] ^= state[(state[a] + state[b]) & 255]
    check = bytearray(encrypted)
    state = [int(value, 16) for value in re.findall(r'0x([0-9a-f]+)', block[:block.index('};')])]
    a = b = 0
    for i in range(len(check)):
        a = (a + 1) & 255
        b = (b + state[a]) & 255
        state[a], state[b] = state[b], state[a]
        check[i] ^= state[(state[a] + state[b]) & 255]
    assert zlib.decompress(check) == raw
    return bytes(header + encrypted)


with tempfile.TemporaryDirectory(prefix='save-archive-review-') as directory:
    library = Path(directory) / 'save.so'
    subprocess.run(['gcc', '-std=c99', '-O1', '-g', '-shared', '-fPIC',
                    '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                    '-fno-sanitize-recover=undefined', '-I', str(ROOT / 'include'),
                    *[str(ROOT / 'src' / name) for name in ('save_archive.c', 'save_lzari.c', 'save_sign.c')],
                    '-lz', '-o', str(library)], check=True)
    lib = c.CDLL(str(library))
    lib.saveArchiveParse.argtypes = [c.c_void_p, c.c_void_p, c.c_size_t, c.c_char_p]
    lib.saveArchiveFree.argtypes = [c.c_void_p]
    lib.ps1CardAppend.argtypes = [c.c_void_p, c.c_char_p, c.c_void_p, c.c_size_t]
    lib.lzari.argtypes = [c.c_void_p, c.c_int, c.c_void_p, c.c_int]
    lib.unlzari.argtypes = [c.c_void_p, c.c_int, c.c_void_p, c.c_int]
    lib.saveHashInit.argtypes = [c.POINTER(Sha1)]
    lib.saveHashUpdate.argtypes = [c.POINTER(Sha1), c.c_void_p, c.c_uint]
    lib.saveHashFinish.argtypes = [c.POINTER(Sha1), c.c_void_p]

    # Raw PS1 names exclude .ps1 and lowercase SC remains valid.
    raw = bytearray(8192)
    raw[:2] = b'sc'
    result, _ = parse(lib, bytes(raw), 'BASLUS-12345ABCDEFGH.ps1')
    assert result and result[0] == 1 and result[2][0][0] == 'BASLUS-12345ABCDEFGH'

    # SC in a PSX-looking title must not make an unaligned raw save ambiguous.
    ambiguous = bytearray(raw)
    ambiguous[54:56] = b'SC'
    result, _ = parse(lib, bytes(ambiguous), 'raw-save.ps1')
    assert result and result[2][0][1] == bytes(ambiguous)
    psx = bytearray(54) + raw
    psx[:4] = b'name'
    result, _ = parse(lib, bytes(psx), 'image.psx')
    assert result and result[2][0][1] == bytes(raw)

    # Deleted directory slots are free and lowercase payload magic is accepted.
    card = c.create_string_buffer(bytes(131072))
    card[128] = 0xa1
    payload = c.create_string_buffer(bytes(raw))
    assert lib.ps1CardAppend(card, b'NEW', payload, len(raw)) == 1
    assert card.raw[128] == 0x51

    # XPS: a nonempty third string, declared body, and optional checksum.
    result, _ = parse(lib, xps_fixture(), 'image.xps')
    assert result and result[2] == [('file', b'data')]
    damaged = bytearray(xps_fixture())
    damaged[-1] ^= 1
    result, _ = parse(lib, bytes(damaged), 'image.xps')
    assert result is None

    # CBS uses its declared payload offset and never mutates the caller's input.
    cbs = cbs_fixture()
    result, data = parse(lib, cbs, 'image.cbs')
    assert result and result[1] == 'root' and result[2] == [('file', b'data')]
    assert data.raw[:len(cbs)] == cbs

    # LZARI: tiny output fails, a decoder failure cannot poison later encoding,
    # and an ordinary round trip preserves bytes.
    source = b'compressible fixture ' * 100
    source_buffer = c.create_string_buffer(source)
    tiny = c.create_string_buffer(1)
    assert lib.lzari(source_buffer, len(source), tiny, 1) == -1
    compressed = c.create_string_buffer(len(source) + 1024)
    compressed_size = lib.lzari(source_buffer, len(source), compressed, len(compressed))
    assert compressed_size > 0
    assert lib.unlzari(compressed, 4, tiny, 1) == -1
    second = c.create_string_buffer(len(source) + 1024)
    second_size = lib.lzari(source_buffer, len(source), second, len(second))
    assert second_size == compressed_size and second.raw[:second_size] == compressed.raw[:compressed_size]
    decoded = c.create_string_buffer(len(source))
    assert lib.unlzari(compressed, compressed_size, decoded, len(source)) == len(source)
    assert decoded.raw == source

    # MAX: validate a real CRC, reject a mismatch, and retain zero-CRC support.
    entry = bytearray(40)
    put32(entry, 0, 4)
    entry[4:8] = b'file'
    entry[36:40] = b'data'
    compressed_size = lib.lzari(c.create_string_buffer(bytes(entry)), len(entry), compressed, len(compressed))
    assert compressed_size > 0
    max_image = bytearray(88 + compressed_size)
    max_image[:12] = b'Ps2PowerSave'
    max_image[16:20] = b'root'
    put32(max_image, 84, 1)
    put32(max_image, 88, len(entry))
    max_image[88:88 + compressed_size] = compressed.raw[:compressed_size]
    put32(max_image, 12, zlib.crc32(max_image))
    result, _ = parse(lib, bytes(max_image), 'image.max')
    assert result and result[2] == [('file', b'data')]
    max_image[-1] ^= 1
    result, _ = parse(lib, bytes(max_image), 'image.max')
    assert result is None
    put32(max_image, 12, 0)
    max_image[-1] ^= 1
    result, _ = parse(lib, bytes(max_image), 'image.max')
    assert result is not None

    # SHA-1 encodes the full 64-bit bit count, including 512 MiB exactly.
    hash_state = Sha1()
    lib.saveHashInit(c.byref(hash_state))
    chunk = c.create_string_buffer(1024 * 1024)
    expected = hashlib.sha1()
    for _ in range(512):
        lib.saveHashUpdate(c.byref(hash_state), chunk, len(chunk))
        expected.update(b'\0' * len(chunk))
    digest = c.create_string_buffer(20)
    lib.saveHashFinish(c.byref(hash_state), digest)
    assert digest.raw == expected.digest()

    short_test = Path(directory) / 'short-max.c'
    short_exe = Path(directory) / 'short-max'
    short_test.write_text(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "save_archive.h"
int main(void) {
    SaveArchive archive;
    uint8_t input[12] = "Ps2PowerSave";
    assert(!saveArchiveParse(&archive, input, sizeof(input), "short.max"));
    return 0;
}
''')
    subprocess.run(['gcc', '-std=c99', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
                    '-I', str(ROOT / 'include'), str(short_test),
                    *[str(ROOT / 'src' / name) for name in ('save_archive.c', 'save_lzari.c', 'save_sign.c')],
                    '-lz', '-o', str(short_exe)], check=True)
    subprocess.run([str(short_exe)], check=True)

print('PASS: reviewed save parsers, card append, LZARI bounds/state, MAX/XPS/CBS integrity, and 64-bit SHA-1 length')
