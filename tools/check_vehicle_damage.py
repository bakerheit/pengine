#!/usr/bin/env python3
"""Validate the shared damage atlas and real-renderer locality/stability.

Requires the built apricot_asset_lab. Does not alter the source images.
"""
import json
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/vehicle-damage-qa"


def check_atlas():
    image = Image.open(ROOT / "assets/textures/effects/vehicle-damage-atlas.png")
    assert image.mode == "RGBA" and image.width == image.height * 2
    alpha = np.asarray(image)[:, :, 3]
    cells = []
    for index in range(7):
        x, y = index % 4, index // 4
        tile = alpha[y*image.height//2:(y+1)*image.height//2,
                     x*image.width//4:(x+1)*image.width//4]
        coverage = float(np.mean(tile > 0))
        partial = float(np.mean((tile > 0) & (tile < 255)))
        assert 0.05 < coverage < 0.65, (index, coverage)
        assert partial > 0.04, (index, partial)
        cells.append(dict(variant=index, coverage=coverage, partial_alpha=partial))
    return cells


def render(name, model, yaw, damaged=False, frames=20, strength=0.75):
    output = OUTPUT / (name + ".png")
    command = [str(ROOT / "build/bin/apricot_asset_lab"),
               "--model", f"models/vehicles/{model}/body.emesh",
               "--texture", f"textures/vehicles/{model}/body.png",
               "--yaw", str(yaw), "--frames", str(frames),
               "--screenshot", str(output)]
    if damaged:
        command += ["--damage-zone", "7", "--damage-contact", "-1", ".33", "0",
                    "--damage-strength", str(strength)]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=True)
    (OUTPUT / (name + ".log")).write_text(result.stdout + result.stderr)
    assert "0 GL errors" in result.stdout, result.stdout
    return np.asarray(Image.open(output).convert("RGB"))


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    atlas = check_atlas()
    models = ("harrow_workman", "glr_zip", "montrose_regent_eight")
    results = []
    for model in models:
        clean = render(model + "-clean", model, 67.22)
        damaged = render(model + "-damaged", model, 67.22, True)
        changed = np.any(clean != damaged, axis=2)
        h, w = changed.shape
        body_roi = changed[int(h*.40):int(h*.62), int(w*.30):int(w*.70)]
        assert np.count_nonzero(body_roi) > 100, "damage not visible"
        assert np.mean(body_roi) < .45, "damage spread across most of the body view"
        # Repair clears both records: identical paint, no baked-in stain.
        repaired = render(model + "-repaired", model, 67.22, True, strength=0)
        assert np.array_equal(clean, repaired), "repair changed clean paint"
        results.append(dict(model=model, changed_body_view_fraction=float(np.mean(body_roi))))
    stable = render("harrow_workman-stable", models[0], 67.22, True, frames=90)
    original = np.asarray(Image.open(OUTPUT / "harrow_workman-damaged.png").convert("RGB"))
    assert np.array_equal(stable, original), "damage moved or flickered between frames"
    back_clean = render("opposite-clean", models[0], 247.22)
    back_damaged = render("opposite-damaged", models[0], 247.22, True)
    h, w, _ = back_clean.shape
    # Center of the opposite door retains its paint. Allow one 8-bit level
    # for raster/lighting rounding on unchanged surfaces in a deformed draw.
    region = np.s_[int(h*.51):int(h*.545), int(w*.465):int(w*.505)]
    opposite_delta = np.abs(back_clean[region].astype(int) - back_damaged[region].astype(int))
    assert opposite_delta.max() <= 1, "damage leaked to opposite door"
    report = dict(atlas=atlas, models=results, repair="pixel-identical to clean",
                  stability="20 and 90 frame captures pixel-identical",
                  opposite_door_max_channel_delta=int(opposite_delta.max()),
                  renderer="0 GL errors in every capture")
    (OUTPUT / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
