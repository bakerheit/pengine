#!/usr/bin/env python3
"""Split a real ignition recording by envelope/spectral stability, not fixed cuts.

Requires numpy and ffmpeg. No synthesis: PCM comes only from the supplied clip.
The report records all selected times, quality metrics and hashes for audit.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import wave

import numpy as np

RATE = 48000


def write_wav(path, samples):
    pcm = np.round(np.clip(samples, -1, 1) * 32767).astype('<i2')
    with wave.open(str(path), 'wb') as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(pcm.tobytes())


def prepare(source, destination):
    raw = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(source),
                                   '-ac', '1', '-ar', str(RATE), '-f', 'f32le', '-'])
    x = np.frombuffer(raw, dtype='<f4').astype(np.float64)
    if len(x) < RATE * 5 or not np.isfinite(x).all():
        raise ValueError('Need at least five seconds of finite recorded audio')
    x -= np.mean(x)
    # 50ms spectral/envelope frames. Normalize spectra to separate a change
    # in timbre from a simple gain change as the startup settles.
    hop = RATE // 20
    frames = x[:len(x) // hop * hop].reshape(-1, hop)
    energy = np.sqrt(np.mean(frames ** 2, axis=1))
    spectra = np.abs(np.fft.rfft(frames * np.hanning(hop), axis=1))
    spectra /= np.maximum(np.linalg.norm(spectra, axis=1, keepdims=True), 1e-12)
    audible = np.flatnonzero(energy > np.max(energy) * .08)
    if not len(audible):
        raise ValueError('No audible engine found')
    onset = max(0, int(audible[0]) * hop - int(.025 * RATE))
    last = int(audible[-1])
    # Search a three-second bed after the initial ignition burst and before
    # the recording's shutdown/fade. Penalize both gain and spectral drift.
    width = 60
    candidates = []
    for i in range(int(onset / hop) + 30, last - width):
        e = energy[i:i + width]
        if np.min(e) < np.median(energy[audible]) * .55:
            continue
        spec = spectra[i:i + width]
        score = np.std(e) / np.mean(e) + np.mean(np.linalg.norm(spec - spec.mean(0), axis=1))
        candidates.append((float(score), i))
    if not candidates:
        raise ValueError('No stable idle region found; needs a different source')
    stability, selected = min(candidates)
    reference = np.median(energy[selected:selected + width])
    reference_spectrum = spectra[selected:selected + width].mean(0)
    # Earliest sustained half-second matching the idle bed ends the startup.
    settled = selected
    for i in range(int(onset / hop) + 12, selected + 1):
        e = energy[i:i + 10]
        distance = np.mean(np.linalg.norm(spectra[i:i + 10] - reference_spectrum, axis=1))
        if np.all((e > reference * .70) & (e < reference * 1.4)) and distance < .48:
            settled = i
            break
    start_end = max(onset + int(.8 * RATE), (settled + 10) * hop)
    # Find a correlated tail near the end of the selected bed. Preserve the
    # samples *following* the head overlap, so wrap is phase-continuous.
    begin = selected * hop
    fade = int(.12 * RATE)
    head = x[begin:begin + fade]
    ends = range(begin + int(2.5 * RATE), begin + int(3 * RATE), 24)
    end = min(ends, key=lambda j: np.mean((x[j:j + fade] - head) ** 2))
    bed = x[begin:end + fade].copy()
    t = np.linspace(0, 1, fade)
    blend = .5 - .5 * np.cos(np.pi * t)
    bed[-fade:] = bed[-fade:] * (1 - blend) + head * blend
    idle = bed[fade:]
    # Shared gain preserves the actual start/idle relationship.
    gain = min(4., .78 / np.max(np.abs(x[onset:start_end])))
    startup = x[onset:start_end].copy() * gain
    startup[:int(.008 * RATE)] *= np.linspace(0, 1, int(.008 * RATE))
    startup[-int(.15 * RATE):] *= np.linspace(1, 0, int(.15 * RATE))
    idle *= gain
    seam = float(abs(idle[-1] - idle[0]))
    p999 = float(np.quantile(np.abs(np.diff(idle)), .999))
    assert seam <= p999, (seam, p999)
    assert np.max(np.abs(idle)) < 1 and np.max(np.abs(startup)) < 1
    destination.mkdir(parents=True, exist_ok=True)
    write_wav(destination / 'engine_start.wav', startup)
    write_wav(destination / 'engine_idle.wav', idle)
    # Listening fixture: ignition, three wraps, then a short exit fade.
    overlap = int(.15 * RATE)
    preview = np.concatenate([startup[:-overlap], startup[-overlap:] + idle[:overlap] * np.linspace(0, 1, overlap),
                              np.tile(idle, 3)[overlap:]])
    preview[-overlap:] *= np.linspace(1, 0, overlap)
    write_wav(destination / 'engine_start_idle_preview.wav', preview)
    report = dict(source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  source_seconds=len(x) / RATE, startup_source_seconds=[onset / RATE, start_end / RATE],
                  idle_source_seconds=[begin / RATE, (end + fade) / RATE],
                  idle_loop_seconds=len(idle) / RATE, startup_seconds=len(startup) / RATE,
                  stability_score=stability, gain=gain, seam_step=seam, interior_step_p999=p999,
                  idle_rms=float(np.sqrt(np.mean(idle ** 2))),
                  files={name: hashlib.sha256((destination / name).read_bytes()).hexdigest()
                         for name in ['engine_start.wav', 'engine_idle.wav']})
    (destination / 'engine_idle_analysis.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    prepare(args.source, args.destination)
