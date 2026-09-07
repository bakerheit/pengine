#!/usr/bin/env python3
"""Stamp the exact Righteous logo onto the generated loading-screen plate."""

from __future__ import annotations

import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
PLATE = ROOT / "assets/textures/ui/probable-cause-loading-plate-v1.png"
FONT = ROOT / "assets/fonts/Righteous-Regular.ttf"
OUTPUT = ROOT / "assets/textures/ui/probable-cause-loading-righteous-v1.png"
TEXT = "Probable Cause"


def fit_font(draw: ImageDraw.ImageDraw, max_width: int) -> ImageFont.FreeTypeFont:
    size = 260
    while size > 80:
        font = ImageFont.truetype(str(FONT), size)
        if draw.textbbox((0, 0), TEXT, font=font, stroke_width=0)[2] <= max_width:
            return font
        size -= 2
    return ImageFont.truetype(str(FONT), 80)


def main() -> None:
    image = Image.open(PLATE).convert("RGBA")
    draw = ImageDraw.Draw(image)
    width, height = image.size
    font = fit_font(draw, int(width * 0.73))
    bbox = draw.textbbox((0, 0), TEXT, font=font, stroke_width=0)
    text_width = bbox[2] - bbox[0]
    text_height = bbox[3] - bbox[1]
    x = (width - text_width) // 2 - bbox[0]
    top_y = int(height * 0.14)
    y = top_y - bbox[1]

    # Offset-print style red registration shadow with a heavy black keyline.
    draw.text(
        (x + 15, y + 18),
        TEXT,
        font=font,
        fill=(151, 25, 20, 255),
        stroke_width=14,
        stroke_fill=(8, 8, 9, 245),
    )
    draw.text(
        (x, y),
        TEXT,
        font=font,
        fill=(229, 207, 157, 255),
        stroke_width=9,
        stroke_fill=(7, 8, 10, 255),
    )

    # Thin red rules give the wordmark a printed case-file / title-card frame.
    rule_y = top_y - 18
    rule_bottom = top_y + text_height + 19
    left = max(80, x - 36)
    right = min(width - 80, x + text_width + 36)
    draw.rectangle((left, rule_y, right, rule_y + 5), fill=(158, 28, 22, 235))
    draw.rectangle((left, rule_bottom, right, rule_bottom + 5), fill=(158, 28, 22, 235))

    # Add restrained, deterministic ink wear inside the cream letter shapes.
    mask = Image.new("L", image.size, 0)
    mask_draw = ImageDraw.Draw(mask)
    mask_draw.text((x, y), TEXT, font=font, fill=255, stroke_width=0)
    wear = Image.new("RGBA", image.size, (0, 0, 0, 0))
    wear_draw = ImageDraw.Draw(wear)
    rng = random.Random(1991)
    for _ in range(420):
        px = rng.randrange(max(0, x), min(width, x + text_width))
        py = rng.randrange(max(0, y), min(height, y + text_height))
        if mask.getpixel((px, py)) > 0:
            length = rng.randrange(2, 13)
            wear_draw.line((px, py, px + length, py), fill=(36, 29, 22, rng.randrange(25, 85)), width=1)
    image = Image.composite(Image.alpha_composite(image, wear), image, mask)
    image.convert("RGB").save(OUTPUT, optimize=True)
    print(f"wrote {OUTPUT} ({width}x{height}) using {FONT}")


if __name__ == "__main__":
    main()
