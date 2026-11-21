"""Analyse one ld.lld link: map file, --print-gc-sections log, --why-extract file, link inputs.

Usage:
  analyze_link.py NAME MAP GCLOG WHYEXTRACT RSP_OR_INPUTLIST LINKCWD OUTDIR

Writes into OUTDIR (prefix NAME-):
  NAME-output-sections.tsv         output section, VMA, size
  NAME-contrib-by-object.tsv       bytes each input file contributes to each output section
  NAME-archive-members.tsv         archive members pulled into the link, with bytes and why-extract reason
  NAME-gc-discarded-all.tsv        every section the linker garbage-collected, with size from the object
  NAME-gc-discarded-first-party.tsv  same, limited to first-party objects
  NAME-gc-summary.txt              counts and totals
  NAME-objects-contributing-nothing.txt
"""
import os
import re
import sys
from collections import defaultdict, OrderedDict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import elfmini  # noqa: E402

name, map_path, gc_path, why_path, inputs_path, link_cwd, outdir = sys.argv[1:8]

REPO = os.environ.get('REPO_ROOT', '').replace('\\', '/').rstrip('/')
SDK = os.environ.get('SDK_ROOT', '').replace('\\', '/').rstrip('/')


def norm(path):
    """Normalise a path to repo-relative (or $SDK-relative) form with forward slashes."""
    p = path.replace('\\', '/')
    if p.startswith('<'):
        return p          # ld.lld synthetic input, e.g. <internal> (merged string tables, stubs)
    if not re.match(r'^([A-Za-z]:)?/', p):
        p = os.path.normpath(os.path.join(link_cwd, p)).replace('\\', '/')
    else:
        p = os.path.normpath(p).replace('\\', '/')
    low = p.lower()
    if REPO and low.startswith(REPO.lower() + '/'):
        return p[len(REPO) + 1:]
    if SDK and low.startswith(SDK.lower() + '/'):
        return '$SDK/' + p[len(SDK) + 1:]
    return p


def category(n):
    if n.startswith('<'):
        return 'linker-internal'
    if n.startswith('$SDK/'):
        return 'sdk'
    if n.startswith('.cloud-deps/') or n.startswith('.jpeg-deps/') or n.startswith('.smbd-deps/') \
            or '/.smbd-deps/' in n:
        return 'third-party-dep'
    if '/generated/' in n or n.startswith('build/generated/'):
        return 'generated'
    if '/obj/_embed/' in n:
        return 'embed-blob'
    return 'first-party'


def split_input(spec):
    """'path/lib.a(member.o):(.text.x)' -> (file, member, section)."""
    m = re.match(r'^(.*?):\((.*)\)$', spec)
    if not m:
        return None
    f, sec = m.group(1), m.group(2)
    m2 = re.match(r'^(.*\.a)\((.*)\)$', f)
    if m2:
        return m2.group(1), m2.group(2), sec
    return f, '', sec


# ---------------------------------------------------------------- map ----
out_sections = OrderedDict()
contrib = defaultdict(lambda: defaultdict(int))   # (file, member) -> outsec -> bytes
insecs = []                                       # (outsec, file, member, insec, size)
cur_out = None
with open(map_path, 'r', errors='replace') as f:
    header = f.readline()
    for line in f:
        line = line.rstrip('\n')
        m = re.match(r'^\s*([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+(\d+) (.*)$', line)
        if not m:
            continue
        vma, lma, size, align, rest = m.groups()
        size = int(size, 16)
        indent = len(rest) - len(rest.lstrip(' '))
        text = rest.strip()
        if indent == 0:
            cur_out = text
            out_sections[cur_out] = (int(vma, 16), size)
        elif indent == 8:
            sp = split_input(text)
            if sp is None:
                continue      # linker script assignment / padding statement
            fpath, member, sec = sp
            key = (norm(fpath), member)
            contrib[key][cur_out] += size
            insecs.append((cur_out, key[0], member, sec, size))

with open(os.path.join(outdir, name + '-output-sections.tsv'), 'w', newline='\n') as f:
    f.write('section\tvma_hex\tsize_bytes\tsum_of_input_sections\n')
    sums = defaultdict(int)
    for o, _f, _m, _s, sz in insecs:
        sums[o] += sz
    for o, (vma, size) in out_sections.items():
        f.write('%s\t%x\t%d\t%d\n' % (o, vma, size, sums.get(o, 0)))

cols = ['.text', '.rodata', '.data', '.sdata', '.bss', '.sbss']
others = [o for o in out_sections if o not in cols and not o.startswith('.debug') and o not in
          ('.comment', '.symtab', '.shstrtab', '.strtab', '.pdr', '.mdebug.abi32', '.mdebug.abiN32',
           '.gnu.attributes', '.MIPS.abiflags', '.reginfo')]
with open(os.path.join(outdir, name + '-contrib-by-object.tsv'), 'w', newline='\n') as f:
    f.write('category\tfile\tmember\t' + '\t'.join(cols) + '\tother_alloc\ttotal_alloc\n')
    rows = []
    for (fp, member), d in contrib.items():
        vals = [d.get(c, 0) for c in cols]
        oth = sum(d.get(o, 0) for o in others)
        rows.append((sum(vals) + oth, category(fp), fp, member, vals, oth))
    rows.sort(key=lambda r: -r[0])
    for tot, cat, fp, member, vals, oth in rows:
        f.write('%s\t%s\t%s\t%s\t%d\t%d\n' % (cat, fp, member, '\t'.join(str(v) for v in vals), oth, tot))

# ------------------------------------------------------ why-extract ----
why = {}
if os.path.exists(why_path):
    with open(why_path, 'r', errors='replace') as f:
        for line in f:
            parts = line.rstrip('\n').split('\t')
            if len(parts) >= 3 and parts[0] != 'reference':
                ref, extracted, sym = parts[0], parts[1], parts[2]
                m2 = re.match(r'^(.*\.a)\((.*)\)$', extracted)
                if m2:
                    m3 = re.match(r'^(.*\.a)\((.*)\)$', ref)
                    refn = (norm(m3.group(1)) + '(' + m3.group(2) + ')') if m3 else norm(ref) if ref else ref
                    why[(norm(m2.group(1)), m2.group(2))] = (refn, sym)

with open(os.path.join(outdir, name + '-archive-members.tsv'), 'w', newline='\n') as f:
    f.write('category\tarchive\tmember\ttotal_alloc_bytes\tfirst_reference_from\tsymbol\n')
    rows = []
    keys = set(k for k in contrib if k[1]) | set(why)
    for k in keys:
        d = contrib.get(k, {})
        tot = sum(v for o, v in d.items() if o in cols or o in others)
        r = why.get(k, ('', ''))
        rows.append((k[0], -tot, k[1], r))
    rows.sort()
    for arch, ntot, member, r in rows:
        f.write('%s\t%s\t%s\t%d\t%s\t%s\n' % (category(arch), arch, member, -ntot, r[0], r[1]))

# ------------------------------------------------------- gc sections ----
elf_cache = {}
ar_cache = {}


def find_file(n):
    if n.startswith('$SDK/'):
        return SDK + '/' + n[5:]
    return REPO + '/' + n


def section_sizes(fn, member):
    key = (fn, member)
    if key in elf_cache:
        return elf_cache[key]
    real = find_file(fn)
    try:
        if member:
            if fn not in ar_cache:
                ar_cache[fn] = {}
                for mname, body in elfmini.ar_members(real):
                    ar_cache[fn].setdefault(mname, body)
            data = ar_cache[fn].get(member)
        else:
            with open(real, 'rb') as fh:
                data = fh.read()
        e = elfmini.Elf(data, fn)
        d = defaultdict(list)
        for s in e.sections:
            d[s['name']].append((s['size'], s['flags'], s['type']))
    except Exception as ex:  # pragma: no cover - evidence script, report and continue
        sys.stderr.write('warn: %s(%s): %s\n' % (fn, member, ex))
        d = {}
    elf_cache[key] = d
    return d


gc_rows = []
seen_count = defaultdict(int)
if os.path.exists(gc_path):
    with open(gc_path, 'r', errors='replace') as f:
        for line in f:
            m = re.search(r'removing unused section (.*)$', line.rstrip())
            if not m:
                continue
            sp = split_input(m.group(1))
            if sp is None:
                continue
            fpath, member, sec = sp
            fn = norm(fpath)
            lst = section_sizes(fn, member).get(sec, [])
            idx = seen_count[(fn, member, sec)]
            seen_count[(fn, member, sec)] += 1
            if idx < len(lst):
                size, flags, typ = lst[idx]
            elif lst:
                size, flags, typ = lst[-1]
            else:
                size, flags, typ = -1, 0, 0
            gc_rows.append((category(fn), fn, member, sec, size, flags, typ))


def kind(sec, flags, typ):
    if sec in ('.reginfo', '.MIPS.abiflags', '.pdr') or sec.startswith('.mdebug'):
        return 'metadata'      # per-object ABI records the linker always merges; not dead code
    if not flags & elfmini.SHF_ALLOC:
        return 'non-alloc'
    if flags & elfmini.SHF_EXEC:
        return 'code'
    if typ == elfmini.SHT_NOBITS:
        return 'bss'
    if flags & elfmini.SHF_WRITE:
        return 'data'
    return 'rodata'


with open(os.path.join(outdir, name + '-gc-discarded-all.tsv'), 'w', newline='\n') as f:
    f.write('category\tfile\tmember\tsection\tkind\tsize_bytes\n')
    for cat, fn, member, sec, size, flags, typ in sorted(gc_rows, key=lambda r: (r[0], r[1], r[2], -r[4])):
        f.write('%s\t%s\t%s\t%s\t%s\t%d\n' % (cat, fn, member, sec, kind(sec, flags, typ), size))
with open(os.path.join(outdir, name + '-gc-discarded-first-party.tsv'), 'w', newline='\n') as f:
    f.write('# zero-size sections and ABI metadata (.reginfo, .MIPS.abiflags, .pdr) are left out\n')
    f.write('category\tfile\tsection\tkind\tsize_bytes\n')
    for cat, fn, member, sec, size, flags, typ in sorted(gc_rows, key=lambda r: (r[1], -r[4])):
        k = kind(sec, flags, typ)
        if cat in ('first-party', 'embed-blob') and k not in ('metadata', 'non-alloc') and size != 0:
            f.write('%s\t%s\t%s\t%s\t%d\n' % (cat, fn + ('(' + member + ')' if member else ''), sec, k, size))

with open(os.path.join(outdir, name + '-gc-summary.txt'), 'w', newline='\n') as f:
    f.write('Link: %s\nSource: %s (ld.lld --print-gc-sections), sizes read from the input objects\n\n' % (name, os.path.basename(gc_path)))
    f.write('Total discarded sections: %d\n' % len(gc_rows))
    alloc = [r for r in gc_rows if kind(r[3], r[5], r[6]) not in ('metadata', 'non-alloc')]
    nz = [r for r in alloc if r[4] > 0]
    f.write('Discarded code/data sections (ABI metadata excluded): %d, of which non-empty: %d, %d bytes\n' % (
        len(alloc), len(nz), sum(r[4] for r in nz)))
    for want in ('first-party', 'embed-blob', 'sdk', 'third-party-dep', 'generated'):
        sub = [r for r in nz if r[0] == want]
        f.write('  %s: %d non-empty sections, %d bytes\n' % (want, len(sub), sum(r[4] for r in sub)))
    f.write('Sections whose size could not be read: %d\n\n' % sum(1 for r in gc_rows if r[4] < 0))
    f.write('category\tkind\tcount\tbytes\n')
    agg = defaultdict(lambda: [0, 0])
    for cat, fn, member, sec, size, flags, typ in gc_rows:
        k = (cat, kind(sec, flags, typ))
        if size == 0:
            k = (cat, k[1] + ' (empty)')
        agg[k][0] += 1
        agg[k][1] += max(size, 0)
    for k in sorted(agg):
        f.write('%s\t%s\t%d\t%d\n' % (k[0], k[1], agg[k][0], agg[k][1]))
    f.write('\nFirst-party and embed-blob discarded bytes per object, non-empty code/data sections (top 80):\nbytes\tcount\tfile\n')
    per = defaultdict(lambda: [0, 0])
    for cat, fn, member, sec, size, flags, typ in gc_rows:
        if cat in ('first-party', 'embed-blob') and size > 0 and kind(sec, flags, typ) not in ('metadata', 'non-alloc'):
            per[fn + ('(' + member + ')' if member else '')][0] += max(size, 0)
            per[fn + ('(' + member + ')' if member else '')][1] += 1
    for fn, (b, c) in sorted(per.items(), key=lambda kv: -kv[1][0])[:80]:
        f.write('%d\t%d\t%s\n' % (b, c, fn))

# ------------------------------------- objects contributing nothing ----
inputs = []
with open(inputs_path, 'r', errors='replace') as f:
    for tok in f.read().split():
        if tok.endswith('.o'):
            inputs.append(norm(tok))
alloc_out = set(cols) | set(others)
with open(os.path.join(outdir, name + '-objects-contributing-nothing.txt'), 'w', newline='\n') as f:
    f.write('# Objects on the %s link line with zero bytes in any allocated output section (from the map)\n' % name)
    n = 0
    for obj in inputs:
        d = contrib.get((obj, ''), {})
        tot = sum(v for o, v in d.items() if o in alloc_out)
        if tot == 0:
            f.write(obj + '\n')
            n += 1
    f.write('# count: %d of %d objects on the link line\n' % (n, len(inputs)))
print('%s: %d output sections, %d input files, %d gc rows, %d link objects' % (name, len(out_sections), len(contrib), len(gc_rows), len(inputs)))
