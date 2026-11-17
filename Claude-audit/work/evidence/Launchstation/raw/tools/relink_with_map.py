"""Re-run selected link steps of a ps2build/ninja build with extra, output-neutral linker flags.

Usage: relink_with_map.py BUILD_DIR NINJA_EXE OUT_DIR TARGET_OUTPUT [TARGET_OUTPUT ...]

For each TARGET_OUTPUT (the ninja output path of a link edge, e.g. obj/wLaunchELF/wLaunchELF.unstripped.elf)
the exact link command is taken from `ninja -t compdb -x` (response file expanded), the output is redirected
to OUT_DIR/<name>.relink.elf and these flags are appended:
    -Map=OUT_DIR/<name>.map --print-gc-sections --why-extract=OUT_DIR/<name>.why-extract.tsv --cref
No tracked file and no build output is modified. The relinked file is compared byte for byte with the
build's own output and the result is printed.
"""
import json
import os
import shlex
import subprocess
import sys

build_dir, ninja, out_dir = sys.argv[1:4]
targets = sys.argv[4:]
os.makedirs(out_dir, exist_ok=True)

db = json.loads(subprocess.run([ninja, '-C', build_dir, '-t', 'compdb', '-x', 'ee_link', 'ee_link_cxx',
                                'ee_link_rsp', 'iop_link'], capture_output=True, text=True, check=True).stdout)
by_out = {e['output'].replace('\\', '/'): e for e in db}

for t in targets:
    e = by_out[t]
    name = os.path.basename(t).split('.')[0]
    args = shlex.split(e['command'], posix=True)
    relinked = os.path.join(out_dir, name + '.relink.elf').replace('\\', '/')
    i = args.index('-o')
    args[i + 1] = relinked
    inputs = [a for a in args[1:] if a.endswith('.o')]
    with open(os.path.join(out_dir, name + '.link-inputs.txt'), 'w', newline='\n') as f:
        f.write('\n'.join(inputs) + '\n')
    with open(os.path.join(out_dir, name + '.link-command.txt'), 'w', newline='\n') as f:
        f.write(' '.join(args) + '\n')
    args += ['-Map=' + os.path.join(out_dir, name + '.map').replace('\\', '/'), '--print-gc-sections',
             '--why-extract=' + os.path.join(out_dir, name + '.why-extract.tsv').replace('\\', '/'), '--cref']
    p = subprocess.run(args, cwd=build_dir, capture_output=True, text=True, errors='replace')
    with open(os.path.join(out_dir, name + '.gc-sections.log'), 'w', newline='\n') as f:
        f.write(p.stderr)
    with open(os.path.join(out_dir, name + '.cref.txt'), 'w', newline='\n') as f:
        f.write(p.stdout)
    same = 'n/a'
    orig = os.path.join(build_dir, t)
    if p.returncode == 0 and os.path.exists(orig):
        with open(orig, 'rb') as a, open(relinked, 'rb') as b:
            same = 'IDENTICAL' if a.read() == b.read() else 'DIFFERENT'
    print('%s: exit %d, relinked output vs build output: %s, %d object inputs' % (t, p.returncode, same, len(inputs)))
