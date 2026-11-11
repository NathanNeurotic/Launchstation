"""#346: Launchstation keeps its settings in LAUNCHSTATION.CNF.

Other wLaunchELF builds read mc?:/SYS-CONF/LAUNCHELF.CNF too, and our
translated MISC/ launch-key names broke them. The old file may only be
read (one-time import when ours is missing); nothing may write it.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
boot = (root / 'src/main_boot.c').read_text()

own = boot.index('"LAUNCHSTATION.CNF"')
legacy = boot.index('"LAUNCHELF.CNF"')
assert own < legacy, 'own file must be tried before the legacy import'
assert boot.index('loadConfig(main_msg, cnf_path_buf)') < legacy
assert 'loadConfig(main_msg, legacy)' in boot
# After an import, the remembered path names our file in the same folder.
assert re.search(r'snprintf\(slash \+ 1,[^;]*cnf_path_buf\)', boot)

# The legacy name exists as a string literal only in that import.
hits = []
for p in list((root / 'src').glob('*.c')) + list((root / 'include').glob('*.h')):
    for n, line in enumerate(p.read_text(errors='replace').splitlines(), 1):
        code = line.split('//')[0]
        if '"LAUNCHELF.CNF"' in code or 'LAUNCHELF.CNF' in code and '"' in code and '/*' not in code:
            hits.append(f'{p.name}:{n}')
assert hits == [f'main_boot.c:{boot[:legacy].count(chr(10)) + 1}'], hits
print('ok')
