#!/usr/bin/env python3
"""Deterministic interior textures for the Loom (Pinatty) Museum.

Everything here is drawn from fixed geometry and one fixed-seed LCG, so a
re-run reproduces the shipped PNGs byte for byte. Diffuse albedo only: the
renderer supplies lighting, and a baked highlight in a wall texture fights
the gallery fixtures that are meant to be doing the work.

The wall, floor and ceiling sheets are deliberately near-neutral. Each room
tints them at runtime, which is how one 256px sheet dresses eight galleries.
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets/textures/world/loom_museum'
OUT.mkdir(parents=True, exist_ok=True)

SERIF = '/System/Library/Fonts/Supplemental/Didot.ttc'
SERIF_TEXT = '/System/Library/Fonts/Supplemental/Baskerville.ttc'
SANS = '/System/Library/Fonts/Helvetica.ttc'


def serif(size, bold=False):
    return ImageFont.truetype(SERIF, size, index=1 if bold else 0)


def body(size, italic=False):
    return ImageFont.truetype(SERIF_TEXT, size, index=1 if italic else 0)


def sans(size, bold=False):
    return ImageFont.truetype(SANS, size, index=1 if bold else 0)


class Noise:
    """A tiny fixed-seed LCG. Deterministic across machines and Python builds,
    which `random` only promises for a given implementation."""

    def __init__(self, seed):
        self.state = seed & 0xFFFFFFFFFFFFFFFF

    def next(self):
        self.state = (self.state * 6364136223846793005 + 1442695040888963407) & 0xFFFFFFFFFFFFFFFF
        return (self.state >> 33) / float(1 << 31)

    def spread(self, amount):
        return (self.next() - 0.5) * 2.0 * amount


def centred(draw, box, text, font, fill, dy=0):
    x0, y0, x1, y1 = box
    b = draw.textbbox((0, 0), text, font=font)
    draw.text((x0 + (x1 - x0 - (b[2] - b[0])) / 2 - b[0],
               y0 + (y1 - y0 - (b[3] - b[1])) / 2 - b[1] + dy), text, font=font, fill=fill)


def tracked(draw, x, y, text, font, fill, tracking):
    """Letter-spaced caps. Museum lettering is always tracked out, and doing it
    by hand keeps the look without shipping a second display font."""
    for ch in text:
        draw.text((x, y), ch, font=font, fill=fill)
        x += draw.textlength(ch, font=font) + tracking
    return x


def tracked_width(draw, text, font, tracking):
    return sum(draw.textlength(c, font=font) + tracking for c in text) - tracking


def tracked_centred(draw, cx, y, text, font, fill, tracking):
    tracked(draw, cx - tracked_width(draw, text, font, tracking) / 2, y, text, font, fill, tracking)


def fitted(draw, text, maker, size, tracking, max_width, floor=10):
    """Shrink until the tracked line fits. A cell that overflows bleeds into
    its atlas neighbour, which shows up in game as a letter from the next
    room's sign, so the fit is checked rather than eyeballed per string."""
    while size > floor:
        font = maker(size)
        if tracked_width(draw, text, font, tracking) <= max_width:
            return font, tracking
        if tracking > 1:
            tracking -= 1
            continue
        size -= 1
    return maker(floor), max(tracking, 0)


def fitted_centred(draw, cx, y, text, maker, size, fill, tracking, max_width):
    font, track = fitted(draw, text, maker, size, tracking, max_width)
    tracked_centred(draw, cx, y, text, font, fill, track)


def fitted_line(draw, box, text, maker, size, fill, max_width, floor=9):
    """Plain centred text that shrinks to fit, for the subtitles under a rule."""
    while size > floor and draw.textlength(text, font=maker(size)) > max_width:
        size -= 1
    centred(draw, box, text, maker(size), fill)


# --- Gallery plaster -------------------------------------------------------
# Near-white so a room tint lands where the author asked. The stipple is one
# value wide; anything stronger tiles visibly across a 36 m wall.
def make_wall():
    size = 256
    im = Image.new('RGBA', (size, size), (238, 234, 226, 255))
    px = im.load()
    n = Noise(0x4C4F4F4D57414C4C)
    for y in range(size):
        for x in range(size):
            # Two long, low-frequency bands plus grain. The bands wrap by
            # construction so the sheet stays seamless.
            import math
            drift = (math.sin(x * math.pi * 2 / size) * 1.6 +
                     math.sin(y * math.pi * 4 / size) * 1.1)
            v = drift + n.spread(2.4)
            r, g, b, a = px[x, y]
            px[x, y] = (int(r + v), int(g + v), int(b + v * 0.9), a)
    im = im.filter(ImageFilter.SMOOTH)
    im.save(OUT / 'gallery-plaster.png')


# --- Parquet floor ---------------------------------------------------------
# Square basketweave: a 4x4 grid of 64 px blocks, each four planks laid at
# right angles to its neighbours. It tiles exactly and reads as a museum
# floor at every distance the player can stand at.
def make_floor():
    size, block, plank = 256, 64, 16
    im = Image.new('RGBA', (size, size), (146, 111, 71, 255))
    d = ImageDraw.Draw(im)
    n = Noise(0x50415251554554ff)
    for by in range(size // block):
        for bx in range(size // block):
            horizontal = (bx + by) % 2 == 0
            for i in range(block // plank):
                tone = 18 * n.next() - 6
                fill = (int(150 + tone), int(112 + tone * 0.8), int(70 + tone * 0.6), 255)
                if horizontal:
                    x0, y0 = bx * block, by * block + i * plank
                    d.rectangle((x0, y0, x0 + block - 1, y0 + plank - 1), fill=fill)
                    d.line((x0, y0, x0 + block - 1, y0), fill=(112, 82, 51, 255))
                    for g in range(3):
                        gy = y0 + 4 + g * 4
                        d.line((x0 + int(n.next() * 20), gy, x0 + block - 1, gy),
                               fill=(int(134 + tone), int(100 + tone), int(63 + tone), 255))
                else:
                    x0, y0 = bx * block + i * plank, by * block
                    d.rectangle((x0, y0, x0 + plank - 1, y0 + block - 1), fill=fill)
                    d.line((x0, y0, x0, y0 + block - 1), fill=(112, 82, 51, 255))
                    for g in range(3):
                        gx = x0 + 4 + g * 4
                        d.line((gx, y0 + int(n.next() * 20), gx, y0 + block - 1),
                               fill=(int(134 + tone), int(100 + tone), int(63 + tone), 255))
    # Block seams last so they survive the plank fills.
    for k in range(size // block):
        d.line((k * block, 0, k * block, size - 1), fill=(103, 75, 46, 255))
        d.line((0, k * block, size - 1, k * block), fill=(103, 75, 46, 255))
    im.save(OUT / 'gallery-parquet.png')


# --- Coffered ceiling ------------------------------------------------------
# One coffer per tile. At the 4 m uv scale the museum uses, that is a coffer
# the size of a real one, and the stepped reveal gives the flat slab the
# shading it otherwise has no geometry for.
def make_ceiling():
    size = 256
    im = Image.new('RGBA', (size, size), (226, 221, 209, 255))
    d = ImageDraw.Draw(im)
    steps = [(0, (214, 208, 195)), (10, (231, 226, 214)), (20, (203, 196, 182)),
             (30, (238, 233, 222)), (40, (191, 184, 170))]
    for inset, colour in steps:
        d.rectangle((inset, inset, size - 1 - inset, size - 1 - inset), fill=colour + (255,))
    # Recessed panel with a soft corner shade, then a small centre rosette.
    d.rectangle((52, 52, size - 53, size - 53), fill=(219, 213, 200, 255))
    for i in range(14):
        v = 200 - i * 2
        d.rectangle((52 + i, 52 + i, size - 53 - i, size - 53 - i), outline=(v, v - 6, v - 18, 255))
    d.ellipse((112, 112, 143, 143), fill=(205, 197, 181, 255), outline=(180, 172, 156, 255))
    d.ellipse((121, 121, 134, 134), fill=(224, 218, 205, 255))
    im.save(OUT / 'coffer-ceiling.png')


# --- Portal plaques --------------------------------------------------------
PLAQUES = [
    ('ART', 'European and Loom painting'),
    ('ANTIQUITIES', 'Vessels, stone and inscription'),
    ('NATURAL HISTORY', 'Fossil vertebrates of the crescent'),
    ('PINATTY HISTORY', 'The city in eight decades'),
    ('SCIENCE', 'Motion, force and mechanism'),
    ('SPACE', 'Flight beyond the atmosphere'),
    ('TRANSPORT', 'Rail, road and harbour'),
    ('DESIGN', 'Objects of the modern century'),
]


def make_plaques():
    cw, ch = 256, 128
    im = Image.new('RGBA', (cw * 4, ch * 2), (0, 0, 0, 255))
    d = ImageDraw.Draw(im)
    n = Noise(0x42524F4E5A45504C)
    for i, (title, sub) in enumerate(PLAQUES):
        ox, oy = (i % 4) * cw, (i // 4) * ch
        for y in range(ch):
            for x in range(cw):
                v = n.spread(6)
                d.point((ox + x, oy + y), fill=(int(224 + v), int(215 + v), int(194 + v), 255))
        # Bevelled bronze border, light from the top-left the way a cast frame
        # reads. The field is pale: an engraved bronze plate is the classic
        # look and is also invisible at the light level a gallery runs at.
        d.rectangle((ox, oy, ox + cw - 1, oy + ch - 1), outline=(146, 116, 62, 255), width=4)
        d.line((ox + 4, oy + 4, ox + cw - 5, oy + 4), fill=(196, 164, 100, 255), width=2)
        d.line((ox + 4, oy + 4, ox + 4, oy + ch - 5), fill=(188, 157, 96, 255), width=2)
        d.line((ox + 4, oy + ch - 5, ox + cw - 5, oy + ch - 5), fill=(96, 74, 38, 255), width=2)
        d.line((ox + cw - 5, oy + 4, ox + cw - 5, oy + ch - 5), fill=(96, 74, 38, 255), width=2)
        fitted_centred(d, ox + cw / 2, oy + 30, title, serif, 30, (58, 44, 26, 255), 3, cw - 48)
        d.line((ox + 44, oy + 74, ox + cw - 45, oy + 74), fill=(150, 122, 74, 255), width=2)
        fitted_line(d, (ox, oy + 82, ox + cw, oy + 112), sub, lambda p: body(p, italic=True), 15,
                    (104, 88, 62, 255), cw - 34)
    im.save(OUT / 'gallery-plaques.png')


# --- Room graphic panels ---------------------------------------------------
# One large wall graphic per gallery, carrying the room title. It replaces the
# old raised-letter signs, which cost 2,399 parts to spell eight words.
def panel_base(d, ox, oy, s, ground, ink):
    d.rectangle((ox, oy, ox + s - 1, oy + s - 1), fill=ground)
    d.rectangle((ox + 8, oy + 8, ox + s - 9, oy + s - 9), outline=ink, width=2)


def panel_title(d, ox, oy, s, title, ink, sub=None):
    fitted_centred(d, ox + s / 2, oy + 26, title, serif, 44, ink, 6, s - 80)
    d.line((ox + 70, oy + 92, ox + s - 71, oy + 92), fill=ink, width=2)
    if sub:
        fitted_line(d, (ox, oy + 96, ox + s, oy + 126), sub, lambda p: body(p, italic=True), 19,
                    ink, s - 70)


def make_panels():
    import math
    s = 512
    im = Image.new('RGBA', (s * 4, s * 2), (0, 0, 0, 255))
    d = ImageDraw.Draw(im)

    # 0 ART - a hang plan of the room, the way a gallery guide draws it.
    ox, oy = 0, 0
    panel_base(d, ox, oy, s, (233, 227, 214, 255), (86, 74, 56, 255))
    panel_title(d, ox, oy, s, 'ART', (86, 74, 56, 255), 'The Loom collection, rooms 1-2')
    for i in range(4):
        x = ox + 58 + i * 100
        d.rectangle((x, oy + 178, x + 76, oy + 240), outline=(120, 104, 78, 255), width=3)
        d.rectangle((x + 8, oy + 186, x + 68, oy + 232), fill=(176, 160, 132, 255))
    d.line((ox + 48, oy + 262, ox + s - 49, oy + 262), fill=(150, 132, 102, 255), width=3)
    for i, line in enumerate(['OIL ON CANVAS', 'GIFT OF THE LOOM TRUST', 'ROOM ONE, NORTH WALL',
                              'PLEASE DO NOT TOUCH THE FRAMES']):
        centred(d, (ox, oy + 292 + i * 34, ox + s, oy + 322 + i * 34), line, sans(20),
                (110, 96, 72, 255))

    # 1 ANTIQUITIES - vessel silhouettes against a typological rule.
    ox, oy = s, 0
    panel_base(d, ox, oy, s, (226, 214, 192, 255), (98, 72, 44, 255))
    panel_title(d, ox, oy, s, 'ANTIQUITIES', (98, 72, 44, 255), 'Vessels of the inland coast')
    for i, (w, h) in enumerate([(46, 118), (62, 96), (38, 132), (54, 104)]):
        cx = ox + 96 + i * 108
        base = oy + 320
        d.ellipse((cx - w // 2, base - h, cx + w // 2, base - h // 3), fill=(140, 88, 52, 255))
        d.polygon([(cx - w // 3, base - h // 2), (cx + w // 3, base - h // 2),
                   (cx + w // 6, base), (cx - w // 6, base)], fill=(140, 88, 52, 255))
        d.rectangle((cx - w // 5, base - h - 16, cx + w // 5, base - h + 6), fill=(140, 88, 52, 255))
        d.line((cx - w // 2 - 10, base + 12, cx + w // 2 + 10, base + 12), fill=(150, 118, 80, 255), width=2)
        centred(d, (cx - 50, base + 18, cx + 50, base + 44), f'{i + 1}', sans(18), (120, 92, 60, 255))
    centred(d, (ox, oy + 400, ox + s, oy + 430), 'STORAGE, TRANSPORT AND OFFERING FORMS',
            sans(19), (110, 84, 54, 255))

    # 2 NATURAL HISTORY - a stratigraphic column, which is what a fossil hall
    # actually puts on its wall.
    ox, oy = s * 2, 0
    panel_base(d, ox, oy, s, (214, 219, 222, 255), (52, 66, 78, 255))
    panel_title(d, ox, oy, s, 'NATURAL HISTORY', (52, 66, 78, 255), 'Reading the crescent bedrock')
    bands = [((150, 138, 116), 'ALLUVIUM'), ((176, 158, 122), 'SANDSTONE'),
             ((120, 128, 122), 'SHALE'), ((162, 156, 140), 'LIMESTONE'),
             ((96, 100, 108), 'BASEMENT')]
    for i, (colour, label) in enumerate(bands):
        y = oy + 150 + i * 54
        d.rectangle((ox + 60, y, ox + 220, y + 46), fill=colour + (255,), outline=(70, 82, 92, 255))
        d.text((ox + 238, y + 12), label, font=sans(19), fill=(60, 74, 86, 255))
        d.line((ox + 224, y + 23, ox + 234, y + 23), fill=(70, 82, 92, 255), width=2)
    # The fossil horizon is a marker on the column, not another band label.
    horizon = oy + 150 + 54 * 3 + 46
    d.line((ox + 52, horizon, ox + 228, horizon), fill=(176, 72, 52, 255), width=3)
    d.text((ox + 238, horizon - 12), 'FOSSIL HORIZON', font=sans(17, bold=True), fill=(150, 62, 44, 255))

    # 3 PINATTY HISTORY - the crescent street plan the room's model repeats.
    ox, oy = s * 3, 0
    panel_base(d, ox, oy, s, (236, 230, 214, 255), (70, 68, 58, 255))
    panel_title(d, ox, oy, s, 'PINATTY HISTORY', (70, 68, 58, 255), 'Growth of the southwest crescent')
    d.rectangle((ox + 56, oy + 150, ox + s - 57, oy + 452), fill=(224, 217, 198, 255),
                outline=(150, 144, 126, 255))
    for i in range(6):
        x = ox + 88 + i * 66
        d.line((x, oy + 156, x, oy + 446), fill=(196, 189, 170, 255), width=5)
    for i in range(5):
        y = oy + 186 + i * 62
        d.line((ox + 62, y, ox + s - 63, y), fill=(196, 189, 170, 255), width=5)
    for cx, cy, r in [(150, 250, 40), (330, 350, 52)]:
        d.ellipse((ox + cx - r, oy + cy - r, ox + cx + r, oy + cy + r),
                  outline=(174, 166, 146, 255), width=5)
    d.polygon([(ox + 56, oy + 380), (ox + 200, oy + 452), (ox + 56, oy + 452)], fill=(168, 190, 196, 255))
    d.text((ox + 70, oy + 414), 'BAY', font=sans(18), fill=(96, 116, 124, 255))

    # 4 SCIENCE - the pendulum period relation the room demonstrates.
    ox, oy = 0, s
    panel_base(d, ox, oy, s, (224, 226, 220, 255), (54, 62, 58, 255))
    panel_title(d, ox, oy, s, 'SCIENCE', (54, 62, 58, 255), 'Period, force and gear ratio')
    pivot = (ox + s // 2, oy + 160)
    for angle, shade in [(-34, 190), (-17, 150), (0, 90), (17, 150), (34, 190)]:
        rad = math.radians(angle)
        end = (pivot[0] + 210 * math.sin(rad), pivot[1] + 210 * math.cos(rad))
        d.line((pivot, end), fill=(shade, shade, shade - 10, 255), width=3)
        d.ellipse((end[0] - 15, end[1] - 15, end[0] + 15, end[1] + 15),
                  fill=(shade - 40, shade - 46, shade - 60, 255))
    d.ellipse((pivot[0] - 8, pivot[1] - 8, pivot[0] + 8, pivot[1] + 8), fill=(54, 62, 58, 255))
    d.arc((ox + 130, oy + 250, ox + s - 131, oy + 470), 200, 340, fill=(120, 128, 122, 255), width=2)
    centred(d, (ox, oy + 452, ox + s, oy + 486), 'T = 2 pi (L / g) ^ 1/2', body(30, italic=True),
            (54, 62, 58, 255))

    # 5 SPACE - a plate-style star chart with the room's solar sequence.
    ox, oy = s, s
    d.rectangle((ox, oy, ox + s - 1, oy + s - 1), fill=(20, 26, 44, 255))
    d.rectangle((ox + 8, oy + 8, ox + s - 9, oy + s - 9), outline=(150, 164, 200, 255), width=2)
    stars = Noise(0x53544152434841ff)
    for _ in range(420):
        x = ox + 14 + stars.next() * (s - 28)
        y = oy + 130 + stars.next() * (s - 150)
        r = 0.6 + stars.next() * 1.8
        v = int(150 + stars.next() * 105)
        d.ellipse((x - r, y - r, x + r, y + r), fill=(v, v, min(255, v + 24), 255))
    for a, b in [((120, 200), (180, 260)), ((180, 260), (250, 235)), ((250, 235), (300, 300)),
                 ((300, 300), (360, 270)), ((180, 260), (200, 340))]:
        d.line((ox + a[0], oy + a[1], ox + b[0], oy + b[1]), fill=(120, 140, 190, 255))
        for p in (a, b):
            d.ellipse((ox + p[0] - 4, oy + p[1] - 4, ox + p[0] + 4, oy + p[1] + 4),
                      fill=(226, 234, 255, 255))
    tracked_centred(d, ox + s / 2, oy + 26, 'SPACE', serif(44), (226, 234, 255, 255), 6)
    d.line((ox + 70, oy + 92, ox + s - 71, oy + 92), fill=(150, 164, 200, 255), width=2)
    fitted_line(d, (ox, oy + 96, ox + s, oy + 126), 'The sky above the crescent',
                lambda p: body(p, italic=True), 19, (176, 190, 226, 255), s - 70)
    for i in range(7):
        r = 5 + i * 2
        cx, cy = ox + 74 + i * 60, oy + 452
        d.ellipse((cx - r, cy - r, cx + r, cy + r),
                  fill=[(214, 186, 140), (176, 160, 148), (150, 176, 200), (196, 122, 96),
                        (206, 182, 130), (200, 190, 150), (150, 180, 210)][i] + (255,))

    # 6 TRANSPORT - a route diagram, the poster every rail hall owns.
    ox, oy = s * 2, s
    panel_base(d, ox, oy, s, (232, 224, 206, 255), (86, 46, 40, 255))
    panel_title(d, ox, oy, s, 'TRANSPORT', (86, 46, 40, 255), 'The crescent line, 1908')
    d.line((ox + 70, oy + 300, ox + s - 71, oy + 300), fill=(86, 46, 40, 255), width=6)
    for i, name in enumerate(['LOOM', 'BRIAR', 'MERCER', 'SABLE', 'PORT']):
        x = ox + 74 + i * 92
        d.ellipse((x - 13, oy + 287, x + 13, oy + 313), fill=(238, 232, 216, 255),
                  outline=(86, 46, 40, 255), width=4)
        tracked_centred(d, x, oy + 328, name, sans(15), (86, 46, 40, 255), 1)
    d.line((ox + 166, oy + 300, ox + 258, oy + 210), fill=(140, 108, 60, 255), width=4)
    d.ellipse((ox + 245, oy + 197, ox + 271, oy + 223), fill=(238, 232, 216, 255),
              outline=(140, 108, 60, 255), width=4)
    tracked_centred(d, ox + 258, oy + 172, 'YARD', sans(15), (140, 108, 60, 255), 1)
    centred(d, (ox, oy + 410, ox + s, oy + 444), 'GAUGE 1435 MM  /  RULING GRADE 1 IN 80',
            sans(19), (110, 70, 58, 255))

    # 7 DESIGN - a colour and proportion study, which is the room's subject.
    ox, oy = s * 3, s
    panel_base(d, ox, oy, s, (238, 236, 230, 255), (48, 48, 52, 255))
    panel_title(d, ox, oy, s, 'DESIGN', (48, 48, 52, 255), 'Proportion, colour and use')
    # Colour above, proportion below. Overlapping the two made both unreadable.
    swatches = [(188, 62, 52), (214, 148, 44), (238, 226, 206), (44, 108, 106),
                (36, 44, 58), (150, 152, 146)]
    for i, colour in enumerate(swatches):
        x = ox + 44 + i * 71
        d.rectangle((x, oy + 152, x + 68, oy + 262), fill=colour + (255,),
                    outline=(206, 204, 198, 255))
    d.rectangle((ox + 44, oy + 152, ox + 44 + 6 * 71 - 3, oy + 262), outline=(48, 48, 52, 255), width=2)
    # Golden-section rectangle, drawn once and squarely: the room's thesis.
    gx, gy, gh = ox + 118, oy + 300, 168
    gw = int(gh * 1.618)
    d.rectangle((gx, gy, gx + gw, gy + gh), outline=(48, 48, 52, 255), width=3)
    d.line((gx + gh, gy, gx + gh, gy + gh), fill=(48, 48, 52, 255), width=3)
    d.arc((gx, gy, gx + 2 * gh, gy + 2 * gh), 180, 270, fill=(188, 62, 52, 255), width=3)
    centred(d, (ox, oy + 476, ox + s, oy + 502), '1 : 1.618', body(24, italic=True), (48, 48, 52, 255))

    im.save(OUT / 'gallery-panels.png')


# --- Object labels ---------------------------------------------------------
# 4 x 8 cells of 256 x 128, hung at 0.6 x 0.3 m. The title carries at reading
# distance; the body is texture, exactly as a real label is from four metres.
LABELS = [
    ('THE BRIDGE AT SABLE REACH', 'Oil on canvas, c. 1898', 'Loom Trust, 1954'),
    ('WOMAN IN A BURGUNDY DRESS', 'Oil on canvas, c. 1881', 'Gift of the sitter'),
    ('PEARS, GRAPES AND BLUE VASE', 'Oil on panel, c. 1904', 'Purchased 1961'),
    ('HARBOUR AT EVENING', 'Oil on canvas, c. 1912', 'Loom Trust, 1954'),
    ('STORAGE AMPHORA', 'Terracotta, wheel thrown', 'Crescent shore find'),
    ('TRANSPORT AMPHORA', 'Terracotta, slipped rim', 'Crescent shore find'),
    ('VOTIVE STELE', 'Cut limestone, inscribed', 'Mercer Street cutting'),
    ('OFFERING VESSELS', 'Terracotta, six forms', 'Assembled group'),
    ('CRESCENT SAUROPOD', 'Cast skeleton, 11 m', 'Quarried at Briar Bluff'),
    ('BEDDING PLANE BLOCK', 'Limestone with impressions', 'In situ orientation'),
    ('PINATTY IN EIGHT DECADES', 'Painted timber and card', 'Scale 1 : 500'),
    ('THE CLOSED FIRST STREET', 'Survey overlay', 'City engineer, 1971'),
    ('SECONDS PENDULUM', 'Brass bob on steel wire', 'Swing arc 34 degrees'),
    ('REDUCTION GEAR TRAIN', 'Cut steel, 12 : 30', 'Working sectional model'),
    ('SOUNDING ROCKET', 'Aluminium and steel', 'Flown, recovered 1969'),
    ('THE SOLAR SEQUENCE', 'Turned hardwood spheres', 'Not to relative scale'),
    ('CRESCENT LINE 0-6-0', 'Steam locomotive, 1908', 'Withdrawn 1962'),
    ('COUPLING ROD AND CRANK', 'Forged steel', 'Cutaway for display'),
    ('SIDE CHAIR, MODEL 12', 'Bent ply and steel', 'Designed 1948'),
    ('PRESSED GLASS SERVICE', 'Moulded lead glass', 'Designed 1936'),
    ('TABLE LAMP, TYPE C', 'Enamelled steel', 'Designed 1954'),
    ('READING BENCH', 'Oak and wool', 'Made for this room'),
    ('RECEPTION', 'Tickets, cloakroom, guides', 'Open daily'),
    ('THE ENTRANCE HALL', 'Loom Way portico, 1907', 'Doric order, four columns'),
]


def make_labels():
    cw, ch = 256, 128
    # Opaque ground. These are printed cards, and a transparent atlas ground
    # is not something the world texture path is asked to carry anywhere else.
    im = Image.new('RGBA', (cw * 4, ch * 8), (238, 235, 228, 255))
    d = ImageDraw.Draw(im)
    for i, (title, line1, line2) in enumerate(LABELS):
        ox, oy = (i % 4) * cw, (i // 4) * ch
        d.rectangle((ox, oy, ox + cw - 1, oy + ch - 1), fill=(248, 246, 240, 255))
        d.rectangle((ox, oy, ox + cw - 1, oy + ch - 1), outline=(206, 200, 186, 255))
        d.line((ox, oy + ch - 2, ox + cw - 1, oy + ch - 2), fill=(196, 190, 176, 255), width=2)
        fitted_centred(d, ox + cw / 2, oy + 20, title, lambda p: sans(p, bold=True), 19,
                       (38, 36, 32, 255), 1, cw - 24)
        d.line((ox + 34, oy + 52, ox + cw - 35, oy + 52), fill=(176, 60, 44, 255), width=2)
        fitted_line(d, (ox, oy + 58, ox + cw, oy + 82), line1, lambda p: body(p, italic=True), 17,
                    (78, 74, 66, 255), cw - 24)
        fitted_line(d, (ox, oy + 84, ox + cw, oy + 108), line2, sans, 15, (120, 114, 102, 255), cw - 24)
    im.save(OUT / 'exhibit-labels.png')


# --- Lobby directory -------------------------------------------------------
def make_directory():
    w, h = 512, 768
    # Cream stone with bronze lettering. A dark board is invisible in a hall
    # this dim, and the hall is dim by design.
    im = Image.new('RGBA', (w, h), (226, 219, 202, 255))
    d = ImageDraw.Draw(im)
    n = Noise(0x4449524543544F52)
    for y in range(h):
        for x in range(w):
            v = n.spread(5)
            d.point((x, y), fill=(int(228 + v), int(221 + v), int(203 + v), 255))
    d.rectangle((14, 14, w - 15, h - 15), outline=(140, 112, 62, 255), width=3)
    tracked_centred(d, w / 2, 44, 'PINATTY', serif(58), (74, 58, 34, 255), 10)
    tracked_centred(d, w / 2, 110, 'MUSEUM', serif(58), (74, 58, 34, 255), 10)
    d.line((70, 188, w - 71, 188), fill=(140, 112, 62, 255), width=2)
    centred(d, (0, 196, w, 228), 'Loom Way at Briar Street', body(22, italic=True), (110, 92, 64, 255))

    def floor_block(y, heading, rooms):
        tracked(d, 58, y, heading, sans(21, bold=True), (142, 110, 56, 255), 2)
        d.line((58, y + 32, w - 59, y + 32), fill=(178, 170, 150, 255))
        for i, (num, name) in enumerate(rooms):
            row = y + 46 + i * 40
            d.text((58, row), num, font=serif(24), fill=(142, 110, 56, 255))
            tracked(d, 104, row + 3, name, sans(19), (52, 48, 42, 255), 1)
        return y + 46 + len(rooms) * 40 + 26

    y = floor_block(266, 'GROUND FLOOR', [('1', 'ART'), ('2', 'ANTIQUITIES'),
                                          ('3', 'NATURAL HISTORY'), ('4', 'PINATTY HISTORY')])
    y = floor_block(y, 'UPPER FLOOR', [('5', 'SCIENCE'), ('6', 'SPACE'),
                                       ('7', 'TRANSPORT'), ('8', 'DESIGN')])
    d.line((58, y - 8, w - 59, y - 8), fill=(178, 170, 150, 255))
    for i, line in enumerate(['STAIR TO UPPER FLOOR AT THE NORTH END',
                              'CLOAKROOM AND TICKETS AT RECEPTION',
                              'ADMISSION FREE TO RESIDENTS OF PINATTY']):
        centred(d, (0, y + 6 + i * 30, w, y + 34 + i * 30), line, sans(16), (112, 106, 94, 255))
    im.save(OUT / 'museum-directory.png')


for fn in (make_wall, make_floor, make_ceiling, make_plaques, make_panels, make_labels,
           make_directory):
    fn()
for path in sorted(OUT.glob('*.png')):
    with Image.open(path) as check:
        print(f'{path.relative_to(ROOT)}  {check.size[0]}x{check.size[1]}  '
              f'{path.stat().st_size // 1024} KB')
