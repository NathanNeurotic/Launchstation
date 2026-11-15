"""Execute the shared browser eligibility rules against supported and rejected inputs."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
browser = (root/'src/filer_browser.c').read_text()


def function(source, signature):
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
#include <strings.h>
#define TRUE 1
#define FALSE 0
#define sceMcFileAttrSubdir 0x20
typedef struct {char name[256];struct {int AttrFile;} stats;} FILEINFO;
static struct {char Misc[16];} config={"MISC:/"},*setting=&config;
static int online=1,nclipFiles,host_elflist;
enum {PSU_ACTION_NONE=0};
static int genCmpFileExt(const char *p,const char *ext){const char *dot=strrchr(p,'.');return dot&&!strcasecmp(dot+1,ext);}
static int classifyPsuAction(const char *p){(void)p;return 0;}
static int isPcCardCfPath(const char *p){return !strncmp(p,"cf",2);}
static int isPcCardCfOnline(void){return online;}
static int isHddRootPath(const char *p){return !strcmp(p,"hdd0:/");}
static int isTitleCfgPathEligible(const char *p,int disabled){(void)p;return !disabled;}
static int filerCanOrganizeFolderTimestamps(const char *p){(void)p;return 0;}
static int genCanRenamePath(const char *p){(void)p;return 1;}
static int filerIsExploitProtectedPath(const char *p,const FILEINFO *f){(void)p;(void)f;return 0;}
'''
header = (root/'include/filer_shared.h').read_text()
start = header.index('enum {')
code += header[start:header.index('};', start)+2] + '\n'
text = (root/'src/gui_texteditor.c').read_text()
for sig in ('static int isDirectoryEntry(', 'static int isParentEntry(', 'int canOpenInTextEditor('):
    code += function(text, sig)
code += function((root/'src/main_fileops.c').read_text(), 'int IsTextEditorFileType(')
for sig in ('int filerIsVmcImageName(', 'static int menuVisible(', 'void filerMenuEnables('):
    code += function(browser, sig)
code += r'''
int main(void){
 FILEINFO f={0};char e[NUM_MENU];
 const char *suffix[]={"bin","VMC","mC2","ps2","MCD","VM2","RAW"};
 for(unsigned i=0;i<sizeof(suffix)/sizeof(suffix[0]);i++){
  sprintf(f.name,"CARD.%s",suffix[i]);assert(filerIsVmcImageName(f.name));
  filerMenuEnables("mass0:/",&f,0,e);assert(e[MOUNTVMC0]&&e[MOUNTVMC1]);
  for(int command=MOUNTVMC0;command<=MOUNTVMC1;command++)assert(menuVisible(command,e));
  const char *blocked[]={"mc0:/","vmc0:/","cf0:/","hdd0:/",""};
  for(unsigned k=0;k<sizeof(blocked)/sizeof(blocked[0]);k++){
   filerMenuEnables(blocked[k],&f,0,e);assert(!e[MOUNTVMC0]&&!e[MOUNTVMC1]);
   assert(!menuVisible(MOUNTVMC0,e)&&!menuVisible(MOUNTVMC1,e));
  }
 }
 const char *rejected[]={"","CARD","x.mcr","x.gme","x.psv","x.bin.txt"};
 assert(!filerIsVmcImageName(NULL));
 for(unsigned i=0;i<sizeof(rejected)/sizeof(rejected[0]);i++)assert(!filerIsVmcImageName(rejected[i]));
 strcpy(f.name,"FLAGS.arg");filerMenuEnables("cf0:/",&f,0,e);assert(e[LAUNCH_ELF_ARGS]);
 online=0;filerMenuEnables("cf0:/",&f,0,e);assert(!e[LAUNCH_ELF_ARGS]&&!menuVisible(LAUNCH_ELF_ARGS,e));online=1;
 strcpy(f.name,"BOOT.ELF");filerMenuEnables("mass0:/",&f,0,e);assert(!e[LAUNCH_ELF_ARGS]);
 strcpy(f.name,"FLAGS.TXT");f.stats.AttrFile=sceMcFileAttrSubdir;
 filerMenuEnables("mass0:/",&f,0,e);assert(!e[LAUNCH_ELF_ARGS]&&!e[MOUNTVMC0]);
 puts("PASS: shared PS2 mount eligibility, hidden classic actions, online CF argument reads and rejected inputs");
}
'''
# Drawing and cursor use the same visibility rule without renumbering commands.
menu = function(browser, 'static int menu(const char *path, FILEINFO *file)')
assert 'menu_ch_h += menuVisible(i, enable)' in menu
assert 'if (!menuVisible(i, enable))' in menu and 'sel_y = y;' in menu
assert 'drawChar(LEFT_CUR, mSprite_X1 + FONT_WIDTH, sel_y,' in menu
with tempfile.TemporaryDirectory(prefix='selected-menu-') as folder:
    p = Path(folder)
    (p/'test.c').write_text(code)
    subprocess.run([shutil.which('gcc') or shutil.which('cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-variable', str(p/'test.c'), '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True, timeout=10)
