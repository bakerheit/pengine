"""Municipal Cruiser 91-B transitional-aero police sedan contract.

Public/runtime coordinates are X/right, Y/up, Z/forward.  The Blender source
uses X/right, Y/forward, Z/up and the exporter performs the axis conversion.
"""

ATLAS_SIZE = 256

WHEELS = {
    "x": 0.94,
    "front_z": 1.63,
    "rear_z": -1.53,
    "arch_y": 0.42,
    "radius": 0.34,
}

# Opening driver-front door on +X.  These are cooked/runtime coordinates.
DOOR = {
    "hinge": (1.012, 0.40, 0.82),
    "handle": (1.018, 0.88, -0.08),
    "rear_z": -0.30,
    "front_z": 0.82,
    "sill_y": 0.38,
    "top_y": 1.52,
    "open_degrees": -65.0,
}

DRIVER = {
    "hip": (0.43, 0.72, -0.12),
    "wrists": ((0.32, 0.96, 0.43), (0.54, 0.96, 0.43)),
    "ankles": ((0.33, 0.44, 0.52), (0.53, 0.44, 0.52)),
    "knees": ((0.33, 0.90, 0.24), (0.53, 0.90, 0.24)),
    "elbows": ((0.09, 0.75, -0.04), (0.77, 0.75, -0.04)),
    "floor_y": 0.38,
    "cushion_top_y": 0.64,
    "back_z": -0.34,
    "dashboard_z": 0.58,
    "roof_inner_y": 1.48,
    "approach_x": 1.06,
}

SHAPE = {
    "name": "Municipal Cruiser 91-B",
    "slug": "municipal_cruiser_91b",
    "length": 5.30,
    "shell_width": 2.03,
    "overall_width": 2.08,
    "body_bottom": 0.20,
    "beltline": 1.02,
    "hood_height": 0.98,
    "deck_height": 0.94,
    "roof_height": 1.56,
    "lightbar_height": 1.77,
    "wheelbase": 3.16,
    "track": 1.88,
    "cabin": (-1.30, 0.84),
    "roof": (-0.98, 0.34),
    "arch_long_radius": 0.54,
    "arch_height_radius": 0.45,
    "triangle_budget": (900, 1500),
    "atlas": (256, 256, "RGBA"),
    "traits": [
        "full-size four-door notchback with a clipped formal trunk",
        "low sloped hood, raked windshield and gently faceted aero shoulders",
        "flush rectangular composite lamps and restrained black rub strips",
        "broad C pillars, low push bar and split red/blue roof lightbar",
        "chunky PSX and early-PS2 massing with deliberate visible facets",
    ],
}

# Inclusive pixel rectangles.  Every receiver has a two-pixel gutter.
REGIONS = {
    "BODY_SIDE": (4, 4, 252, 60),
    "BODY_TOP": (4, 64, 84, 180),
    "BODY_FRONT": (88, 64, 168, 104),
    "BODY_REAR": (172, 64, 252, 104),
    "GLASS_SIDE": (88, 108, 252, 140),
    "GLASS_FRONT": (88, 144, 168, 176),
    "GLASS_REAR": (172, 144, 252, 176),
    "NAVY": (4, 184, 84, 220),
    "WHITE": (4, 224, 84, 252),
    "POLICE_DRIVER": (88, 180, 168, 206),
    "POLICE_PASSENGER": (172, 180, 252, 206),
    "RUBBER": (88, 210, 112, 252),
    "METAL": (116, 210, 140, 252),
    "INTERIOR": (144, 210, 168, 252),
    "SEAM": (172, 210, 196, 232),
    "HEADLIGHT": (200, 210, 224, 228),
    "AMBER": (228, 210, 252, 228),
    "TAIL": (172, 236, 196, 252),
    "REVERSE": (200, 232, 224, 252),
    "LIGHTBAR_RED": (228, 232, 240, 252),
    "LIGHTBAR_BLUE": (242, 232, 252, 252),
}

# Blender/source projections: axis 0=X/right, 1=Y/forward, 2=Z/up.
UV_PROJECTIONS = {
    "BODY_SIDE": ((1, 2), ((-2.58, 2.58), (0.20, 1.04))),
    "BODY_TOP": ((0, 1), ((-1.02, 1.02), (-2.58, 2.58))),
    "BODY_FRONT": ((0, 2), ((-1.02, 1.02), (0.20, 1.02))),
    "BODY_REAR": ((0, 2), ((-1.02, 1.02), (0.20, 1.02))),
    "GLASS_SIDE": ((1, 2), ((-1.28, 0.80), (1.04, 1.50))),
    "GLASS_FRONT": ((0, 2), ((-0.88, 0.88), (1.04, 1.50))),
    "GLASS_REAR": ((0, 2), ((-0.86, 0.86), (1.04, 1.50))),
    "POLICE_DRIVER": ((1, 2), ((-0.22, 0.65), (0.61, 0.91))),
    "POLICE_PASSENGER": ((1, 2), ((-0.22, 0.65), (0.61, 0.91))),
}

HEADLIGHTS = {
    "plane_z": 2.585,
    "y": (0.56, 0.79),
    "x": ((-0.94, -0.50), (0.50, 0.94)),
}

BRAKELIGHTS = {
    "plane_z": -2.585,
    "y": (0.56, 0.80),
    "x": ((-0.94, -0.58), (0.58, 0.94)),
}

LIGHTBAR = {
    "red": {
        "bounds_min": (-0.76, 1.62, -0.18),
        "bounds_max": (-0.08, 1.77, 0.12),
        "atlas": "LIGHTBAR_RED",
    },
    "blue": {
        "bounds_min": (0.08, 1.62, -0.18),
        "bounds_max": (0.76, 1.77, 0.12),
        "atlas": "LIGHTBAR_BLUE",
    },
    "base": {
        "bounds_min": (-0.80, 1.58, -0.20),
        "bounds_max": (0.80, 1.62, 0.14),
    },
}

GLASS_NAMES = (
    "windshield",
    "rear_glass",
    "passenger_glass",
    "driver_glass",
    "driver_rear_glass",
    "passenger_rear_glass",
)
