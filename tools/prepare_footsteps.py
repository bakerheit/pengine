#!/usr/bin/env python3
"""Prepare the recorded per-surface footstep takes the character walks on.

Reads the supplied "40 Free PSX Footsteps" pack and writes 48 kHz mono 16-bit
WAVs under assets/audio/character/runtime/, one per take, named for the
FootstepSurface family the runtime selects by:

    footstep_<family>_<n>.wav

Only the families the collider can actually distinguish are cooked. Metal,
Stairs and Wood stay in the pack: nothing in TerrainCollider::GroundHit reports
a timber floor, a steel walkway or a staircase, so cooking them would ship
assets the runtime has no way to choose. See assets/audio/character/SOURCES.md.

Two decisions worth naming, because both look like details and are not:

*   Normalisation is PER FAMILY, not per file. A per-file normalise flattens
    the takes into five copies of one loudness and the rotation stops reading
    as a rotation; leaving them raw lets gravel land three times louder than
    concrete, so a walk across a kerb sounds like a volume bug. One gain per
    family preserves the variation inside it and makes the families comparable.

*   Silence is trimmed from the head. The recordings carry up to a few tens of
    milliseconds of lead-in, and a footstep is a transient triggered ON the
    footfall: pre-roll turns the whole cadence late, which reads as the
    animation being out of sync rather than as an audio offset.
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

# (family, source subdirectory, source file stem, how many takes the pack has)
# The Metal folder really is spelled "Foodstep" in the pack; it is not cooked,
# and the spelling is recorded here so a later pass does not think it is a typo.
FAMILIES = (
    ("concrete", "Concrete", "Footstep Concrete", 5),
    ("stone", "Stone", "Footstep Stone", 4),
    ("gravel", "Gravel", "Footstep Gravel", 5),
    ("dirt", "Dirt", "Footstep Dirt", 5),
    ("grass", "Grass", "Footstep Grass", 6),
)

# Target RMS over the loudest take in a family. Footsteps sit under the engine,
# the city bed and dialogue, and a step that peaks near full scale reads as a
# stamp rather than a footfall.
TARGET_PEAK = 0.62
TARGET_RMS = 0.085

# Activity gate for trimming, relative to the take's own peak. Low enough to
# keep the leading edge of a soft grass step, high enough to drop room noise.
GATE = 0.02
LEAD_S = 0.004   # kept in front of the first active sample, so the attack is whole
TAIL_S = 0.030   # kept after the last, so a decay is not chopped square
FADE_IN_S = 0.0015
FADE_OUT_S = 0.010


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def decode_mono(path):
    """Decode to mono float at RATE. The sources are stereo recordings of a
    single foot, so downmixing loses nothing the player could localise — the
    runtime plays footsteps non-spatially at the listener anyway."""
    samples = array.array("f", subprocess.check_output([
        "ffmpeg", "-v", "error", "-i", str(path), "-ac", "1",
        "-ar", str(RATE), "-f", "f32le", "pipe:1"]))
    if sys.byteorder != "little":
        samples.byteswap()
    if not samples or not all(math.isfinite(v) for v in samples):
        raise ValueError(f"Invalid audio: {path}")
    return samples


def trim(samples):
    peak = max(abs(v) for v in samples)
    if peak < 0.01:
        raise ValueError("Silent take")
    active = [i for i, v in enumerate(samples) if abs(v) >= peak * GATE]
    start = max(0, active[0] - int(RATE * LEAD_S))
    end = min(len(samples), active[-1] + int(RATE * TAIL_S) + 1)
    return samples[start:end], start / RATE, end / RATE


def write_wav(path, samples, gain):
    pcm = array.array("h")
    fade_in = max(1, int(RATE * FADE_IN_S))
    fade_out = max(1, int(RATE * FADE_OUT_S))
    n = len(samples)
    for i, value in enumerate(samples):
        window = min(1.0, (i + 1) / fade_in, (n - i) / fade_out)
        scaled = max(-1.0, min(1.0, value * gain * window))
        pcm.append(round(scaled * 32767))
    if sys.byteorder != "little":
        pcm.byteswap()
    with wave.open(str(path), "wb") as wav:
        wav.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        wav.writeframes(pcm.tobytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path,
                        help="extracted '40 Free PSX Footsteps' directory")
    parser.add_argument("output", type=Path,
                        help="assets/audio/character/runtime")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    manifest = []
    for family, folder, stem, count in FAMILIES:
        takes = []
        for index in range(1, count + 1):
            source = args.source / folder / f"{stem} {index}.ogg"
            cut, cut_start, cut_end = trim(decode_mono(source))
            takes.append((source, cut, cut_start, cut_end))

        # One gain for the whole family, set by whichever take is loudest so no
        # take in it can clip after the shared scale is applied.
        family_peak = max(max(abs(v) for v in cut) for _, cut, _, _ in takes)
        family_rms = max(
            math.sqrt(sum(v * v for v in cut) / len(cut)) for _, cut, _, _ in takes)
        gain = min(TARGET_PEAK / family_peak, TARGET_RMS / family_rms)

        for index, (source, cut, cut_start, cut_end) in enumerate(takes):
            name = f"footstep_{family}_{index}.wav"
            output = args.output / name
            write_wav(output, cut, gain)
            entry = dict(
                file=name, family=family, variant=index,
                source=str(source.relative_to(args.source)),
                source_sha256=sha(source), runtime_sha256=sha(output),
                family_gain=gain, channels=1, sample_rate=RATE,
                source_cut_s=[round(cut_start, 4), round(cut_end, 4)],
                duration_s=round(len(cut) / RATE, 4))
            manifest.append(entry)
            print(json.dumps(entry))

    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (args.output.parent / "RUNTIME_SHA256SUMS").write_text("".join(
        f"{m['runtime_sha256']}  runtime/{m['file']}\n" for m in manifest))


if __name__ == "__main__":
    main()
