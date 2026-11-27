"""Cross-reference candidates for one link.

Usage: xref_candidates.py NAME FINAL_UNSTRIPPED_ELF CONTRIB_TSV RSP_OR_INPUTLIST LINKCWD OUT_TSV

For every first-party object on the link line: global (non-static) symbols it defines that no OTHER
input of the same link (objects on the link line plus the archive members the map shows were pulled in)
leaves undefined. This is a CANDIDATE list only: a symbol listed here may still be used inside its own
object (see relocs_in_own_object), through a function-pointer table in the same file, from assembly,
by name from the linker script or crt0, or in another build variant.
"""
import os
import re
import struct
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import elfmini  # noqa: E402

name, final_elf, contrib_tsv, inputs_path, link_cwd, out_tsv = sys.argv[1:7]
REPO = os.environ.get('REPO_ROOT', '').replace('\\', '/').rstrip('/')
SDK = os.environ.get('SDK_ROOT', '').replace('\\', '/').rstrip('/')


def norm(path):
    p = path.replace('\\', '/')
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


def real(n):
    return SDK + '/' + n[5:] if n.startswith('$SDK/') else REPO + '/' + n


def is_first_party(n):
    return not (n.startswith('$SDK/') or n.startswith('.cloud-deps/') or n.startswith('.jpeg-deps/')
                or '.smbd-deps/' in n or '/generated/' in n)


SKIP_RELOC_TARGETS = ('.pdr', '.eh_frame', '.debug', '.mdebug', '.gcc_except_table', '.gnu.attributes')


def load(data, label):
    e = elfmini.Elf(data, label)
    syms = e.symbols
    refs = defaultdict(int)
    for s in e.sections:
        if s['type'] not in (4, 9):  # SHT_RELA, SHT_REL
            continue
        tgt = e.sections[s['info']]['name'] if s['info'] < len(e.sections) else ''
        if tgt.startswith(SKIP_RELOC_TARGETS):
            continue
        ent = s['entsize'] or (8 if s['type'] == 9 else 12)
        for i in range(s['size'] // ent):
            r_info = struct.unpack_from('<I', data, s['offset'] + i * ent + 4)[0]
            refs[r_info >> 8] += 1
    defined, undefined = [], set()
    for idx, sy in enumerate(syms):
        if sy['bind'] == 'LOCAL' or not sy['name']:
            continue
        if sy['shndx'] == elfmini.SHN_UNDEF:
            undefined.add(sy['name'])
        else:
            if sy['shndx'] < len(e.sections):
                sec = e.sections[sy['shndx']]
                secname = sec['name']
            elif sy['shndx'] == elfmini.SHN_COMMON:
                secname = 'COMMON'
            else:
                secname = 'ABS'
            defined.append((sy['name'], sy['type'], sy['size'], sy['bind'], secname, refs.get(idx, 0)))
    return defined, undefined


objects = []
with open(inputs_path, 'r', errors='replace') as f:
    for tok in f.read().split():
        if tok.endswith('.o'):
            objects.append(norm(tok))

members = set()
with open(contrib_tsv, 'r') as f:
    next(f)
    for line in f:
        p = line.rstrip('\n').split('\t')
        if p[2]:
            members.add((p[1], p[2]))

per_obj = {}
undef_by = defaultdict(set)   # symbol -> set(files that leave it undefined)
for o in objects:
    with open(real(o), 'rb') as fh:
        d, u = load(fh.read(), o)
    per_obj[o] = d
    for s in u:
        undef_by[s].add(o)
by_arch = defaultdict(set)
for a, m in members:
    by_arch[a].add(m)
for a, ms in by_arch.items():
    try:
        for mname, body in elfmini.ar_members(real(a)):
            if mname in ms and body[:4] == b'\x7fELF':
                d, u = load(body, a + '(' + mname + ')')
                for s in u:
                    undef_by[s].add(a + '(' + mname + ')')
                if is_first_party(a):
                    per_obj[a + '(' + mname + ')'] = d
    except Exception as ex:  # pragma: no cover
        sys.stderr.write('warn: %s: %s\n' % (a, ex))

final_syms = {}
with open(final_elf, 'rb') as fh:
    fe = elfmini.Elf(fh.read(), final_elf)
for sy in fe.symbols:
    if sy['name'] and sy['shndx'] != elfmini.SHN_UNDEF:
        final_syms[sy['name']] = sy

rows = []
for o, defs in per_obj.items():
    if not is_first_party(o):
        continue
    for sname, typ, size, bind, secname, nrel in defs:
        others = [x for x in undef_by.get(sname, ()) if x != o]
        if others:
            continue
        rows.append((o, sname, typ, size, bind, secname, nrel, 'yes' if sname in final_syms else 'no'))

rows.sort(key=lambda r: (r[0], -r[3], r[1]))
with open(out_tsv, 'w', newline='\n') as f:
    f.write('# CANDIDATE LIST ONLY (%s link). Global symbols defined in a first-party object and left undefined by no other\n' % name)
    f.write('# input of the same link. relocs_in_own_object counts relocations against the symbol inside its own object\n')
    f.write('# (0 = no reference found anywhere in this link; >0 = used only inside its own file, so it could be static).\n')
    f.write('# in_final_elf=no means the linker dropped it (--gc-sections) or it was never extracted.\n')
    f.write('# Not checked here: other build variants (DS34), calls by name from crt0/linker script, IRX export tables in other links.\n')
    f.write('object\tsymbol\ttype\tsize\tbind\tsection\trelocs_in_own_object\tin_final_elf\n')
    for r in rows:
        f.write('%s\t%s\t%s\t%d\t%s\t%s\t%d\t%s\n' % r)
n0 = sum(1 for r in rows if r[6] == 0)
print('%s: %d candidates (%d with no relocation in own object, %d kept in final ELF)' % (
    name, len(rows), n0, sum(1 for r in rows if r[7] == 'yes')))
