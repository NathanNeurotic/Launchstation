"""#346: Launchstation keeps its settings in LAUNCHSTATION.CNF only.

Other wLaunchELF builds read mc?:/SYS-CONF/LAUNCHELF.CNF, and our
translated MISC/ launch-key names broke them. That file is never read or
written, so a new install always starts from the built-in defaults.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
boot = (root / 'src/main_boot.c').read_text()

assert '"LAUNCHSTATION.CNF"' in boot
assert boot.count('loadConfig(main_msg,') == 1, 'only our own file is loaded'

# The legacy name appears in no source line outside comments.
hits = []
for p in list((root / 'src').glob('*.c')) + list((root / 'include').glob('*.h')):
    for n, line in enumerate(p.read_text(errors='replace').splitlines(), 1):
        code = line.split('//')[0]
        if 'LAUNCHELF.CNF' in code and '"' in code and '/*' not in code:
            hits.append(f'{p.name}:{n}')
assert hits == [], hits
print('ok')
