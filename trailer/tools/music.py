"""Original chiptune-style track for the 3DScord trailer (120 BPM, A minor). Pure numpy, no samples: nothing to license."""
import sys, wave
import numpy as np

SR = 44100
BPM = 120
BEAT = 60.0 / BPM
BAR = BEAT * 4
rng = np.random.default_rng(7)

CHORDS = [  # (bass root, chord tones) per bar: Am F C G
    (45, [57, 60, 64]),
    (41, [53, 57, 60]),
    (48, [55, 60, 64]),
    (43, [55, 59, 62]),
]

def hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)

def env(n, a=0.002, d=0.1, s=0.0, r=0.05):
    t = np.arange(n) / SR
    e = np.where(t < a, t / max(a, 1e-6), np.exp(-(t - a) / max(d, 1e-6)) * (1 - s) + s)
    tail = int(r * SR)
    if tail and n > tail:
        e[-tail:] *= np.linspace(1, 0, tail)
    return e

def square(f, n, duty=0.5):
    t = np.arange(n) / SR
    return np.where((t * f) % 1.0 < duty, 1.0, -1.0)

def saw(f, n):
    t = np.arange(n) / SR
    return 2 * ((t * f) % 1.0) - 1

def lowpass(x, cutoff):
    a = np.exp(-2 * np.pi * cutoff / SR)
    out = np.empty_like(x)
    y = 0.0
    for i in range(len(x)):  # slow but the buffers are short
        y = (1 - a) * x[i] + a * y
        out[i] = y
    return out

def add(buf, start, sig, gain=1.0, pan=0.0):
    i = int(start * SR)
    if i >= buf.shape[0]:
        return
    j = min(buf.shape[0], i + len(sig))
    seg = sig[: j - i] * gain
    buf[i:j, 0] += seg * (1 - max(0, pan))
    buf[i:j, 1] += seg * (1 + min(0, pan))

def kick():
    n = int(0.28 * SR)
    t = np.arange(n) / SR
    f = 45 + 110 * np.exp(-t * 30)
    ph = 2 * np.pi * np.cumsum(f) / SR
    return np.sin(ph) * np.exp(-t * 11) * 1.0

def snare():
    n = int(0.22 * SR)
    t = np.arange(n) / SR
    noise = rng.standard_normal(n) * np.exp(-t * 24)
    tone = np.sin(2 * np.pi * 190 * t) * np.exp(-t * 28)
    return noise * 0.55 + tone * 0.4

def hat(open_=False):
    n = int((0.16 if open_ else 0.045) * SR)
    t = np.arange(n) / SR
    noise = rng.standard_normal(n)
    noise = noise - np.concatenate([[0], noise[:-1]]) * 0.95  # crude high-pass
    return noise * np.exp(-t * (18 if open_ else 70)) * 0.35

def render(bars, intro_bars=2, riser_bar=None):
    total = bars * BAR
    buf = np.zeros((int(total * SR) + SR, 2))
    for bar in range(bars):
        t0 = bar * BAR
        root, tones = CHORDS[bar % 4]
        full = bar >= intro_bars
        # pad: soft saws, quieter in the full part
        n = int(BAR * SR)
        pad = sum(saw(hz(m) * (1 + 0.003 * k), n) for m in tones for k in (-1, 1)) / 6
        pad = lowpass(pad[::4], 1800)  # decimated for speed, then stretched back
        pad = np.repeat(pad, 4)[:n]
        pad *= env(n, a=0.3, d=5, s=0.9, r=0.4)
        add(buf, t0, pad, 0.30 if not full else 0.10)
        # arp: 16ths up and down the chord, one octave up when the track is full
        steps = [0, 1, 2, 1] * 4
        for s_i, k in enumerate(steps):
            st = t0 + s_i * BEAT / 4
            m = tones[k % 3] + (12 if full else 0) + (12 if bar % 8 >= 4 and full else 0)
            ln = int(BEAT / 4 * SR * 0.9)
            sig = square(hz(m), ln, 0.25) * env(ln, 0.002, 0.07, 0.15, 0.02)
            add(buf, st, sig, 0.10 if full else 0.09, pan=0.25 if s_i % 2 else -0.25)
        if full:
            # bass: eighths on the root, octave jump on the off beats
            for e in range(8):
                st = t0 + e * BEAT / 2
                m = root + (12 if e % 4 == 3 else 0)
                ln = int(BEAT / 2 * SR * 0.9)
                sig = (square(hz(m), ln, 0.5) * 0.6 + saw(hz(m), ln) * 0.4) * env(ln, 0.003, 0.12, 0.3, 0.02)
                add(buf, st, sig, 0.30)
            for b in range(4):
                add(buf, t0 + b * BEAT, kick(), 0.85)
                if b in (1, 3):
                    add(buf, t0 + b * BEAT, snare(), 0.6)
            for e in range(8):
                add(buf, t0 + e * BEAT / 2 + (BEAT / 4 if e % 2 else 0), hat(e % 4 == 3), 0.5)
        else:
            add(buf, t0, kick(), 0.5)
            add(buf, t0 + 2 * BEAT, kick(), 0.35)
    # riser into the drop
    if riser_bar is not None:
        n = int(BAR * SR)
        t = np.arange(n) / SR
        noise = rng.standard_normal(n) * (t / BAR) ** 2
        sweep = np.sin(2 * np.pi * np.cumsum(200 + 2600 * (t / BAR) ** 2) / SR) * (t / BAR) ** 2
        add(buf, riser_bar * BAR, (noise * 0.25 + sweep * 0.3), 0.5)
    # last hit
    root, tones = CHORDS[0]
    n = int(2.0 * SR)
    hit = sum(square(hz(m), n, 0.25) for m in tones + [57 + 12]) / 4 * env(n, 0.002, 0.8, 0.0, 0.3)
    add(buf, (bars - 1) * BAR + 0.0, np.zeros(1), 0)
    return buf

def master(buf, seconds):
    n = int(seconds * SR)
    buf = buf[:n]
    peak = np.max(np.abs(buf)) or 1
    buf = np.tanh(buf / peak * 1.6) / np.tanh(1.6)
    buf *= 0.7
    fade = int(1.5 * SR)
    buf[-fade:] *= np.linspace(1, 0, fade)[:, None]
    return buf

def write(path, buf):
    pcm = (np.clip(buf, -1, 1) * 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())

if __name__ == "__main__":
    bars = int(sys.argv[2])
    intro = int(sys.argv[3])
    riser = intro - 1 if intro > 0 else None
    out = master(render(bars, intro, riser), bars * BAR)
    write(sys.argv[1], out)
    print("wrote", sys.argv[1], bars * BAR, "s")
