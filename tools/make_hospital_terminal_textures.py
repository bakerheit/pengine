#!/usr/bin/env python3
"""Build deterministic 1991-style hospital prop terminal screen textures."""

from __future__ import annotations

import argparse
import hashlib
import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "assets/textures/world/hospital/interior_detail"
SCREEN_SIZE = (640, 480)
SEEDS = {"records-screen": 9211, "vitals-screen": 9212, "scale-screen": 9213}

FONT_CANDIDATES = {
    "bold": (
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    ),
    "regular": (
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ),
    "mono": (
        "/System/Library/Fonts/Supplemental/Courier New.ttf",
        "/System/Library/Fonts/Andale Mono.ttf",
        "/Library/Fonts/Courier New.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    ),
}

CRT_BG = (7, 19, 18)
CRT_GREEN = (121, 201, 150)
CRT_BRIGHT = (179, 221, 185)
CRT_DIM = (58, 112, 83)
CRT_AMBER = (212, 174, 99)


def find_font(style: str, size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    for candidate in FONT_CANDIDATES[style]:
        if Path(candidate).exists():
            return ImageFont.truetype(candidate, size)
    return ImageFont.load_default()


def center_text(draw: ImageDraw.ImageDraw, x: int, y: int, text: str,
                font: ImageFont.ImageFont, fill: tuple[int, int, int]) -> None:
    box = draw.textbbox((0, 0), text, font=font)
    draw.text((x - (box[2] - box[0]) / 2, y), text, font=font, fill=fill)


def draw_crt_frame(draw: ImageDraw.ImageDraw, title: str, seed: int) -> None:
    width, height = SCREEN_SIZE
    rng = random.Random(seed)
    draw.rectangle((12, 12, width - 13, height - 13), outline=CRT_DIM, width=2)
    draw.rectangle((20, 20, width - 21, 68), fill=(13, 34, 29))
    draw.text((34, 32), "PINATTY REGIONAL", font=find_font("bold", 18), fill=CRT_GREEN)
    draw.text((465, 34), "14 AUG 1991", font=find_font("mono", 15), fill=CRT_AMBER)
    draw.text((34, 82), title, font=find_font("bold", 26), fill=CRT_BRIGHT)
    draw.line((34, 119, width - 35, 119), fill=CRT_DIM, width=1)
    # A few steady phosphor flecks give the black glass a faint analog texture.
    for _ in range(120):
        x, y = rng.randrange(25, width - 24), rng.randrange(23, height - 23)
        draw.point((x, y), fill=(10, 28 + rng.randrange(0, 9), 23 + rng.randrange(0, 8)))


def finish_crt(image: Image.Image, seed: int) -> Image.Image:
    """Add restrained, deterministic scanlines and a little signal grain."""
    pixels = image.load()
    width, height = image.size
    for y in range(2, height, 4):
        for x in range(width):
            r, g, b = pixels[x, y]
            pixels[x, y] = (int(r * 0.91), int(g * 0.92), int(b * 0.91))
    rng = random.Random(seed + 100)
    for _ in range(150):
        x, y = rng.randrange(22, width - 22), rng.randrange(20, height - 20)
        r, g, b = pixels[x, y]
        pixels[x, y] = (r, min(255, g + 7), min(255, b + 3))
    return image


def make_records_screen() -> Image.Image:
    image = Image.new("RGB", SCREEN_SIZE, CRT_BG)
    draw = ImageDraw.Draw(image)
    seed = SEEDS["records-screen"]
    draw_crt_frame(draw, "CENTRAL REGISTRATION", seed)
    draw.text((37, 128), "ADMISSIONS TERMINAL  /  DESK 02",
              font=find_font("mono", 16), fill=CRT_GREEN)

    # Large, period-appropriate menu lettering stays clear on the small prop.
    menu_box = (37, 165, 602, 374)
    draw.rectangle(menu_box, outline=CRT_DIM, width=2)
    draw.rectangle((43, 171, 596, 211), fill=(20, 56, 41))
    draw.text((58, 180), "01  NEW REGISTRATION", font=find_font("mono", 21), fill=CRT_BRIGHT)
    draw.text((58, 222), "02  WARD CENSUS", font=find_font("mono", 21), fill=CRT_GREEN)
    draw.text((58, 264), "03  TRANSFER LOG", font=find_font("mono", 21), fill=CRT_GREEN)
    draw.text((58, 306), "04  DISCHARGE INDEX", font=find_font("mono", 21), fill=CRT_GREEN)
    draw.line((52, 350, 585, 350), fill=CRT_DIM, width=1)
    draw.text((58, 354), "SELECT A FUNCTION", font=find_font("bold", 16), fill=CRT_AMBER)

    draw.text((38, 394), "NO ACTIVE FILE OPEN", font=find_font("bold", 16), fill=CRT_BRIGHT)
    draw.text((38, 424), "F1  HELP     ENTER  SELECT     ESC  EXIT",
              font=find_font("mono", 14), fill=CRT_GREEN)
    draw.text((436, 394), "SYSTEM READY", font=find_font("bold", 15), fill=CRT_AMBER)
    return finish_crt(image, seed)


def make_vitals_screen() -> Image.Image:
    image = Image.new("RGB", SCREEN_SIZE, CRT_BG)
    draw = ImageDraw.Draw(image)
    seed = SEEDS["vitals-screen"]
    draw_crt_frame(draw, "BEDSIDE MONITOR", seed)
    draw.text((38, 128), "ECG  /  CONTINUOUS",
              font=find_font("mono", 16), fill=CRT_AMBER)

    # Quiet grid, one illustrative green trace, and no live-patient information.
    trace_box = (37, 165, 435, 382)
    draw.rectangle(trace_box, outline=CRT_DIM, width=2)
    for x in range(57, 426, 46):
        draw.line((x, 177, x, 369), fill=(22, 51, 39), width=1)
    for y in range(189, 371, 36):
        draw.line((49, y, 423, y), fill=(22, 51, 39), width=1)
    draw.text((53, 174), "ECG  /  LEAD II", font=find_font("mono", 14), fill=CRT_DIM)

    points: list[tuple[int, int]] = []
    x = 52
    baseline = 285
    # Four broad, repeatable monitor beats, drawn as a simple prop graphic.
    for _ in range(4):
        points.extend(((x, baseline), (x + 12, baseline), (x + 19, baseline - 5),
                       (x + 25, baseline + 5), (x + 33, baseline),
                       (x + 39, baseline), (x + 46, baseline + 12),
                       (x + 52, baseline - 78), (x + 58, baseline + 30),
                       (x + 64, baseline), (x + 73, baseline),
                       (x + 83, baseline - 17), (x + 92, baseline)))
        x += 94
    draw.line(points, fill=CRT_GREEN, width=3, joint="curve")
    draw.text((54, 344), "SAMPLE TRACE  /  NOT LIVE", font=find_font("mono", 14), fill=CRT_DIM)

    draw.rectangle((454, 165, 602, 382), outline=CRT_DIM, width=2)
    draw.text((470, 184), "HEART RATE", font=find_font("bold", 15), fill=CRT_GREEN)
    draw.text((470, 220), "72", font=find_font("bold", 64), fill=CRT_BRIGHT)
    draw.text((473, 293), "BPM", font=find_font("mono", 20), fill=CRT_GREEN)
    draw.line((471, 329, 585, 329), fill=CRT_DIM, width=1)
    draw.text((471, 344), "SIGNAL OK", font=find_font("bold", 14), fill=CRT_AMBER)

    draw.text((38, 411), "LEAD II  /  ALARMS ENABLED",
              font=find_font("mono", 15), fill=CRT_GREEN)
    return finish_crt(image, seed)


def draw_segment_digit(draw: ImageDraw.ImageDraw, x: int, y: int,
                       digit: str, color: tuple[int, int, int]) -> None:
    """Draw one clean seven-segment LCD digit, using coordinates at 1x scale."""
    width, height, thick = 78, 136, 9
    mid = y + height // 2
    segments = {
        "a": ((x + thick, y), (x + width - thick, y + thick)),
        "g": ((x + thick, mid - thick // 2), (x + width - thick, mid + thick // 2)),
        "d": ((x + thick, y + height - thick), (x + width - thick, y + height)),
        "f": ((x, y + thick), (x + thick, mid - thick)),
        "b": ((x + width - thick, y + thick), (x + width, mid - thick)),
        "e": ((x, mid + thick), (x + thick, y + height - thick)),
        "c": ((x + width - thick, mid + thick), (x + width, y + height - thick)),
    }
    lit = {"0": "abcdef", "1": "bc", "2": "abdeg", "3": "abcdg",
           "4": "bcfg", "5": "acdfg", "6": "acdefg", "7": "abc",
           "8": "abcdefg", "9": "abcdfg"}[digit]
    for name in lit:
        draw.rounded_rectangle(segments[name], radius=3, fill=color)


def make_scale_screen() -> Image.Image:
    image = Image.new("RGB", SCREEN_SIZE, (177, 180, 168))
    draw = ImageDraw.Draw(image)
    seed = SEEDS["scale-screen"]
    draw.rectangle((14, 14, 625, 465), fill=(195, 197, 185), outline=(84, 91, 82), width=3)
    draw.rectangle((24, 24, 615, 456), outline=(222, 222, 208), width=2)
    draw.text((48, 43), "PINATTY REGIONAL  /  PHARMACY",
              font=find_font("bold", 21), fill=(44, 53, 45))
    draw.text((49, 78), "COMPOUNDING BENCH SCALE", font=find_font("mono", 17), fill=(69, 77, 68))

    # A simple fixed LCD readout, with no implied measurement beyond this prop.
    draw.rounded_rectangle((47, 119, 593, 319), radius=8,
                           fill=(124, 144, 113), outline=(67, 77, 64), width=4)
    draw.rectangle((59, 131, 581, 307), outline=(164, 178, 151), width=2)
    inactive = (106, 126, 99)
    active = (31, 48, 33)
    for position, digit in zip((108, 212, 316), "000"):
        draw_segment_digit(draw, position, 148, digit, active)
    draw.ellipse((194, 270, 207, 283), fill=active)
    draw.text((432, 200), "g", font=find_font("bold", 61), fill=active)
    draw.text((73, 139), "LCD", font=find_font("mono", 13), fill=inactive)

    # Lower controls/status are decorative legends baked into this static face.
    draw.rectangle((47, 342, 593, 424), fill=(173, 176, 164), outline=(102, 108, 99), width=2)
    draw.rounded_rectangle((67, 359, 190, 405), radius=6, fill=(136, 160, 125), outline=(61, 74, 58), width=2)
    draw.text((85, 371), "READY", font=find_font("bold", 20), fill=(35, 55, 36))
    draw.text((219, 366), "TARE  0.00 g", font=find_font("mono", 20), fill=(43, 52, 43))
    draw.text((219, 393), "ZERO / TARE", font=find_font("mono", 13), fill=(77, 84, 75))
    draw.text((415, 437), "SCALE CALIBRATED",
              font=find_font("mono", 13), fill=(74, 80, 72))

    # Fixed pixel noise makes the LCD glass feel embedded without obscuring it.
    rng = random.Random(seed)
    for _ in range(65):
        x, y = rng.randrange(62, 578), rng.randrange(134, 303)
        draw.point((x, y), fill=(rng.randrange(119, 130), rng.randrange(140, 150),
                                 rng.randrange(108, 119)))
    return image


BUILDERS = {
    "records-screen": make_records_screen,
    "vitals-screen": make_vitals_screen,
    "scale-screen": make_scale_screen,
}


def build_assets(output_dir: Path, contact_sheet: Path | None = None) -> dict[str, Image.Image]:
    output_dir.mkdir(parents=True, exist_ok=True)
    images = {name: builder() for name, builder in BUILDERS.items()}
    for name, image in images.items():
        path = output_dir / f"{name}.png"
        image.save(path, format="PNG", optimize=False, compress_level=9)
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        print(f"{path}: {image.width}x{image.height} RGB sha256={digest}")
    if contact_sheet:
        save_contact_sheet(images, contact_sheet)
    return images


def save_contact_sheet(images: dict[str, Image.Image], path: Path) -> None:
    thumb_size = (320, 240)
    gap, label_height = 18, 34
    columns = len(images)
    sheet = Image.new("RGB", (columns * thumb_size[0] + (columns + 1) * gap,
                               thumb_size[1] + label_height + 2 * gap), (235, 233, 221))
    draw = ImageDraw.Draw(sheet)
    for index, (name, image) in enumerate(images.items()):
        x, y = gap + index * (thumb_size[0] + gap), gap
        preview = image.resize(thumb_size, Image.Resampling.LANCZOS)
        sheet.paste(preview, (x, y))
        draw.text((x + 2, y + thumb_size[1] + 6), f"{name}  640x480",
                  font=find_font("bold", 15), fill=(38, 52, 46))
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path, format="PNG", optimize=False, compress_level=9)
    print(f"contact sheet: {path}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--contact-sheet", type=Path)
    args = parser.parse_args()
    build_assets(args.output_dir, args.contact_sheet)


if __name__ == "__main__":
    main()
