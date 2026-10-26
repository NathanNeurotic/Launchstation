"""Real CUE/BIN and equivalent VCD fixtures must produce identical RetroGem IDs."""
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
def fn(source,signature):
 a=source.index(signature);p=source.index('{',a)+1;depth=1
 while depth:depth+=(source[p]=='{')-(source[p]=='}');p+=1
 return source[a:p]+'\n'

def sectors(boot=b'BOOT = cdrom0:\\SLUS_123.45;1\r\n',timestamp=b'1994111009000000'):
 data=bytearray(24*2048);pvd=16*2048
 data[pvd:pvd+7]=b'\x01CD001\x01';data[pvd+0x32D:pvd+0x32D+16]=timestamp
 data[pvd+156]=34;data[pvd+158:pvd+162]=struct.pack('<I',20)
 data[pvd+166:pvd+170]=struct.pack('<I',2048)
 if boot:
  record=20*2048;data[record]=46;data[record+2:record+6]=struct.pack('<I',21)
  data[record+10:record+14]=struct.pack('<I',len(boot));data[record+32]=12
  data[record+33:record+45]=b'SYSTEM.CNF;1';data[21*2048:21*2048+len(boot)]=boot
 return data

def image(logical,size,mode=2):
 if size==2048:return logical
 result=bytearray()
 for n in range(len(logical)//2048):
  sector=bytearray(size)
  if size==2352:sector[15]=mode;offset=16 if mode==1 else 24
  else:offset=8
  sector[offset:offset+2048]=logical[n*2048:(n+1)*2048];result+=sector
 return result

prefix=r'''
#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
typedef uint32_t u32;typedef long long s64;
#define MAX_PATH 1025
#define MAX_PART_NAME 32
#include "popstarter_internal.h"
#include "main_gameid.h"
static FILE *files[16];static int opened,read_limit,read_error,seek_error;
static int wle_ascii_tolower(int c){return tolower((unsigned char)c);}
#define stricmp host_stricmp
static int stricmp(const char *a,const char *b){while(*a && tolower((unsigned char)*a)==tolower((unsigned char)*b)){a++;b++;}return tolower((unsigned char)*a)-tolower((unsigned char)*b);}
static int genCmpFileExt(const char *s,const char *ext){const char *p=strrchr(s,'.');return p && !stricmp(p+1,ext);}
int openPathForRead(const char *path,char *out,size_t size){(void)out;(void)size;for(int i=1;i<16;i++)if(!files[i]){files[i]=fopen(path,"rb");if(!files[i])return -1;opened++;return i;}return -1;}
static s64 genLseek(int fd,s64 off,int how){if(seek_error || fseek(files[fd],(long)off,how))return -1;return ftell(files[fd]);}
static int genRead(int fd,void *out,int n){if(read_error)return -1;if(read_limit && n>read_limit)n=read_limit;return fread(out,1,n,files[fd]);}
static void genClose(int fd){assert(files[fd]);fclose(files[fd]);files[fd]=NULL;opened--;}
int splitPopstarterHddLaunchPath(const char *s,char *d,size_t ds,char *p,size_t ps,const char **sub){(void)s;(void)d;(void)ds;(void)p;(void)ps;(void)sub;return 0;}
int buildPS1GenericDiscGameID(char *id,size_t n){(void)id;(void)n;return 0;}
'''
common=(root/'src/main_gameid.c').read_text()
program=prefix+(root/'src/main_gameid_table.c').read_text().replace('#include "main_gameid.h"','')
program+=fn((root/'src/popstarter_path.c').read_text(),'int prefixCaseCmp(')
for sig in ['static int isDiscExecPath(', 'static int isLikelyTitleID(', 'int buildPS1GenericGameIDFromTimestamp(', 'int buildLaunchGameID(']:
 program+=fn(common,sig).replace('isLikelyTitleID','isLikelyLaunchTitleID')
for filename in ['popstarter_vcd.c','popstarter_gameid.c','ember_gameid.c']:
 source=(root/'src'/filename).read_text()
 source='\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
 program+=source+'\n'
program+=r'''
int main(int argc,char **argv){
 assert(argc>=5);char cue[12],vcd[12];
 int fd=openPathForRead(argv[1],NULL,0);assert(fd>0);genClose(fd);
 assert(buildEmberGameID(argv[1],cue,sizeof(cue)) && !opened);
 assert(buildPopstarterVcdGameID(argv[2],vcd,sizeof(vcd)) && !opened);
 assert(!strcmp(cue,vcd) && !strcmp(cue,argv[3]));
 read_limit=7;
 assert(buildEmberGameID(argv[1],cue,sizeof(cue)) && !strcmp(cue,vcd) && !opened);
 assert(buildPopstarterVcdGameID(argv[2],vcd,sizeof(vcd)) && !strcmp(cue,vcd) && !opened);
 read_limit=0;
 for(int i=4;i<argc;i++){strcpy(cue,"stale");assert(!buildEmberGameID(argv[i],cue,sizeof(cue)) && !cue[0] && !opened);}
 read_error=1;assert(!buildEmberGameID(argv[1],cue,sizeof(cue)) && !opened);read_error=0;
 seek_error=1;assert(!buildEmberGameID(argv[1],cue,sizeof(cue)) && !opened);seek_error=0;
 assert(!buildEmberGameID(argv[1],cue,11) && !opened);
}
'''
with tempfile.TemporaryDirectory(prefix='ember-gameid-') as td:
 folder=Path(td)
 (folder/'popstarter_internal.h').write_text((root/'src/popstarter_internal.h').read_text())
 (folder/'launchelf.h').write_text('')
 (folder/'main_gameid.h').write_text((root/'include/main_gameid.h').read_text())
 (folder/'test.c').write_text(program)
 binary=folder/'test.exe'
 subprocess.run([shutil.which('cc') or shutil.which('gcc'),'-std=c11','-Wall','-Wextra','-Werror','-I',str(folder),str(folder/'test.c'),'-o',str(binary)],check=True)
 bad=[]
 for i,text in enumerate(['x','\ufeff','FILE "unterminated','FILE "absent.bin" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00',
                          'FILE "data.bin" BINARY\nTRACK 01 MODE2/2448\nINDEX 01 00:00:00',
                          'FILE "data.bin" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:60:00',
                          'FILE "data.bin" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:75',
                          'FILE "data.bin" BINARY\nTRACK 01 MODE2/2352\nTRACK 02 AUDIO\nINDEX 01 00:00:00',
                          'FILE "mmce0:/data.bin" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00', 'x'*65537]):
  p=folder/f'bad{i}.cue';p.write_text(text,encoding='utf-8');bad.append(p)
 (folder/'data.bin').write_bytes(image(sectors(),2352))
 (folder/'standalone.exe').write_bytes(b'PS-X EXE'+bytes(2048));bad.append(folder/'standalone.exe')
 count=0
 for size,mode in [(2352,1),(2352,2),(2048,1),(2336,2)]:
  for has_serial in [True,False]:
   logical=sectors(boot=b'BOOT = cdrom0:\\SLUS_123.45;1\r\n' if has_serial else b'BOOT = cdrom0:\\PSX.EXE;1\r\n')
   expected='SLUS_123.45' if has_serial else 'SLPS_000.01'
   stem=f'case{count}';count+=1
   data=folder/(stem+' track 01.bin');data.write_bytes(bytes(size*2)+image(logical,size,mode))
   cue=folder/(stem+'.cue');cue.write_text('\ufeffREM comment\r\nFILE "audio.wav" WAVE\r\n TRACK 01 AUDIO\r\n INDEX 01 00:00:00\r\n'+
       f'FILE "{data.name}" BINARY\r\n TRACK 02 MODE{mode}/{size}\r\n PREGAP 00:02:00\r\n INDEX 00 00:00:00\r\n INDEX 01 00:00:02\r\n',encoding='utf-8')
   vcd=folder/(stem+'.VCD');vcd.write_bytes(bytes(0x100000)+image(logical,2352,mode))
   subprocess.run([str(binary),cue.as_posix(),vcd.as_posix(),expected,*[p.as_posix() for p in bad]],check=True,timeout=15)
   raw=folder/(stem+'.bin');raw.write_bytes(image(logical,size,mode))
   # Existing same-stem CUE must take precedence over the raw BIN's zero offset.
   subprocess.run([str(binary),raw.as_posix(),vcd.as_posix(),expected,*[p.as_posix() for p in bad]],check=True,timeout=15)
   cue.unlink()
   subprocess.run([str(binary),raw.as_posix(),vcd.as_posix(),expected,*[p.as_posix() for p in bad]],check=True,timeout=15)
 # Filename fallback uses the same stem, only after confirming an ISO PVD.
 logical=sectors(boot=b'',timestamp=b'0000000000000000')
 raw=folder/'fallback.bin';raw.write_bytes(image(logical,2352))
 cue=folder/'SLUS_987.65.cue';cue.write_text('FILE "fallback.bin" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n')
 vcd=folder/'SLUS_987.65.VCD';vcd.write_bytes(bytes(0x100000)+image(logical,2352))
 subprocess.run([str(binary),cue.as_posix(),vcd.as_posix(),'SLUS_987.65',*[p.as_posix() for p in bad]],check=True,timeout=15)
print('PASS: actual CUE/raw BIN and VCD IDs match across MODE1/MODE2, 2048/2336/2352, INDEX/pregap, timestamp fallback, fragmented/error IO and malformed cues')
