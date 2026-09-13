#!/usr/bin/env python3
"""Check exposed headlight lenses through the production renderer.

Run after building apricot_asset_lab. Captures each side independently, checks
off/left/right separation, closed pop-ups, and deformed-lens temporal stability.
Source meshes and textures are read-only. Inspect fleet.png and damage.png too:
nonzero pixels alone do not prove that an outline matches the painted lens.
"""
import argparse
import json
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/headlight-fit-qa"
MODELS = ("car5", "car8", "ambulance", "halcyon_six",
          "montrose_regent_eight", "firetruck", "glm_lunge", "glm_zip",
          "harrow_workman", "alder_wayfarer", "vesper_vx91",
          "spagatti_shu")


def capture(model, lamp=-1, yaw=180, frames=3, damage=False, power=1.0, instancing=True):
    stem = "body_surface" if (ROOT / f"assets/models/vehicles/{model}/body_surface.emesh").exists() else "body"
    name = f"{model}-lamp{lamp}-yaw{yaw}-frames{frames}-damage{int(damage)}-power{power}"
    if not instancing:
        name += "-unbatched"
    path = OUTPUT / (name + ".png")
    command = [str(ROOT / "build/bin/apricot_asset_lab"),
               "--model", f"models/vehicles/{model}/{stem}.emesh",
               "--texture", f"textures/vehicles/{model}/{stem}.png",
               "--yaw", str(yaw), "--zoom", ".55", "--frames", str(frames),
               "--screenshot", str(path)]
    if lamp >= 0:
        command += ["--lamp-preview", str(lamp), "--lamp-power", str(power)]
    if not instancing:
        command += ["--no-instancing"]
    if damage:
        command += ["--damage-zone", "5" if lamp >= 2 else "2", "--damage-contact", ".7", ".35", ".95" if lamp >= 2 else "-.95",
                    "--damage-strength", ".7"]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                            check=True, timeout=60)
    (OUTPUT / (name + ".log")).write_text(result.stdout + result.stderr)
    assert "0 GL errors" in result.stdout, name
    return np.asarray(Image.open(path).convert("RGB"))


def thumbnail(pixels):
    image = Image.fromarray(pixels)
    w, h = image.size
    return image.crop((int(w*.286), int(h*.358), int(w*.802), int(h*.809))).resize((400,260))


def main():
    global OUTPUT
    parser = argparse.ArgumentParser()
    parser.add_argument('--brakes', action='store_true', help='rear red lenses and running/braking levels')
    brakes = parser.parse_args().brakes
    if brakes:
        OUTPUT = ROOT / 'build/brake-light-fit-qa'
    yaw = 0 if brakes else 180
    lamps = (-1,2,3) if brakes else (-1,0,1)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    sheet = Image.new("RGB", (1200, len(MODELS)*280), (10,12,15))
    draw = ImageDraw.Draw(sheet)
    report = []
    for row, model in enumerate(MODELS):
        images = [capture(model, lamp, yaw) for lamp in lamps]
        masks = [np.max(np.abs(image.astype(int)-images[0].astype(int)),axis=2)>5
                 for image in images[1:]]
        counts = [int(mask.sum()) for mask in masks]
        if model == "vesper_vx91" and not brakes:
            assert counts == [0,0], "closed pop-up covers must not glow"
        else:
            assert min(counts)>20, (model, "missing visible lens", counts)
            assert max(counts)<images[0].shape[0]*images[0].shape[1]*.03, (model, "glow escaped lens")
            assert not np.any(masks[0]&masks[1]), (model, "left/right glow overlaps")
        if brakes:
            running = capture(model,2,yaw,power=.22)
            disabled = capture(model,2,yaw,power=0)
            assert np.array_equal(disabled,images[0]), (model,'off state changes paint')
            assert np.mean(images[1][:,:,0][masks[0]]) > np.mean(running[:,:,0][masks[0]])+10, (model,'brakes not brighter than tail lamps')
        for col, image in enumerate(images):
            sheet.paste(thumbnail(image), (col*400,row*280))
            draw.text((col*400+5,row*280+263), f"{model}: {('OFF','LEFT','RIGHT')[col]}", fill="white")
        report.append(dict(model=model, changed_pixels=counts))
        print(model, counts, flush=True)
    sheet.save(OUTPUT / "fleet.png")

    damage_sheet = Image.new("RGB", (1200,840), (10,12,15))
    draw = ImageDraw.Draw(damage_sheet)
    for row, model in enumerate(("car5", "halcyon_six", "glm_zip")):
        images = [capture(model,lamps[2],yaw,3,True), capture(model,lamps[2],yaw,90,True),
                  capture(model,lamps[2],yaw-45,3,True)]
        assert np.array_equal(images[0],images[1]), (model,"damaged lens flicker")
        unbatched = capture(model,lamps[2],yaw,3,True,instancing=False)
        assert np.array_equal(images[0],unbatched), (model,"draw mode changes damaged lens")
        for col, image in enumerate(images):
            damage_sheet.paste(thumbnail(image),(col*400,row*280))
            draw.text((col*400+5,row*280+263),f"{model}: {('frame 3','frame 90','opposite angle')[col]}",fill="white")
    damage_sheet.save(OUTPUT / "damage.png")
    (OUTPUT / "report.json").write_text(json.dumps(dict(
        fleet=report, damage="3 and 90 frame captures and unbatched draws pixel-identical",
        renderer="0 GL errors"),indent=2)+"\n")


if __name__ == "__main__":
    main()
