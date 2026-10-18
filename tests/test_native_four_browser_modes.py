"""Execute native Files display predicates and guard worker/render integration."""
from pathlib import Path
import shutil
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
src=(root/'src/dash_browser.c').read_text()
def function(sig):
 a=src.index(sig);b=src.index('\n}\n',a)+3
 return src[a:b]
c='#include <assert.h>\n'+function('static int fbShowsTitles(')+function('static int fbShowsDetails(')+r"""
int main(void) {
 const int titles[]={0,0,1,1},details[]={0,1,1,0};
 for(int mode=0;mode<4;mode++) {
  assert(fbShowsTitles(mode)==titles[mode]);
  assert(fbShowsDetails(mode)==details[mode]);
  /* Legacy title-consuming file operations work before or after #177. */
  int legacy=fbShowsTitles(mode)?2:mode;
  assert(legacy==(titles[mode]?2:mode));
 }
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix='native-browser-modes-') as folder:
 p=Path(folder);(p/'test.c').write_text(c);exe=p/'test.exe'
 subprocess.run([shutil.which('gcc') or shutil.which('cc'),'-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=10)
assert '"File names", "Names and details", "Game titles and details", "Game titles"' in src
assert 'dashModalChoice("Show", show_names, 4, fb_show)' in src
assert 'j->titles = (fbShowsTitles(fb_show) || fb_sort == FB_SORT_TITLE);' in src
assert 'int details = fbShowsDetails(fb_show) && !isMisc(fb.dir);' in src
assert 'if (fbShowsTitles(fb_show) && f->title[0])' in src
assert 'file_show = fbShowsTitles(fb_show) ? 2 : fb_show;' in src
print('PASS: four native display modes, title loading/fallback, details and legacy mapping')
