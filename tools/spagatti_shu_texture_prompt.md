# Spagatti Shū legacy texture brief

This is the retained brief for the previous model. The current finish uses the
model-derived atlas in `make_spagatti_shu_assets.py`; it does not read
`body-imagegen-source.png`. See `docs/assets/spagatti-shu.md` for the current
cook and UV contract.

## Prompt

Edit Image 1 directly. It is an exact UV texture template exported from the
finished low-poly game-car model, not a loose visual reference. Preserve the
square canvas, every rectangular cell, every cell boundary, and the precise
pixel position and proportions of all receivers. Do not rearrange, rotate,
resize, add, or remove atlas cells.

Replace all cyan UV wires, magenta borders, and white labels completely with
finished texture paint. Do not leave any guide marks, words, letters, numbers,
logos, badges, or watermarks in the result. Keep the result a flat orthographic
2D texture atlas. Do not draw a car, wheels, scenery, lighting setup, or a
presentation sheet.

This texture is for the fictional Spagatti Shū, an original rounded French-style
grand-touring hypercar. Use deep Italian red and oxblood body paint with subtle
warm-red value steps, dark charcoal lower trim, smoke-blue glass,
restrained warm brushed-metal trim, pale warm headlights, and deep red round
tail lamps. It should feel expensive and sculpted, but not copy any real badge,
logo, grille mesh, or trademarked two-tone scheme.

The large BODY_SIDE cell is the complete side elevation: tail at left, nose at
right, ground at bottom, roof at top. Follow its visible wheel openings and body
silhouette. Use broad coherent paint areas, one restrained shoulder highlight,
a darker lower rocker, and a subtle C-shaped warm-metal accent around the cabin
and rear intake. No racing stripe and no bright white slash across the doors.

BODY_TOP is the complete top projection: left/right across the narrow cell,
front toward its top and rear toward its bottom. Keep a quiet centre field with
subtle bilateral highlights that follow the long hood and rear deck. BODY_FRONT
and BODY_REAR are exact end-face projections; keep their red body paint calm.
The cooker adds the exact flush grille, lamp, and exhaust details afterward.

GLASS_SIDE, GLASS_FRONT, and GLASS_REAR are exact projections from separate,
two-sided panes fitted inside solid window frames. Use nearly black smoke-blue
glass with only two or three hard-edged muted reflection bands.
Do not paint interiors or scenery into the glass. Keep CLADDING, BODY_SHADOW,
BLACK, EXHAUST, and LENS_DARK very dark and low contrast. Keep METAL and SEAM
warm but restrained. Keep HEADLIGHT and TAIL_RED clean and readable.

Imagegen supplies the broad paint character. After reduction, the cooker locks
the horseshoe grille, two headlamps, four red brake lamps, and paired exhausts
into exact BODY_FRONT/BODY_REAR model coordinates. Those details live on the
curved body facets and must never be exported as offset cards.

Style: authentic late-1990s console texture art, hard-edged 4-12 pixel clusters,
flat value bands, sparse ordered dither, nearest-neighbour feel, no soft
airbrushing, no photoreal reflections, no random noise, and no tiny decorative
fragments. The whole atlas must remain usable as a production UV texture.
