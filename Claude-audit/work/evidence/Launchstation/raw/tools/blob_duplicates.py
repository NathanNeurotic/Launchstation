"""List embedded payloads (symbol X with a size_X companion) of several ELF files and find identical payloads.

Usage: blob_duplicates.py OUT_TSV LABEL=ELF [LABEL=ELF ...]
"""
import hashlib
import struct
import sys
import os
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import elfmini  # noqa: E402

out = sys.argv[1]
rows = []
for spec in sys.argv[2:]:
    label, path = spec.split('=', 1)
    with open(path, 'rb') as fh:
        e = elfmini.Elf(fh.read(), path)
    by = {s['name']: s for s in e.symbols if s['name'] and s['shndx'] != 0}
    for nm, s in by.items():
        if nm.startswith('size_') and nm[5:] in by:
            raw = e.read_vaddr(s['value'], 4)
            if raw is None:
                continue
            size = struct.unpack('<I', raw)[0]
            data = e.read_vaddr(by[nm[5:]]['value'], size)
            if data is None or len(data) != size:
                continue
            rows.append((label, nm[5:], size, hashlib.sha256(data).hexdigest()[:16]))

groups = defaultdict(list)
for r in rows:
    groups[r[3]].append(r)
with open(out, 'w', newline='\n') as f:
    f.write('# Embedded payloads with identical content (same SHA-256), within one ELF or across ELFs.\n')
    f.write('# wle_igr is itself embedded whole in wLaunchELF (symbol wle_igr_elf_irx), so a payload present in both\n')
    f.write('# is stored twice in the shipped main ELF.\n')
    f.write('sha256_prefix\tpayload_bytes\tcopies\twhere\n')
    dup_total = 0
    for h, g in sorted(groups.items(), key=lambda kv: -kv[1][0][2]):
        if len(g) > 1:
            dup_total += g[0][2] * (len(g) - 1)
            f.write('%s\t%d\t%d\t%s\n' % (h, g[0][2], len(g), ', '.join('%s:%s' % (r[0], r[1]) for r in g)))
    f.write('# redundant bytes (size x (copies-1)): %d\n' % dup_total)
    f.write('#\n# all payloads\nelf\tsymbol\tpayload_bytes\tsha256_prefix\n')
    for r in sorted(rows, key=lambda r: (r[0], -r[2])):
        f.write('%s\t%s\t%d\t%s\n' % r)
print('blob duplicates: %d payloads, %d redundant bytes' % (len(rows), dup_total))
