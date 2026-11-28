"""Installer choices, allocation accounting, Linux assets and boot configuration."""
from pathlib import Path
import ctypes as c
import json
import re
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
class Extent(c.Structure):
    _fields_=[('start',c.c_uint32),('sectors',c.c_uint32)]
class Install(c.Structure):
    _fields_=[('flags',c.c_uint),('jail',c.c_int),('commit',c.c_char*41),('stage',c.c_char*1024),('ca_path',c.c_char*1024),('error',c.c_char*256),
              ('sizes',c.c_uint32*15),('hashes',(c.c_char*41)*15),('archives',(c.c_char*128)*2),('linux',Extent*10),
              ('owner',c.c_ubyte*20),('owned',c.c_uint),('ready',c.c_uint),('ca_data',c.c_void_p),('ca_size',c.c_uint),
              ('music_mb',c.c_uint),('contents_mb',c.c_uint),('linux_home_mb',c.c_uint),('reserved_mb',c.c_uint), ('game_launcher', c.c_uint), ('launcher_files', c.c_void_p), ('launcher_count', c.c_uint), ('language', c.c_char * 52), ('archive_item', c.c_char * 96), ('channels', c.c_char * 128)]
def function(source,signature):
    start=source.index(signature);depth=0
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]',source[start:],re.S):
        depth+=(token.group()=='{')-(token.group()=='}')
        if not depth and token.group()=='}':return source[start:start+token.end()]+'\n'
    raise AssertionError(signature)
with tempfile.TemporaryDirectory(prefix='hdd-install-choices-') as directory:
    d=Path(directory)
    subprocess.run(['gcc','-std=c99','-shared','-fPIC','-O2','-Wall','-Wextra','-Werror','-I',str(root/'include'),str(root/'src/hdd_install_plan.c'),'-o',str(d/'plan.so')],check=True)
    lib=c.CDLL(str(d/'plan.so'))
    lib.hddInstallRequiredMb.argtypes=[c.POINTER(Install),c.c_uint]
    lib.hddInstallPartitionMb.argtypes=[c.POINTER(Install),c.c_uint]
    lib.hddInstallNeedsAsset.argtypes=[c.c_uint,c.c_uint]
    lib.hddInstallLinuxMetadata.argtypes=[c.c_char_p,c.c_void_p,c.c_void_p]
    lib.hddInstallConfig.argtypes=[c.POINTER(Install),c.c_int,c.c_void_p,c.c_size_t]
    in_=Install(flags=7,jail=1)
    assert lib.hddInstallRequiredMb(c.byref(in_),2048)==6528
    assert lib.hddInstallPartitionMb(c.byref(in_),7)==1024 and not lib.hddInstallPartitionMb(c.byref(in_),8)
    for asset in (10,11,12):assert not lib.hddInstallNeedsAsset(7,asset) and lib.hddInstallNeedsAsset(15,asset)
    in_.flags=15
    assert lib.hddInstallRequiredMb(c.byref(in_),2048)==9600
    for home in (1024,2048,4096,8192,16384):
        in_.linux_home_mb=home
        needed=lib.hddInstallRequiredMb(c.byref(in_),2048)
        assert needed>=6528+2048+home and needed<=6528+2048+home+16384
        assert lib.hddInstallPartitionMb(c.byref(in_),9)==home
        in_.reserved_mb=3072
        assert lib.hddInstallRequiredMb(c.byref(in_),2048)==needed+3072
        in_.reserved_mb=0
    in_.linux_home_mb=32768;assert lib.hddInstallRequiredMb(c.byref(in_),2048)<0
    in_.linux_home_mb=1024;in_.music_mb=1536;assert lib.hddInstallRequiredMb(c.byref(in_),2048)<0
    in_.music_mb=4096;in_.contents_mb=2048
    assert lib.hddInstallRequiredMb(c.byref(in_),2048)>9600
    in_.reserved_mb=1;assert lib.hddInstallRequiredMb(c.byref(in_),2048)<0
    in_.reserved_mb=131072;assert lib.hddInstallRequiredMb(c.byref(in_),2048)<0
    in_.reserved_mb=0;in_.flags=8;assert lib.hddInstallRequiredMb(c.byref(in_),2048)<0
    in_.flags=15;in_.commit=b'a'*40
    out=c.create_string_buffer(4096)
    assert lib.hddInstallConfig(c.byref(in_),1,out,len(out))>0
    assert b'boot_circle = $PSBBN\n' in out.value and b'pfs0:/p2lboot/ps2-linux-ntsc' in out.value
    in_.flags=7;assert lib.hddInstallConfig(c.byref(in_),1,out,len(out))>0 and b'boot_circle' not in out.value
    for choice, elf, label in ((0,b'OPNPS2LD.ELF',b'Open PS2 Loader'),(1,b'nhddl.elf',b'Neutrino (NHDDL)')):
        in_.game_launcher=choice
        assert lib.hddInstallConfig(c.byref(in_),1,out,len(out))>0
        assert b'boot_auto = $PSBBN\n' in out.value and b'boot_square = hdd0:__system:pfs:/launcher/'+elf+b'\n' in out.value
        assert (b'boot_square_arg1 = -mode=ata' in out.value)==bool(choice)
        assert lib.hddInstallConfig(c.byref(in_),0,out,len(out))>0
        assert b'name_OSDSYS_ITEM_1 = '+label+b'\n' in out.value
        assert b'OPNPS2LD.ELF' in out.value and b'nhddl.elf' in out.value and b'name_OSDSYS_ITEM_3 = wLaunchELF-R3Z' in out.value
    in_.game_launcher=2
    assert lib.hddInstallConfig(c.byref(in_),1,out,len(out))>0 and b'RIPTOPL-RA-UNPAKT.ELF' in out.value
    assert b'boot_square_arg1' not in out.value
    assert lib.hddInstallConfig(c.byref(in_),0,out,len(out))>0
    assert b'name_OSDSYS_ITEM_1 = Open PS2 Loader: RiptOPL' in out.value
    assert b'arg_OSDSYS_ITEM_1 = -titleid=APP_RIPTOPL' in out.value
    assert b'name_OSDSYS_ITEM_2 = wLaunchELF-R3Z' in out.value and b'nhddl.elf' not in out.value
    assert lib.hddInstallGameShortcut(c.byref(in_),out,len(out))>0 and out.value.startswith(b'RiptOPL ')
    in_.game_launcher=3;assert lib.hddInstallConfig(c.byref(in_),0,out,len(out))>0 and b'RiptOPL (Neutrino)' in out.value
    in_.game_launcher=4;assert lib.hddInstallConfig(c.byref(in_),0,out,len(out))<0
    in_.game_launcher=0
    hash_=c.create_string_buffer(41);size=c.c_uint32()
    dependency={'name':'dep.zip','sha1':'a'*40,'size':'3174561'}
    assert lib.hddInstallDependencyMetadata(json.dumps({'files':[dependency]}).encode(),hash_,c.byref(size))==1
    assert size.value==3174561
    for entries in ([dependency,dependency],[dict(dependency,size='16777217')],[dict(dependency,name='x.zip')],[dict(dependency,sha1='bad')]):
        assert not lib.hddInstallDependencyMetadata(json.dumps({'files':entries}).encode(),hash_,c.byref(size))
    file={'name':'PS2Linux.tar.gz','sha1':'b'*40,'size':'269864865'}
    assert lib.hddInstallLinuxMetadata(json.dumps({'files':[file]}).encode(),hash_,c.byref(size))==1
    assert size.value==269864865 and hash_.value==b'b'*40
    for files in ([file,file],[dict(file,sha1='bad')],[dict(file,size='999999999')],[dict(file,name='elsewhere')],[{'size':'3'}]):
        assert not lib.hddInstallLinuxMetadata(json.dumps({'files':files}).encode(),hash_,c.byref(size))
    source=(root/'src/dash_hdd_install.c').read_text()
    code=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hdd_install.h"
#define ETH
#define WLE_NATIVE_CLOUD
#define EXFAT
#define DI_INFO 1
#define DI_CHECK 2
typedef struct {int icon;const char *label,*detail;int disabled;} DashMenuItem;
static int expected_check=-1,expected_focus;
static int answers[4],asked,messages,cancel=-1;
static int dashModalMenu(const char *title,const char *subtitle,const DashMenuItem *items,int count,int initial){
    assert(title&&count>0&&count<=7&&initial==expected_focus&&items[0].label);
    int software=!strcmp(title,"Software after formatting")&&count==2;
    for(int j=0;j<count;j++)assert(items[j].icon==(j==expected_check?DI_CHECK:-1)&&(!items[j].detail||(software&&j==1))&&!items[j].disabled);
    if(software){assert(!strcmp(items[0].label,"No software, format only"));assert(!strcmp(items[1].label,"PSBBN DP, HOSDMenu")&&!strcmp(items[1].detail,"EXPERIMENTAL"));}
    if(strstr(title,"Default PS2 game core"))assert(!strcmp(title,"Choice 1 of 6: Default PS2 game core")&&messages==1&&!strcmp(items[0].label,"Neutrino (RiptOPL)")&&!strcmp(items[1].label,"OPL (RiptOPL)"));
    if(strstr(title,"Music partition"))assert(!strcmp(title,"Choice 2 of 6: Music partition size"));
    if(strstr(title,"Contents partition"))assert(!strcmp(title,"Choice 3 of 6: Contents partition size"));
    if(strstr(title,"Standalone PS2 Linux")){
        const char *linux_choices[]={"Do not install PS2 Linux","1 GiB","2 GiB","4 GiB","8 GiB"};
        assert(!strcmp(title,"Choice 4 of 6: Standalone PS2 Linux")&&subtitle&&strstr(subtitle,"2 GiB system")&&count==5);
        for(int j=0;j<count;j++)assert(!strcmp(items[j].label,linux_choices[j]));
    }
    else assert(!subtitle);
    int i=asked++;if(i==cancel)return -1;
    assert(answers[i]<count);return answers[i];
}
static void dashModalMessage(int icon,const char *title,const char *body,const char *detail){assert(icon==DI_INFO&&title&&body&&!detail);assert(!strcmp(title,"PSBBN DP installation")&&asked==1&&strstr(body,"Sony Network Adapter")&&strstr(body,"GameStar")&&strstr(body,"HOSDMenu, is installed as well"));messages++;}
'''
    choice_source=(root/'src/dash_ui.c').read_text()
    for signature in ('static int modalChoice(', 'int dashModalChoice(', 'int dashModalSelect('):
        code+=function(choice_source,signature)
    code+=function(source,'const char *dashHddInstallChoiceTitle(')+function(source,'int dashHddInstallChoose(')+function(source,'static int installSize(')+function(source,'int dashHddInstallConfigure(')
    code+=r'''
int main(void){HddInstall in={.flags=7,.jail=1};
 const char *options[]={"First","Second"};
 assert(dashModalSelect("empty",NULL,0,0)==-1);
 expected_check=expected_focus=1;answers[0]=1;assert(dashModalChoice("Existing setting",options,2,1)==1);
 expected_check=-1;expected_focus=0;asked=0;answers[0]=0;assert(dashModalSelect("New choice",options,2,0)==0);
 asked=0;
 for(int core=0;core<2;core++){
  answers[0]=1;answers[1]=core;asked=messages=0;
  assert(dashHddInstallChoose(&in,1)&&in.flags==7&&in.game_launcher==(core?HDD_GAME_RIPTOPL:HDD_GAME_RIPTOPL_NEUTRINO)&&asked==2&&messages==1);
 }
 asked=messages=0;cancel=1;assert(!dashHddInstallChoose(&in,1)&&asked==2&&messages==1);cancel=-1;
 for(int profile=0;profile<4;profile++){
  const unsigned expected[]={0,2,1,3};answers[0]=profile;asked=messages=0;
  assert(dashHddInstallChoose(&in,0)&&in.flags==expected[profile]&&!in.game_launcher&&asked==1);
 }
 memset(answers,0,sizeof(answers));asked=messages=0;in.flags=7;in.jail=1;in.game_launcher=HDD_GAME_RIPTOPL;
 assert(dashHddInstallConfigure(&in,8192)&&asked==3&&!messages&&in.flags==7&&in.music_mb==1024&&in.contents_mb==1024&&!in.linux_home_mb);
 asked=messages=0;answers[0]=1;answers[1]=2;answers[2]=4;
 assert(dashHddInstallConfigure(&in,8192)&&asked==3&&!messages&&in.flags==15&&in.music_mb==2048&&in.contents_mb==4096&&in.linux_home_mb==8192);
 asked=messages=0;assert(dashHddInstallConfigure(&in,32768)&&asked==3&&in.linux_home_mb==8192);
 for(cancel=0;cancel<3;cancel++){asked=0;assert(!dashHddInstallConfigure(&in,8192)&&asked==cancel+1);}
 for(unsigned step=1;step<=HDD_CHOICE_LANGUAGE;step++){char title[100],expected[100];snprintf(expected,sizeof(expected),"Choice %u of 6: Question",step);assert(!strcmp(dashHddInstallChoiceTitle(&in,step,"Question",title,sizeof(title)),expected));}
 puts("PASS: installer choice defaults/cancellation, single-extent sizes, APA allocation/reserve bounds, optional Linux metadata/assets and Circle boot config");}
'''
    (d/'ui.c').write_text(code)
    subprocess.run(['gcc','-std=c99','-Wall','-Wextra','-Werror','-I',str(root/'include'),str(d/'ui.c'),'-o',str(d/'ui')],check=True)
    subprocess.run([str(d/'ui')],check=True)
