"""Execute the hold-input wrapper: menu suppression, validity and nested state."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/dash_ui.c').read_text()
def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

poll = function('void dashInputPollEx(')
assert poll.index('physical_sample_valid = 0;') < poll.index('if (dashIopParked())')
assert poll.index('readpad_no_KB()') < poll.index('physical_sample_valid = ret > 0;')
code = r'''
#include <assert.h>
typedef struct { int start; } DashInput;
static int dash_start_menu_blocked,physical_sample_valid,reported,menus,polls;
static void dashInputPollEx(DashInput *in,int usb) {
    assert(!usb);polls++;
    if(in->start && !dash_start_menu_blocked)menus++;
    physical_sample_valid=reported;
}
'''+function('int dashInputPollPhysical(')+r'''
int main(void) {
    for(int blocked=0;blocked<2;blocked++)for(int valid=0;valid<2;valid++) {
        DashInput input={1};dash_start_menu_blocked=blocked;reported=valid;
        physical_sample_valid=!valid;
        assert(dashInputPollPhysical(&input)==valid);
        assert(dash_start_menu_blocked==blocked && !menus);
    }
    assert(polls==4);
}
'''
cc = shutil.which('gcc') or shutil.which('clang')
assert cc, 'Host C compiler required'
with tempfile.TemporaryDirectory(prefix='physical-hold-input-') as folder:
    path, exe = Path(folder)/'test.c', Path(folder)/'test'
    path.write_text(code)
    subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', str(path), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=5)
print('PASS: physical hold polling suppresses START menus, reports validity and restores nested state')
