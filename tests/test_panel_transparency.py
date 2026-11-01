"""Check actual slider expressions against opacity endpoints and saved alpha."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/dash_settings.c').read_text()
getter = re.search(r'case S_T_PANEL_A:\s*\*names = pct_names, \*cur = ([^;]+);', source).group(1)
setter = re.search(r'case S_T_PANEL_A:\s*t->panel.a = ([^;]+);', source).group(1)
program = r'''
#include <assert.h>
typedef unsigned char u8;
typedef struct { struct { u8 a; } panel; } Theme;
static Theme dashTheme;
static int choice(void) { return GETTER; }
static void set(int v) { Theme *t=&dashTheme; t->panel.a=SETTER; }
int main(void) {
    set(0); assert(dashTheme.panel.a==255 && choice()==0);
    set(10); assert(dashTheme.panel.a==0 && choice()==10);
    for(int v=0;v<=10;v++) { set(v); assert(choice()==v); }
    for(int alpha=0;alpha<=255;alpha++) {
        dashTheme.panel.a=(u8)alpha;
        assert(choice()>=0 && choice()<=10 && dashTheme.panel.a==alpha);
    }
}
'''.replace('GETTER', getter).replace('SETTER', setter)
cc = shutil.which('gcc') or shutil.which('clang')
assert cc, 'Host C compiler required'
with tempfile.TemporaryDirectory(prefix='panel-transparency-') as folder:
    path, exe = Path(folder)/'test.c', Path(folder)/'test'
    path.write_text(program)
    subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', str(path), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=5)
print('PASS: transparency endpoints, all 11 choices and all 256 saved alpha values')
