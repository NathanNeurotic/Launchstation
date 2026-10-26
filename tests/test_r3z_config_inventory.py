"""Keep the reviewed configuration inventory and reachable UI map current."""
from pathlib import Path
import importlib.util
import re
import subprocess
import sys
from unittest.mock import patch

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
# Remove only the complete editor ID, leaving S_GAMEID_DISC reachable.
# Patch the source read so this exercises the complete routine audit.
original_read_text = Path.read_text
disconnected = re.sub(r'\bS_GAMEID\b', 'S_DISCONNECTED_GAMEID', settings)
assert 'S_GAMEID_DISC' in disconnected
def read_with_disconnected_editor(path, *args, **kwargs):
    if path == root / 'src/dash_settings.c':
        return disconnected
    return original_read_text(path, *args, **kwargs)

with patch.object(Path, 'read_text', read_with_disconnected_editor):
    try:
        audit.check()
    except AssertionError as error:
        assert error.args == (('APP_GameID', 'unreachable control'),), error
    else:
        raise AssertionError('Audit accepted S_GAMEID_DISC in place of S_GAMEID')

browser = (root / 'src/dash_browser.c').read_text()
_, arrays = audit.check()
ui = ''.join(arrays.values())
rejects(lambda: audit.check_editor_routes(settings.replace('editLaunchMenu(it);', 'choosePreset();'), browser, ui))
rejects(lambda: audit.check_editor_routes(settings.replace('editLaunchKey(it, 1);', 'choosePreset();'), browser, ui))
rejects(lambda: audit.check_editor_routes(settings, browser.replace('case F_BOOKMARK:', 'case F_OTHER:'), ui))
print('PASS: offline inventory deletion and disconnected launch-key/bookmark routes are rejected')
