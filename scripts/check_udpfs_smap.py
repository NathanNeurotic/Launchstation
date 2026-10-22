"""Verify linked variants embed their built SMAP and retain the bundled peer ABI."""
import argparse
import hashlib
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def table(data, magic):
    header = struct.pack('<III8s', magic, 0, 0x100, b'smap')
    offset = data.find(header)
    if offset < 0 or data.find(header, offset + 1) >= 0:
        raise ValueError('Expected one smap version 1.0 table')
    return offset + len(header)


def verify(directory):
    module = (directory / 'bin/udpfs_smap.irx').read_bytes()
    start = table(module, 0x41C00000)
    exports = struct.unpack_from('<9I', module, start)
    if len(set(exports[:4])) != 1 or not all(exports[:8]) or exports[8] != 0:
        raise ValueError('SMAP must retain four reserved and four function exports')
    for name in ('udpfs_ministack.irx', 'udpfs_ioman.irx'):
        peer = (ROOT / 'iop/__precompiled' / name).read_bytes()
        pos = table(peer, 0x41E00000)
        while struct.unpack_from('<I', peer, pos)[0] == 0x03E00008:  # jr ra import stub
            ordinal = struct.unpack_from('<I', peer, pos + 4)[0] & 0xffff
            if ordinal >= 8:
                raise ValueError(f'{name} imports an absent SMAP ordinal {ordinal}')
            pos += 8
    elf = (directory / 'obj/wLaunchELF/wLaunchELF.unstripped.elf').read_bytes()
    if module not in elf:
        raise ValueError('Linked ELF does not contain the source-built SMAP')
    old = (ROOT / 'iop/__precompiled/udpfs_smap.irx').read_bytes()
    if module == old or old in elf:
        raise ValueError('Linked ELF still embeds the defective historical SMAP')
    print(f'PASS {directory.name}: source-built SMAP {len(module)} bytes, '
          f'SHA256 {hashlib.sha256(module).hexdigest()}, bundled peer ABI preserved')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('builds', nargs='+', type=Path)
    for directory in parser.parse_args().builds:
        verify(directory)
