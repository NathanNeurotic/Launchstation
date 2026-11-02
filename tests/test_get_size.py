"""Get Size in the classic browser and the dashboard, on POSIX folders.

Runs the real getFileSize() (src/filer_actions.c), the classic browser's
submenu_func_GetSize() and the dashboard's opSize() and targets(). The folder
listing is a fixture with the contract the real one keeps
(tests/test_listing_complete.py runs that), with MAX_ENTRY 16, so a listing
holds 14 entries. Stats fail the way the devices' do: a udpfsd server may
answer one only for an open file, ps2client's host: never answers (-5), and a
memory card file here fails one with -EIO.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
actions = (root / 'src/filer_actions.c').read_text()
browser = (root / 'src/filer_browser.c').read_text()
dash = (root / 'src/dash_browser.c').read_text()


def function(source, signature):
    a = source.index(signature)
    b = source.index('{', a) + 1
    depth = 1
    while depth:
        depth += (source[b] == '{') - (source[b] == '}')
        b += 1
    return source[a:b] + '\n'


code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <malloc.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;typedef int64_t s64;
#define MAX_PATH 1024
#define MAX_NAME 256
#define MAX_ENTRY 16
#define TRUE 1
#define FALSE 0
#define UDPFS 1
#define ETH 1
#define LNG(x) #x
#define DPRINTF(...) ((void)0)
#define sceMcFileAttrSubdir 0x20
typedef struct {unsigned char unknown,sec,min,hour,day,month;unsigned short year;} PS2TIME;
typedef struct {u8 Resv2,Sec,Min,Hour,Day,Month;u16 Year;} sceMcStDateTime;
typedef struct {sceMcStDateTime _Create,_Modify;u32 FileSizeByte;u16 AttrFile,Reserve1;u32 Reserve2,PdaAplNo;u8 EntryName[32];}
 __attribute__((aligned(64))) sceMcTblGetDir;
typedef struct {char name[MAX_NAME];unsigned char title[32*2+2];sceMcTblGetDir stats;} FILEINFO;
typedef struct {unsigned mode,attr,size;u8 ctime[8],atime[8],mtime[8];unsigned hisize;} iox_stat_t;
enum {DI_INFO,DI_WARNING};
/* Folder listings are on the heap; one chosen allocation size can fail. */
static size_t fail_alloc_bytes;static int listings_live;
static void *test_memalign(size_t a,size_t n){
 assert(a>=__alignof__(FILEINFO));if(fail_alloc_bytes&&n==fail_alloc_bytes)return NULL;
 void *p=memalign(a,n);if(p&&n>=MAX_ENTRY*sizeof(FILEINFO))listings_live++;return p;
}
static void test_free(void *p){if(p&&malloc_usable_size(p)>=MAX_ENTRY*sizeof(FILEINFO))listings_live--;free(p);}
#define memalign test_memalign
#define free test_free
static int ensurePathDeviceStackReady(const char *p){(void)p;return 1;}
static char *makeHostPath(char *d,char *s){if(d!=s)strcpy(d,s);return d;}
static int getHddParty(const char *p,const FILEINFO *f,char *a,char *b){(void)p;(void)f;(void)a;(void)b;assert(!"no HDD here");return -1;}
static int mountParty(const char *p){(void)p;assert(!"no HDD here");return -1;}
static int genGetStat(const char *p,iox_stat_t *out){
 struct stat st;
 if(!strncmp(p,"udpfs:",6))return -ENOENT;   /* a udpfsd server that stats only open files */
 if(!strncmp(p,"host:",5))return -5;          /* ps2client */
 if(strstr(p,"BAD"))return -EIO;
 if(stat(p,&st))return -errno;
 memset(out,0,sizeof(*out));out->size=(u32)st.st_size;out->hisize=(u32)((u64)st.st_size>>32);return 0;
}
/* The listing, with a fault per folder: its open fails, or its read fails after some entries. */
static struct {char path[MAX_PATH];int open_error,fail_after,fail_code;} faults[8];static int nfaults;
static void fault(const char *p,int open_error,int fail_after,int fail_code){
 assert(nfaults<8);snprintf(faults[nfaults].path,MAX_PATH,"%s",p);faults[nfaults].open_error=open_error;
 faults[nfaults].fail_after=fail_after;faults[nfaults++].fail_code=fail_code;
}
static int by_name(const void *a,const void *b){return strcmp(*(char *const *)a,*(char *const *)b);}
static int list(const char *p,FILEINFO *out,int max,int *error){
 int fail_after=-1,fail_code=0,n=0,count=0;char *names[256];
 *error=0;
 for(int i=0;i<nfaults;i++)if(!strcmp(faults[i].path,p)){*error=faults[i].open_error;fail_after=faults[i].fail_after;fail_code=faults[i].fail_code;}
 if(*error)return 0;
 DIR *d=opendir(p);if(!d){*error=-errno;return 0;}
 struct dirent *e;
 while((e=readdir(d)))if(strcmp(e->d_name,".")&&strcmp(e->d_name,"..")){assert(count<256);names[count++]=strdup(e->d_name);}
 closedir(d);qsort(names,count,sizeof(names[0]),by_name);
 for(int i=0;i<count;i++){
  if(i==fail_after){*error=fail_code;break;}
  if(n==max)break;
  char full[MAX_PATH];struct stat st;snprintf(full,sizeof(full),"%s%s",p,names[i]);assert(!stat(full,&st));
  memset(&out[n],0,sizeof(out[n]));snprintf(out[n].name,sizeof(out[n].name),"%s",names[i]);
  out[n].stats.AttrFile=S_ISDIR(st.st_mode)?sceMcFileAttrSubdir:0;out[n].stats.FileSizeByte=S_ISDIR(st.st_mode)?0:st.st_size;n++;
 }
 for(int i=0;i<count;i++)free(names[i]);
 return n;
}
static int getDir(const char *p,FILEINFO *out){int error;return list(p,out,MAX_ENTRY-2,&error);}
static int getDirComplete(const char *p,FILEINFO *out,int *more){
 int error,n=list(p,out,MAX_ENTRY-1,&error);
 if(more)*more=0;
 if(error)return error;
 if(n>MAX_ENTRY-2){if(!more)return -E2BIG;*more=1;}
 return n;
}
/* The classic browser. */
static int browser_nfiles,browser_sel,nmarks,browser_pushed,mctype_PSx;static char marks[MAX_ENTRY];
static void drawMsg(const char *s){(void)s;}
static void menuTitleGetClockFormat(int *use_12h,int *date_format){*use_12h=0;*date_format=0;}
static void formatBrowserTimestamp(char *dst,size_t size,const PS2TIME *t,int use_12h,int date_format){(void)t;(void)use_12h;(void)date_format;snprintf(dst,size,"TIME");}
static int mcGetInfo(int p,int s,int *type,int *a,int *b){(void)p;(void)s;(void)a;(void)b;*type=2;return 0;}
static int mcSync(int mode,void *c,int *r){(void)mode;(void)c;if(r)*r=0;return 1;}
/* The dashboard. */
static struct {char dir[MAX_PATH];FILEINFO *files;u8 *mark;int n,nmarked,sel;} fb;
static int modal_icon=-1;static char modal_title[MAX_NAME],modal_body[96],modal_detail[64];
static int prepareOp(void){return 1;}
static void opDone(void){}
static void dashLoadingBegin(const char *s){(void)s;}
static void dashLoadingStatus(const char *s,int i,int n){(void)s;(void)i;(void)n;}
static void dashModalMessage(int icon,const char *title,const char *body,const char *detail){
 modal_icon=icon;snprintf(modal_title,sizeof(modal_title),"%s",title);snprintf(modal_body,sizeof(modal_body),"%s",body);
 snprintf(modal_detail,sizeof(modal_detail),"%s",detail?detail:"(none)");
}
'''
# getFileSize() and what precedes it after the exploit-path helpers.
start = actions.index('\treturn filerConfirmExploitAction(path, file, LNG(Modify));\n}\n')
code += actions[actions.index('}\n', start) + 2:actions.index('//endfunc getFileSize')]
code += function(browser, 'static void submenu_func_GetSize(char *mess, char *path, FILEINFO *files)\n{')
code += function(dash, 'static int targets(int *idx, int max)\n{')
code += function(dash, 'static void opSize(void)\n{')
code += r'''
static FILEINFO files[MAX_ENTRY],dash_files[MAX_ENTRY];static u8 dash_marks[MAX_ENTRY];
static char mess[MAX_PATH];
static void put(const char *p,int size){int fd=open(p,O_CREAT|O_TRUNC|O_WRONLY,0600);assert(fd>=0);assert(!ftruncate(fd,size));assert(!close(fd));}
static void tree(const char *dir,int n,int size){
 char p[MAX_PATH];assert(!mkdir(dir,0700));
 for(int i=0;i<n;i++){snprintf(p,sizeof(p),"%s/F%03d",dir,i);put(p,size);}
}
/* Shows 'dir' in both browsers, with entries 'select' (a comma-separated list) marked. */
static void show(const char *dir,const char *select){
 int error,n=list(dir,&files[1],MAX_ENTRY-2,&error);assert(!error&&n>0);
 memset(&files[0],0,sizeof(files[0]));strcpy(files[0].name,"..");files[0].stats.AttrFile=sceMcFileAttrSubdir;
 browser_nfiles=n+1;nmarks=0;memset(marks,0,sizeof(marks));
 memcpy(dash_files,&files[1],n*sizeof(FILEINFO));memset(dash_marks,0,sizeof(dash_marks));
 snprintf(fb.dir,sizeof(fb.dir),"%s",dir);fb.files=dash_files;fb.mark=dash_marks;fb.n=n;fb.nmarked=0;
 for(int i=1;i<=n;i++){
  char wanted[MAX_PATH];snprintf(wanted,sizeof(wanted),",%s,",select);
  char name[MAX_NAME+2];snprintf(name,sizeof(name),",%s,",files[i].name);
  if(strstr(wanted,name)){marks[i]=1;nmarks++;browser_sel=i;dash_marks[i-1]=1;fb.nmarked++;fb.sel=i-1;}
 }
 assert(nmarks>0);
 if(nmarks==1){nmarks=0;marks[browser_sel]=0;fb.nmarked=0;dash_marks[fb.sel]=0;} /* one entry: the focused one */
}
/* Measures in both browsers; the classic text is in mess, the dashboard's in modal_*. */
static void measure(void){
 mess[0]=0;modal_icon=-1;
 submenu_func_GetSize(mess,fb.dir,files);
 assert(!listings_live);
 opSize();
 assert(!listings_live&&modal_icon>=0);
}
static int starts(const char *s,const char *prefix){return !strncmp(s,prefix,strlen(prefix));}
static unsigned stored(const char *name){for(int i=1;i<browser_nfiles;i++)if(!strcmp(files[i].name,name))return files[i].stats.FileSizeByte;assert(0);return 0;}
int main(void){
 assert(!mkdir("udpfs:",0700)&&!mkdir("host:",0700)&&!mkdir("mc0:",0700)&&!mkdir("mass0:",0700));
 /* A server that does not stat a file: the size its listing gave is used. */
 put("udpfs:/BOOT.ELF",37);tree("udpfs:/GAME",2,100);tree("udpfs:/GAME/DATA",1,300);
 show("udpfs:/","BOOT.ELF");measure();
 assert(starts(mess,"SIZE = 37B "));
 assert(modal_icon==DI_INFO&&!strcmp(modal_body,"37 bytes")&&!strcmp(modal_detail,"(none)"));
 show("udpfs:/","GAME");measure();
 assert(starts(mess,"SIZE = 500B ")&&stored("GAME")==500); /* an exact folder size is kept */
 assert(modal_icon==DI_INFO&&!strcmp(modal_body,"500 bytes"));
 put("host:/PC.BIN",5000);
 show("host:/","PC.BIN");measure();
 assert(starts(mess,"SIZE = 4.9KB "));
 assert(modal_icon==DI_INFO&&!strcmp(modal_body,"5000 bytes"));
 /* A memory card file whose stat fails was not measured. */
 put("mc0:/A.BIN",64);put("mc0:/BAD.BIN",64);put("mc0:/OK.BIN",64);
 show("mc0:/","BAD.BIN");measure();
 assert(starts(mess,"Size_test_Failed "));
 assert(modal_icon==DI_WARNING&&!strcmp(modal_body,"The size could not be measured.")&&!strcmp(modal_detail,"error -5"));
 /* With several marked, those measured before it are a lower bound and those after are not measured. */
 show("mc0:/","A.BIN,BAD.BIN,OK.BIN");measure();
 assert(!strcmp(mess,"SIZE >= 64B"));
 assert(modal_icon==DI_WARNING&&!strcmp(modal_title,"Selected items")&&!strcmp(modal_body,"At least 64 bytes"));
 assert(!strcmp(modal_detail,"Stopped by error -5"));
 show("mc0:/","A.BIN,OK.BIN");measure();
 assert(!strcmp(mess,"SIZE = 128B")&&modal_icon==DI_INFO&&!strcmp(modal_body,"128 bytes"));
 /* A folder over the listing cap: what one listing holds is counted, shown as a lower bound. */
 tree("mass0:/BIG",MAX_ENTRY+4,10);tree("mass0:/BIG/SUB",2,1000);
 show("mass0:/","BIG");measure();
 assert(starts(mess,"SIZE >= 150B ")&&stored("BIG")==0); /* 15 of 20 files; not kept as the folder's size */
 assert(modal_icon==DI_WARNING&&!strcmp(modal_body,"At least 150 bytes")&&!strcmp(modal_detail,"A folder holds over 14 items"));
 /* The rest of the tree is still measured: here past a subfolder over the cap. */
 tree("mass0:/DEEP",0,0);put("mass0:/DEEP/A.BIN",3000);tree("mass0:/DEEP/M",MAX_ENTRY,1);put("mass0:/DEEP/Z.BIN",5);
 show("mass0:/","DEEP");measure();
 assert(starts(mess,"SIZE >= 2.9KB ")&&stored("DEEP")==0);
 assert(modal_icon==DI_WARNING&&!strcmp(modal_body,"At least 3020 bytes"));
 put("mass0:/TAIL.BIN",7);
 show("mass0:/","BIG,TAIL.BIN");measure();
 assert(!strcmp(mess,"SIZE >= 157B"));
 assert(modal_icon==DI_WARNING&&!strcmp(modal_body,"At least 157 bytes")&&!strcmp(modal_detail,"A folder holds over 14 items"));
 /* A folder that does not open, or fails partway, stops the measurement. */
 tree("mass0:/MIX",0,0);put("mass0:/MIX/A.BIN",2048);tree("mass0:/MIX/B",3,1);put("mass0:/MIX/C.BIN",4096);
 fault("mass0:/MIX/B/",-EACCES,-1,0);
 show("mass0:/","MIX");measure();
 assert(starts(mess,"SIZE >= 2.0KB ")&&stored("MIX")==0);
 assert(modal_icon==DI_WARNING&&!strcmp(modal_body,"At least 2048 bytes")&&!strcmp(modal_detail,"Stopped by error -13"));
 nfaults=0;fault("mass0:/MIX/",0,1,-EIO);
 show("mass0:/","MIX");measure();
 assert(starts(mess,"Size_test_Failed ")&&modal_icon==DI_WARNING&&!strcmp(modal_detail,"error -5"));
 nfaults=0;
 show("mass0:/","MIX");measure();
 assert(starts(mess,"SIZE = 6.0KB ")&&stored("MIX")==6147);
 assert(modal_icon==DI_INFO&&!strcmp(modal_body,"6147 bytes"));
 /* With no memory for a listing, nothing is measured. */
 fail_alloc_bytes=MAX_ENTRY*sizeof(FILEINFO);
 show("mass0:/","MIX");measure();
 assert(starts(mess,"Size_test_Failed ")&&!strcmp(modal_detail,"error -12"));
 fail_alloc_bytes=0;
 puts("PASS: Get Size uses a network listing's sizes when a stat fails, shows a total missing anything "
      "(over the listing cap, a folder or stat that failed, no memory) as a lower bound or a failure "
      "in both browsers, and keeps a folder's size only when it is exact");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='get-size-') as folder:
    p = Path(folder)
    (p / 'test.c').write_text(code)
    exe = p / 'test'
    cc = shutil.which('gcc') or shutil.which('cc')
    if not cc:
        raise SystemExit('Host C compiler required')
    subprocess.run([cc, '-std=gnu99', '-Werror=implicit-function-declaration', str(p / 'test.c'), '-o', str(exe)],
                   check=True)
    subprocess.run([str(exe)], cwd=p, check=True, timeout=60)
