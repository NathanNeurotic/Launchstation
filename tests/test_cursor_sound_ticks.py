"""Every cursor move plays its tick, even at full autorepeat speed.

Compiles the real dashSoundPlay() (src/dash_sound.c) on the host with the
audio driver mocked. Pad autorepeat (src/pad.c) speeds up to one move every
40 ms; each move must key on one cursor voice, rotating over the six cursor
voices, and never the reserved music voice.

An optional git ref runs the same checks against that revision's sources.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]


def read(path):
    if len(sys.argv) > 1:
        return subprocess.check_output(['git', 'show', f'{sys.argv[1]}:{path}'], cwd=root, text=True)
    return (root / path).read_text()


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


sound = read('src/dash_sound.c')
pad = read('src/pad.c')
defines = ''.join(m.group(0) + '\n' for m in re.finditer(r'^#define SND_\w+ .*$', sound, re.M))
min_repeat = int(re.search(r'rpt_time\[port\] = Timer\(\) \+ (\d+);\s*//Min delay', pad).group(1))

program = r'''
#include <stdint.h>
#include <stdio.h>
typedef uint64_t u64;
enum {DASH_SND_CURSOR, DASH_SND_CONFIRM, DASH_SND_CANCEL, DASH_SND_TRANSITION, DASH_SND_COUNT};
typedef struct { void *buffer; } audsrv_adpcm_t;
static audsrv_adpcm_t samples[DASH_SND_COUNT];
static struct { int sound, sound_vol; } dashCfg = {1, 100};
static u64 now;
static u64 Timer(void) { return now; }
static int soundAlive(void) { return 1; }
static int dashIopParked(void) { return 0; }
static int played[64], nplayed;
static int audsrv_adpcm_set_volume_and_pan(int ch, int vol, int pan) { (void)ch; (void)vol; (void)pan; return 0; }
static int audsrv_ch_play_adpcm(int ch, audsrv_adpcm_t *s) { (void)s; if (nplayed < 64) played[nplayed++] = ch; return ch; }
'''
program += defines + function(sound, 'void dashSoundPlay(int id)')
program += r'''
int main(void) {
    static char sample;
    int moves = 24, i, failures = 0;
    for (i = 0; i < DASH_SND_COUNT; i++) samples[i].buffer = &sample;
    (void)Timer;
    for (i = 0; i < moves; i++) { now = 1000 + (u64)i * MIN_REPEAT; dashSoundPlay(DASH_SND_CURSOR); }
    if (nplayed != moves) { fprintf(stderr, "FAIL %d moves %d ms apart played %d ticks\n", moves, MIN_REPEAT, nplayed); failures++; }
    for (i = 0; i < nplayed; i++) {
        int want = SND_CURSOR_CH_BASE + i % SND_CURSOR_CH_COUNT;
        if (played[i] != want) { fprintf(stderr, "FAIL tick %d on voice %d, expected %d\n", i, played[i], want); failures++; break; }
    }
    if (failures) return 1;
    printf("PASS: %d cursor moves %d ms apart play %d ticks over voices %d-%d\n", moves, MIN_REPEAT, nplayed,
           SND_CURSOR_CH_BASE, SND_CURSOR_CH_BASE + SND_CURSOR_CH_COUNT - 1);
    return 0;
}
'''
program = f'#define MIN_REPEAT {min_repeat}\n' + program
with tempfile.TemporaryDirectory(prefix='cursor-sound-ticks-') as folder:
    p = Path(folder)
    (p / 'test.c').write_text(program)
    exe = p / 'test'
    cc = shutil.which('gcc') or shutil.which('cc')
    if not cc:
        raise SystemExit('Host C compiler required')
    subprocess.run([cc, '-std=gnu99', '-Werror=implicit-function-declaration', str(p / 'test.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=20)
