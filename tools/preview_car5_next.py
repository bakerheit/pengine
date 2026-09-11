#!/usr/bin/env python3
"""Render Car 5-NEXT shut and swung, so the door can be judged by eye.

A passing geometry check does not tell you the panel reads as a door. This
sheet does: closed assembly, open from outside, the cabin the opening reveals,
and the doorway from behind.
"""
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from render_firetruck_preview import Part, read_part, raster_view
import car5_next_spec as spec

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "assets/models/vehicles/car5_next"
TEXTURE = ROOT / "assets/textures/vehicles/car5/body.png"


def swung(part, fraction=1.0):
    hinge = np.array(spec.DOOR["hinge"])
    angle = math.radians(spec.DOOR["open_degrees"]) * fraction
    c, s = math.cos(angle), math.sin(angle)
    rotation = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
    return Part((part.positions - hinge) @ rotation.T + hinge,
                part.normals @ rotation.T, part.uvs, part.indices, part.texture)


PANES = ("windshield", "rear_glass", "passenger_glass", "driver_glass",
         "driver_rear_glass", "passenger_rear_glass")


def main():
    body = read_part(MODEL / "body_open.emesh", TEXTURE)
    door = read_part(MODEL / "driver_door.emesh", TEXTURE)
    # The glass is a separate material in game; here it is just drawn as the
    # rest of the body so the openings and the panes can be checked as shapes.
    glass = [read_part(MODEL / (name + ".emesh"), TEXTURE) for name in PANES
             if name != "driver_glass"]
    door_glass = read_part(MODEL / "driver_glass.emesh", TEXTURE)
    shut = [body, door, door_glass, *glass]
    open_ = [body, swung(door), swung(door_glass), *glass]
    half = [body, swung(door, 0.5), swung(door_glass, 0.5), *glass]
    # Driver is +X, so a negative renderer yaw looks at the doorway.
    views = [("CLOSED ASSEMBLY", shut, -32, 18),
             ("DOOR OPEN", open_, -32, 18),
             ("HALF OPEN", half, -62, 10),
             ("CABIN THROUGH THE OPENING", open_, -78, 26),
             ("REAR VIEW, DOOR OPEN", open_, -145, 20),
             ("TOP", open_, -90, 82)]
    sheet = Image.new("RGB", (1200, 1275), (20, 23, 26))
    draw = ImageDraw.Draw(sheet)
    for i, (label, parts, yaw, pitch) in enumerate(views):
        image = raster_view(parts, yaw, pitch, 600, 392)
        x, y = (i % 2) * 600, (i // 2) * 425
        sheet.paste(image, (x, y))
        draw.text((x + 12, y + 402), label, fill=(235, 235, 220))
    output = ROOT / "build/car5-next-door-preview.png"
    output.parent.mkdir(exist_ok=True)
    sheet.save(output)
    print(output)


if __name__ == "__main__":
    main()
