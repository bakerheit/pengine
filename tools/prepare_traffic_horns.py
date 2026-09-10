#!/usr/bin/env python3
"""Prepare the three documented Pixabay traffic horns from local MP3s."""
import argparse
import array
import hashlib
import json
import math
import subprocess
import sys
import wave
from pathlib import Path

TAKES = (
    ("car-horn-02-153260.mp3", "horn_short.wav", "Car Horn 02", 44100,
     "https://pixabay.com/sound-effects/city-car-horn-02-153260/",
     "https://cdn.pixabay.com/audio/2023/06/11/audio_1777c08c36.mp3"),
    ("double-car-honk-352443.mp3", "horn_double.wav", "Double Car Honk", 48000,
     "https://pixabay.com/sound-effects/film-special-effects-double-car-honk-352443/",
     "https://cdn.pixabay.com/audio/2025/05/31/audio_1704fa63e4.mp3"),
    ("truck-horn-153263.mp3", "horn_truck.wav", "Truck Horn", 44100,
     "https://pixabay.com/sound-effects/city-truck-horn-153263/",
     "https://cdn.pixabay.com/audio/2023/06/11/audio_6bc6f187b1.mp3"),
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = []
    for source_name, name, title, rate, page, media in TAKES:
        source = args.source / source_name
        probe = json.loads(subprocess.check_output([
            "ffprobe", "-v", "error", "-show_entries",
            "stream=sample_rate,channels:format=duration", "-of", "json", str(source)]))
        if int(probe["streams"][0]["sample_rate"]) != rate:
            raise ValueError(f"Unexpected source sample rate: {source}")
        samples = array.array("f", subprocess.check_output([
            "ffmpeg", "-v", "error", "-i", str(source), "-ac", "1", "-ar", str(rate),
            "-f", "f32le", "pipe:1"]))
        if sys.byteorder != "little":
            samples.byteswap()
        if not samples or not all(math.isfinite(v) for v in samples):
            raise ValueError(f"Invalid audio: {source}")
        peak = max(abs(v) for v in samples)
        activity = [i for i, v in enumerate(samples) if abs(v) >= peak * 0.025]
        if peak < 0.01 or not activity:
            raise ValueError(f"Silent horn: {source}")
        start = max(0, activity[0] - int(rate * 0.008))
        end = min(len(samples), activity[-1] + int(rate * 0.04) + 1)
        cut = samples[start:end]
        rms = math.sqrt(sum(v * v for v in cut) / len(cut))
        gain = min(0.21 / rms, 0.80 / peak)
        pcm = array.array("h")
        for i, value in enumerate(cut):
            fade = min(1.0, i / max(1, rate * 0.005),
                       (len(cut) - 1 - i) / max(1, rate * 0.025))
            pcm.append(round(max(-1.0, min(1.0, value * gain * fade)) * 32767))
        if sys.byteorder != "little":
            pcm.byteswap()
        output = args.output / name
        with wave.open(str(output), "wb") as wav:
            wav.setparams((1, 2, rate, 0, "NONE", "not compressed"))
            wav.writeframes(pcm.tobytes())
        entry = dict(file=name, title=title, creator="Universfield", source=page, media=media,
                     license="Pixabay Content License", retrieved="2026-09-09",
                     source_sha256=sha(source), runtime_sha256=sha(output),
                     source_probe=probe, source_cut_s=[start / rate, end / rate],
                     gain=gain, channels=1, sample_rate=rate, duration_s=len(cut) / rate)
        manifest.append(entry)
        print(json.dumps(entry))
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (args.output.parent / "RUNTIME_SHA256SUMS").write_text("".join(
        f"{m['runtime_sha256']}  runtime/{m['file']}\n" for m in manifest))


if __name__ == "__main__":
    main()
