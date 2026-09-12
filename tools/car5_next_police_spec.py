"""Car 5-NEXT PATROL: the livery and the roof lightbar.

Everything here is in Car 5 SOURCE units, the same space as car5_next_spec.py
-- not metres. The catalog fits this body with scale_x = .751 and
scale_y = scale_z = .664, so a number meant as a real-world dimension has to be
divided by those before it lands here.

The lightbar is the Municipal Cruiser 91-C's, converted. Its proportions are
worth keeping because they are the ones already tuned against this engine's
flash, glow and spot-light rig: a shallow black plinth on the roof carrying a
red lens, a chrome centre and a blue lens, about a fifth of the roof's height
tall and a quarter of its length deep. What does NOT carry over is the width:
the 91-C's bar is 1.52 m across a body half a metre wider than this one, so it
is re-fitted to Car 5's narrower roof rather than scaled.
"""

# Car 5's roof panel, measured off the cooked shell: a flat slab at this
# height, running between these Z bounds, this far out to each side.
ROOF_Y = 2.019
ROOF_Z = (-1.412, 0.516)
ROOF_HALF_X = 0.875

# Plinth. Sits on the roof just behind the windscreen header, where a real bar
# is bolted, and stays well inside the roof edge so it never overhangs.
BAR_HALF_X = 0.66
BAR_Z = (-0.10, 0.28)
PLINTH_TOP_Y = 2.109

# Lenses, inset from the plinth so its edge reads as a separate part. The top
# is 0.22 m of world height above the roof, which is the 91-C's proportion.
LENS_TOP_Y = 2.350
LENS_Z = (-0.085, 0.265)
LENS_HALF_X = 0.60
LENS_GAP_X = 0.045  # half-width of the chrome divider between the two banks

# Which atlas swatch each box samples. make_car5_next_police_assets.py paints
# these as flat patches in space Car 5's own charts never touch.
SWATCHES = {
    "lightbar_red": ((4, 4, 20, 20), (198, 26, 24)),
    "lightbar_blue": ((24, 4, 40, 20), (34, 56, 208)),
    "lightbar_black": ((4, 24, 20, 40), (22, 22, 24)),
    "lightbar_metal": ((24, 24, 40, 40), (168, 172, 176)),
}
ATLAS_SIZE = 256


def swatch_uv(name):
    """Centre of a swatch, as the single UV a flat box face samples."""
    x0, y0, x1, y1 = SWATCHES[name][0]
    u = (x0 + x1) * 0.5 / ATLAS_SIZE
    v = 1.0 - (y0 + y1) * 0.5 / ATLAS_SIZE
    return (round(u, 6), round(v, 6))


def lightbar_boxes():
    """(name, x0, x1, y0, y1, z0, z1, swatch) for every box in the bar."""
    return [
        ("Plinth", -BAR_HALF_X, BAR_HALF_X, ROOF_Y, PLINTH_TOP_Y,
         BAR_Z[0], BAR_Z[1], "lightbar_black"),
        # Red is the -X bank, blue the +X one, matching every 91 cruiser and
        # the mirrored X the lit shader tests the red bank against.
        ("RedLens", -LENS_HALF_X, -LENS_GAP_X, PLINTH_TOP_Y, LENS_TOP_Y,
         LENS_Z[0], LENS_Z[1], "lightbar_red"),
        ("Divider", -LENS_GAP_X, LENS_GAP_X, PLINTH_TOP_Y, LENS_TOP_Y,
         LENS_Z[0], LENS_Z[1], "lightbar_metal"),
        ("BlueLens", LENS_GAP_X, LENS_HALF_X, PLINTH_TOP_Y, LENS_TOP_Y,
         LENS_Z[0], LENS_Z[1], "lightbar_blue"),
    ]


def lens_box():
    """The volume the lit shader lets the emergency glow show through.

    Returned mirrored-X, the way the shader tests it: the red bank negates X
    before comparing, so one box covers both banks.
    """
    return {"x": (LENS_GAP_X, LENS_HALF_X), "y": (PLINTH_TOP_Y, LENS_TOP_Y),
            "z": (LENS_Z[0], LENS_Z[1])}


def lamp_centres():
    """Centre of each lens bank: the spot-light origins, red bank first."""
    x = (LENS_GAP_X + LENS_HALF_X) * 0.5
    y = (PLINTH_TOP_Y + LENS_TOP_Y) * 0.5
    z = (LENS_Z[0] + LENS_Z[1]) * 0.5
    return ((-round(x, 4), round(y, 4), round(z, 4)),
            (round(x, 4), round(y, 4), round(z, 4)))


if __name__ == "__main__":
    import json
    print(json.dumps({"boxes": lightbar_boxes(), "lens_box": lens_box(),
                      "centres": lamp_centres(),
                      "swatch_uv": {n: swatch_uv(n) for n in SWATCHES}}, indent=2))
