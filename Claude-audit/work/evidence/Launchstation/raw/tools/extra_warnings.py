"""Re-run every compile command from compile_commands.json with -fsyntax-only and extra warning flags.

Usage: extra_warnings.py COMPILE_COMMANDS_JSON SDK_ROOT OUT_LOG [jobs]

Nothing is written to the build tree: the output (-o) and dependency-file arguments are removed and
-fsyntax-only is added. The compiler is called directly (clang.exe --config=<cfg>) instead of through the
SDK .bat wrapper; the wrapper does exactly that.
"""
import ast
import concurrent.futures
import json
import subprocess
import sys

cc_json, sdk, out_log = sys.argv[1:4]
jobs = int(sys.argv[4]) if len(sys.argv) > 4 else 12
sdk = sdk.replace('\\', '/').rstrip('/')
CLANG = sdk + '/llvm-toolchain/llvm/bin/clang.exe'
EXTRA = ['-fsyntax-only', '-Wall', '-Wextra', '-Wunused', '-Wunused-macros', '-Wunused-parameter',
         '-Wunreachable-code', '-Wmissing-prototypes', '-Wmissing-variable-declarations', '-Wshadow',
         '-Wimplicit-function-declaration', '-Wredundant-decls', '-Wduplicate-decl-specifier',
         '-Wno-sign-compare', '-Wno-missing-field-initializers', '-fno-color-diagnostics',
         '-fno-caret-diagnostics', '-fno-diagnostics-fixit-info', '-ferror-limit=0']

with open(cc_json) as f:
    db = json.load(f)


def run(entry):
    args = entry['arguments']
    if isinstance(args, str):
        args = ast.literal_eval(args)
    wrapper = args[0].replace('\\', '/')
    cfg = wrapper[:-4] + '.cfg'
    rest = []
    skip = False
    for a in args[1:]:
        if skip:
            skip = False
            continue
        if a in ('-o', '-MF'):
            skip = True
            continue
        if a in ('-c', '-MMD', '-MD'):
            continue
        rest.append(a)
    if entry['file'].lower().endswith(('.s',)):
        return entry['file'], 0, ''
    cmd = [CLANG, '--config=' + cfg] + rest + EXTRA
    p = subprocess.run(cmd, cwd=entry['directory'], capture_output=True, text=True, errors='replace')
    return entry['file'], p.returncode, p.stderr + p.stdout


with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex, open(out_log, 'w', newline='\n') as log:
    n = bad = 0
    for fn, rc, out in ex.map(run, db):
        n += 1
        if rc:
            bad += 1
        log.write('### %s (exit %d)\n' % (fn, rc))
        log.write(out)
    log.write('### done: %d translation units, %d with non-zero exit\n' % (n, bad))
print('extra warnings pass: %d TUs, %d non-zero exits' % (n, bad))
