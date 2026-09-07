#!/usr/bin/env python3
"""Original, deterministic title score and rain. No sampled or licensed music."""
import array
import math
from pathlib import Path
import random
import wave

ROOT = Path(__file__).resolve().parents[1] / "assets/audio/intro"
RATE = 24000
DURATION = 16

def save(name, samples):
    ROOT.mkdir(parents=True, exist_ok=True)
    pcm = array.array("h", (round(max(-1, min(1, value)) * 32767) for value in samples))
    import sys
    if sys.byteorder != "little":
        pcm.byteswap()
    with wave.open(str(ROOT / name), "wb") as out:
        out.setparams((1, 2, RATE, len(pcm), "NONE", "not compressed"))
        out.writeframes(pcm.tobytes())
    print(ROOT / name)

def score():
    # Am(add9), Fmaj7, Dm9, E(sus): slow sustained chords, soft bass pulses.
    chords = ((57, 60, 64, 71), (53, 57, 60, 64), (50, 53, 57, 64), (52, 57, 59, 64))
    for i in range(RATE * DURATION):
        t = i / RATE
        bar = int(t / 4)
        local = t % 4
        envelope = min(local / 0.7, 1, (4 - local) / 1.2)
        pad = 0.0
        for note in chords[bar]:
            hz = 440 * 2 ** ((note - 69) / 12)
            pad += math.sin(math.tau * hz * t + 0.16 * math.sin(math.tau * 0.25 * t))
            pad += 0.22 * math.sin(math.tau * hz * 2 * t)
        pulse = t % 2
        bass_hz = 440 * 2 ** ((chords[bar][0] - 24 - 69) / 12)
        bass = math.sin(math.tau * bass_hz * t) * min(pulse / 0.025, 1) * math.exp(-pulse * 2)
        edge = min(t / 0.1, 1, (DURATION - t) / 0.4)
        yield (pad * envelope * 0.037 + bass * 0.18) * edge

def rain():
    rng = random.Random(1991)
    low = 0.0
    for i in range(RATE * DURATION):
        t = i / RATE
        noise = rng.uniform(-1, 1)
        low += 0.08 * (noise - low)
        edge = min(t / 0.05, 1, (DURATION - t) / 0.05)
        yield (noise * 0.09 + low * 0.75) * (0.8 + 0.2 * math.sin(math.tau * t / 16)) * edge

if __name__ == "__main__":
    save("probable-cause-title-score-v1.wav", score())
    save("probable-cause-title-rain-v1.wav", rain())
