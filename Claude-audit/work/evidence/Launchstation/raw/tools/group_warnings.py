"""Group compiler warnings in a log by warning flag and by source area.

Usage: group_warnings.py LOG OUT_TXT [OUT_TSV]
"""
import os
import re
import sys
from collections import Counter, defaultdict

log, out_txt = sys.argv[1:3]
out_tsv = sys.argv[3] if len(sys.argv) > 3 else None
REPO = os.environ.get('REPO_ROOT', '').replace('\\', '/').rstrip('/')
SDK = os.environ.get('SDK_ROOT', '').replace('\\', '/').rstrip('/')

pat = re.compile(r'^(.*?):(\d+):(?:(\d+):)? (warning|error): (.*?)(?: \[(-W[^\]]+)\])?$')


def norm(p):
    p = os.path.normpath(p).replace('\\', '/')
    if REPO and p.lower().startswith(REPO.lower() + '/'):
        return p[len(REPO) + 1:]
    if SDK and p.lower().startswith(SDK.lower() + '/'):
        return '$SDK/' + p[len(SDK) + 1:]
    return p


def area(p):
    if p.startswith('$SDK/'):
        return 'sdk-header'
    if p.startswith(('.smbd-deps/', '.cloud-deps/', '.jpeg-deps/')):
        return 'third-party-dep'
    if p.startswith('build/generated/'):
        return 'generated'
    parts = p.split('/')
    if parts[0] == 'iop' and len(parts) > 1:
        return 'iop/' + parts[1]
    return parts[0]


seen = set()
rows = []
with open(log, 'r', errors='replace') as f:
    for line in f:
        m = pat.match(line.rstrip())
        if not m:
            continue
        path, ln, col, sev, msg, flag = m.groups()
        path = norm(path)
        key = (path, ln, col, msg)
        if key in seen:      # the same header diagnostic repeats once per including translation unit
            continue
        seen.add(key)
        rows.append((path, int(ln), sev, flag or ('(error)' if sev == 'error' else '(no flag)'), msg))

by_flag = Counter(r[3] for r in rows)
by_flag_area = defaultdict(Counter)
for r in rows:
    by_flag_area[r[3]][area(r[0])] += 1

with open(out_txt, 'w', newline='\n') as f:
    f.write('Source log: %s\n' % os.path.basename(log))
    f.write('Unique diagnostics (same file:line:col:message counted once): %d\n\n' % len(rows))
    f.write('count\tflag\n')
    for flag, c in by_flag.most_common():
        f.write('%d\t%s\n' % (c, flag))
    f.write('\nPer flag, per source area:\n')
    for flag, c in by_flag.most_common():
        f.write('\n%s (%d)\n' % (flag, c))
        for a, n in by_flag_area[flag].most_common():
            f.write('  %6d  %s\n' % (n, a))
    f.write('\nPer file (top 60):\n')
    for p, n in Counter(r[0] for r in rows).most_common(60):
        f.write('  %6d  %s\n' % (n, p))
if out_tsv:
    with open(out_tsv, 'w', newline='\n') as f:
        f.write('file\tline\tseverity\tflag\tmessage\n')
        for r in sorted(rows, key=lambda r: (r[3], r[0], r[1])):
            f.write('%s\t%d\t%s\t%s\t%s\n' % r)
print('%s: %d unique diagnostics, %d flags' % (os.path.basename(log), len(rows), len(by_flag)))
