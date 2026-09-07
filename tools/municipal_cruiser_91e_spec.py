"""Municipal Cruiser 91E Highway shape, articulation, and atlas contract.

Blender source coordinates are X/right, Y/forward, Z/up. Public anchors use
Apricot cooked coordinates: X/right, Y/up, +Z/nose.
"""

ATLAS_SIZE = 256

WHEELS = {
    "x": 0.97,
    "front_z": 1.72,
    "rear_z": -1.60,
    "arch_y": 0.41,
    "radius": 0.41,
}

DOOR = {
    "hinge": (1.045, 0.47, 0.78),
    "handle": (1.065, 0.98, -0.05),
    "rear_z": -0.24,
    "front_z": 0.78,
    "sill_y": 0.47,
    "belt_y": 0.99,
    "top_y": 1.47,
    "open_degrees": -70.0,
}

DRIVER = {
    "hip": (0.44, 0.70, 0.14),
    "knees": ((0.34, 0.67, 0.43), (0.54, 0.67, 0.43)),
    "wrists": ((0.32, 1.04, 0.56), (0.55, 1.04, 0.56)),
    "elbows": ((0.19, 0.90, 0.27), (0.67, 0.90, 0.27)),
    "ankles": ((0.34, 0.45, 0.68), (0.54, 0.45, 0.68)),
    "handle_elbow": (0.72, 0.94, -0.05),
    "approach_x": 1.60,
    "floor_y": 0.40,
    "cushion_top_y": 0.70,
    "seat_back_z": -0.08,
    "dashboard_z": 0.68,
    "roof_inner_y": 1.47,
}

SHAPE = {
    "name": "Municipal Cruiser 91E Highway",
    "length": 5.56,
    "width": 2.16,
    "body_bottom": 0.18,
    "hood_height": 0.98,
    "deck_height": 0.97,
    "beltline": 0.99,
    "body_roof": 1.50,
    "lightbar_height": 1.70,
    "wheelbase": 3.32,
    "track": 1.94,
    "arch_long_radius": 0.56,
    "arch_height_radius": 0.47,
    "cabin": (-1.31, 0.78),
    "flat_roof": (-0.99, 0.28),
    "triangle_budget": (950, 1800),
    "atlas": (256, 256, "RGBA"),
    "traits": [
        "long-hood low-roof 1991 highway interceptor stance",
        "broad faceted shoulders and a long formal rear deck",
        "four inset rectangular lamps and a deep horizontal grille",
        "heavy wraparound push bumper with twin A-pillar spotlights",
        "dual whip antennae and a full period red-blue roof lightbar",
        "cream over dark-brown generic highway-patrol livery",
    ],
}

# Inclusive semantic cells. These are frozen before the 4x imagegen edit.
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
    "BODY_SIDE": ((1, 2), ((-2.78, 2.78), (0.18, 1.03))),
    "BODY_TOP": ((0, 1), ((-1.08, 1.08), (-2.78, 2.78))),
    "DOOR_WHITE": ((1, 2), ((-1.31, 0.78), (0.47, 0.99))),
    "POLICE": ((1, 2), ((-0.24, 0.78), (0.51, 0.97))),
    "FRONT": ((0, 2), ((-1.08, 1.08), (0.18, 1.00))),
    "REAR": ((0, 2), ((-1.08, 1.08), (0.18, 1.00))),
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
            ((-0.88, 0.57, 2.775), (-0.50, 0.76, 2.780)),
            ((-0.44, 0.57, 2.775), (-0.12, 0.76, 2.780)),
            ((0.12, 0.57, 2.775), (0.44, 0.76, 2.780)),
            ((0.50, 0.57, 2.775), (0.88, 0.76, 2.780)),
        ),
        "atlas": "HEADLIGHT",
    },
    "brakelights": {
        "bounds": (
            ((-0.95, 0.58, -2.780), (-0.55, 0.80, -2.775)),
            ((0.55, 0.58, -2.780), (0.95, 0.80, -2.775)),
        ),
        "atlas": "TAIL_RED",
    },
    "reverse": {
        "bounds": (
            ((-0.52, 0.58, -2.780), (-0.36, 0.80, -2.775)),
            ((0.36, 0.58, -2.780), (0.52, 0.80, -2.775)),
        ),
        "atlas": "REVERSE",
    },
}

LIGHTBAR = {
    "red": {
        "bounds_min": (-0.76, 1.56, -0.28),
        "bounds_max": (-0.05, 1.70, 0.02),
        "atlas": "LIGHTBAR_RED",
    },
    "blue": {
        "bounds_min": (0.05, 1.56, -0.28),
        "bounds_max": (0.76, 1.70, 0.02),
        "atlas": "LIGHTBAR_BLUE",
    },
    "base": {
        "bounds_min": (-0.80, 1.52, -0.30),
        "bounds_max": (0.80, 1.56, 0.04),
    },
}
