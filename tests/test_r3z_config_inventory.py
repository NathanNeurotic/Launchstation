"""Keep the reviewed configuration inventory and reachable UI map current."""
from pathlib import Path
import importlib.util
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, str(root / 'scripts/audit_r3z_settings.py')],
               cwd=root, check=True)

spec = importlib.util.spec_from_file_location('r3z_audit', root / 'scripts/audit_r3z_settings.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)

def rejects(callback):
    try:
        callback()
    except AssertionError:
        return
    raise AssertionError('Audit accepted a missing key family or disconnected route')

# Ordinary offline checks must reject deleting a mapped family, not just
# compare the remaining map as a subset of downstream keys.
saved = audit.ROWS.pop('APP_GameID')
try:
    rejects(audit.check)
finally:
    audit.ROWS['APP_GameID'] = saved

settings = (root / 'src/dash_settings.c').read_text()
browser = (root / 'src/dash_browser.c').read_text()
_, arrays = audit.check()
ui = ''.join(arrays.values())
rejects(lambda: audit.check_editor_routes(settings.replace('editLaunchMenu(it);', 'choosePreset();'), browser, ui))
rejects(lambda: audit.check_editor_routes(settings.replace('editLaunchKey(it, 1);', 'choosePreset();'), browser, ui))
rejects(lambda: audit.check_editor_routes(settings, browser.replace('case F_BOOKMARK:', 'case F_OTHER:'), ui))
print('PASS: offline inventory deletion and disconnected launch-key/bookmark routes are rejected')
