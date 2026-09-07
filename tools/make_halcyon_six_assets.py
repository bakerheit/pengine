#!/usr/bin/env python3
"""Cook the original 1936 Halcyon Six Sedan body and PSX paint atlas."""

from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw


EMESH_MAGIC = 0x48534D45
EMESH_VERSION = 2
ARCH_INNER_RADIUS = 0.53
FITTED_WHEEL_RADIUS = 0.34375 / (5.0 / 6.60)

# UV rectangles use OpenGL's bottom-left origin. The generated reference is a
# straight-on material sheet; two exact pixel tiles are added at cook time for
# the dashboard-only windshield and the model's three-bar grille.
NAVY = (0.015, 0.690, 0.290, 0.985)
CREAM = (0.310, 0.690, 0.650, 0.985)
FRONT_DOOR = (0.468, 0.414, 0.652, 0.670)
REAR_DOOR = (0.274, 0.414, 0.458, 0.670)
REAR_WINDOW = (0.675, 0.648, 0.815, 0.845)
HEADLIGHT = (0.805, 0.522, 0.930, 0.625)
TAIL_LIGHT = (0.812, 0.285, 0.975, 0.390)
RUNNING_BOARD = (0.015, 0.125, 0.495, 0.195)
CHROME = (0.935, 0.405, 0.990, 0.635)
SPARE_DECAL = (0.770, 0.055, 0.970, 0.275)
CUSTOM_GRILLE = (4.0 / 256.0, 3.0 / 256.0,
                  45.0 / 256.0, 35.0 / 256.0)
CUSTOM_WINDSHIELD = (50.0 / 256.0, 3.0 / 256.0,
                      105.0 / 256.0, 31.0 / 256.0)


def subtract(a: tuple[float, ...], b: tuple[float, ...]) -> tuple[float, ...]:
    return tuple(x - y for x, y in zip(a, b))


def cross(a: tuple[float, float, float],
          b: tuple[float, float, float]) -> tuple[float, float, float]:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def dot(a: tuple[float, ...], b: tuple[float, ...]) -> float:
    return sum(x * y for x, y in zip(a, b))


def normalise(v: tuple[float, ...]) -> tuple[float, ...]:
    length = math.sqrt(dot(v, v))
    if length <= 1e-9:
        raise ValueError("zero-length vector")
    return tuple(value / length for value in v)


class Mesh:
    def __init__(self) -> None:
        self.vertices: list[tuple[float, ...]] = []
        self.indices: list[int] = []

    def triangle(
        self,
        points: tuple[tuple[float, float, float], ...],
        uvs: tuple[tuple[float, float], ...],
        normal: tuple[float, float, float] | None = None,
    ) -> None:
        if normal is None:
            normal = normalise(cross(subtract(points[1], points[0]),
                                     subtract(points[2], points[0])))
        tangent = normalise(subtract(points[1], points[0]))
        start = len(self.vertices)
        for point, uv in zip(points, uvs):
            self.vertices.append((*point, *normal, *uv, *tangent, 1.0))
        self.indices.extend((start, start + 1, start + 2))

    def quad(
        self,
        points: tuple[tuple[float, float, float], ...],
        uv: tuple[float, float, float, float],
        desired_normal: tuple[float, float, float] | None = None,
    ) -> None:
        points_list = list(points)
        face_normal = normalise(cross(subtract(points_list[1], points_list[0]),
                                      subtract(points_list[2], points_list[0])))
        if desired_normal is not None and dot(face_normal, desired_normal) < 0.0:
            points_list = [points_list[0], points_list[3],
                           points_list[2], points_list[1]]
            face_normal = tuple(-value for value in face_normal)
        u0, v0, u1, v1 = uv
        quad_uvs = ((u0, v0), (u1, v0), (u1, v1), (u0, v1))
        start = len(self.vertices)
        tangent = normalise(subtract(points_list[1], points_list[0]))
        for point, texcoord in zip(points_list, quad_uvs):
            self.vertices.append(
                (*point, *face_normal, *texcoord, *tangent, 1.0)
            )
        self.indices.extend(
            (start, start + 1, start + 2, start, start + 2, start + 3)
        )

    def box(
        self,
        lo: tuple[float, float, float],
        hi: tuple[float, float, float],
        uv: tuple[float, float, float, float] = NAVY,
    ) -> None:
        x0, y0, z0 = lo
        x1, y1, z1 = hi
        self.quad(((x0, y0, z1), (x1, y0, z1), (x1, y1, z1),
                   (x0, y1, z1)), uv, (0.0, 0.0, 1.0))
        self.quad(((x1, y0, z0), (x0, y0, z0), (x0, y1, z0),
                   (x1, y1, z0)), uv, (0.0, 0.0, -1.0))
        self.quad(((x1, y0, z1), (x1, y0, z0), (x1, y1, z0),
                   (x1, y1, z1)), uv, (1.0, 0.0, 0.0))
        self.quad(((x0, y0, z0), (x0, y0, z1), (x0, y1, z1),
                   (x0, y1, z0)), uv, (-1.0, 0.0, 0.0))
        self.quad(((x0, y1, z1), (x1, y1, z1), (x1, y1, z0),
                   (x0, y1, z0)), uv, (0.0, 1.0, 0.0))
        self.quad(((x0, y0, z0), (x1, y0, z0), (x1, y0, z1),
                   (x0, y0, z1)), uv, (0.0, -1.0, 0.0))

    def tapered_box_z(
        self,
        z0: float,
        z1: float,
        back: tuple[float, float, float],
        front: tuple[float, float, float],
        uv: tuple[float, float, float, float] = NAVY,
    ) -> None:
        back_half_x, back_y0, back_y1 = back
        front_half_x, front_y0, front_y1 = front
        a = (-back_half_x, back_y0, z0)
        b = (back_half_x, back_y0, z0)
        c = (back_half_x, back_y1, z0)
        d = (-back_half_x, back_y1, z0)
        e = (-front_half_x, front_y0, z1)
        f = (front_half_x, front_y0, z1)
        g = (front_half_x, front_y1, z1)
        h = (-front_half_x, front_y1, z1)
        self.quad((e, f, g, h), uv, (0.0, 0.0, 1.0))
        self.quad((b, a, d, c), uv, (0.0, 0.0, -1.0))
        self.quad((f, b, c, g), uv, (1.0, 0.0, 0.0))
        self.quad((a, e, h, d), uv, (-1.0, 0.0, 0.0))
        self.quad((h, g, c, d), uv, (0.0, 1.0, 0.0))
        self.quad((a, b, f, e), uv, (0.0, -1.0, 0.0))

    def extrude_xy(
        self,
        section: tuple[tuple[float, float], ...],
        z0: float,
        z1: float,
        uv: tuple[float, float, float, float],
    ) -> None:
        count = len(section)
        for i in range(count):
            j = (i + 1) % count
            x0, y0 = section[i]
            x1, y1 = section[j]
            desired = normalise((y1 - y0, -(x1 - x0), 0.0))
            self.quad(((x0, y0, z0), (x1, y1, z0),
                       (x1, y1, z1), (x0, y0, z1)), uv, desired)
        centre_x = sum(point[0] for point in section) / count
        centre_y = sum(point[1] for point in section) / count
        u0, v0, u1, v1 = uv
        centre_uv = ((u0 + u1) * 0.5, (v0 + v1) * 0.5)
        for front, z, normal in ((True, z1, (0.0, 0.0, 1.0)),
                                 (False, z0, (0.0, 0.0, -1.0))):
            for i in range(count):
                j = (i + 1) % count
                if not front:
                    i, j = j, i
                p0 = (centre_x, centre_y, z)
                p1 = (*section[i], z)
                p2 = (*section[j], z)
                point_uvs = (
                    centre_uv,
                    (u0 + (section[i][0] - centre_x) * 0.1,
                     v0 + (section[i][1] - centre_y) * 0.1),
                    (u0 + (section[j][0] - centre_x) * 0.1,
                     v0 + (section[j][1] - centre_y) * 0.1),
                )
                self.triangle((p0, p1, p2), point_uvs, normal)

    def arch_fender(
        self,
        side: float,
        wheel_z: float,
        wheel_y: float = 0.68,
        inner_radius: float = ARCH_INNER_RADIUS,
        outer_radius: float = 0.73,
        segments: int = 10,
    ) -> None:
        x_inner = side * 0.91
        x_outer = side * 1.25
        for index in range(segments):
            a0 = math.pi * index / segments
            a1 = math.pi * (index + 1) / segments

            def ring_point(x: float, radius: float, angle: float):
                return (x, wheel_y + math.sin(angle) * radius,
                        wheel_z + math.cos(angle) * radius)

            outer0_i = ring_point(x_inner, outer_radius, a0)
            outer1_i = ring_point(x_inner, outer_radius, a1)
            inner0_i = ring_point(x_inner, inner_radius, a0)
            inner1_i = ring_point(x_inner, inner_radius, a1)
            outer0_o = ring_point(x_outer, outer_radius, a0)
            outer1_o = ring_point(x_outer, outer_radius, a1)
            inner0_o = ring_point(x_outer, inner_radius, a0)
            inner1_o = ring_point(x_outer, inner_radius, a1)
            mid = (a0 + a1) * 0.5
            radial = (0.0, math.sin(mid), math.cos(mid))
            self.quad((outer0_o, outer1_o, inner1_o, inner0_o), NAVY,
                      (side, 0.0, 0.0))
            self.quad((inner0_i, inner1_i, outer1_i, outer0_i), NAVY,
                      (-side, 0.0, 0.0))
            self.quad((outer0_i, outer1_i, outer1_o, outer0_o), NAVY, radial)
            self.quad((inner0_o, inner1_o, inner1_i, inner0_i), CHROME,
                      tuple(-value for value in radial))
        for angle, tangent_z in ((0.0, 1.0), (math.pi, -1.0)):
            outer_i = (x_inner,
                       wheel_y + math.sin(angle) * outer_radius,
                       wheel_z + math.cos(angle) * outer_radius)
            outer_o = (x_outer, outer_i[1], outer_i[2])
            inner_o = (x_outer, wheel_y + math.sin(angle) * inner_radius,
                       wheel_z + math.cos(angle) * inner_radius)
            inner_i = (x_inner, inner_o[1], inner_o[2])
            self.quad((outer_i, outer_o, inner_o, inner_i), NAVY,
                      (0.0, 0.0, tangent_z))

    def cylinder_z(
        self,
        x: float,
        y: float,
        z0: float,
        z1: float,
        radius: float,
        sides: int,
        face_uv: tuple[float, float, float, float],
    ) -> None:
        u0, v0, u1, v1 = face_uv
        centre_uv = ((u0 + u1) * 0.5, (v0 + v1) * 0.5)
        for index in range(sides):
            a0 = 2.0 * math.pi * index / sides
            a1 = 2.0 * math.pi * (index + 1) / sides
            p0 = (x + math.cos(a0) * radius,
                  y + math.sin(a0) * radius, z0)
            p1 = (x + math.cos(a1) * radius,
                  y + math.sin(a1) * radius, z0)
            p2 = (p1[0], p1[1], z1)
            p3 = (p0[0], p0[1], z1)
            mid = (a0 + a1) * 0.5
            self.quad((p0, p1, p2, p3), CHROME,
                      (math.cos(mid), math.sin(mid), 0.0))
            rim0_uv = (centre_uv[0] + math.cos(a0) * (u1 - u0) * 0.5,
                       centre_uv[1] + math.sin(a0) * (v1 - v0) * 0.5)
            rim1_uv = (centre_uv[0] + math.cos(a1) * (u1 - u0) * 0.5,
                       centre_uv[1] + math.sin(a1) * (v1 - v0) * 0.5)
            self.triangle(((x, y, z1), p3, p2),
                          (centre_uv, rim0_uv, rim1_uv), (0.0, 0.0, 1.0))
            self.triangle(((x, y, z0), p1, p0),
                          (centre_uv, rim1_uv, rim0_uv), (0.0, 0.0, -1.0))


def side_panel(mesh: Mesh, side: float, z0: float, z1: float,
               uv: tuple[float, float, float, float]) -> None:
    x = side * 0.991
    mesh.quad(((x, 1.18, z0), (x, 1.18, z1),
               (x, 2.04, z1), (x, 2.04, z0)), uv,
              (side, 0.0, 0.0))


def build_halcyon_six() -> Mesh:
    mesh = Mesh()
    # Narrow core and high belt leave the fender arches genuinely open for the
    # shared wheels while retaining the heavy body-on-frame silhouette.
    mesh.box((-0.87, 0.34, -3.08), (0.87, 0.58, 3.08), RUNNING_BOARD)
    mesh.box((-0.96, 0.90, -2.72), (0.96, 1.38, 0.62), NAVY)
    mesh.box((-0.91, 0.82, -2.82), (0.91, 1.31, -1.34), NAVY)
    mesh.box((-0.96, 1.28, -1.52), (0.96, 2.08, 0.60), NAVY)

    # Long tapered hood: two coarse sections sell the 1930s profile without
    # smoothing it into a modern aerodynamic wedge.
    mesh.tapered_box_z(0.48, 2.55, (0.88, 0.96, 1.58),
                       (0.77, 0.91, 1.45), NAVY)
    mesh.tapered_box_z(2.55, 3.14, (0.77, 0.91, 1.45),
                       (0.68, 0.86, 1.34), NAVY)

    # Five-facet cream roof: chunky enough for PS1, round enough to read as a
    # stamped 1930s steel roof rather than a square van cabin.
    roof_section = (
        (-0.94, 2.02), (0.94, 2.02), (0.84, 2.25),
        (0.58, 2.43), (-0.58, 2.43), (-0.84, 2.25),
    )
    mesh.extrude_xy(roof_section, -1.57, 0.43, CREAM)

    # Four independent arch-shaped fenders. They are part of the one cooked
    # mesh, but never blended into the central body shell.
    for side in (-1.0, 1.0):
        mesh.arch_fender(side, 2.18)
        mesh.arch_fender(side, -1.86)
        x0, x1 = ((-1.19, -0.94) if side < 0.0 else (0.94, 1.19))
        mesh.box((x0, 0.40, -1.25), (x1, 0.51, 1.55), RUNNING_BOARD)
        side_panel(mesh, side, -1.36, -0.43, REAR_DOOR)
        side_panel(mesh, side, -0.36, 0.50, FRONT_DOOR)

    # Front/rear glass. The windshield samples an exact dashboard-only tile;
    # there is no steering-wheel geometry or painted wheel silhouette.
    mesh.quad(((-0.79, 1.51, 0.611), (0.79, 1.51, 0.611),
               (0.72, 2.02, 0.611), (-0.72, 2.02, 0.611)),
              CUSTOM_WINDSHIELD, (0.0, 0.0, 1.0))
    mesh.quad(((0.68, 1.53, -1.531), (-0.68, 1.53, -1.531),
               (-0.62, 2.01, -1.531), (0.62, 2.01, -1.531)),
              REAR_WINDOW, (0.0, 0.0, -1.0))

    # Three vertical grille bars, period round lamps, and dull bumpers.
    mesh.box((-0.43, 0.67, 3.145), (0.43, 1.32, 3.175), CUSTOM_GRILLE)
    for x in (-0.27, 0.0, 0.27):
        mesh.box((x - 0.025, 0.69, 3.176),
                 (x + 0.025, 1.30, 3.205), CHROME)
    mesh.cylinder_z(-0.91, 1.10, 2.91, 3.08, 0.22, 10, HEADLIGHT)
    mesh.cylinder_z(0.91, 1.10, 2.91, 3.08, 0.22, 10, HEADLIGHT)
    # Narrow period crossmember behind the lamps also gives Apricot's generic
    # damage-aware headlight overlays real body skin to follow.
    mesh.box((-1.03, 0.68, 3.075), (1.03, 0.94, 3.115), NAVY)
    mesh.box((-1.05, 0.42, 3.15), (1.05, 0.56, 3.30), CHROME)
    mesh.box((-1.02, 0.42, -3.30), (1.02, 0.56, -3.12), CHROME)

    # Rear lamps and one flat spare-wheel suggestion. The spare is paint only:
    # a paper-thin quad on the trunk face, never tire geometry.
    mesh.box((-0.85, 0.70, -2.835), (-0.62, 0.96, -2.805), TAIL_LIGHT)
    mesh.box((0.62, 0.70, -2.835), (0.85, 0.96, -2.805), TAIL_LIGHT)
    mesh.quad(((0.49, 0.83, -2.836), (-0.49, 0.83, -2.836),
               (-0.49, 1.62, -2.836), (0.49, 1.62, -2.836)),
              SPARE_DECAL, (0.0, 0.0, -1.0))
    return mesh


def cook_texture(reference: Path, output: Path) -> None:
    with Image.open(reference) as source:
        atlas = source.convert("RGBA").resize((256, 256), Image.Resampling.NEAREST)
    draw = ImageDraw.Draw(atlas)

    # Bottom-left grille tile: exactly three dull vertical bars.
    draw.rectangle((4, 221, 45, 253), fill=(16, 18, 20, 255))
    draw.rectangle((4, 221, 45, 253), outline=(128, 122, 110, 255), width=2)
    for x in (13, 24, 35):
        draw.rectangle((x, 225, x + 3, 249), fill=(151, 145, 132, 255))
        draw.line((x + 1, 225, x + 1, 249), fill=(205, 198, 180, 255))

    # Adjacent windshield tile: dark glass and dashboard silhouette only.
    draw.rectangle((50, 225, 105, 253), fill=(103, 98, 88, 255))
    draw.rectangle((52, 227, 103, 251), fill=(7, 18, 31, 255))
    draw.polygon(((52, 247), (65, 242), (93, 242), (103, 247),
                  (103, 251), (52, 251)), fill=(5, 8, 12, 255))
    draw.rectangle((54, 229, 58, 241), fill=(18, 36, 53, 255))
    draw.rectangle((97, 229, 101, 241), fill=(18, 36, 53, 255))

    output.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(output, optimize=True)


def cook_emesh(mesh: Mesh, output: Path) -> None:
    material = b"halcyon_six\0"
    triangle_count = len(mesh.indices) // 3
    if not 500 <= triangle_count <= 1200:
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
    if not (mins[1] >= 0.0 and maxs[1] > 2.0):
        raise ValueError("body is not upright")
    if ARCH_INNER_RADIUS - FITTED_WHEEL_RADIUS < 0.07:
        raise ValueError("wheel arch does not clear the shared wheel")
    for index in mesh.indices:
        if index >= len(mesh.vertices):
            raise ValueError("out-of-range mesh index")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--reference", type=Path,
        default=Path("assets/textures/vehicles/halcyon_six/body-reference.png"),
    )
    parser.add_argument(
        "--texture", type=Path,
        default=Path("assets/textures/vehicles/halcyon_six/body.png"),
    )
    parser.add_argument(
        "--mesh", type=Path,
        default=Path("assets/models/vehicles/halcyon_six/body.emesh"),
    )
    args = parser.parse_args()
    mesh = build_halcyon_six()
    validate(mesh)
    cook_texture(args.reference, args.texture)
    cook_emesh(mesh, args.mesh)
    print(
        f"cooked {args.mesh} ({len(mesh.vertices)} vertices, "
        f"{len(mesh.indices) // 3} triangles) and {args.texture}"
    )


    from bake_vehicle_surfaces import bake_if_canonical
    bake_if_canonical("halcyon_six", args.mesh, args.texture)


if __name__ == "__main__":
    main()
