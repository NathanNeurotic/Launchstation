"""Size report for the built artifacts (pure Python, reads ELF files directly).

Usage: report_sizes.py REPO_BUILD_COPY EVIDENCE_DIR
Writes into EVIDENCE_DIR/sizes/.
"""
import os
import re
import struct
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import elfmini  # noqa: E402

repo, evid = sys.argv[1:3]
out = os.path.join(evid, 'sizes')
os.makedirs(out, exist_ok=True)


def w(name):
    return open(os.path.join(out, name), 'w', newline='\n')


# ------------------------------------------------------------ artifacts ----
with w('artifacts.tsv') as f:
    f.write('path\tbytes\n')
    for d in ('build/bin', 'build-ds34/bin'):
        full = os.path.join(repo, d)
        if not os.path.isdir(full):
            continue
        for fn in sorted(os.listdir(full)):
            f.write('%s/%s\t%d\n' % (d, fn, os.path.getsize(os.path.join(full, fn))))
    for d in ('build/obj', 'build-ds34/obj'):
        full = os.path.join(repo, d)
        if not os.path.isdir(full):
            continue
        for t in sorted(os.listdir(full)):
            for fn in sorted(os.listdir(os.path.join(full, t))):
                if fn.endswith('.elf'):
                    f.write('%s/%s/%s\t%d\n' % (d, t, fn, os.path.getsize(os.path.join(full, t, fn))))
    for d in ('build/lib', 'build-ds34/lib'):
        full = os.path.join(repo, d)
        if os.path.isdir(full):
            for fn in sorted(os.listdir(full)):
                f.write('%s/%s\t%d\n' % (d, fn, os.path.getsize(os.path.join(full, fn))))


def flagstr(fl):
    return ('A' if fl & 2 else '') + ('W' if fl & 1 else '') + ('X' if fl & 4 else '')


def elf_report(rel, tag, top=80):
    path = os.path.join(repo, rel)
    if not os.path.exists(path):
        return None
    with open(path, 'rb') as fh:
        e = elfmini.Elf(fh.read(), rel)
    with w(tag + '-sections.tsv') as f:
        f.write('# %s (%d bytes on disk)\n' % (rel, os.path.getsize(path)))
        f.write('section\ttype\tflags\taddr_hex\tsize_bytes\n')
        tot = defaultdict(int)
        for s in e.sections[1:]:
            f.write('%s\t%d\t%s\t%x\t%d\n' % (s['name'], s['type'], flagstr(s['flags']), s['addr'], s['size']))
            if s['flags'] & 2:
                k = 'bss (NOBITS)' if s['type'] == 8 else ('text (AX)' if s['flags'] & 4 else ('data (AW)' if s['flags'] & 1 else 'rodata (A)'))
                tot[k] += s['size']
            else:
                tot['non-alloc'] += s['size']
        f.write('# totals\n')
        for k in sorted(tot):
            f.write('# %s\t%d\n' % (k, tot[k]))
        f.write('# program headers: type offset vaddr filesz memsz flags\n')
        for p in e.segments:
            f.write('# PHDR\t%d\t0x%x\t0x%x\t%d\t%d\t%d\n' % (p['type'], p['offset'], p['vaddr'], p['filesz'], p['memsz'], p['flags']))
    syms = [s for s in e.symbols if s['size'] and s['type'] in ('FUNC', 'OBJECT') and s['shndx'] not in (0,)]
    if syms:
        syms.sort(key=lambda s: -s['size'])
        with w(tag + '-top-symbols.tsv') as f:
            f.write('# top %d symbols by size in %s (ELF symbol table st_size)\n' % (top, rel))
            f.write('size_bytes\ttype\tbind\tsection\taddr_hex\tsymbol\n')
            for s in syms[:top]:
                sec = e.sections[s['shndx']]['name'] if s['shndx'] < len(e.sections) else str(s['shndx'])
                f.write('%d\t%s\t%s\t%s\t%x\t%s\n' % (s['size'], s['type'], s['bind'], sec, s['value'], s['name']))
        with w(tag + '-all-symbols-by-size.tsv') as f:
            f.write('size_bytes\ttype\tbind\tsection\taddr_hex\tsymbol\n')
            for s in syms:
                sec = e.sections[s['shndx']]['name'] if s['shndx'] < len(e.sections) else str(s['shndx'])
                f.write('%d\t%s\t%s\t%s\t%x\t%s\n' % (s['size'], s['type'], s['bind'], sec, s['value'], s['name']))
    return e


elf_report('build/bin/wLaunchELF.elf', 'wLaunchELF-stripped')
e = elf_report('build/obj/wLaunchELF/wLaunchELF.unstripped.elf', 'wLaunchELF-unstripped')
elf_report('build/obj/loader/loader.unstripped.elf', 'loader-unstripped', 40)
elf_report('build/bin/loader.elf', 'loader-stripped')
elf_report('build/obj/wle_igr/wle_igr.unstripped.elf', 'wle_igr-unstripped', 40)
elf_report('build/bin/wle_igr.elf', 'wle_igr-stripped')
elf_report('build/bin/wLaunchELF-packed.elf', 'wLaunchELF-packed')
elf_report('build-ds34/bin/wLaunchELF.elf', 'ds34-wLaunchELF-stripped')
elf_report('build-ds34/obj/wLaunchELF/wLaunchELF.unstripped.elf', 'ds34-wLaunchELF-unstripped')

# ------------------------------------------------- embedded blobs ----
# symbol -> defining input file, from the relink map
sym_file = {}
mp = os.path.join(repo, 'evidence-relink', 'wLaunchELF.map')
cur = ''
if os.path.exists(mp):
    with open(mp, 'r', errors='replace') as f:
        for line in f:
            m = re.match(r'^\s*[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+\d+ (.*)$', line.rstrip('\n'))
            if not m:
                continue
            rest = m.group(1)
            ind = len(rest) - len(rest.lstrip(' '))
            if ind == 8:
                cur = rest.strip()
            elif ind == 16:
                sym_file.setdefault(rest.strip(), cur)

if e is not None:
    by_name = {s['name']: s for s in e.symbols if s['name'] and s['shndx'] != 0}
    rows = []
    for nm, s in by_name.items():
        if not nm.startswith('size_'):
            continue
        base = nm[5:]
        if base not in by_name:
            continue
        raw = e.read_vaddr(s['value'], 4)
        if raw is None:
            continue
        size = struct.unpack('<I', raw)[0]
        b = by_name[base]
        sec = e.sections[b['shndx']]['name'] if b['shndx'] < len(e.sections) else '?'
        src = sym_file.get(base, '')
        src = re.sub(r':\(.*\)$', '', src)
        magic = e.read_vaddr(b['value'], 4) or b''
        kind = 'ELF/IRX' if magic == b'\x7fELF' else magic.hex()
        rows.append((size, base, sec, src, kind))
    rows.sort(reverse=True)
    with w('wLaunchELF-embedded-blobs.tsv') as f:
        f.write('# Embedded payloads in build/obj/wLaunchELF/wLaunchELF.unstripped.elf: every symbol X that has a size_X companion.\n')
        f.write('# payload_bytes is the 32-bit value stored at size_X. defined_in is the link input that defines X (from the map).\n')
        f.write('payload_bytes\tsymbol\toutput_section\tdefined_in\tfirst_4_bytes\n')
        tot = 0
        for size, base, sec, src, kind in rows:
            tot += size
            f.write('%d\t%s\t%s\t%s\t%s\n' % (size, base, sec, src, kind))
        f.write('# total payload bytes: %d in %d blobs\n' % (tot, len(rows)))
    with w('wLaunchELF-crt-symbols.txt') as f:
        f.write('# stack/heap related symbols in the unstripped main ELF\n')
        for nm in ('_stack', '_stack_size', '_heap_size', '_end', '_end_bss', '_fbss', '_edata', '_gp', '_ftext', '_etext', '__start', '_fdata'):
            if nm in by_name:
                s = by_name[nm]
                f.write('%s\tvalue=0x%x\tshndx=%d\ttype=%s\n' % (nm, s['value'], s['shndx'], s['type']))
print('sizes written to', out)
