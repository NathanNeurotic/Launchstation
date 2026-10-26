#!/usr/bin/env python3
"""Synthesize the "PS2" sound theme and encode it as audsrv .adp files.

The sounds are original, procedurally generated tones in the style of the
console's browser: soft glassy partials, short fast-attack blips for input,
a slow ambient swell for boot.  Nothing is sampled from Sony firmware.

Output: res/sfx_ps2/<name>.adp, mono, 44.1 kHz PS-ADPCM, same container as
res/sfx/transition.adp (16-byte "APCM" header; flags 1,7 on the final blocks).

    python3 scripts/gen_sfx.py            # write the files
    python3 scripts/gen_sfx.py --check    # verify committed files match
"""
import struct
import sys
from pathlib import Path

import numpy as np

RATE = 44100
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "res" / "sfx_ps2"

# PS-ADPCM predictor coefficients (x/64)
K0 = (0, 60, 115, 98, 122)
K1 = (0, 0, -52, -55, -60)


def encode_block(samples, s1, s2):
    """Pick the filter/shift with least error for 28 samples."""
    best = None
    for f in range(5):
        for shift in range(0, 13):
            h1, h2 = s1, s2
            nib = []
            err = 0.0
            for x in samples:
                pred = (h1 * K0[f] + h2 * K1[f]) / 64.0
                d = x - pred
                q = int(round(d / (1 << (12 - shift))))
                q = max(-8, min(7, q))
                rec = pred + q * (1 << (12 - shift))
                rec = max(-32768, min(32767, int(round(rec))))
                err += (x - rec) ** 2
                nib.append(q & 0xF)
                h2, h1 = h1, rec
            if best is None or err < best[0]:
                best = (err, f, shift, nib, h1, h2)
    _, f, shift, nib, h1, h2 = best
    out = bytes([(f << 4) | shift, 0])
    for i in range(0, 28, 2):
        out += bytes([nib[i] | (nib[i + 1] << 4)])
    return out, h1, h2


def decode(blob):
    """Reference decoder (used by --check and by the tests)."""
    d = blob[16:]
    h1 = h2 = 0
    pcm = []
    for i in range(0, len(d), 16):
        b = d[i:i + 16]
        f, shift = b[0] >> 4, b[0] & 15
        for byte in b[2:]:
            for n in (byte & 15, byte >> 4):
                v = n - 16 if n > 7 else n
                x = v * (1 << (12 - shift)) + (h1 * K0[f] + h2 * K1[f]) / 64.0
                x = max(-32768, min(32767, int(round(x))))
                pcm.append(x)
                h2, h1 = h1, x
    return np.array(pcm, dtype=np.int16)


def to_adp(x):
    x = np.asarray(x, dtype=np.float64)
    x = np.clip(x, -1, 1) * 32000
    pad = (-len(x)) % 28
    x = np.concatenate([x, np.zeros(pad)])
    blocks = []
    h1 = h2 = 0
    for i in range(0, len(x), 28):
        b, h1, h2 = encode_block(x[i:i + 28], h1, h2)
        blocks.append(bytearray(b))
    # a silent terminating pair, flagged like the existing effects (1 then 7)
    for flag in (1, 7):
        blocks.append(bytearray([0, flag]) + bytes(14))
    data = b"".join(bytes(b) for b in blocks)
    header = b"APCM\x01\x01\x00\x00" + struct.pack("<II", 0x0EB3, len(blocks) * 28)
    return header + data


# --- synthesis helpers -------------------------------------------------------
def t_axis(sec):
    return np.arange(int(RATE * sec)) / RATE


def env(n, attack_ms, decay_tau_ms):
    t = np.arange(n) / RATE
    a = np.minimum(1.0, t / (attack_ms / 1000.0))
    return a * np.exp(-t / (decay_tau_ms / 1000.0))


def bell(freq, sec, attack_ms=2.0, tau_ms=60.0, partials=((1, 1.0), (2.01, 0.35), (3.02, 0.12)),
         glide=0.0):
    t = t_axis(sec)
    f = freq * (1.0 + glide * np.exp(-t / 0.03))
    ph = 2 * np.pi * np.cumsum(f) / RATE
    y = sum(a * np.sin(p * ph) for p, a in partials)
    return y * env(len(t), attack_ms, tau_ms)


def swell(freqs, sec, rise=0.6, fall=0.5):
    t = t_axis(sec)
    y = np.zeros_like(t)
    for i, f in enumerate(freqs):
        drift = 1.0 + 0.002 * np.sin(2 * np.pi * (0.3 + 0.11 * i) * t + i)
        y += np.sin(2 * np.pi * f * np.cumsum(drift) / RATE) / (1 + 0.3 * i)
    a = np.clip(t / rise, 0, 1) ** 2
    r = np.clip((sec - t) / fall, 0, 1) ** 2
    return y * a * r


def echo(y, delays_ms=(70, 140), gains=(0.28, 0.12)):
    out = np.copy(y)
    for d, g in zip(delays_ms, gains):
        n = int(RATE * d / 1000)
        out = np.concatenate([out, np.zeros(n)])
        out[n:n + len(y)] += g * y
    return out


def norm(y, peak):
    return y / (np.max(np.abs(y)) + 1e-9) * peak


def fade_out(y, ms=8):
    n = min(len(y), int(RATE * ms / 1000))
    y = np.copy(y)
    y[-n:] *= np.linspace(1, 0, n)
    return y


def at(y, ms):
    return np.pad(y, (int(RATE * ms / 1000), 0))


def mix(*layers):
    n = max(len(y) for y in layers)
    out = np.zeros(n)
    for y in layers:
        out[:len(y)] += y
    return out


def make_all():
    s = {}
    # Input blips: <=120 ms, 1.5 ms attack so they land the frame they are triggered.
    s["cursor"] = fade_out(norm(bell(1568, 0.08, 1.5, 18, ((1, 1), (2.76, 0.18))), 0.55))
    s["confirm"] = fade_out(norm(mix(bell(1175, 0.30, 1.5, 70), 0.8 * at(bell(1760, 0.26, 1.5, 70), 45)), 0.7))
    s["cancel"] = fade_out(norm(mix(bell(880, 0.20, 1.5, 45, glide=-0.18), 0.6 * bell(587, 0.20, 1.5, 45)), 0.65))
    # L1/R1 tab hop: one clean airy sweep (single event, no tail that could read as a double).
    t = t_axis(0.22)
    sweep = np.sin(2 * np.pi * np.cumsum(700 + 1500 * (t / 0.22) ** 2) / RATE)
    s["transition"] = fade_out(norm(sweep * env(len(t), 3, 55), 0.5), 12)
    s["message"] = fade_out(norm(echo(mix(bell(1318, 0.30, 2, 90), at(bell(1976, 0.30, 2, 90), 120))), 0.6), 20)
    s["bd_connect"] = fade_out(norm(echo(mix(bell(784, 0.18, 2, 60), at(bell(1047, 0.30, 2, 90), 110))), 0.6), 20)
    s["bd_disconnect"] = fade_out(norm(echo(mix(bell(1047, 0.18, 2, 60), at(bell(784, 0.30, 2, 90), 110))), 0.6), 20)
    # Boot: slow ambient swell, kept short so it does not delay the first frame's input.
    s["boot"] = fade_out(norm(echo(swell([110, 165, 220, 330, 441], 2.4, 0.9, 1.1),
                                   (180, 360), (0.3, 0.15)), 0.6), 30)
    return s


def main():
    check = "--check" in sys.argv
    OUT.mkdir(parents=True, exist_ok=True)
    bad = 0
    for name, pcm in make_all().items():
        blob = to_adp(pcm)
        path = OUT / f"{name}.adp"
        if check:
            if not path.exists() or path.read_bytes() != blob:
                print("MISMATCH", path)
                bad += 1
        else:
            path.write_bytes(blob)
            print(f"{path.relative_to(ROOT)}  {len(blob):7d} bytes  {len(pcm) / RATE * 1000:.0f} ms")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
