"""Execute video preview rollback and SFX voice isolation with host stubs."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]

def function(path, signature):
    text = (root / path).read_text()
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'

code = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
typedef uint64_t u64;
static struct {int TV_mode,screen_x,screen_y;} cfg,*setting=&cfg;
static int accepted,updates,invalidates;
static void updateScreenMode(void) { updates++; }
static int dashGfxCheckScreenChange(void) {return 1;}
static void dashArtInvalidateVram(void) {invalidates++;}
static int dashModalKeepVideo(void) {setting->screen_x=77;setting->screen_y=88;return accepted;}
'''
code += function('src/dash_settings.c', 'static int previewVideoMode(')
code += r'''
#define DASH_SND_COUNT 8
#define DASH_SND_CURSOR 0
#define DASH_SND_TRANSITION 3
#define SND_CURSOR_CH_BASE 8
#define SND_CURSOR_CH_COUNT 6
#define SND_TRANS_CH_BASE 14
#define SND_TRANS_CH_COUNT 4
#define SND_SFX_CH_MAX 23
static struct {int sound,sound_vol;} dashCfg={1,60};
static struct {void *buffer;} samples[DASH_SND_COUNT];
static int gain_ch=-1,attempts,success=-1;
static int soundAlive(void) {return 1;}
static int dashIopParked(void) {return 0;}
static u64 Timer(void) {return 100;}
static void audsrv_adpcm_set_volume_and_pan(int ch,int gain,int pan) {
 assert(ch>=0 && ch<23 && gain==60 && !pan);gain_ch=ch;
}
static int audsrv_ch_play_adpcm(int ch,void *sample) {
 assert(ch==gain_ch && ch>=0 && ch<23 && sample);attempts++;
 return ch==success?ch:-1;
}
'''
code += function('src/dash_sound.c', 'void dashSoundPlay(')
code += r'''
int main(void) {
 cfg.TV_mode=2;cfg.screen_x=5;cfg.screen_y=-7;
 assert(!previewVideoMode(2) && !updates);
 assert(!previewVideoMode(3));
 assert(cfg.TV_mode==2 && cfg.screen_x==5 && cfg.screen_y==-7);
 assert(updates==2 && invalidates==2);
 accepted=1;assert(previewVideoMode(3) && cfg.TV_mode==3 && updates==3);
 samples[1].buffer=&cfg;
 success=1;dashSoundPlay(1);assert(attempts==1);
 attempts=0;success=18;dashSoundPlay(1);assert(attempts==2);
 attempts=0;success=22;dashSoundPlay(1);assert(attempts==6);
 attempts=0;success=-1;dashSoundPlay(1);assert(attempts==23);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder)
    (path / 'test.c').write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('PASS: video preview acceptance/rollback and SFX fallback never selects music voice')
