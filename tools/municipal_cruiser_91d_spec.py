"""Municipal Cruiser 91D Metro shape, articulation, and atlas contract.

Source geometry uses Blender X/right, Y/forward, Z/up. Public anchors below
use Apricot cooked coordinates: X/right, Y/up, +Z/nose.
"""

ATLAS_SIZE = 256

WHEELS = {
    "x": 0.91,
    "front_z": 1.48,
    "rear_z": -1.43,
    "arch_y": 0.42,
    "radius": 0.38,
}

DOOR = {
    "hinge": (0.986, 0.49, 0.72),
    "handle": (1.006, 1.03, -0.12),
    "rear_z": -0.32,
    "front_z": 0.72,
    "sill_y": 0.49,
    "belt_y": 1.06,
    "top_y": 1.66,
    "open_degrees": -66.0,
}

DRIVER = {
    "hip": (0.42, 0.76, 0.06),
    "knees": ((0.33, 0.69, 0.35), (0.53, 0.69, 0.35)),
    "wrists": ((0.31, 1.10, 0.50), (0.53, 1.10, 0.50)),
    "elbows": ((0.17, 0.94, 0.16), (0.67, 0.94, 0.16)),
    "ankles": ((0.33, 0.47, 0.60), (0.53, 0.47, 0.60)),
    "handle_elbow": (0.72, 0.99, -0.13),
    "approach_x": 1.52,
    "floor_y": 0.43,
    "cushion_top_y": 0.76,
    "seat_back_z": -0.13,
    "dashboard_z": 0.62,
    "roof_inner_y": 1.66,
}

SHAPE = {
    "name": "Municipal Cruiser 91D Metro",
    "length": 5.08,
    "width": 2.04068,
    "body_bottom": 0.20,
    "hood_height": 1.04,
    "deck_height": 1.05,
    "beltline": 1.06,
    "body_roof": 1.70,
    "lightbar_height": 1.88,
    "wheelbase": 2.91,
    "track": 1.82,
    "arch_long_radius": 0.51,
    "arch_height_radius": 0.44,
    "cabin": (-1.18, 0.72),
    "flat_roof": (-0.89, 0.27),
    "triangle_budget": (900, 1800),
    "atlas": (256, 256, "RGBA"),
    "traits": [
        "short-overhang tall-greenhouse 1991 urban fleet sedan",
        "chunky faceted shoulders with a compact formal notchback",
        "paired rectangular sealed beams and narrow vertical grille",
        "forest-green and cream municipal livery with generic POLICE text",
        "simple push bar, driver spotlight, whip antenna, and full roof bar",
    ],
}

# Inclusive atlas cells. Two-pixel gutters keep nearest-filter samples apart.
# These cells are frozen before the imagegen edit.
REGIONS = {
    "BODY_GREEN": (4, 4, 124, 64),
    "BODY_TOP": (128, 4, 252, 64),
    "DOOR_CREAM": (4, 68, 92, 128),
    "POLICE": (96, 68, 180, 128),
    "GLASS": (184, 68, 252, 128),
    "FRONT": (4, 132, 124, 176),
    "REAR": (128, 132, 252, 176),
    "METAL": (4, 180, 44, 208),
    "RUBBER": (48, 180, 88, 208),
    "INTERIOR": (92, 180, 132, 208),
    "SEAM": (136, 180, 176, 208),
    "HEADLIGHT": (180, 180, 216, 208),
    "AMBER": (220, 180, 252, 208),
    "TAIL_RED": (4, 212, 44, 240),
    "REVERSE": (48, 212, 88, 240),
    "LIGHTBAR_RED": (92, 212, 156, 240),
    "LIGHTBAR_BLUE": (160, 212, 224, 240),
    "SHADOW": (228, 212, 252, 240),
    "BLACK": (4, 244, 124, 252),
}

UV_PROJECTIONS = {
    "BODY_GREEN": ((1, 2), ((-2.54, 2.54), (0.20, 1.12))),
    "BODY_TOP": ((0, 1), ((-1.01, 1.01), (-2.54, 2.54))),
    "DOOR_CREAM": ((1, 2), ((-1.18, 0.72), (0.49, 1.06))),
    "POLICE": ((1, 2), ((-0.32, 0.72), (0.54, 1.04))),
    "FRONT": ((0, 2), ((-1.01, 1.01), (0.20, 1.08))),
    "REAR": ((0, 2), ((-1.01, 1.01), (0.20, 1.08))),
}

PANE_FILES = (
    "windshield",
    "rear_glass",
    "passenger_glass",
    "driver_glass",
    "driver_rear_glass",
    "passenger_rear_glass",
)

LAMPS = {
    "headlights": {
        "bounds": (
            ((-0.93, 0.70, 2.535), (-0.71, 0.88, 2.540)),
            ((-0.68, 0.70, 2.535), (-0.46, 0.88, 2.540)),
            ((0.46, 0.70, 2.535), (0.68, 0.88, 2.540)),
            ((0.71, 0.70, 2.535), (0.93, 0.88, 2.540)),
        ),
        "atlas": "HEADLIGHT",
    },
    "brakelights": {
        "bounds": (
            ((-0.91, 0.70, -2.540), (-0.56, 0.91, -2.535)),
            ((0.56, 0.70, -2.540), (0.91, 0.91, -2.535)),
        ),
        "atlas": "TAIL_RED",
    },
    "reverse": {
        "bounds": (
            ((-0.54, 0.70, -2.540), (-0.38, 0.91, -2.535)),
            ((0.38, 0.70, -2.540), (0.54, 0.91, -2.535)),
        ),
        "atlas": "REVERSE",
    },
}

LIGHTBAR = {
    "red": {
        "bounds_min": (-0.72, 1.74, -0.20),
        "bounds_max": (-0.06, 1.88, 0.10),
        "atlas": "LIGHTBAR_RED",
    },
    "blue": {
        "bounds_min": (0.06, 1.74, -0.20),
        "bounds_max": (0.72, 1.88, 0.10),
        "atlas": "LIGHTBAR_BLUE",
    },
    "base": {
        "bounds_min": (-0.76, 1.70, -0.22),
        "bounds_max": (0.76, 1.74, 0.12),
    },
}
