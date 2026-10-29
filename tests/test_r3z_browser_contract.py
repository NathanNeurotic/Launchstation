"""Execute production page, centred viewport, hardware and control policies."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
browser = (root / 'src/dash_browser.c').read_text()
init = (root / 'src/init.c').read_text()
main = (root / 'src/dash_main.c').read_text()

def function(source, signature):
    start = source.index(signature)
    pos = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos] + '\n'

code = r'''
#include <assert.h>
#include <string.h>
#define FB_ROWS 10
#define MMCE 1
#define MX4SIO 1
static struct { int sel,top,n,ndevs; float sel_anim; char dir[20]; } fb;
static int console_is_PSX;
static char ROMVER_data[16];
enum { STORAGE_STACK_DEFAULT, STORAGE_STACK_MMCE, STORAGE_STACK_MX4SIO };
static int have_mmce,storage_driver_stack_mode,mx4sio_driver_running;
static int loads,core_calls,load_ok=1;
static const char mmceman_irx[1]={0};
static int size_mmceman_irx=1;
static void ensureCoreIoStackReady(void) { core_calls++; }
static int SifExecModuleBuffer(const void *p,int size,int argc,const char *argv,int *ret) {
    (void)p;(void)size;(void)argc;(void)argv;loads++;*ret=load_ok?0:-1;return load_ok?1:-1;
}
'''
code += (root / 'include/list_scroll.h').read_text()
code += function(browser, 'static int pageIndex(')
code += function(browser, 'static void revealSelection(')
code += function(init, 'int consoleHasIlink(void)')
code += function(init, 'int ensureMmceGameIdControl(void)')
code += r'''
int main(void) {
    assert(pageIndex(0,0,1)==0);
    for(int n=1;n<40;n++) for(int i=0;i<n;i++) {
        int right=pageIndex(i,n,1),left=pageIndex(i,n,-1);
        assert(right==(i==n-1?0:(i+10<n?i+10:n-1)));
        assert(left==(i==0?n-1:(i<10?0:i-10)));
        for(int root=0;root<2;root++) {
            strcpy(fb.dir,root?"":"usb0:/");fb.n=root?0:n;fb.ndevs=root?n:0;
            fb.sel=i;fb.top=0;fb.sel_anim=-20;revealSelection();
            assert(fb.top>=0 && fb.top<=(n>10?n-10:0));
            assert(i>=fb.top && i<fb.top+10);
            if(i>=4 && i-4<=n-10) assert(fb.top==i-4);
            assert(fb.sel_anim==i);
        }
    }
    const char *roms[]={"0100JC20000117","0160AC20020207","0170JC20030206","0190EC20040623","0230EC20080220","","bad!JC","0200JD20040614"};
    const int expected[]={1,1,0,0,0,1,1,1};
    for(int i=0;i<8;i++) {
        strcpy(ROMVER_data,roms[i]);console_is_PSX=0;assert(consoleHasIlink()==expected[i]);
        console_is_PSX=1;assert(consoleHasIlink());
    }
    storage_driver_stack_mode=STORAGE_STACK_DEFAULT;
    assert(ensureMmceGameIdControl() && loads==1 && core_calls==1);
    assert(storage_driver_stack_mode==STORAGE_STACK_MMCE);
    assert(ensureMmceGameIdControl() && loads==1);
    have_mmce=0;storage_driver_stack_mode=STORAGE_STACK_MX4SIO;
    assert(!ensureMmceGameIdControl() && loads==1);
    storage_driver_stack_mode=STORAGE_STACK_DEFAULT;mx4sio_driver_running=1;
    assert(!ensureMmceGameIdControl() && loads==1);
    mx4sio_driver_running=0;load_ok=0;
    assert(!ensureMmceGameIdControl() && storage_driver_stack_mode==STORAGE_STACK_DEFAULT);
}
'''
with tempfile.TemporaryDirectory(prefix='r3z-browser-') as td:
    path = Path(td)
    (path / 'test.c').write_text(code)
    subprocess.run([shutil.which('cc') or shutil.which('gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror', str(path/'test.c'), '-o', str(path/'test.exe')], check=True)
    subprocess.run([str(path/'test.exe')], check=True)

open_browser = function(main, 'void dashOpenBrowser(')
assert 'dashSwitchSection(DASH_SEC_FILES)' in open_browser
assert 'dashFileBrowserTabOpen' in open_browser and 'dashFileBrowser(' not in open_browser
assert '#define FB_ROWS 10' in browser
# Ten rows end at virtual y=350, within the 448-high virtual canvas in both modes.
assert '#define FB_Y 100.0f' in browser and '#define FB_ROW_H 25.0f' in browser
print('PASS: exhaustive page/viewport matrix, iLink PSX exception, non-reset MMCE control, unified routes')
