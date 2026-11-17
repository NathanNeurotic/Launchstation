"""Run every evidence script in one Python process (process start-up is slow on the build machine).

Usage: run_all.py BUILD_COPY SDK_ROOT RAW_DIR
  BUILD_COPY  the repository build copy (contains build/, build-ds34/, evidence-relink/)
  RAW_DIR     evidence/<Repo>/raw
"""
import glob
import os
import runpy
import shutil
import sys

repo, sdk, raw = [a.replace('\\', '/').rstrip('/') for a in sys.argv[1:4]]
here = os.path.dirname(os.path.abspath(__file__))
os.environ['REPO_ROOT'] = repo
os.environ['SDK_ROOT'] = sdk
rel = repo + '/evidence-relink'


def run(script, *args):
    sys.argv = [script] + list(args)
    try:
        runpy.run_path(os.path.join(here, script), run_name='__main__')
    except SystemExit as ex:
        if ex.code not in (0, None):
            print('!!', script, args, 'exit', ex.code)


for d in ('link', 'xref', 'sizes', 'link-raw'):
    os.makedirs(os.path.join(raw, d), exist_ok=True)

names = sorted(os.path.basename(m)[:-4] for m in glob.glob(rel + '/*.map'))
for n in names:
    cref = rel + '/%s.cref.txt' % n
    with open(cref, 'r', errors='replace') as f:
        lines = f.readlines()
    with open(rel + '/%s.gc-sections.log' % n, 'w', newline='\n') as f:
        f.writelines(l for l in lines if l.startswith('removing unused section'))
    with open(rel + '/%s.cref-only.txt' % n, 'w', newline='\n') as f:
        f.writelines(l for l in lines if not l.startswith('removing unused section'))
    run('analyze_link.py', n, rel + '/%s.map' % n, rel + '/%s.gc-sections.log' % n, rel + '/%s.why-extract.tsv' % n,
        rel + '/%s.link-inputs.txt' % n, repo + '/build', raw + '/link')
    run('xref_candidates.py', n, rel + '/%s.relink.elf' % n, raw + '/link/%s-contrib-by-object.tsv' % n,
        rel + '/%s.link-inputs.txt' % n, repo + '/build', raw + '/xref/%s-xref-candidates.tsv' % n)
    # path-normalised copies of the raw linker outputs
    for ext in ('.map', '.gc-sections.log', '.why-extract.tsv', '.cref-only.txt', '.link-command.txt'):
        src = rel + '/' + n + ext
        if not os.path.exists(src):
            continue
        with open(src, 'r', errors='replace') as f:
            text = f.read()
        for a, b in ((repo + '/', ''), (repo, '.'), (sdk, '$SDK')):
            text = text.replace(a, b).replace(a.replace('/', '\\'), b)
        with open(raw + '/link-raw/' + n + ext, 'w', newline='\n') as f:
            f.write(text)

run('report_sizes.py', repo, raw)
blobs = ['wLaunchELF=' + repo + '/build/obj/wLaunchELF/wLaunchELF.unstripped.elf',
         'wle_igr=' + repo + '/build/obj/wle_igr/wle_igr.unstripped.elf']
run('blob_duplicates.py', raw + '/sizes/embedded-blob-duplicates.tsv', *blobs)
for log, tag in (('build-pass1-ninja-k0.log', 'warnings-default-build'), ('build-ds34.log', 'warnings-ds34-build'),
                 ('extra-warnings-syntax-only.raw.log', 'warnings-extra-pass')):
    if os.path.exists(raw + '/' + log):
        run('group_warnings.py', raw + '/' + log, raw + '/%s.txt' % tag, raw + '/%s.tsv' % tag)
print('done')
