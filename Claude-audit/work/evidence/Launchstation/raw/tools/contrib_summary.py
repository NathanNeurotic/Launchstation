"""Summarise NAME-contrib-by-object.tsv: totals per category, per archive, top contributors per section.
Usage: contrib_summary.py CONTRIB_TSV > out.txt
"""
import collections
import csv
import os
import sys

rows = list(csv.DictReader(open(sys.argv[1]), delimiter='\t'))
cols = ['.text', '.rodata', '.data', '.sdata', '.bss', '.sbss', 'other_alloc', 'total_alloc']
cat = collections.defaultdict(collections.Counter)
for r in rows:
    for c in cols:
        cat[r['category']][c] += int(r[c])
print('# source: %s (bytes of input sections placed in each output section, from the ld.lld map)' % os.path.basename(sys.argv[1]))
print('category', *cols, sep='\t')
for k, v in sorted(cat.items(), key=lambda kv: -kv[1]['total_alloc']):
    print(k, *[v[c] for c in cols], sep='\t')
print('ALL', *[sum(v[c] for v in cat.values()) for c in cols], sep='\t')
arch = collections.defaultdict(lambda: [0, collections.Counter()])
for r in rows:
    if r['member']:
        a = arch[r['file']]
        a[0] += 1
        for c in cols:
            a[1][c] += int(r[c])
print('\narchive\tmembers_in_link\t.text\t.rodata\t.data\t.bss\ttotal')
for k, (n, v) in sorted(arch.items(), key=lambda kv: -kv[1][1]['total_alloc']):
    print(k, n, v['.text'], v['.rodata'], v['.data'], v['.bss'], v['total_alloc'], sep='\t')
for c in ['.text', '.rodata', '.data', '.bss']:
    print('\nTop 40 first-party / embed-blob / linker-internal contributors to', c)
    fp = [r for r in rows if r['category'] in ('first-party', 'embed-blob', 'generated', 'linker-internal')]
    for r in sorted(fp, key=lambda r: -int(r[c]))[:40]:
        if int(r[c]):
            print(int(r[c]), r['file'] + ('(' + r['member'] + ')' if r['member'] else ''), sep='\t')
