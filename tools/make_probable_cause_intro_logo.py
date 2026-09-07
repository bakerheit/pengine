#!/usr/bin/env python3
"""Build the separate, transparent title layer with the approved real font."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "assets/textures/ui/probable-cause-intro-logo-v2.png"
image = Image.new("RGBA", (1600, 740))
draw = ImageDraw.Draw(image)
font = ImageFont.truetype(str(ROOT / "assets/fonts/Righteous-Regular.ttf"), 310)
for text, top in (("Probable", 24), ("Cause", 354)):
    bounds = draw.textbbox((0, 0), text, font=font)
    position = (16 - bounds[0], top - bounds[1])
    draw.text((position[0] + 5, position[1] + 8), text, font=font,
              fill=(12, 10, 9, 200), stroke_width=3)
    draw.text(position, text, font=font, fill=(240, 232, 209, 255))
image.save(OUTPUT, optimize=True)
print(OUTPUT)
