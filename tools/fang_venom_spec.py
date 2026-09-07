"""Single source of truth for the FANG VENOM low-poly motorbike."""

ATLAS_SIZE = 256

# Pixel rectangles are deliberately separated.  The cook maps every component
# into one named cell, then reduces imagegen detail inside that exact cell.
REGIONS = {
    "PAINT_SIDE": (0, 0, 96, 96),
    "PAINT_TOP": (96, 0, 160, 96),
    "NOSE": (160, 0, 256, 64),
    "BLACK": (0, 96, 64, 160),
    "METAL": (64, 96, 128, 160),
    "ENGINE": (128, 96, 192, 160),
    "SEAT": (192, 96, 256, 160),
    "HEADLIGHT": (0, 160, 64, 224),
    "TAIL_RED": (64, 160, 128, 224),
    "GAUGE": (128, 160, 192, 224),
    "TIRE": (192, 160, 256, 224),
    "RIM": (0, 224, 64, 256),
    "EXHAUST": (64, 224, 128, 256),
    "PURPLE": (128, 224, 192, 256),
    "FRAME": (192, 224, 256, 256),
}

WHEELS = {
    "front_z": 0.73,
    "rear_z": -0.72,
    "centre_y": 0.39,
    "radius": 0.365,
    "half_width": 0.065,
}

SHAPE = {
    "period": "1991",
    "archetype": "compact faired street sportbike",
    "length_m": 2.16,
    "handlebar_width_m": 0.80,
    "body_width_m": 0.58,
    "height_m": 1.12,
    "wheelbase_m": 1.45,
    "triangle_budget": (350, 1100),
    "signature": [
        "wedge nose with a single rectangular lamp",
        "faceted fuel tank and stepped solo seat",
        "visible square-tube frame, engine fins, forks and swingarm",
        "paired high tail pipes and venom-green/purple early-1990s graphics",
    ],
}

DRIVER = {
    "hip": (0.0, 0.91, -0.18),
    "wrists": ((-0.25, 1.05, 0.31), (0.25, 1.05, 0.31)),
    "ankles": ((-0.23, 0.55, -0.05), (0.23, 0.55, -0.05)),
    "knees": ((-0.25, 0.82, 0.18), (0.25, 0.82, 0.18)),
    "elbows": ((-0.37, 1.11, 0.12), (0.37, 1.11, 0.12)),
    "approach_x": 0.58,
}

LAMPS = {
    "headlight": (0.04, 0.79, 0.20, 0.98, 1.055, 1.085),
    "rear_red": (0.03, 0.82, 0.17, 0.95, -1.085, -1.045),
}
