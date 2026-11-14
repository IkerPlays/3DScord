"""Generates the sounds of 3DScord: original bell-like tones (nothing from Discord or Nintendo). The app sounds are raw 16-bit mono PCM at
32000 Hz in romfs/discord-sounds; the HOME Menu banner jingle is resources/audio.wav (stereo, 44100 Hz, under 3 s).
Usage: python3 tools/brand/sounds.py  (from the repository root)"""
import os, wave
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
RATE = 32000

def note(freq, dur, sr=RATE, vol=1.0, bright=1.0, decay=7.0):
    """A soft bell: a sine with a few harmonics and a fast decay, with a short attack so it does not click."""
    n = int(dur * sr)
    t = np.arange(n) / sr
    s = (np.sin(2 * np.pi * freq * t) + 0.35 * bright * np.sin(2 * np.pi * freq * 2.01 * t) * np.exp(-t * 12) +
         0.18 * bright * np.sin(2 * np.pi * freq * 3.99 * t) * np.exp(-t * 18))
    env = np.exp(-t * decay) * np.minimum(1.0, t / 0.004)
    tail = int(0.02 * sr)
    if n > tail:
        env[-tail:] *= np.linspace(1, 0, tail)
    return s * env * vol

def hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)

def seq(items, total, sr=RATE):
    """items: (start seconds, midi note, duration, volume). Returns mono float samples."""
    buf = np.zeros(int(total * sr))
    for start, m, dur, vol in items:
        s = note(hz(m), dur, sr, vol)
        i = int(start * sr)
        buf[i:i + len(s)] += s[:len(buf) - i]
    return buf

def save_pcm(name, x):
    x = x / (np.max(np.abs(x)) or 1) * 0.5  # about as loud as the sounds it replaces
    pcm = (np.clip(x, -1, 1) * 32767).astype("<i2")
    path = os.path.join(ROOT, "romfs", "discord-sounds", name + ".pcm")
    open(path, "wb").write(pcm.tobytes())
    print(name, round(len(x) / RATE, 2), "s")

def blip(m0, m1, dur=0.12):
    n = int(dur * RATE)
    t = np.arange(n) / RATE
    f = np.linspace(hz(m0), hz(m1), n)
    ph = 2 * np.pi * np.cumsum(f) / RATE
    env = np.minimum(1.0, t / 0.006) * np.exp(-t * 14)
    return np.sin(ph) * env

def main():
    # notification: a short two-note chime, going up
    save_pcm("notification", seq([(0.0, 88, 0.30, 0.8), (0.11, 95, 0.45, 1.0)], 0.6))
    # joining the call: three notes up; leaving: three notes down; someone else leaves: two soft notes down
    save_pcm("join", seq([(0.0, 76, 0.5, 0.8), (0.13, 80, 0.5, 0.9), (0.26, 83, 0.8, 1.0)], 1.1))
    save_pcm("left", seq([(0.0, 83, 0.5, 0.9), (0.13, 80, 0.5, 0.8), (0.26, 76, 0.8, 0.9)], 1.1))
    save_pcm("left2", seq([(0.0, 79, 0.4, 0.7), (0.14, 74, 0.7, 0.7)], 0.9))
    # microphone and headphones: tiny blips
    save_pcm("mic_on", np.concatenate([blip(72, 79), np.zeros(int(0.1 * RATE))]))
    save_pcm("mic_off", np.concatenate([blip(79, 70), np.zeros(int(0.1 * RATE))]))
    save_pcm("headphone_on", np.concatenate([blip(70, 77, 0.1), np.zeros(int(0.04 * RATE)), blip(77, 84, 0.12), np.zeros(int(0.1 * RATE))]))
    save_pcm("headphone_off", np.concatenate([blip(84, 77, 0.1), np.zeros(int(0.04 * RATE)), blip(77, 70, 0.12), np.zeros(int(0.1 * RATE))]))
    # outgoing call: the double ring, twice
    out = []
    for k in range(2):
        t0 = k * 1.2
        out += [(t0, 81, 0.35, 0.8), (t0, 76, 0.35, 0.6), (t0 + 0.4, 81, 0.35, 0.8), (t0 + 0.4, 76, 0.35, 0.6)]
    save_pcm("call_outgoing", seq(out, 2.5))
    # incoming call: a cheerful melody, twice
    melody = [(0, 76), (0.18, 80), (0.36, 83), (0.54, 88), (0.9, 83), (1.08, 80), (1.26, 83), (1.44, 76), (1.9, 74), (2.08, 78), (2.26, 81), (2.44, 86), (2.8, 81), (2.98, 78), (3.16, 74), (3.34, 78)]
    items = []
    for rep in range(2):
        for st, m in melody:
            items.append((st + rep * 3.9, m, 0.5, 0.9))
        items.append((rep * 3.9, 52, 1.8, 0.5))
        items.append((rep * 3.9 + 1.9, 50, 1.8, 0.5))
    save_pcm("call_incoming", seq(items, 7.8))
    # banner: a rising arpeggio with a sparkle on top, under 3 s, stereo 44100 Hz
    sr = 44100
    jingle = seq([(0.0, 69, 1.2, 0.6), (0.0, 57, 2.0, 0.5), (0.18, 73, 1.2, 0.6), (0.36, 76, 1.2, 0.7), (0.54, 81, 1.4, 0.8), (0.9, 85, 1.6, 0.8), (1.2, 88, 1.6, 0.7), (1.5, 93, 1.4, 0.5)], 2.8, sr)
    jingle = jingle / np.max(np.abs(jingle)) * 0.8
    fade = int(0.4 * sr)
    jingle[-fade:] *= np.linspace(1, 0, fade)
    stereo = np.stack([jingle, np.roll(jingle, 220) * 0.95], axis=1)
    pcm = (np.clip(stereo, -1, 1) * 32767).astype("<i2")
    with wave.open(os.path.join(ROOT, "resources", "audio.wav"), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(pcm.tobytes())
    print("banner", round(len(jingle) / sr, 2), "s")

if __name__ == "__main__":
    main()
