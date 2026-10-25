"""Exercise upstream single-call copy dispatch and terminal short-I/O behavior.

The production read/write statements are extracted directly; short transfers
must report failure rather than invoking a custom completion/retry loop.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (subprocess.check_output(['git', 'show', f'{sys.argv[1]}:src/filer_copy.c'], cwd=root, text=True)
          if len(sys.argv) > 1 else (root/'src/filer_copy.c').read_text())

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
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void __attribute__((unused)) filerCopyDiagStage(const char *s) { (void)s; }
static unsigned char input[131081], output[131081], buffer[16384];
static int pos_in,pos_out,read_cap,write_cap,read_calls,write_calls;
static int read_fault,write_fault,fault_value;
static int genRead(int fd,void *p,int size) {
 assert(fd==11 && size>0); read_calls++;
 if(read_calls==read_fault) return fault_value;
 int count=size<read_cap?size:read_cap;
 if(count>(int)sizeof(input)-pos_in)count=sizeof(input)-pos_in;
 memcpy(p,input+pos_in,count);pos_in+=count;return count;
}
static int genWrite(int fd,void *p,int size) {
 assert(fd==22 && size>0);write_calls++;
 if(write_calls==write_fault) return fault_value;
 int count=size<write_cap?size:write_cap;
 assert(pos_out+count<=(int)sizeof(output));
 memcpy(output+pos_out,p,count);pos_out+=count;return count;
}
static void reset(void) {
 pos_in=pos_out=read_calls=write_calls=read_fault=write_fault=0;
 read_cap=write_cap=sizeof(buffer);memset(output,0xA5,sizeof(output));
}
'''
assert 'copyReadChunk' not in source and 'copyWriteChunk' not in source
# Exercise the exact production dispatch statements, including write suppression.
a = source.index('bytesRead = ', source.index('copyBuff = (buffIndex'))
b = source.index('\n#if FILEOP_TRACE', a)
code += 'static int transfer(int buffSize) {\nchar *copyBuff=(char *)buffer;int in_fd=11,out_fd=22,bytesRead,bytesWritten;\n'
code += source[a:b] + '\nreturn bytesRead<0?bytesRead:bytesWritten<0?bytesWritten:bytesRead==buffSize&&bytesWritten==buffSize?0:-EIO;\n}\n'
code += r'''
int main(void) {
 for(int i=0;i<(int)sizeof(input);i++) input[i]=(unsigned char)(i*17+3);
 reset();
 for(int left=sizeof(input);left>0;) {
  int n=left<(int)sizeof(buffer)?left:(int)sizeof(buffer);
  assert(!transfer(n));left-=n;
 }
 assert(pos_in==sizeof(input)&&pos_out==sizeof(input));
 assert(!memcmp(input,output,sizeof(input)));
 const int caps[][2]={{4096,16384},{16384,17},{7,23},{1,1}};
 for(unsigned c=0;c<sizeof(caps)/sizeof(caps[0]);c++) {
  reset();read_cap=caps[c][0];write_cap=caps[c][1];
  assert(transfer(16384)==-EIO);
  assert(read_calls==1);
  assert(write_calls==(read_cap==16384?1:0));
 }
 const int errors[]={0,-ETIMEDOUT,-ENOSPC,20000};
 for(unsigned e=0;e<sizeof(errors)/sizeof(errors[0]);e++) {
  reset();read_fault=1;fault_value=errors[e];
  assert(transfer(16384)==(errors[e]<0?errors[e]:-EIO));
  assert(read_calls==1&&write_calls==0&&pos_out==0);
  reset();write_fault=1;fault_value=errors[e];
  assert(transfer(16384)==(errors[e]<0?errors[e]:-EIO));
  assert(read_calls==1&&write_calls==1&&pos_out==0);
 }
 puts("PASS: upstream single-call copy dispatch, exact bytes, short-I/O failure and no retry");
}
'''
if len(sys.argv) == 1:
    seek=source[source.index('s64 in_size = genLseek'):source.index('//Here the input file has been opened')]
    assert 'if (genLseek(in_fd, 0, SEEK_SET) != 0)' in seek
    assert 'goto copy_file_exit;' in seek
with tempfile.TemporaryDirectory(prefix='copy-short-io-') as folder:
    path=Path(folder);(path/'test.c').write_text(code)
    exe=path/'test.exe'
    subprocess.run([shutil.which('gcc') or shutil.which('cc'),'-std=c99','-Wall','-Wextra','-Werror',str(path/'test.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=10)

