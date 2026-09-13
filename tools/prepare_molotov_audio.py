#!/usr/bin/env python3
"""Prepare the molotov's three recorded clips.

    python3 tools/prepare_molotov_audio.py \\
      ~/Downloads/dragon-studio-glass-breaking-504033.mp3 \\
      ~/Downloads/djartmusic-short-fire-whoosh_1-317280.mp3 \\
      ~/Downloads/maxhammarback-fire-sound-efftect-21991.mp3 \\
      assets/audio/weapons/runtime

Writes 48 kHz MONO 16-bit WAVs. Mono is the decision, not the default: all
three play SPATIALLY, at the bottle's impact point or at the fire's centroid,
and the mixer derives their pan from that position. A stereo take at a world
position is two sources pretending to be one, and the image smears the moment
the player walks around the fire.

Three clips, three different jobs, three different cooks:

*   `molotov_glass.wav` — the bottle bursting. A transient. Trimmed so the
    SHATTER is sample zero, and the settling shards after it are kept, because
    they are what makes the fire's first half second sound like broken glass
    rather than a generic bang.

*   `molotov_whoosh.wav` — the fuel catching. **THE SUPPLIED TAKE HAS 0.9
    SECONDS OF LEAD-IN AND THAT IS THE ONE THING THAT HAD TO BE FIXED HERE.**
    The file opens with 0.565 s of digital silence, then a further 0.3 s of
    swell sitting under -40 dBFS, i.e. inaudible. Played raw, the ignition you
    can hear would arrive nearly a second after the bottle broke — and
    pre-roll never reads as an audio offset, it reads as the game being late,
    which is exactly the trap tools/prepare_footsteps.py fell into first.
    Trimmed at the -30 dB crossing, the clip opens on an audible swell that
    peaks 0.45 s later, which is the fuel taking hold and matches the fire's
    visual bloom.

*   `fire_loop.wav` — the fire burning, held open under the flames for as long
    as they last. The source is fifteen seconds of continuous crackle, which
    is a bed, not a one-shot: a window of it is cut and its own tail is
    equal-power crossfaded back over its head so the wrap is seamless. Same
    construction as `light_rain_loop.wav`; see assets/audio/world/SOURCES.md.

Gains are per clip and set by PEAK, not matched across the three, because they
are not a family: a bottle bursting at your feet is meant to be louder than the
bed that follows it. Nothing here compresses, pitches or adds effects.
"""
import argparse
import array
import hashlib
import json
import math
import subprocess
import sys
import wave
from pathlib import Path

RATE = 48000

# Head/tail gates, in dB below the take's own peak, and the target peak after
# gain. A gate is picked per clip because the three tails mean different things:
# a glass tail is shards you want, a whoosh tail is room noise you do not.
GLASS = dict(name='molotov_glass', head_db=-40.0, tail_db=-55.0, peak=0.80,
             fade_in_s=0.0005, fade_out_s=0.040)
WHOOSH = dict(name='molotov_whoosh', head_db=-30.0, tail_db=-45.0, peak=0.72,
              fade_in_s=0.0080, fade_out_s=0.060)

# The loop window, in source seconds, and the seam crossfade. Six seconds is
# long enough that the ear does not learn the pattern of a stochastic crackle
# and short enough to stay well under a megabyte.
FIRE = dict(name='fire_loop', start_s=2.0, length_s=6.0, seam_s=1.0, peak=0.62)

# Envelope window for the gate search. 10 ms is short enough to land the glass
# transient within a frame of where it really starts and long enough that one
# stray sample of dither does not open the gate.
ENVELOPE_S = 0.010


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def decode_mono(path):
    samples = array.array('f', subprocess.check_output([
        'ffmpeg', '-v', 'error', '-i', str(path), '-ac', '1',
        '-ar', str(RATE), '-f', 'f32le', 'pipe:1']))
    if sys.byteorder != 'little':
        samples.byteswap()
    if not samples or not all(math.isfinite(v) for v in samples):
        raise ValueError(f'Invalid audio: {path}')
    return samples


def envelope(samples):
    width = int(RATE * ENVELOPE_S)
    return [math.sqrt(sum(v * v for v in samples[i:i + width]) / width)
            for i in range(0, len(samples) - width, width)], width


def gate(samples, head_db, tail_db):
    """Cut the head at the first envelope frame above `head_db` and the tail
    after the last frame above `tail_db`, both relative to the take's own peak
    envelope. Returns (cut, start_s, end_s)."""
    frames, width = envelope(samples)
    loudest = max(frames)
    if loudest <= 0.0:
        raise ValueError('Silent take')
    head = loudest * (10.0 ** (head_db / 20.0))
    tail = loudest * (10.0 ** (tail_db / 20.0))
    first = next(i for i, v in enumerate(frames) if v >= head)
    last = max(i for i, v in enumerate(frames) if v >= tail)
    start = first * width
    end = min(len(samples), (last + 1) * width)
    return samples[start:end], start / RATE, end / RATE


def loop(samples, start_s, length_s, seam_s):
    """Cut `length_s` from `start_s` and fold the following `seam_s` back over
    its head with an equal-power crossfade, so playback wraps without a click
    or a level dip. Equal power rather than linear because fire crackle is
    decorrelated noise: a linear pair sums to -3 dB through the middle of the
    seam and the wrap reads as a dropout."""
    start = int(start_s * RATE)
    length = int(length_s * RATE)
    seam = int(seam_s * RATE)
    if start + length + seam > len(samples):
        raise ValueError('Source is shorter than the requested loop plus seam')
    body = array.array('f', samples[start:start + length])
    tail = samples[start + length:start + length + seam]
    for i in range(seam):
        phase = (i + 0.5) / seam * (math.pi * 0.5)
        body[i] = body[i] * math.sin(phase) + tail[i] * math.cos(phase)
    return body


def write_wav(path, samples, gain, fade_in_s=0.0, fade_out_s=0.0):
    pcm = array.array('h')
    fade_in = max(1, int(RATE * fade_in_s))
    fade_out = max(1, int(RATE * fade_out_s))
    count = len(samples)
    for i, value in enumerate(samples):
        window = min(1.0, (i + 1) / fade_in, (count - i) / fade_out)
        pcm.append(round(max(-1.0, min(1.0, value * gain * window)) * 32767))
    if sys.byteorder != 'little':
        pcm.byteswap()
    with wave.open(str(path), 'wb') as wav:
        wav.setparams((1, 2, RATE, 0, 'NONE', 'not compressed'))
        wav.writeframes(pcm.tobytes())


def stats(samples):
    peak = max(abs(v) for v in samples)
    rms = math.sqrt(sum(v * v for v in samples) / len(samples))
    return peak, rms


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('glass', type=Path, help='glass breaking MP3/WAV')
    parser.add_argument('whoosh', type=Path, help='fire whoosh MP3/WAV')
    parser.add_argument('fire', type=Path, help='fire crackle MP3/WAV')
    parser.add_argument('output', type=Path,
                        help='assets/audio/weapons/runtime')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    manifest = []
    for source, spec in ((args.glass, GLASS), (args.whoosh, WHOOSH)):
        raw = decode_mono(source)
        cut, cut_start, cut_end = gate(raw, spec['head_db'], spec['tail_db'])
        peak, rms = stats(cut)
        gain = spec['peak'] / peak
        name = spec['name'] + '.wav'
        out = args.output / name
        write_wav(out, cut, gain, spec['fade_in_s'], spec['fade_out_s'])
        manifest.append(dict(
            file=name, kind='one-shot', source=source.name,
            source_sha256=sha(source), runtime_sha256=sha(out),
            source_duration_s=round(len(raw) / RATE, 4),
            source_cut_s=[round(cut_start, 4), round(cut_end, 4)],
            duration_s=round(len(cut) / RATE, 4),
            head_gate_db=spec['head_db'], tail_gate_db=spec['tail_db'],
            source_peak=round(peak, 4), source_rms=round(rms, 5),
            gain=round(gain, 5), channels=1, sample_rate=RATE))

    raw = decode_mono(args.fire)
    body = loop(raw, FIRE['start_s'], FIRE['length_s'], FIRE['seam_s'])
    peak, rms = stats(body)
    gain = FIRE['peak'] / peak
    name = FIRE['name'] + '.wav'
    out = args.output / name
    # No fades: a loop that fades in and out at its own edges is a loop with a
    # hole punched in each wrap. The seam crossfade above is what makes the
    # ends meet, and the mixer ramps the voice's gain when the fire starts.
    write_wav(out, body, gain, fade_in_s=1e-9, fade_out_s=1e-9)
    manifest.append(dict(
        file=name, kind='loop', source=args.fire.name,
        source_sha256=sha(args.fire), runtime_sha256=sha(out),
        source_duration_s=round(len(raw) / RATE, 4),
        source_cut_s=[FIRE['start_s'], FIRE['start_s'] + FIRE['length_s']],
        duration_s=round(len(body) / RATE, 4),
        seam_crossfade_s=FIRE['seam_s'], seam_curve='equal power',
        source_peak=round(peak, 4), source_rms=round(rms, 5),
        gain=round(gain, 5), channels=1, sample_rate=RATE))

    for entry in manifest:
        print(json.dumps(entry))
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (args.output.parent / 'RUNTIME_SHA256SUMS').write_text(''.join(
        f"{m['runtime_sha256']}  runtime/{m['file']}\n" for m in manifest))


if __name__ == '__main__':
    main()
