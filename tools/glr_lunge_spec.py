"""Shared model and atlas contract for the fictional GLR Lunge."""

ATLAS_SIZE = 256

REGIONS = {
    "BODY_SIDE": (4, 4, 100, 100),
    "BODY_TOP": (104, 4, 200, 100),
    "BODY_SHADOW": (204, 4, 252, 100),
    "GLASS": (4, 104, 100, 164),
    "CLADDING": (104, 104, 164, 164),
    "SEAM": (168, 104, 200, 164),
    "TAIL_RED": (204, 104, 228, 132),
    "TAIL_AMBER": (232, 104, 252, 132),
    "REVERSE": (204, 136, 228, 164),
    "MARKER_AMBER": (232, 136, 252, 164),
    "METAL": (4, 168, 68, 220),
    "INTERIOR": (72, 168, 132, 220),
    "EXHAUST": (136, 168, 188, 220),
    "LENS_DARK": (192, 168, 252, 220),
    "GRIME": (4, 224, 100, 252),
    "DASH": (104, 224, 200, 252),
    "BLACK": (204, 224, 252, 252),
}
