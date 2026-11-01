"""Execute retained filename/USB rules and list bounds; shared UI has its own suite."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/dash_widgets.c').read_text()
def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define PS2KBD_ESCAPE_KEY 0x1b
static struct { int usbkbd_used; } cfg={1},*setting=&cfg;
static int busy,parked;
static unsigned char usb[80];static int usb_n,usb_i;
static int dashWorkerIoBusy(void) { return busy; }
static int dashIopParked(void) { return parked; }
static int ensureUsbKeyboardReady(void) { return 1; }
static int PS2KbdRead(char *out) { if(usb_i==usb_n)return 0;*out=usb[usb_i++];return 1; }
'''
code += (root / 'include/list_scroll.h').read_text()
for signature in ('static void kbInsert(', 'static void kbDelete(', 'static int kbFilenameChar(',
                  'static int kbFilenameValid(', 'static int kbUsb('):
    code += function(signature)
code += r'''
int main(void) {
    char name[256];int cur;
    for(int c=0;c<256;c++) {
        int bad=c<32 || c==127 || strchr("<>:\"/\\|?*",c)!=NULL;
        assert(kbFilenameChar((unsigned char)c)==!bad);
    }
    assert(kbFilenameValid("My save.PSU",255));
    assert(!kbFilenameValid("",255) && !kbFilenameValid(".",255) && !kbFilenameValid("..",255));
    assert(!kbFilenameValid("BAD/NAME",255) && !kbFilenameValid("BAD.",255) && !kbFilenameValid("BAD ",255));
    assert(!kbFilenameValid("LONG",3));
    strcpy((char *)usb,"A:/*?B");usb_n=6;name[0]=0;cur=0;
    assert(!kbUsb(name,255,&cur,1) && !strcmp(name,"AB"));
    usb_i=0;name[0]=0;cur=0;assert(!kbUsb(name,255,&cur,0) && !strcmp(name,"A:/*?B"));
    usb_i=0;busy=1;assert(!kbUsb(name,255,&cur,0) && !usb_i);
    busy=0;parked=1;assert(!kbUsb(name,255,&cur,0) && !usb_i);
    parked=0;setting->usbkbd_used=0;assert(!kbUsb(name,255,&cur,0) && !usb_i);
    strcpy(name,"AB");cur=1;kbInsert(name,3,&cur,'x');assert(!strcmp(name,"AxB"));
    kbInsert(name,3,&cur,'y');assert(!strcmp(name,"AxB"));
    kbDelete(name,&cur);assert(!strcmp(name,"AB"));
    for(int rows=0;rows<24;rows++)for(int count=0;count<250;count++)for(int sel=-1;sel<=count;sel++) {
        int top=listCenteredTop(sel,count,rows);
        if(rows<=0 || count<=rows) { assert(!top);continue; }
        int chosen=sel<0?0:sel>=count?count-1:sel;
        assert(top>=0 && top<=count-rows && chosen>=top && chosen<top+rows);
        if(top>0 && top<count-rows)assert(chosen-top==(rows-1)/2);
    }
    puts("PASS: all portable-name bytes, validation, USB input gates/bounds and centered list boundaries");
}
'''
cc = shutil.which('gcc') or shutil.which('clang')
assert cc, 'Host C compiler required'
with tempfile.TemporaryDirectory(prefix='launchstation-portable-names-') as folder:
    path, exe = Path(folder)/'test.c', Path(folder)/'test'
    path.write_text(code)
    subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', str(path), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
