#!/usr/bin/env python3
"""Cook the original 1931 Montrose Regent Eight body and PSX paint atlas."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from PIL import Image

import make_halcyon_six_assets as primitives


Mesh = primitives.Mesh
EMESH_MAGIC = primitives.EMESH_MAGIC
EMESH_VERSION = primitives.EMESH_VERSION

# UV rectangles use OpenGL's bottom-left origin. These regions correspond to
# the straight-on panels in body-reference.png.
GREEN = (0.012, 0.695, 0.335, 0.985)
WINDSHIELD = (0.360, 0.825, 0.990, 0.985)
PINSTRIPE = (0.360, 0.750, 0.990, 0.810)
FRONT_DOOR = (0.850, 0.355, 0.985, 0.730)
REAR_DOOR = (0.675, 0.355, 0.815, 0.730)
REAR_WINDOW = (0.535, 0.825, 0.655, 0.975)
GRILLE = (0.015, 0.365, 0.335, 0.675)
HEADLIGHT = (0.020, 0.235, 0.147, 0.345)
TAIL_LIGHT = (0.020, 0.155, 0.147, 0.220)
RUNNING_BOARD = (0.325, 0.160, 0.670, 0.335)
CHROME = (0.015, 0.012, 0.665, 0.135)
SPARE = (0.700, 0.020, 0.985, 0.335)

ARCH_INNER_RADIUS = 0.59
MODEL_LENGTH = 7.10
FITTED_SCALE = 5.0 / MODEL_LENGTH
FITTED_WHEEL_RADIUS = 0.34375 / FITTED_SCALE

# arch_fender reads these module globals when it emits its shell.
primitives.NAVY = GREEN
primitives.CHROME = CHROME


def side_door(mesh: Mesh, side: float, z0: float, z1: float,
              uv: tuple[float, float, float, float]) -> None:
    x = side * 0.986
    mesh.quad(((x, 1.27, z0), (x, 1.27, z1),
               (x, 2.30, z1), (x, 2.30, z0)), uv,
              (side, 0.0, 0.0))


def build_montrose_regent_eight() -> Mesh:
    mesh = Mesh()

    # Narrow ladder-frame core. The four road-wheel volumes remain empty; all
    # body skin around them belongs to separate bolt-on fender shells.
    mesh.box((-0.88, 0.31, -3.16), (0.88, 0.55, 3.16), RUNNING_BOARD)
    mesh.box((-0.91, 0.91, -2.91), (0.91, 1.36, 0.56), GREEN)

    # A squared luggage trunk and very tall formal cabin make this markedly
    # more severe than the rounded, middle-class Halcyon body.
    mesh.box((-0.97, 0.82, -3.19), (0.97, 1.54, -1.49), GREEN)
    mesh.box((-0.98, 1.25, -1.68), (0.98, 2.42, 0.50), GREEN)
    mesh.box((-1.02, 2.38, -1.73), (1.02, 2.62, 0.55), GREEN)

    # Long narrow hood with only a small taper, never a modern wedge.
    mesh.tapered_box_z(0.47, 2.68, (0.86, 0.96, 1.63),
                       (0.77, 0.92, 1.56), GREEN)
    mesh.tapered_box_z(2.68, 3.30, (0.77, 0.92, 1.56),
                       (0.68, 0.88, 1.49), GREEN)

    # Four completely distinct bulbous fenders plus broad, flat boards.
    for side in (-1.0, 1.0):
        mesh.arch_fender(side, 2.30, 0.68, ARCH_INNER_RADIUS, 0.81, 12)
        mesh.arch_fender(side, -2.05, 0.68, ARCH_INNER_RADIUS, 0.81, 12)
        x0, x1 = ((-1.27, -0.92) if side < 0.0 else (0.92, 1.27))
        mesh.box((x0, 0.39, -1.38), (x1, 0.54, 1.62), RUNNING_BOARD)
        side_door(mesh, side, -1.50, -0.55, REAR_DOOR)
        side_door(mesh, side, -0.47, 0.40, FRONT_DOOR)

        # Fine cream beltline, kept as a flat painted strip.
        x = side * 0.988
        mesh.quad(((x, 1.46, -1.54), (x, 1.46, 0.43),
                   (x, 1.50, 0.43), (x, 1.50, -1.54)), PINSTRIPE,
                  (side, 0.0, 0.0))

    # Flat split windshield and rear glass. The center divider is geometry so
    # it stays readable after the texture is reduced to PS1 resolution.
    mesh.quad(((-0.86, 1.48, 0.511), (-0.04, 1.48, 0.511),
               (-0.04, 2.34, 0.511), (-0.80, 2.34, 0.511)),
              WINDSHIELD, (0.0, 0.0, 1.0))
    mesh.quad(((0.04, 1.48, 0.511), (0.86, 1.48, 0.511),
               (0.80, 2.34, 0.511), (0.04, 2.34, 0.511)),
              WINDSHIELD, (0.0, 0.0, 1.0))
    mesh.box((-0.025, 1.46, 0.512), (0.025, 2.38, 0.542), CHROME)
    mesh.quad(((0.78, 1.51, -1.691), (-0.78, 1.51, -1.691),
               (-0.74, 2.32, -1.691), (0.74, 2.32, -1.691)),
              REAR_WINDOW, (0.0, 0.0, -1.0))

    # Tall grille with modeled frame; the many vertical bars are pixel paint.
    mesh.box((-0.49, 0.61, 3.305), (0.49, 1.58, 3.355), GRILLE)
    mesh.box((-0.54, 0.57, 3.356), (-0.47, 1.62, 3.405), CHROME)
    mesh.box((0.47, 0.57, 3.356), (0.54, 1.62, 3.405), CHROME)
    mesh.box((-0.54, 1.56, 3.356), (0.54, 1.63, 3.405), CHROME)
    mesh.box((-0.54, 0.56, 3.356), (0.54, 0.63, 3.405), CHROME)

    # Large lamps sit on thin external supports ahead of the front fenders.
    for x in (-1.05, 1.05):
        mesh.box((x - 0.055, 0.98, 2.87),
                 (x + 0.055, 1.25, 3.34), CHROME)
        mesh.cylinder_z(x, 1.29, 3.22, 3.45, 0.27, 10, HEADLIGHT)

    # Crossmember lets Apricot's damage-aware lamp overlay follow real body.
    mesh.box((-1.14, 0.69, 3.13), (1.14, 0.97, 3.19), GREEN)
    mesh.box((-1.17, 0.39, 3.37), (1.17, 0.52, 3.55), CHROME)
    mesh.box((-1.15, 0.39, -3.55), (1.15, 0.52, -3.37), CHROME)

    # Tail lamps remain body-mounted details. The circular rear object is the
    # external spare's fitted metal cover, not any of the four road wheels.
    mesh.box((-0.90, 0.72, -3.225), (-0.66, 1.00, -3.195), TAIL_LIGHT)
    mesh.box((0.66, 0.72, -3.225), (0.90, 1.00, -3.195), TAIL_LIGHT)
    mesh.cylinder_z(0.0, 1.25, -3.43, -3.19, 0.52, 12, SPARE)
    mesh.cylinder_z(0.0, 1.25, -3.455, -3.425, 0.18, 10, CHROME)

    return mesh


def cook_texture(reference: Path, output: Path) -> None:
    with Image.open(reference) as source:
        atlas = source.convert("RGBA").resize(
            (256, 256), Image.Resampling.NEAREST
        )
    output.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(output, optimize=True)


def cook_emesh(mesh: Mesh, output: Path) -> None:
    material = b"montrose_regent_eight\0"
    triangle_count = len(mesh.indices) // 3
    if not 600 <= triangle_count <= 1200:
        raise ValueError(f"triangle budget missed: {triangle_count}")
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as cooked:
        cooked.write(struct.pack(
            "<8I", EMESH_MAGIC, EMESH_VERSION, 0, len(mesh.vertices),
            len(mesh.indices), 1, len(material), 0,
        ))
        for vertex in mesh.vertices:
            cooked.write(struct.pack("<12f", *vertex))
        cooked.write(struct.pack(f"<{len(mesh.indices)}I", *mesh.indices))
        cooked.write(struct.pack("<4I", 0, len(mesh.indices), 0, 0))
        cooked.write(material)


def validate(mesh: Mesh) -> None:
    positions = [vertex[:3] for vertex in mesh.vertices]
    mins = [min(point[axis] for point in positions) for axis in range(3)]
    maxs = [max(point[axis] for point in positions) for axis in range(3)]
    if abs(mins[0] + maxs[0]) > 1e-6 or abs(mins[2] + maxs[2]) > 1e-6:
        raise ValueError("body is not centered in XZ")
    if not (mins[1] >= 0.0 and maxs[1] > 2.5):
        raise ValueError("body is not upright and formal")
    if ARCH_INNER_RADIUS - FITTED_WHEEL_RADIUS < 0.07:
        raise ValueError("wheel arch does not clear the shared wheel")
    for index in mesh.indices:
        if index >= len(mesh.vertices):
            raise ValueError("out-of-range mesh index")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--reference", type=Path,
        default=Path(
            "assets/textures/vehicles/montrose_regent_eight/body-reference.png"
        ),
    )
    parser.add_argument(
        "--texture", type=Path,
        default=Path("assets/textures/vehicles/montrose_regent_eight/body.png"),
    )
    parser.add_argument(
        "--mesh", type=Path,
        default=Path("assets/models/vehicles/montrose_regent_eight/body.emesh"),
    )
    args = parser.parse_args()
    mesh = build_montrose_regent_eight()
    validate(mesh)
    cook_texture(args.reference, args.texture)
    cook_emesh(mesh, args.mesh)
    print(
        f"cooked {args.mesh} ({len(mesh.vertices)} vertices, "
        f"{len(mesh.indices) // 3} triangles) and {args.texture}"
    )


    from bake_vehicle_surfaces import bake_if_canonical
    bake_if_canonical("montrose_regent_eight", args.mesh, args.texture)


if __name__ == "__main__":
    main()
