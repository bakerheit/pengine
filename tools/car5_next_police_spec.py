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

# The bar is built as a stack of slabs, bottom to top. Heights first, so the
# proportions are readable in one place: the whole assembly stands 0.343 above
# the roof, which is the 91-C's 0.22 m converted into this body's units.
FOOT_TOP_Y = 2.081     # mounting feet, so daylight shows under the bar
BASE_TOP_Y = 2.131     # the dark shell the lenses sit in
LENS_TOP_Y = 2.318     # the coloured band
CAP_TOP_Y = 2.362      # chrome top rail

# Plan. The bar sits just behind the windscreen header, where a real one is
# bolted, and stays well inside the roof edge so it never overhangs.
BAR_HALF_X = 0.66
BAR_Z = (-0.10, 0.28)

# The lens band. Cells stand proud of everything around them; the ribs, end
# caps and top rail are recessed, which is what makes the bar read as parts
# rather than one painted block at the distance you actually see it from.
LENS_Z = (-0.11, 0.29)
FRAME_Z = (-0.085, 0.265)
LENS_OUTER_X = 0.60
DIVIDER_HALF_X = 0.045   # chrome spine between the red and blue banks
RIB_WIDTH = 0.035        # chrome rib between two cells of the same bank
CELLS_PER_BANK = 3

CAP_HALF_X = 0.63        # top rail, inset from the base so its edge shows
FOOT_X = (0.26, 0.50)    # one foot each side
FOOT_Z = (-0.06, 0.24)
SPEAKER_HALF_X = 0.16    # siren speaker slung under the bar between the feet
SPEAKER_Z = (0.00, 0.22)


def lens_cells():
    """The (x0, x1) of each lens cell on the +X bank, inboard to outboard.

    Returned for +X only: the red bank is its mirror, and the lit shader tests
    the red bank by negating X before comparing, so one list serves both.
    """
    span = LENS_OUTER_X - DIVIDER_HALF_X
    width = (span - RIB_WIDTH * (CELLS_PER_BANK - 1)) / CELLS_PER_BANK
    cells = []
    at = DIVIDER_HALF_X
    for _ in range(CELLS_PER_BANK):
        cells.append((round(at, 4), round(at + width, 4)))
        at += width + RIB_WIDTH
    return cells


def lens_ribs():
    """The (x0, x1) of each chrome rib on the +X bank."""
    cells = lens_cells()
    return [(cells[i][1], cells[i + 1][0]) for i in range(len(cells) - 1)]


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
    boxes = [
        ("BaseRail", -BAR_HALF_X, BAR_HALF_X, FOOT_TOP_Y, BASE_TOP_Y,
         BAR_Z[0], BAR_Z[1], "lightbar_black"),
        ("TopRail", -CAP_HALF_X, CAP_HALF_X, LENS_TOP_Y, CAP_TOP_Y,
         FRAME_Z[0], FRAME_Z[1], "lightbar_metal"),
        ("Divider", -DIVIDER_HALF_X, DIVIDER_HALF_X, BASE_TOP_Y, LENS_TOP_Y,
         FRAME_Z[0], FRAME_Z[1], "lightbar_metal"),
        ("Speaker", -SPEAKER_HALF_X, SPEAKER_HALF_X, ROOF_Y, FOOT_TOP_Y,
         SPEAKER_Z[0], SPEAKER_Z[1], "lightbar_black"),
    ]
    for side, tint in ((1, "lightbar_blue"), (-1, "lightbar_red")):
        # Blue is the +X bank and red the -X one, matching every 91 cruiser and
        # the mirrored X the lit shader tests the red bank against.
        label = "Blue" if side > 0 else "Red"
        for index, (x0, x1) in enumerate(lens_cells()):
            boxes.append((f"{label}Lens{index}", *sorted((side * x0, side * x1)),
                          BASE_TOP_Y, LENS_TOP_Y, LENS_Z[0], LENS_Z[1], tint))
        for index, (x0, x1) in enumerate(lens_ribs()):
            boxes.append((f"{label}Rib{index}", *sorted((side * x0, side * x1)),
                          BASE_TOP_Y, LENS_TOP_Y, FRAME_Z[0], FRAME_Z[1],
                          "lightbar_metal"))
        boxes.append((f"{label}Cap", *sorted((side * LENS_OUTER_X, side * BAR_HALF_X)),
                      BASE_TOP_Y, LENS_TOP_Y, FRAME_Z[0], FRAME_Z[1],
                      "lightbar_metal"))
        boxes.append((f"{label}Foot", *sorted((side * FOOT_X[0], side * FOOT_X[1])),
                      ROOF_Y, FOOT_TOP_Y, FOOT_Z[0], FOOT_Z[1], "lightbar_metal"))
    return boxes


def lens_boxes():
    """The volumes the lit shader lets the emergency glow show through.

    One per lens cell, so the chrome ribs between them stay dark instead of
    lighting up with the bank. Returned mirrored-X, the way the shader tests
    them: the red bank negates X first, so these cover both banks.
    """
    return [{"x": cell, "y": (BASE_TOP_Y, LENS_TOP_Y), "z": LENS_Z}
            for cell in lens_cells()]


def glsl():
    """The lit.frag clause for this profile, so the shader can be kept in step."""
    lines = []
    for box in lens_boxes():
        lines.append(
            "            lightbar = lightbar || (v_headlight_profile == 29 &&\n"
            "                x>=%.4f && x<=%.4f && p.y>=%.3f && p.y<=%.3f &&\n"
            "                p.z>=%.3f && p.z<=%.3f);" % (
                box["x"][0], box["x"][1], box["y"][0], box["y"][1],
                box["z"][0], box["z"][1]))
    return "\n".join(lines)


def lamp_centres():
    """Centre of each lens bank: the spot-light origins, red bank first."""
    cells = lens_cells()
    x = (cells[0][0] + cells[-1][1]) * 0.5
    y = (BASE_TOP_Y + LENS_TOP_Y) * 0.5
    z = (LENS_Z[0] + LENS_Z[1]) * 0.5
    return ((-round(x, 4), round(y, 4), round(z, 4)),
            (round(x, 4), round(y, 4), round(z, 4)))


if __name__ == "__main__":
    import json
    print(json.dumps({"boxes": lightbar_boxes(), "lens_boxes": lens_boxes(),
                      "centres": lamp_centres(),
                      "swatch_uv": {n: swatch_uv(n) for n in SWATCHES}}, indent=2))
    print()
    print(glsl())
