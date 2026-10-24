"""Exercise identity stamping independently of any old ignored build header."""
from pathlib import Path
import subprocess
import sys
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
