#!/usr/bin/env python3
"""Original sign and surface textures for the Ferrone Mast transmitter site.

    python3 tools/make_ferrone_mast_textures.py

Writes tracked PNGs to assets/textures/world/ferrone_mast/. They are ours, not
a supplied pack's, which is why they live in the tracked texture tree while the
cooked mesh that wears them lands in the ignored assets/models/ (see
tools/ferrone_mast_blender.py, which copies them next to the .emesh files it
writes because materials.txt names textures without a directory).

Type is the repo's own Bebas Neue and Roboto, not a system face, so the sheet
renders the same on every machine that runs this script.

The station is fictional: WFRN 97.3, the FM transmitter on Ferrone Hill. The
ASR number is invented and deliberately not in the real registry's format.
"""
from pathlib import Path
import random

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets/textures/world/ferrone_mast'
BEBAS = str(ROOT / 'assets/fonts/BebasNeue-Regular.ttf')
ROBOTO = str(ROOT / 'assets/fonts/Roboto-Variable.ttf')


def font(path, size, weight=None):
    f = ImageFont.truetype(path, size)
    if weight is not None:
        try:
            f.set_variation_by_axes([weight])
        except (OSError, ValueError):
            pass
    return f


def centred(d, y, text, f, fill, width):
    box = d.textbbox((0, 0), text, font=f)
    d.text(((width - (box[2] - box[0])) / 2 - box[0], y - box[1]), text,
           font=f, fill=fill)


def weather(im, seed, amount=900):
    """Sun-faded, rain-streaked: a sign on a hilltop is never pristine."""
    rng = random.Random(seed)
    w, h = im.size
    # Drawn on its own layer and composited: ImageDraw on an RGBA image
    # REPLACES pixels, so a faint streak drawn straight onto the sign punches a
    # near-transparent hole through it instead of darkening it.
    grime = Image.new('RGBA', im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(grime)
    for _ in range(amount):
        x, y = rng.randrange(w), rng.randrange(h)
        a = rng.randrange(8, 26)
        d.line((x, y, x + rng.randrange(-1, 2), y + rng.randrange(4, 22)),
               fill=(70, 60, 50, a), width=1)
    return Image.alpha_composite(im, grime)


def station_sign():
    w, h = 1024, 320
    im = Image.new('RGBA', (w, h), (22, 34, 58, 255))
    d = ImageDraw.Draw(im)
    d.rectangle((6, 6, w - 7, h - 7), outline=(214, 196, 150), width=5)
    # The mast drawn as a little lattice glyph beside the call sign.
    x0, base, top = 110, 262, 44
    for i in range(7):
        y0 = base - (base - top) * i / 7
        y1 = base - (base - top) * (i + 1) / 7
        c = (222, 92, 30) if i % 2 == 0 else (238, 236, 228)
        s0 = 46 * (1 - i / 7) + 8
        s1 = 46 * (1 - (i + 1) / 7) + 8
        d.polygon([(x0 - s0, y0), (x0 + s0, y0), (x0 + s1, y1), (x0 - s1, y1)],
                  outline=c)
        d.line((x0 - s0, y0, x0 + s1, y1), fill=c, width=3)
        d.line((x0 + s0, y0, x0 - s1, y1), fill=c, width=3)
    d.ellipse((x0 - 9, top - 22, x0 + 9, top - 4), fill=(236, 40, 30))
    d.text((210, 26), 'WFRN', font=font(BEBAS, 170), fill=(244, 238, 222))
    d.text((560, 38), '97.3', font=font(BEBAS, 150), fill=(236, 138, 52))
    d.text((838, 70), 'FM', font=font(BEBAS, 84), fill=(236, 138, 52))
    d.text((214, 212), 'FERRONE MAST  TRANSMITTER SITE',
           font=font(BEBAS, 62), fill=(214, 196, 150))
    return weather(im, 973)


def danger_sign():
    w, h = 512, 384
    im = Image.new('RGBA', (w, h), (246, 244, 238, 255))
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, w - 1, 118), fill=(18, 18, 18))
    d.ellipse((34, 14, w - 34, 106), fill=(206, 24, 30))
    d.ellipse((34, 14, w - 34, 106), outline=(246, 244, 238), width=4)
    centred(d, 26, 'DANGER', font(BEBAS, 82), (246, 244, 238), w)
    centred(d, 140, 'NO TRESPASSING', font(BEBAS, 78), (18, 18, 18), w)
    centred(d, 232, 'AUTHORIZED PERSONNEL ONLY', font(ROBOTO, 30, 700),
            (18, 18, 18), w)
    centred(d, 282, 'HIGH VOLTAGE  ·  CLIMBING PROHIBITED',
            font(ROBOTO, 24, 500), (150, 20, 24), w)
    centred(d, 326, 'VIOLATORS WILL BE PROSECUTED', font(ROBOTO, 20, 500),
            (60, 60, 60), w)
    d.rectangle((0, 0, w - 1, h - 1), outline=(18, 18, 18), width=6)
    return weather(im, 11)


def rf_caution():
    w, h = 512, 384
    im = Image.new('RGBA', (w, h), (250, 248, 240, 255))
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, w - 1, 110), fill=(250, 204, 18))
    centred(d, 18, 'CAUTION', font(BEBAS, 86), (18, 18, 18), w)
    # Radiating-wave pictogram: a dot and three arcs each side.
    cx, cy = 96, 232
    d.polygon([(cx, cy - 6), (cx - 26, cy + 90), (cx + 26, cy + 90)],
              fill=(18, 18, 18))
    d.ellipse((cx - 11, cy - 20, cx + 11, cy + 2), fill=(18, 18, 18))
    for r in (30, 48, 66):
        d.arc((cx - r, cy - 9 - r, cx + r, cy - 9 + r), 205, 335,
              fill=(18, 18, 18), width=7)
    y = 132
    for line in ('RADIO FREQUENCY FIELDS', 'BEYOND THIS POINT MAY',
                 'EXCEED THE PUBLIC', 'EXPOSURE LIMIT'):
        d.text((176, y), line, font=font(ROBOTO, 23, 700), fill=(18, 18, 18))
        y += 40
    d.text((176, 312), 'OBEY ALL POSTED SIGNS', font=font(ROBOTO, 21, 500),
           fill=(80, 80, 80))
    d.rectangle((0, 0, w - 1, h - 1), outline=(18, 18, 18), width=6)
    return weather(im, 23)


def asr_plate():
    w, h = 512, 192
    im = Image.new('RGBA', (w, h), (26, 86, 52, 255))
    d = ImageDraw.Draw(im)
    d.rectangle((5, 5, w - 6, h - 6), outline=(236, 236, 226), width=4)
    centred(d, 22, 'ANTENNA STRUCTURE', font(BEBAS, 58), (236, 236, 226), w)
    centred(d, 84, 'REGISTRATION NO.', font(BEBAS, 36), (236, 236, 226), w)
    centred(d, 122, 'OH-FM 0973-044', font(BEBAS, 54), (250, 214, 90), w)
    return weather(im, 37, 400)


def precast_concrete():
    """Tileable precast panel: aggregate speckle, form-tie holes, a V-joint."""
    w = h = 512
    rng = random.Random(5)
    im = Image.new('RGB', (w, h), (168, 164, 154))
    px = im.load()
    for y in range(h):
        for x in range(w):
            n = rng.randrange(-9, 10)
            px[x, y] = (168 + n, 164 + n, 154 + n)
    d = ImageDraw.Draw(im)
    for _ in range(2600):
        x, y = rng.randrange(w), rng.randrange(h)
        g = rng.choice((120, 132, 190, 198))
        d.point((x, y), fill=(g, g - 3, g - 8))
    im = im.filter(ImageFilter.GaussianBlur(0.6))
    d = ImageDraw.Draw(im)
    # One vertical V-joint per tile, soft shadow edge, so a wall reads as
    # stacked 1.2 m panels when the cooker maps a tile to 1.2 m.
    d.rectangle((0, 0, 3, h), fill=(112, 108, 100))
    d.rectangle((4, 0, 6, h), fill=(186, 182, 172))
    for x in (128, 384):
        for y in (96, 256, 416):
            d.ellipse((x - 5, y - 5, x + 5, y + 5), fill=(118, 114, 106))
            d.ellipse((x - 3, y - 3, x + 3, y + 3), fill=(92, 88, 82))
    # Rain staining from the drip edge.
    for _ in range(60):
        x = rng.randrange(w)
        d.line((x, 0, x + rng.randrange(-2, 3), rng.randrange(40, 180)),
               fill=(138, 134, 124), width=rng.randrange(1, 4))
    return im.convert('RGBA')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, fn in (('station-sign', station_sign),
                     ('danger-sign', danger_sign),
                     ('rf-caution', rf_caution),
                     ('asr-plate', asr_plate),
                     ('precast-concrete', precast_concrete)):
        fn().save(OUT / f'{name}.png', optimize=True)
        print('wrote', (OUT / f'{name}.png').relative_to(ROOT))


if __name__ == '__main__':
    main()
