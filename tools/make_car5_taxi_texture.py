#!/usr/bin/env python3
"""Create the yellow Car 5 taxi paint beside the original body texture."""

from __future__ import annotations

import argparse
import colorsys
from pathlib import Path

try:
    from PIL import Image
except ModuleNotFoundError as error:
    raise SystemExit(
        "Pillow is required: install it with `python3 -m pip install Pillow`"
    ) from error


DEFAULT_SOURCE = Path("assets/textures/vehicles/car5/body.png")
DEFAULT_OUTPUT_NAME = "taxi.png"
TAXI_HUE = 43.0 / 360.0
TAXI_SATURATION = 0.91
TAXI_VALUE = 0.84
SOURCE_RED_VALUE = 120.0 / 255.0


def clamp01(value: float) -> float:
    return max(0.0, min(1.0, value))


def smoothstep(edge0: float, edge1: float, value: float) -> float:
    t = clamp01((value - edge0) / (edge1 - edge0))
    return t * t * (3.0 - 2.0 * t)


def red_paint_weight(r: int, g: int, b: int, a: int) -> float:
    """Return a soft mask for the red body paint, including its shaded pixels."""
    if a == 0:
        return 0.0
    rf, gf, bf = r / 255.0, g / 255.0, b / 255.0
    hue, saturation, _ = colorsys.rgb_to_hsv(rf, gf, bf)
    red_hue_distance = min(hue, 1.0 - hue)
    hue_weight = 1.0 - smoothstep(0.055, 0.13, red_hue_distance)
    saturation_weight = smoothstep(0.20, 0.48, saturation)
    dominance_weight = smoothstep(0.025, 0.16, rf - max(gf, bf))
    return hue_weight * saturation_weight * dominance_weight


def taxi_pixel(pixel: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
    r, g, b, a = pixel
    weight = red_paint_weight(r, g, b, a)
    if weight <= 0.0:
        return pixel

    _, _, source_value = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
    shade = max(source_value / SOURCE_RED_VALUE, 0.0) ** 0.88
    taxi_value = clamp01(TAXI_VALUE * shade)
    tr, tg, tb = colorsys.hsv_to_rgb(TAXI_HUE, TAXI_SATURATION, taxi_value)
    taxi_rgb = (round(tr * 255.0), round(tg * 255.0), round(tb * 255.0))
    return (
        round(r + (taxi_rgb[0] - r) * weight),
        round(g + (taxi_rgb[1] - g) * weight),
        round(b + (taxi_rgb[2] - b) * weight),
        a,
    )


def build_taxi_texture(source_path: Path, output_path: Path) -> int:
    source = Image.open(source_path).convert("RGBA")
    source_pixels = list(source.getdata())
    taxi_pixels = [taxi_pixel(pixel) for pixel in source_pixels]
    changed = sum(before != after for before, after in zip(source_pixels, taxi_pixels))
    if changed < source.width * source.height // 4:
        raise RuntimeError(
            f"paint mask changed only {changed} pixels; refusing a likely bad texture"
        )
    if any(before[3] != after[3] for before, after in zip(source_pixels, taxi_pixels)):
        raise RuntimeError("taxi recolor changed the source alpha channel")

    output = Image.new("RGBA", source.size)
    output.putdata(taxi_pixels)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output.save(output_path, format="PNG", optimize=True, compress_level=9)
    return changed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "source", nargs="?", type=Path, default=DEFAULT_SOURCE,
        help=f"source texture (default: {DEFAULT_SOURCE})",
    )
    parser.add_argument(
        "--output", type=Path,
        help="output path (default: taxi.png beside the source)",
    )
    args = parser.parse_args()
    source_path = args.source
    output_path = args.output or source_path.with_name(DEFAULT_OUTPUT_NAME)
    if source_path.resolve() == output_path.resolve():
        parser.error("output must not overwrite the source texture")

    changed = build_taxi_texture(source_path, output_path)
    print(f"wrote {output_path} ({changed} recolored pixels)")


if __name__ == "__main__":
    main()
