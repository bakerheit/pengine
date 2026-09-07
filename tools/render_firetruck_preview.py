#!/usr/bin/env python3
"""Render the cooked firetruck and shared wheels without an OpenGL window."""

from __future__ import annotations

import argparse
import math
import struct
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw


@dataclass
class Part:
    positions: np.ndarray
    normals: np.ndarray
    uvs: np.ndarray
    indices: np.ndarray
    texture: np.ndarray


def read_part(mesh_path: Path, texture_path: Path) -> Part:
    data = mesh_path.read_bytes()
    magic, version, flags, vertex_count, index_count, submeshes, strings, _ = (
        struct.unpack_from("<8I", data, 0)
    )
    if magic != 0x48534D45 or version != 2 or flags != 0 or submeshes < 1:
        raise ValueError(f"unsupported emesh: {mesh_path}")
    vertices = np.frombuffer(
        data, dtype="<f4", count=vertex_count * 12, offset=32
    ).reshape(vertex_count, 12).copy()
    index_offset = 32 + vertex_count * 48
    indices = np.frombuffer(
        data, dtype="<u4", count=index_count, offset=index_offset
    ).reshape(-1, 3).copy()
    texture = np.asarray(Image.open(texture_path).convert("RGB"))
    return Part(vertices[:, 0:3], vertices[:, 3:6], vertices[:, 6:8],
                indices, texture)


def transformed(part: Part, scale: float, translation: tuple[float, ...]) -> Part:
    positions = part.positions * scale + np.asarray(translation, dtype=np.float32)
    return Part(positions, part.normals.copy(), part.uvs.copy(),
                part.indices.copy(), part.texture)


def view_rotation(yaw_degrees: float, pitch_degrees: float) -> np.ndarray:
    yaw = math.radians(yaw_degrees)
    pitch = math.radians(pitch_degrees)
    cy, sy = math.cos(yaw), math.sin(yaw)
    cp, sp = math.cos(pitch), math.sin(pitch)
    yaw_matrix = np.array(
        ((cy, 0.0, sy), (0.0, 1.0, 0.0), (-sy, 0.0, cy)),
        dtype=np.float32,
    )
    pitch_matrix = np.array(
        ((1.0, 0.0, 0.0), (0.0, cp, -sp), (0.0, sp, cp)),
        dtype=np.float32,
    )
    return pitch_matrix @ yaw_matrix


def raster_view(parts: list[Part], yaw: float, pitch: float,
                width: int, height: int) -> Image.Image:
    all_positions = np.concatenate([part.positions for part in parts])
    centre = (all_positions.min(axis=0) + all_positions.max(axis=0)) * 0.5
    rotation = view_rotation(yaw, pitch)
    rotated_parts = [
        ((part.positions - centre) @ rotation.T, part.normals @ rotation.T, part)
        for part in parts
    ]
    projected = np.concatenate([item[0][:, :2] for item in rotated_parts])
    projected[:, 1] *= -1.0
    size = np.maximum(projected.max(axis=0) - projected.min(axis=0), 1e-5)
    scale = min((width - 34) / size[0], (height - 34) / size[1])
    offset = np.array((width * 0.5, height * 0.52), dtype=np.float32)

    image = np.empty((height, width, 3), dtype=np.uint8)
    image[:] = (31, 34, 39)
    for y in range(height):
        image[y, :, :] += 5 if (y // 12) % 2 else 0
    depth_buffer = np.full((height, width), -np.inf, dtype=np.float32)
    light = np.array((-0.35, 0.72, 0.60), dtype=np.float32)
    light /= np.linalg.norm(light)

    for positions, normals, part in rotated_parts:
        screen = positions[:, :2].copy()
        screen[:, 1] *= -1.0
        screen = screen * scale + offset
        for triangle in part.indices:
            points = screen[triangle]
            depths = positions[triangle, 2]
            tri_uvs = part.uvs[triangle]
            x_min = max(0, int(math.floor(points[:, 0].min())))
            x_max = min(width - 1, int(math.ceil(points[:, 0].max())))
            y_min = max(0, int(math.floor(points[:, 1].min())))
            y_max = min(height - 1, int(math.ceil(points[:, 1].max())))
            if x_min > x_max or y_min > y_max:
                continue
            x0, y0 = points[0]
            x1, y1 = points[1]
            x2, y2 = points[2]
            denominator = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
            if abs(denominator) < 1e-6:
                continue
            grid_y, grid_x = np.mgrid[y_min:y_max + 1, x_min:x_max + 1]
            sample_x = grid_x + 0.5
            sample_y = grid_y + 0.5
            w0 = ((y1 - y2) * (sample_x - x2) +
                  (x2 - x1) * (sample_y - y2)) / denominator
            w1 = ((y2 - y0) * (sample_x - x2) +
                  (x0 - x2) * (sample_y - y2)) / denominator
            w2 = 1.0 - w0 - w1
            inside = (w0 >= -1e-5) & (w1 >= -1e-5) & (w2 >= -1e-5)
            depth = w0 * depths[0] + w1 * depths[1] + w2 * depths[2]
            target_depth = depth_buffer[y_min:y_max + 1, x_min:x_max + 1]
            visible = inside & (depth > target_depth)
            if not np.any(visible):
                continue

            uv = (w0[..., None] * tri_uvs[0] +
                  w1[..., None] * tri_uvs[1] +
                  w2[..., None] * tri_uvs[2])
            tex_height, tex_width = part.texture.shape[:2]
            tex_x = np.clip((uv[..., 0] * (tex_width - 1)).astype(int),
                            0, tex_width - 1)
            tex_y = np.clip(((1.0 - uv[..., 1]) * (tex_height - 1)).astype(int),
                            0, tex_height - 1)
            colour = part.texture[tex_y, tex_x].astype(np.float32)
            normal = normals[triangle].mean(axis=0)
            normal_length = np.linalg.norm(normal)
            if normal_length > 1e-6:
                normal /= normal_length
            shade = 0.58 + 0.42 * max(0.0, float(np.dot(normal, light)))
            colour = np.clip(colour * shade, 0, 255).astype(np.uint8)
            target = image[y_min:y_max + 1, x_min:x_max + 1]
            target[visible] = colour[visible]
            target_depth[visible] = depth[visible]

    return Image.fromarray(image)


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--body-mesh", type=Path,
                        default=Path("assets/models/vehicles/firetruck/body.emesh"))
    parser.add_argument("--body-texture", type=Path,
                        default=Path("assets/textures/vehicles/firetruck/body.png"))
    parser.add_argument("--output", type=Path,
                        default=Path("build/firetruck-preview.png"))
    parser.add_argument("--body-length", type=float, default=6.4)
    parser.add_argument("--wheel-x", type=float, default=1.13)
    parser.add_argument("--arch-y", type=float, default=0.72)
    parser.add_argument("--front-wheel-z", type=float, default=2.18)
    parser.add_argument("--rear-wheel-z", type=float, default=-2.12)
    args = parser.parse_args()

    def rooted(path: Path) -> Path:
        return path if path.is_absolute() else root / path

    body = read_part(
        rooted(args.body_mesh), rooted(args.body_texture),
    )
    wheel = read_part(
        root / "assets/models/vehicles/common/wheel.emesh",
        root / "assets/textures/vehicles/common/wheel.png",
    )
    # Match the actual traffic rig: the 6.4-unit body fits 5 m, and the shared
    # wheel cook is resized to the same 0.34375 m visible radius.
    body_scale = 5.0 / args.body_length
    wheel_native_radius = max(
        wheel.positions[:, 1].max() - wheel.positions[:, 1].min(),
        wheel.positions[:, 2].max() - wheel.positions[:, 2].min(),
    ) * 0.5
    wheel_scale = (0.34375 / body_scale) / wheel_native_radius
    parts = [body]
    for x in (-args.wheel_x, args.wheel_x):
        for z in (args.front_wheel_z, args.rear_wheel_z):
            parts.append(transformed(wheel, wheel_scale, (x, args.arch_y, z)))

    views = (
        ("FRONT 3/4", -32.0, 13.0),
        ("SIDE", -90.0, 7.0),
        ("REAR 3/4", -148.0, 13.0),
    )
    panel_width, panel_height = 420, 390
    preview = Image.new("RGB", (panel_width * len(views), panel_height + 44),
                        (19, 21, 25))
    draw = ImageDraw.Draw(preview)
    for index, (label, yaw, pitch) in enumerate(views):
        panel = raster_view(parts, yaw, pitch, panel_width, panel_height)
        preview.paste(panel, (index * panel_width, 0))
        draw.text((index * panel_width + 12, panel_height + 13), label,
                  fill=(225, 216, 194))
    output = rooted(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    preview.save(output)
    print(output)


if __name__ == "__main__":
    main()
