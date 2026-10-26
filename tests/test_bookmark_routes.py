"""Execute File Browser F_BOOKMARK dispatch and its real add/jump/remove flow."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/dash_browser.c').read_text()
def function(signature):
    start = source.index(signature)
    pos = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos] + '\n'

program = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MAX_PATH 1025
#define PATHPAD_SLOTS 30
enum {DI_STAR,DI_PLUS,DI_CLOSE,DI_WARNING,DASH_TOAST_OK,DASH_TOAST_WARN,DASH_TOAST_INFO,F_BOOKMARK};
typedef struct {int icon;const char *label,*detail;int disabled;} DashMenuItem;
static char PathPad[PATHPAD_SLOTS][MAX_PATH],opened[MAX_PATH];
static struct {char dir[MAX_PATH];} fb;
static struct {int PathPad_Lock;} config,*setting=&config;
static int choices[4],choice_index,saves,opens,messages;
static int isMisc(const char *dir){return !strcmp(dir,"MISC/");}
static int dashModalMenu(const char *title,const char *detail,DashMenuItem *items,int n,int selected){
 (void)title;(void)detail;(void)selected;int r=choices[choice_index++];
 if(setting->PathPad_Lock)for(int i=0;i<n;i++)assert(items[i].icon==DI_STAR);
 assert(r<0 || r<n);return r;
}
static void dashModalMessage(int icon,const char *t,const char *b,const char *d){(void)icon;(void)t;(void)b;(void)d;messages++;}
static int dashSettingsSaveCnf(void){saves++;return 1;}
static void dashToast(int type,const char *text){(void)type;(void)text;}
static void dashStrCopy(char *out,const char *in,int size){snprintf(out,size,"%s",in);}
static void openDir(const char *dir,const char *selected){(void)selected;snprintf(opened,sizeof(opened),"%s",dir);opens++;}
'''
for name in ['static int bookmarkItems(', 'static void addBookmark(', 'static void removeBookmark(', 'static void opBookmark(']:
    program += function(name)
case = re.search(r'case F_BOOKMARK:\s*opBookmark\(\);\s*break;', source)
assert case, 'File Browser menu does not dispatch F_BOOKMARK'
program += 'static void dispatch(int action){switch(action){' + case[0] + 'default:break;}}\n'
program += r'''
int main(void){
 strcpy(fb.dir,"mass0:/folder/");choices[0]=0;dispatch(F_BOOKMARK);
 assert(saves==1 && !strcmp(PathPad[0],fb.dir) && !opens);
 choice_index=0;dispatch(F_BOOKMARK);assert(saves==1); /* duplicate add */
 choice_index=0;choices[0]=2;dispatch(F_BOOKMARK);assert(opens==1 && !strcmp(opened,fb.dir));
 choice_index=0;choices[0]=1;choices[1]=0;dispatch(F_BOOKMARK);
 assert(saves==2 && !PathPad[0][0]);
 setting->PathPad_Lock=1;strcpy(PathPad[3],"mass0:/another");
 choice_index=0;choices[0]=0;dispatch(F_BOOKMARK);
 assert(saves==2 && opens==2 && !strcmp(opened,"mass0:/another/"));
 PathPad[3][0]=0;choice_index=0;dispatch(F_BOOKMARK);assert(messages==1 && saves==2);
 strcpy(PathPad[0],"invalid");choice_index=0;dispatch(F_BOOKMARK);assert(opens==2);
 choices[0]=-1;choice_index=0;dispatch(F_BOOKMARK);assert(opens==2 && saves==2);
}
'''
with tempfile.TemporaryDirectory(prefix='bookmark-routes-') as td:
    path = Path(td)
    (path / 'test.c').write_text(program)
    subprocess.run([shutil.which('cc') or shutil.which('gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    str(path / 'test.c'), '-o', str(path / 'test.exe')], check=True)
    subprocess.run([str(path / 'test.exe')], check=True)
print('PASS: actual F_BOOKMARK add/jump/remove dispatch, duplicate/cancel/invalid entries and locked navigation')
