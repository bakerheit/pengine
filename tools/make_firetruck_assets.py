#!/usr/bin/env python3
"""Cook the original low-poly firetruck body and its PSX paint atlas.

The runtime body deliberately contains no wheels. Apricot attaches the shared
wheel cook at the four authored arch centres, just like the legacy vehicles.
"""

from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw


EMESH_MAGIC = 0x48534D45
EMESH_VERSION = 2

# UV rectangles use OpenGL's bottom-left origin. The generated reference is a
# straight-on texture sheet, so each small modeled detail can sample one clean
# swatch instead of stretching a whole vehicle photograph over the mesh.
RED = (0.015, 0.635, 0.390, 0.985)
CREAM = (0.015, 0.745, 0.390, 0.805)
WINDOW = (0.410, 0.775, 0.795, 0.980)
DIAMOND = (0.365, 0.415, 0.720, 0.625)
GRILLE = (0.015, 0.420, 0.345, 0.615)
LIGHTS = (0.010, 0.215, 0.465, 0.395)
HOSE = (0.720, 0.270, 0.990, 0.415)
DARK = (0.880, 0.015, 0.985, 0.105)
FIRE_SIGN = (0.030, 0.025, 0.375, 0.095)


class Mesh:
    def __init__(self) -> None:
        self.vertices: list[tuple[float, ...]] = []
        self.indices: list[int] = []

    def quad(
        self,
        points: tuple[tuple[float, float, float], ...],
        normal: tuple[float, float, float],
        uv: tuple[float, float, float, float],
    ) -> None:
        start = len(self.vertices)
        u0, v0, u1, v1 = uv
        uvs = ((u0, v0), (u1, v0), (u1, v1), (u0, v1))
        tangent = _normalise(_subtract(points[1], points[0]))
        for point, texcoord in zip(points, uvs):
            self.vertices.append(
                (*point, *normal, *texcoord, *tangent, 1.0)
            )
        self.indices.extend(
            (start, start + 1, start + 2, start, start + 2, start + 3)
        )

    def box(
        self,
        lo: tuple[float, float, float],
        hi: tuple[float, float, float],
        uv: tuple[float, float, float, float] = RED,
    ) -> None:
        x0, y0, z0 = lo
        x1, y1, z1 = hi
        self.quad(((x0, y0, z1), (x1, y0, z1), (x1, y1, z1),
                   (x0, y1, z1)), (0.0, 0.0, 1.0), uv)
        self.quad(((x1, y0, z0), (x0, y0, z0), (x0, y1, z0),
                   (x1, y1, z0)), (0.0, 0.0, -1.0), uv)
        self.quad(((x1, y0, z1), (x1, y0, z0), (x1, y1, z0),
                   (x1, y1, z1)), (1.0, 0.0, 0.0), uv)
        self.quad(((x0, y0, z0), (x0, y0, z1), (x0, y1, z1),
                   (x0, y1, z0)), (-1.0, 0.0, 0.0), uv)
        self.quad(((x0, y1, z1), (x1, y1, z1), (x1, y1, z0),
                   (x0, y1, z0)), (0.0, 1.0, 0.0), uv)
        self.quad(((x0, y0, z0), (x1, y0, z0), (x1, y0, z1),
                   (x0, y0, z1)), (0.0, -1.0, 0.0), uv)

    def cylinder_x(
        self,
        x0: float,
        x1: float,
        y: float,
        z: float,
        radius: float,
        sides: int,
        uv: tuple[float, float, float, float],
    ) -> None:
        u0, v0, u1, v1 = uv
        for i in range(sides):
            a0 = 2.0 * math.pi * i / sides
            a1 = 2.0 * math.pi * (i + 1) / sides
            p0 = (x0, y + math.cos(a0) * radius, z + math.sin(a0) * radius)
            p1 = (x1, y + math.cos(a0) * radius, z + math.sin(a0) * radius)
            p2 = (x1, y + math.cos(a1) * radius, z + math.sin(a1) * radius)
            p3 = (x0, y + math.cos(a1) * radius, z + math.sin(a1) * radius)
            mid = (a0 + a1) * 0.5
            face_uv = (
                u0 + (u1 - u0) * i / sides,
                v0,
                u0 + (u1 - u0) * (i + 1) / sides,
                v1,
            )
            self.quad((p0, p1, p2, p3),
                      (0.0, math.cos(mid), math.sin(mid)), face_uv)


def _subtract(a: tuple[float, ...], b: tuple[float, ...]) -> tuple[float, ...]:
    return tuple(x - y for x, y in zip(a, b))


def _normalise(v: tuple[float, ...]) -> tuple[float, ...]:
    length = math.sqrt(sum(value * value for value in v))
    return tuple(value / length for value in v)


def build_firetruck() -> Mesh:
    mesh = Mesh()

    # Low chassis, cab-over front, and raised rear apparatus body.
    mesh.box((-1.25, 0.34, -3.20), (1.25, 0.60, 3.20), DARK)
    mesh.box((-1.18, 0.58, 0.45), (1.18, 1.42, 3.02), RED)
    mesh.box((-1.11, 1.40, 0.62), (1.11, 2.42, 2.82), RED)
    mesh.box((-1.20, 0.95, -2.92), (1.20, 2.34, 0.65), RED)
    mesh.box((-1.22, 2.30, -2.98), (1.22, 2.48, 0.74), DIAMOND)

    # Cab glass, grille, bumper, and lights sit slightly proud of the panels.
    mesh.box((-0.96, 1.52, 2.821), (0.96, 2.25, 2.855), WINDOW)
    mesh.box((-0.82, 0.68, 3.021), (0.82, 1.18, 3.075), GRILLE)
    mesh.box((-1.17, 0.42, 3.04), (1.17, 0.62, 3.18), DIAMOND)
    mesh.box((-1.03, 0.78, 3.076), (-0.55, 1.03, 3.11), LIGHTS)
    mesh.box((0.55, 0.78, 3.076), (1.03, 1.03, 3.11), LIGHTS)

    # Broad reflective stripe and readable cab-door markings on both sides.
    for side in (-1.0, 1.0):
        x0, x1 = ((-1.232, -1.205) if side < 0 else (1.205, 1.232))
        mesh.box((x0, 1.17, -2.84), (x1, 1.38, 2.72), CREAM)
        mesh.box((x0 - 0.006, 1.48, 1.04),
                 (x1 + 0.006, 1.82, 2.27), FIRE_SIGN)

    # Three diamond-plate lockers per side, with chunky black seams.
    for side in (-1.0, 1.0):
        x0, x1 = ((-1.242, -1.218) if side < 0 else (1.218, 1.242))
        for z0, z1 in ((-2.73, -1.72), (-1.62, -0.61), (-0.51, 0.50)):
            mesh.box((x0, 1.42, z0), (x1, 2.16, z1), DIAMOND)
        mesh.box((x0 - 0.004, 1.47, -2.46),
                 (x1 + 0.004, 2.03, -1.92), HOSE)

    # Rear chevrons/equipment, tail lamps, and hose couplings.
    mesh.box((-1.05, 0.72, -3.02), (1.05, 1.28, -2.965), CREAM)
    mesh.box((-1.04, 1.36, -3.025), (1.04, 2.17, -2.965), DIAMOND)
    mesh.box((-1.08, 0.69, -3.04), (-0.62, 0.96, -3.00), LIGHTS)
    mesh.box((0.62, 0.69, -3.04), (1.08, 0.96, -3.00), LIGHTS)
    mesh.cylinder_x(-0.63, -0.50, 1.72, -3.02, 0.15, 8, HOSE)
    mesh.cylinder_x(0.50, 0.63, 1.72, -3.02, 0.15, 8, HOSE)

    # PSX-simple roof ladder: two rails and nine squared-off rungs.
    mesh.box((-0.70, 2.53, -2.72), (-0.58, 2.65, 1.55), DIAMOND)
    mesh.box((0.58, 2.53, -2.72), (0.70, 2.65, 1.55), DIAMOND)
    for rung in range(10):
        z = -2.60 + rung * 0.44
        mesh.box((-0.70, 2.55, z), (0.70, 2.68, z + 0.08), DIAMOND)

    # Low-poly emergency bar and roof beacons.
    mesh.box((-0.86, 2.45, 1.52), (0.86, 2.54, 1.82), DARK)
    mesh.box((-0.78, 2.52, 1.55), (-0.08, 2.68, 1.79), LIGHTS)
    mesh.box((0.08, 2.52, 1.55), (0.78, 2.68, 1.79), LIGHTS)
    return mesh


PIXEL_FONT = {
    "F": ("11111", "10000", "11110", "10000", "10000", "10000", "10000"),
    "I": ("11111", "00100", "00100", "00100", "00100", "00100", "11111"),
    "R": ("11110", "10001", "10001", "11110", "10100", "10010", "10001"),
    "E": ("11111", "10000", "11110", "10000", "10000", "10000", "11111"),
}


def draw_pixel_word(draw: ImageDraw.ImageDraw, word: str, x: int, y: int,
                    scale: int, colour: tuple[int, int, int, int]) -> None:
    cursor = x
    for character in word:
        glyph = PIXEL_FONT[character]
        for row, bits in enumerate(glyph):
            for column, bit in enumerate(bits):
                if bit == "1":
                    draw.rectangle(
                        (cursor + column * scale, y + row * scale,
                         cursor + (column + 1) * scale - 1,
                         y + (row + 1) * scale - 1),
                        fill=colour,
                    )
        cursor += 6 * scale


def cook_texture(reference: Path, output: Path) -> None:
    with Image.open(reference) as source:
        atlas = source.convert("RGBA").resize((256, 256), Image.Resampling.NEAREST)
    draw = ImageDraw.Draw(atlas)
    draw.rectangle((7, 231, 97, 251), fill=(113, 12, 14, 255))
    draw.rectangle((8, 232, 96, 250), outline=(35, 8, 10, 255), width=2)
    draw_pixel_word(draw, "FIRE", 17, 235, 2, (241, 216, 164, 255))
    output.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(output, optimize=True)


def cook_emesh(mesh: Mesh, output: Path) -> None:
    material = b"firetruck\0"
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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--reference",
        type=Path,
        default=Path("assets/textures/vehicles/firetruck/body-reference.png"),
    )
    parser.add_argument(
        "--texture",
        type=Path,
        default=Path("assets/textures/vehicles/firetruck/body.png"),
    )
    parser.add_argument(
        "--mesh",
        type=Path,
        default=Path("assets/models/vehicles/firetruck/body.emesh"),
    )
    args = parser.parse_args()
    cook_texture(args.reference, args.texture)
    mesh = build_firetruck()
    cook_emesh(mesh, args.mesh)
    print(
        f"cooked {args.mesh} ({len(mesh.vertices)} vertices, "
        f"{len(mesh.indices)} indices) and {args.texture}"
    )


    from bake_vehicle_surfaces import bake_if_canonical
    bake_if_canonical("firetruck", args.mesh, args.texture)


if __name__ == "__main__":
    main()
