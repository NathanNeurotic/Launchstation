"""The built-in (synthesized) sound set: reproducible, well-formed and wired in."""
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "scripts"))
import numpy as np
import gen_sfx

names = ["cursor", "confirm", "cancel", "transition", "message", "boot", "bd_connect", "bd_disconnect"]
# committed files must decode to what the generator synthesizes (not byte-compared:
# libm differences between numpy builds may flip ADPCM rounding)
synth = gen_sfx.make_all()

for n in names:
    b = (root / f"res/sfx_ps2/{n}.adp").read_bytes()
    assert b[:4] == b"APCM" and b[4:8] == b"\x01\x01\x00\x00", n
    rate, samples = np.frombuffer(b[8:16], "<u4")
    assert rate == 0x0EB3                                  # 44.1 kHz pitch
    assert (len(b) - 16) % 16 == 0
    blocks = (len(b) - 16) // 16
    assert samples == blocks * 28
    flags = [b[16 + i * 16 + 1] for i in range(blocks)]
    assert flags[-2:] == [1, 7] and not any(flags[:-2]), n
    pcm = gen_sfx.decode(b).astype(float)
    assert np.abs(pcm).max() < 32000 and np.abs(pcm).max() > 8000, n   # loud enough, no clipping
    ref = np.clip(synth[n], -1, 1) * 32000
    m = min(len(ref), len(pcm))
    snr = 10 * np.log10(np.sum(ref[:m] ** 2) / (np.sum((ref[:m] - pcm[:m]) ** 2) + 1e-9))
    assert snr > 30, (n, snr)
    onset = np.argmax(np.abs(pcm) > 300) / 44.1
    if n != "boot":
        assert onset < 2.0, (n, onset)                        # input effects start immediately
    dur = len(pcm) / 44.1
    limits = {"cursor": 120, "transition": 260, "cancel": 260, "confirm": 350}
    assert dur < limits.get(n, 3200), (n, dur)

# wiring: blobs embedded, and the only set (the old Classic set and Sound theme are gone)
S = (root / "src/embed_blobs.S").read_text(encoding="latin-1")
C = (root / "src/dash_sound.c").read_text(encoding="latin-1")
for n in names:
    assert f'EMBED_FILE(sfx2_{n}_adp, "res/sfx_ps2/{n}.adp")' in S
    assert f"sfx2_{n}_adp" in C
assert 'res/sfx/' not in S and "sound_theme" not in C
st = (root / "src/dash_store.c").read_text(encoding="latin-1")
assert "Dash_SoundTheme" not in st   # an old saved value is skipped like any unknown key
assert '"Dash_CustomSounds"' in st and "c->custom_sounds" in st
print("Built-in sound set: OK")
