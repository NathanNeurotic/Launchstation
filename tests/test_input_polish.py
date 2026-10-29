"""Exercise filename input, text-only repeat consumers and centered viewports."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/dash_widgets.c').read_text()

def function(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'

header = (root / 'include/dash_internal.h').read_text()
inputs = header[header.rfind('enum {', 0, header.index('DASH_IN_UP')):
                header.index('void dashInputPoll(DashInput')]
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
'''+inputs+(root/'include/list_scroll.h').read_text()+r'''
#define VKEY_LAYOUT_COLS 16
#define VKEY_LAYOUT_ROWS 5
#define DASH_VW 640
#define DASH_VH 448
#define PS2KBD_ESCAPE_KEY 0x1b
#define DASH_TOAST_WARN 1
#define DASH_SND_CURSOR 1
#define DASH_SND_CANCEL 2
#define DASH_SND_CONFIRM 3
static struct {int usbkbd_used,virtual_keyboard_layout;} cfg={1,0},*setting=&cfg;
static int busy,parked,pending,frames,toasts,flushes,scenario;
static char typed='A';
static unsigned char usb[80];static int usb_n,usb_i;
static DashInput events[40];
static int normalizeVirtualKeyboardLayout(int n) {return n;}
static int getVirtualKeyboardLayoutFirstColumn(int n) {(void)n;return 0;}
static int getVirtualKeyboardLayoutColumnCount(int n) {(void)n;return 12;}
static int getVirtualKeyboardLayoutNextKey(int n,int sel,int dx,int dy) {(void)n;return sel+dx+dy*16;}
static int isVirtualKeyboardLayoutKey(int n,int sel) {(void)n;return sel>=0 && sel<80;}
static char getVirtualKeyboardLayoutChar(int n,int sel,int caps) {(void)n;(void)sel;return caps?'Z':typed;}
static int dashWorkerIoBusy(void) {return busy;}
static int dashIopParked(void) {return parked;}
static int ensureUsbKeyboardReady(void) {return 1;}
static int PS2KbdRead(char *out) {if(usb_i==usb_n)return 0;*out=usb[usb_i++];return 1;}
static void dashInputFlush(void) {flushes++;}
static void dashStrCopy(char *out,const char *in,int n) {snprintf(out,n,"%s",in);}
static void dashSoundPlay(int n) {(void)n;}
static void dashToast(int n,const char *s) {(void)n;(void)s;toasts++;}
static int dashDashboardPending(void) {return pending;}
static void dashInputPollEx(DashInput *in,int keyboard) {
 assert(!keyboard && frames<40);*in=events[frames++];
 if(scenario==1 && frames==2) {strcpy((char *)usb,"X:?.ELF\n");usb_n=8;usb_i=0;}
}
'''
code += source[source.index('enum {'):source.index('static const char *const kb_act_names')]
for name in ('static void kbInsert(', 'static void kbDelete(', 'static int kbFilenameChar(',
             'static int kbFilenameValid(', 'static int kbUsb('):
    code += function(source, name)
# Run the production modal input/control flow. Rendering alone is excluded;
# both real render paths are compiled by the standard/DS34 ps2build checks.
modal = function(source, 'static int modalKeyboard(')
cut = modal.index('\n\t\topen = dashApproach(')
end = modal.index('\n\tif (!result)', cut)
modal = modal[:cut]+'\n\t}\n'+modal[end:]
code += modal
code += r'''
static void reset(void) {
 busy=parked=pending=frames=toasts=flushes=scenario=usb_n=usb_i=0;typed='A';
 memset(events,0,sizeof(events));
}
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
 /* Real USB insertion filters names but free-form text retains punctuation. */
 reset();strcpy((char *)usb,"A:/*?B");usb_n=6;name[0]=0;cur=0;
 assert(!kbUsb(name,255,&cur,1) && !strcmp(name,"AB"));
 usb_i=0;name[0]=0;cur=0;assert(!kbUsb(name,255,&cur,0) && !strcmp(name,"A:/*?B"));
 /* Hold-to-type repeats; Caps is a one-press action even while held. */
 reset();name[0]=0;events[0].pressed=DASH_IN_OK;events[0].text_repeat=DASH_IN_OK;
 events[1].text_repeat=DASH_IN_OK;events[2].pressed=DASH_IN_FAV;
 events[3].text_repeat=DASH_IN_OK;events[4].pressed=DASH_IN_SELECT;
 assert(modalKeyboard("Text",name,255,0,NULL) && !strcmp(name,"AAZ"));
 /* Pad Delete repeats, but SELECT still accepts only on a fresh action. */
 reset();strcpy(name,"ABCDE");events[0].text_repeat=events[1].text_repeat=DASH_IN_BACK;
 events[2].pressed=DASH_IN_SELECT;
 assert(modalKeyboard("Name",name,255,1,"usb0:/") && !strcmp(name,"ABC"));
 /* Holding Confirm over Done must not submit; Cancel still restores text. */
 reset();strcpy(name,"Original");
 for(int i=0;i<4;i++)events[i].pressed=DASH_IN_DOWN;
 events[4].text_repeat=DASH_IN_OK;events[5].pressed=DASH_IN_MENU;
 assert(!modalKeyboard("Name",name,255,1,"usb0:/") && frames==6 && !strcmp(name,"Original"));
 /* Caps on the action row toggles only once, not once per repeat. */
 reset();name[0]=0;
 for(int i=0;i<4;i++)events[i].pressed=DASH_IN_DOWN;
 for(int i=4;i<7;i++)events[i].pressed=DASH_IN_LEFT;
 events[7].pressed=DASH_IN_OK;events[7].text_repeat=DASH_IN_OK;
 events[8].text_repeat=DASH_IN_OK;events[9].pressed=DASH_IN_UP;
 events[10].pressed=DASH_IN_OK;events[11].pressed=DASH_IN_SELECT;
 assert(modalKeyboard("Text",name,255,0,NULL) && !strcmp(name,"Z"));
 /* Disallowed pad characters cannot enter a filename. */
 reset();strcpy(name,"FILE");typed=':';events[0].pressed=DASH_IN_OK;
 events[1].text_repeat=DASH_IN_OK;events[2].pressed=DASH_IN_SELECT;
 assert(modalKeyboard("Name",name,255,1,"usb0:/") && !strcmp(name,"FILE"));
 /* Invalid prefilled text is rejected, then corrected via USB before Enter. */
 reset();strcpy(name,"BAD/");events[0].pressed=DASH_IN_SELECT;
 events[1].text_repeat=DASH_IN_BACK;events[2].pressed=DASH_IN_SELECT;
 assert(modalKeyboard("Name",name,255,1,"usb0:/") && !strcmp(name,"BAD") && toasts==1);
 reset();name[0]=0;scenario=1;
 assert(modalKeyboard("Name",name,255,1,"usb0:/") && !strcmp(name,"X.ELF"));
 reset();strcpy(name,"Original");events[0].text_repeat=DASH_IN_BACK;events[1].pressed=DASH_IN_MENU;
 assert(!modalKeyboard("Name",name,255,1,"usb0:/") && !strcmp(name,"Original"));
 /* Scrolling: clamped at both ends; selection visible; upper middle for
  * even row counts. Zero/short lists never produce a negative viewport. */
 for(int rows=0;rows<24;rows++)for(int count=0;count<250;count++)for(int sel=-1;sel<=count;sel++) {
  int top=listCenteredTop(sel,count,rows);
  if(rows<=0 || count<=rows) {assert(!top);continue;}
  int chosen=sel<0?0:sel>=count?count-1:sel;
  assert(top>=0 && top<=count-rows && chosen>=top && chosen<top+rows);
  if(top>0 && top<count-rows)assert(chosen-top==(rows-1)/2);
 }
 puts("PASS: filename pad/USB filtering, validation/cancel, text repeat and centered list boundaries");
}
'''
with tempfile.TemporaryDirectory(prefix='launchstation-input-polish-') as folder:
    path = Path(folder)
    (path/'test.c').write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                    '-Wno-unused-parameter','-Wno-unused-variable','-Wno-unused-but-set-variable',
                    str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True,timeout=10)
