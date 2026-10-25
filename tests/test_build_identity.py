"""Exercise identity stamping independently of any old ignored build header."""
from pathlib import Path
import subprocess
import sys
import shutil
import tempfile

root = Path(__file__).resolve().parents[1]
sha = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
date = subprocess.check_output(['git', 'show', '-s', '--format=%cs', 'HEAD'], cwd=root, text=True).strip()
with tempfile.TemporaryDirectory(prefix='build-identity-') as folder:
    output = Path(folder) / 'githash.h'
    output.write_text('#define GIT_HASH "stale-build"\n')
    args = [sys.executable, str(root / 'scripts/gen_githash.py'), '--require-git', '--output', str(output)]
    subprocess.run(args, cwd=folder, check=True)
    content = output.read_text()
    assert f'#define GIT_HASH "{sha[:12]}"' in content
    assert f'#define ULE_VERDATE "{date}"' in content
    timestamp = output.stat().st_mtime_ns
    subprocess.run(args, cwd=folder, check=True)
    assert output.read_text() == content and output.stat().st_mtime_ns == timestamp
workflow = (root / '.github/workflows/compile.yml').read_text()
assert workflow.index('python3 scripts/gen_githash.py --require-git') < workflow.index('ps2build build\n')
print('PASS: stale header replaced with checkout identity; stable reruns; CI stamps before target builds')

# An unpacked source tree nested in another checkout has no own identity.
with tempfile.TemporaryDirectory(prefix='archive-identity-') as folder:
    parent = Path(folder)
    subprocess.run(['git', 'init', str(parent)], check=True, capture_output=True)
    subprocess.run(['git', '-C', str(parent), '-c', 'user.name=Fixture',
                    '-c', 'user.email=fixture@example.invalid', 'commit',
                    '--allow-empty', '-m', 'Parent repository'], check=True, capture_output=True)
    archive = parent / 'unpacked'
    (archive / 'scripts').mkdir(parents=True)
    shutil.copyfile(root / 'scripts/gen_githash.py', archive / 'scripts/gen_githash.py')
    script = archive / 'scripts/gen_githash.py'
    result = subprocess.run([sys.executable, str(script), '--require-git'], capture_output=True)
    assert result.returncode != 0 and not (archive / 'githash.h').exists()
    subprocess.run([sys.executable, str(script)], check=True, capture_output=True)
    assert '#define GIT_HASH "local"' in (archive / 'githash.h').read_text()
