"""Municipal Cruiser 91A shape, articulation, and fixed-atlas contract.

Source coordinates use Blender X/right, Y/forward, Z/up.  Public anchors use
the cooked Apricot convention X/right, Y/up, Z/forward.
"""

ATLAS_SIZE = 256

WHEELS = {
    "x": 0.94,
    "front_z": 1.63,
    "rear_z": -1.53,
    "arch_y": 0.42,
    "radius": 0.43,
}

DOOR = {
    "hinge": (1.015, 0.50, 0.84),
    "handle": (1.028, 1.00, -0.02),
    "rear_z": -0.22,
    "front_z": 0.84,
    "sill_y": 0.50,
    "belt_y": 1.02,
    "top_y": 1.55,
    "open_degrees": -68.0,
}

DRIVER = {
    "hip": (0.43, 0.73, 0.17),
    "knees": ((0.33, 0.67, 0.46), (0.53, 0.67, 0.46)),
    "wrists": ((0.32, 1.08, 0.58), (0.54, 1.08, 0.58)),
    "elbows": ((0.22, 0.94, 0.30), (0.62, 0.94, 0.30)),
    "ankles": ((0.33, 0.47, 0.66), (0.53, 0.47, 0.66)),
    "handle_elbow": (0.69, 0.96, -0.01),
    "approach_x": 1.56,
    "floor_y": 0.44,
    "cushion_top_y": 0.73,
    "seat_back_z": -0.05,
    "dashboard_z": 0.68,
    "roof_inner_y": 1.54,
}

SHAPE = {
    "name": "Municipal Cruiser 91A",
    "length": 5.35,
    "width": 2.08,
    "body_bottom": 0.20,
    "hood_height": 1.01,
    "deck_height": 1.00,
    "beltline": 1.02,
    "body_roof": 1.58,
    "lightbar_height": 1.78,
    "wheelbase": 3.16,
    "track": 1.88,
    "arch_long_radius": 0.54,
    "arch_height_radius": 0.46,
    "cabin": (-1.25, 0.84),
    "flat_roof": (-0.96, 0.39),
    "triangle_budget": (900, 1500),
    "atlas": (256, 256, "RGBA"),
    "traits": [
        "formal square 1991 full-size four-door notchback silhouette",
        "long nearly flat hood and deck with restrained faceted shoulders",
        "upright framed greenhouse and genuinely hollow visible cabin",
        "faceted chrome bumpers, sealed-beam lamps, and simple push bar",
        "twin A-pillar spotlights and low-profile red/blue roof lightbar",
    ],
}

# Inclusive image-space cells.  Two-pixel gutters keep nearest-filter sampling
# away from boundaries.  These rectangles never move after the final UV cook.
REGIONS = {
    "BODY_SIDE": (4, 4, 124, 64),
    "BODY_TOP": (128, 4, 252, 64),
    "DOOR_WHITE": (4, 68, 92, 128),
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
    "BODY_SIDE": ((1, 2), ((-2.675, 2.675), (0.20, 1.08))),
    "BODY_TOP": ((0, 1), ((-1.04, 1.04), (-2.675, 2.675))),
    "DOOR_WHITE": ((1, 2), ((-1.25, 0.84), (0.48, 1.02))),
    "POLICE": ((1, 2), ((-0.22, 0.84), (0.53, 1.00))),
    "FRONT": ((0, 2), ((-1.04, 1.04), (0.20, 1.04))),
    "REAR": ((0, 2), ((-1.04, 1.04), (0.20, 1.04))),
}

PANE_FILES = (
    "windshield",
    "rear_glass",
    "passenger_glass",
    "driver_glass",
    "driver_rear_glass",
    "passenger_rear_glass",
)

# Exact cooked-coordinate receiver volumes for shared runtime lamp profiles.
LAMPS = {
    "headlights": {
        "bounds": (
            ((-0.94, 0.70, 2.640), (-0.68, 0.88, 2.675)),
            ((-0.65, 0.70, 2.640), (-0.42, 0.88, 2.675)),
            ((0.42, 0.70, 2.640), (0.65, 0.88, 2.675)),
            ((0.68, 0.70, 2.640), (0.94, 0.88, 2.675)),
        ),
        "atlas": "HEADLIGHT",
    },
    "brakelights": {
        "bounds": (
            ((-0.94, 0.68, -2.675), (-0.58, 0.90, -2.642)),
            ((0.58, 0.68, -2.675), (0.94, 0.90, -2.642)),
        ),
        "atlas": "TAIL_RED",
    },
    "reverse": {
        "bounds": (
            ((-0.56, 0.68, -2.675), (-0.39, 0.90, -2.642)),
            ((0.39, 0.68, -2.675), (0.56, 0.90, -2.642)),
        ),
        "atlas": "REVERSE",
    },
}

LIGHTBAR = {
    "red": {
        "bounds_min": (-0.79, 1.65, -0.235),
        "bounds_max": (-0.06, 1.78, 0.015),
        "atlas": "LIGHTBAR_RED",
    },
    "blue": {
        "bounds_min": (0.06, 1.65, -0.235),
        "bounds_max": (0.79, 1.78, 0.015),
        "atlas": "LIGHTBAR_BLUE",
    },
    "base": {
        "bounds_min": (-0.82, 1.62, -0.250),
        "bounds_max": (0.82, 1.65, 0.030),
    },
}
