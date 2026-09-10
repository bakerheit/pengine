#!/usr/bin/env python3
"""Deterministic signage for Halberd Field, the north shore air station.

Two boards and one crest, all drawn from fixed geometry so a re-run reproduces
the shipped PNGs. Diffuse albedo only.

The gate board is the only text on the station a player reads at walking pace,
so it carries the whole identity: unit, station, and the warning that makes the
open barrier a decision rather than scenery.
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets/textures/world/halberd'
OUT.mkdir(parents=True, exist_ok=True)

SANS = '/System/Library/Fonts/Helvetica.ttc'
SERIF = '/System/Library/Fonts/Supplemental/Didot.ttc'


def sans(size, bold=False):
    return ImageFont.truetype(SANS, size, index=1 if bold else 0)


def serif(size):
    return ImageFont.truetype(SERIF, size)


def tracked(draw, cx, y, text, font, fill, tracking):
    width = sum(draw.textlength(c, font=font) + tracking for c in text) - tracking
    x = cx - width / 2
    for ch in text:
        draw.text((x, y), ch, font=font, fill=fill)
        x += draw.textlength(ch, font=font) + tracking


def fitted(draw, cx, y, text, maker, size, fill, tracking, max_width, floor=8):
    while size > floor:
        font = maker(size)
        width = sum(draw.textlength(c, font=font) + tracking for c in text) - tracking
        if width <= max_width:
            break
        size -= 1
    tracked(draw, cx, y, text, maker(size), fill, tracking)


# --- Main gate board -------------------------------------------------------
# Dark olive ground, white rule, and the warning band in red at the foot. Sized
# 1024 x 224 for an 11 x 2.4 m board, which is about 90 px per metre.
def make_gate_sign():
    w, h = 1024, 224
    im = Image.new('RGBA', (w, h), (44, 50, 40, 255))
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, w - 1, h - 1), outline=(206, 200, 182, 255), width=5)
    d.rectangle((10, 10, w - 11, h - 11), outline=(120, 128, 108, 255), width=2)
    fitted(d, w / 2, 24, 'HALBERD FIELD', serif, 62, (238, 234, 220, 255), 10, w - 90)
    d.line((150, 100, w - 151, 100), fill=(176, 168, 140, 255), width=2)
    fitted(d, w / 2, 108, "PINATTY AIR STATION", lambda p: sans(p, bold=True), 26,
           (206, 202, 186, 255), 6, w - 140)
    d.rectangle((60, 152, w - 61, 200), fill=(140, 38, 32, 255))
    fitted(d, w / 2, 162, 'RESTRICTED AREA - AUTHORISED PERSONNEL ONLY',
           lambda p: sans(p, bold=True), 26, (246, 238, 232, 255), 3, w - 140)
    im.save(OUT / 'gate-sign.png')


# --- Hangar door numbers ---------------------------------------------------
# One 2 x 2 atlas: hangars 1, 2, 3 and a spare bay marking. Painted on the door
# leaves, which is where a station numbers its hangars.
def make_hangar_numbers():
    cell = 256
    im = Image.new('RGBA', (cell * 2, cell * 2), (0, 0, 0, 255))
    d = ImageDraw.Draw(im)
    for i, text in enumerate(['1', '2', '3', 'H']):
        ox, oy = (i % 2) * cell, (i // 2) * cell
        d.rectangle((ox, oy, ox + cell - 1, oy + cell - 1), fill=(38, 42, 36, 255))
        d.rectangle((ox + 8, oy + 8, ox + cell - 9, oy + cell - 9),
                    outline=(214, 186, 74, 255), width=6)
        font = sans(150, bold=True)
        box = d.textbbox((0, 0), text, font=font)
        d.text((ox + (cell - (box[2] - box[0])) / 2 - box[0],
                oy + (cell - (box[3] - box[1])) / 2 - box[1]), text, font=font,
               fill=(214, 186, 74, 255))
    im.save(OUT / 'hangar-numbers.png')


# --- Chain link ------------------------------------------------------------
# Mostly transparent, which is the whole point: drawn as a solid box the
# perimeter reads as a 2.5 km concrete wall, and a wall is a different
# building. Tiled about 2 m per repeat by the uv scale in world.cpp.
def make_chain_link():
    size = 128
    im = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    wire = (176, 180, 184, 255)
    shade = (128, 133, 138, 255)
    # Two opposing diagonal families make the diamond. Drawn wrapped so the
    # sheet tiles in both directions.
    for k in range(-size, size * 2, 16):
        d.line((k, 0, k + size, size), fill=wire, width=2)
        d.line((k + size, 0, k, size), fill=shade, width=2)
    im.save(OUT / 'chain-link.png')


for fn in (make_gate_sign, make_hangar_numbers, make_chain_link):
    fn()
for path in sorted(OUT.glob('*.png')):
    with Image.open(path) as check:
        print(f'{path.relative_to(ROOT)}  {check.size[0]}x{check.size[1]}  '
              f'{path.stat().st_size // 1024} KB')
