#!/usr/bin/env python3
"""Cook Car 5-NEXT PATROL: the Car 5-NEXT shell in police livery, with a lightbar.

Two things separate it from car5_next, and both are made here:

  a livery   Car 5's paint repainted black-and-white. The atlas is not labelled,
             so the livery is decided in MODEL space and looked up per texel:
             every texel is mapped back to the position and normal of the
             triangle that owns it, and the doors are recognised by where they
             are on the car rather than by where they sit in the atlas. The
             recipe therefore survives a re-cook of the body.

  a lightbar Bodywork Car 5 does not have, welded onto both the closed shell
             and the articulated one. It has to be on the body rather than a
             separate part, because the emergency glow pass redraws the body
             mesh and lets the lit shader discard everything outside the lens
             box -- see vehicle_headlight_profiles.inc and lit.frag.

ONE CONSTRAINT SHAPES THE WHOLE LIVERY. Car 5's flanks, bonnet, roof and boot
each map BOTH halves of the car onto one chart, so every texel is painted onto
a left-hand panel and its right-hand mirror at once. Anything asymmetric --
lettering above all -- comes out reversed on one side of the car. The livery is
built from what mirrors cleanly: a two-tone split and a five-pointed star.

The atlas is written at 256, twice Car 5's own, because the door star needs the
texels. The upscale is NEAREST, so no pixel of the original is blended; the
extra resolution only carries what this script draws.

Read-only inputs. Run from anywhere:
  python3 tools/make_car5_next_police_assets.py
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

import car5_next_police_spec as police
import make_car5_next_assets as cook

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "assets/models/vehicles/car5_next_police"
PAINT = ROOT / "assets/textures/vehicles/car5_next_police/body.png"
SOURCE_PAINT = ROOT / "assets/textures/vehicles/car5/body.png"

WHITE = np.array([232.0, 234.0, 236.0])
BLACK = np.array([26.0, 27.0, 30.0])
GOLD = (214, 176, 66)
BADGE_OUTLINE = (40, 40, 46)

# The white panel is the two doors, front fender shut line to rear door shut
# line. Both are Car 5's own painted seams -- the same ones the door cut in
# car5_next_spec.py uses.
DOOR_FRONT_Z = 1.33
DOOR_REAR_Z = -1.43
DOOR_SILL_Y = 0.55
DOOR_BELT_Y = 1.46


# --------------------------------------------------------------------------
# livery


def uv_lookup(triangles, size):
    """Per texel: the model position and face normal of the triangle that owns it."""
    position = np.full((size, size, 3), np.nan)
    normal = np.zeros((size, size, 3))
    for tri in triangles:
        face = np.cross(tri[1, :3] - tri[0, :3], tri[2, :3] - tri[0, :3])
        length = np.linalg.norm(face)
        if length < 1e-12:
            continue
        face = face / length
        sx, sy = tri[:, 6] * (size - 1), (1 - tri[:, 7]) * (size - 1)
        x0 = int(max(0, np.floor(sx.min()) - 1))
        x1 = int(min(size - 1, np.ceil(sx.max()) + 1))
        y0 = int(max(0, np.floor(sy.min()) - 1))
        y1 = int(min(size - 1, np.ceil(sy.max()) + 1))
        if x1 < x0 or y1 < y0:
            continue
        den = (sy[1] - sy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy[0] - sy[2])
        if abs(den) < 1e-9:
            continue
        gy, gx = np.mgrid[y0:y1 + 1, x0:x1 + 1]
        l1 = ((sy[1] - sy[2]) * (gx - sx[2]) + (sx[2] - sx[1]) * (gy - sy[2])) / den
        l2 = ((sy[2] - sy[0]) * (gx - sx[2]) + (sx[0] - sx[2]) * (gy - sy[2])) / den
        l3 = 1 - l1 - l2
        # A one-texel bleed keeps the seam between charts from staying red.
        covered = (l1 >= -0.06) & (l2 >= -0.06) & (l3 >= -0.06)
        if not covered.any():
            continue
        point = (l1[..., None] * tri[0, :3] + l2[..., None] * tri[1, :3] +
                 l3[..., None] * tri[2, :3])
        window = position[y0:y1 + 1, x0:x1 + 1]
        fresh = covered & np.isnan(window[:, :, 0])
        window[fresh] = point[fresh]
        normal[y0:y1 + 1, x0:x1 + 1][fresh] = face
    return position, normal


def red_paint(pixels):
    """Soft mask over Car 5's red bodywork, including its shaded and lit texels.

    Deliberately narrower than "anything reddish": the atlas also carries amber
    indicators and red tail lenses, which are brighter and more saturated than
    any painted panel and must survive into the police car unchanged.
    """
    rgb = pixels[:, :, :3].astype(np.float64) / 255.0
    top, bottom = rgb.max(2), rgb.min(2)
    chroma = top - bottom
    reddest = (rgb[:, :, 0] >= top - 1e-6) & (chroma > 0.10)
    toward_green = (rgb[:, :, 1] - bottom) / np.maximum(chroma, 1e-6)
    return reddest & (toward_green < 0.45) & (top > 0.12) & (top < 0.86)


def panel_colour(position, normal):
    """White doors on a black car: the period black-and-white."""
    y, z = position[:, :, 1], position[:, :, 2]
    flank = np.abs(normal[:, :, 0]) > 0.4
    doors = (flank & (z < DOOR_FRONT_Z) & (z > DOOR_REAR_Z) &
             (y > DOOR_SILL_Y) & (y < DOOR_BELT_Y))
    out = np.broadcast_to(BLACK, position.shape).copy()
    out[doors] = WHITE
    return out, doors


def star_badge(draw, cx, cy, radius):
    """A five-pointed star in a disc. Symmetric, so it survives the mirror."""
    draw.ellipse((cx - radius, cy - radius, cx + radius, cy + radius),
                 fill=BADGE_OUTLINE)
    draw.ellipse((cx - radius + 1, cy - radius + 1, cx + radius - 1, cy + radius - 1),
                 fill=GOLD)
    points = []
    for i in range(10):
        angle = -np.pi / 2 + i * np.pi / 5
        reach = (radius - 2) if i % 2 == 0 else (radius - 2) * 0.42
        points.append((cx + reach * np.cos(angle), cy + reach * np.sin(angle)))
    draw.polygon(points, fill=BADGE_OUTLINE)


def paint_livery(triangles, size):
    position, normal = uv_lookup(triangles, size)
    source = Image.open(SOURCE_PAINT).convert("RGBA").resize((size, size), Image.NEAREST)
    pixels = np.asarray(source).astype(np.float64)

    painted = red_paint(pixels) & ~np.isnan(position[:, :, 0])
    target, doors = panel_colour(np.nan_to_num(position), normal)

    # Keep the original panel shading. Car 5's red carries its low-poly light
    # bake, so relight the new colour by the same relative value rather than
    # flooding each panel flat and losing every crease.
    value = pixels[:, :, :3].max(2) / 255.0
    reference = np.where(doors, 0.52, 0.58)
    scale = np.clip(value / np.maximum(reference, 1e-6), 0.55, 1.35)[:, :, None]
    out = pixels.copy()
    out[painted, :3] = np.clip(target * scale, 0, 255)[painted]

    image = Image.fromarray(out.astype(np.uint8)).convert("RGBA")
    draw = ImageDraw.Draw(image)

    badge = (painted & (np.abs(normal[:, :, 0]) > 0.5) &
             (position[:, :, 1] > 0.80) & (position[:, :, 1] < 1.30) &
             (position[:, :, 2] > -0.10) & (position[:, :, 2] < 1.10))
    ys, xs = np.where(badge)
    if not len(xs):
        raise ValueError("no front-door texels found for the badge")
    star_badge(draw, (xs.min() + xs.max()) * 0.5, (ys.min() + ys.max()) * 0.5,
               max(6.0, min(xs.max() - xs.min(), ys.max() - ys.min()) * 0.50))

    for rect, colour in police.SWATCHES.values():
        draw.rectangle((rect[0], rect[1], rect[2] - 1, rect[3] - 1), fill=colour + (255,))
    return image, {"repainted_texels": int(painted.sum()),
                   "white_texels": int((painted & doors).sum()),
                   "badge_texels": int(badge.sum())}


# --------------------------------------------------------------------------
# lightbar


def lightbar():
    triangles = []
    for name, x0, x1, y0, y1, z0, z1, swatch in police.lightbar_boxes():
        triangles += cook.block(x0, x1, y0, y1, z0, z1, police.swatch_uv(swatch))
    return triangles


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--paint-only", action="store_true",
                        help="rewrite the atlas without re-cooking the meshes")
    arguments = parser.parse_args()

    vertices, indices, _ = cook.read_emesh(cook.SOURCE / "body.emesh")
    image, paint_report = paint_livery(vertices[indices], police.ATLAS_SIZE)
    PAINT.parent.mkdir(parents=True, exist_ok=True)
    image.save(PAINT)

    report = {"paint": paint_report, "lightbar": {}}
    if not arguments.paint_only:
        bar = lightbar()
        report.update(cook.build(target=MODEL, extra_body=bar, texture=PAINT))
        report["lightbar"] = {"triangles": len(bar),
                              "boxes": [row[0] for row in police.lightbar_boxes()],
                              "lens_boxes": police.lens_boxes(),
                              "centres": police.lamp_centres()}
    (ROOT / "build").mkdir(exist_ok=True)
    (ROOT / "build/car5-next-police-cook.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"paint": report["paint"], "lightbar": report["lightbar"],
                      "triangles": report.get("triangles")}, indent=2))


if __name__ == "__main__":
    main()
