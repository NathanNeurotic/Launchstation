"""Exercise the production runAction gate and MISC case, not only menu presence."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/dash_settings.c').read_text()
start = source.index('static void runAction(')
gate = source[source.index('{', start) + 1:source.index('switch (id)', start)]
start = source.index('case S_MISC_COMMANDS:', start)
misc = source[start:source.index('case S_CLOCK_HARDWARE:', start)]
ids = source[source.index('enum SetId {'):source.index('};', source.index('enum SetId {')) + 2]
program = r'''
#include <assert.h>
#include <string.h>
#define SETTING_LK_RIGHT 14
#define COLOR_GRAPH4 7
''' + ids + r'''
static struct {const char *Misc;} config={"MISC/"},*setting=&config;
static int theme_calls,browser_calls;
static void runThemeAction(int id){assert(id>=S_T_PRESET && id<=S_T_RESETCAT);theme_calls++;}
static void dashOpenBrowser(const char *path){assert(!strcmp(path,"MISC/"));browser_calls++;}
static void runAction(int id){
''' + gate + 'switch(id){' + misc + 'default:break;}}\n' + r'''
int main(void){
 runAction(S_MISC_COMMANDS);assert(browser_calls==1 && !theme_calls);
 for(int id=S_T_PRESET;id<=S_T_RESETCAT;id++)runAction(id);
 assert(theme_calls==S_T_RESETCAT-S_T_PRESET+1 && browser_calls==1);
}
'''
with tempfile.TemporaryDirectory(prefix='misc-dispatch-') as td:
    path = Path(td)
    (path / 'test.c').write_text(program)
    subprocess.run([shutil.which('cc') or shutil.which('gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    str(path / 'test.c'), '-o', str(path / 'test.exe')], check=True)
    subprocess.run([str(path / 'test.exe')], check=True)
print('PASS: actual Settings MISC action reaches the browser; theme-action range still dispatches to themes')
