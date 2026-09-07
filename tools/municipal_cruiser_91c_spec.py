"""Shape, articulation and model-space UV contract for cruiser attempt 91C."""

ATLAS_SIZE = 256

# Cooked coordinates: X right, Y up, Z forward.  The body remains wheel-less;
# Apricot attaches the existing shared wheel mesh at these four anchors.
WHEELS = {
    "x": 0.94,
    "front_z": 1.63,
    "rear_z": -1.53,
    "arch_y": 0.42,
    "radius": 0.373,
}

# +X is the driver's side.  These anchors are intentionally in cooked space
# so runtime articulation can consume them without a Blender-axis conversion.
DOOR = {
    "hinge": (1.006, 0.55, 0.88),
    "handle": (1.025, 1.00, 0.04),
    "front_z": 0.90,
    "rear_z": -0.12,
    "sill_y": 0.54,
    "belt_y": 0.98,
    "top_y": 1.49,
    "open_degrees": -68.0,
}

DRIVER = {
    "hip": (0.42, 0.73, 0.18),
    "wrists": ((0.31, 1.04, 0.56), (0.53, 1.04, 0.56)),
    "ankles": ((0.34, 0.43, 0.72), (0.54, 0.43, 0.72)),
    "knees": ((0.34, 0.90, 0.38), (0.54, 0.90, 0.38)),
    "elbows": ((0.10, 0.78, 0.16), (0.76, 0.78, 0.16)),
    "handle_elbow": (1.42, 0.94, 0.02),
    "approach_x": 1.08,
    "floor_y": 0.40,
    "cushion_top_y": 0.72,
    "back_z": -0.20,
    "dashboard_z": 0.69,
    "roof_inner_y": 1.47,
}

SHAPE = {
    "name": "Municipal Cruiser 91C",
    "year": 1991,
    "length": 5.42,
    "width": 2.10,
    "body_roof": 1.55,
    "lightbar_height": 1.77,
    "wheelbase": 3.16,
    "track": 1.88,
    "body_bottom": 0.18,
    "beltline": 0.98,
    "hood_height": 1.03,
    "deck_height": 1.00,
    "cabin": (-1.23, 0.90),
    "triangle_budget": (900, 1500),
    "atlas": (256, 256, "RGBA"),
    "traits": [
        "low wide 1991 pursuit-sedan stance",
        "long power-bulge hood and four inset rectangular headlamps",
        "formal four-door roof with a restrained fastback rear screen",
        "squared rear quarters and a short raised deck",
        "heavy black push bumper, A-pillar spotlight and whip antenna",
        "period red and blue roof lightbar",
        "chunky hand-built PSX and early-PS2 readability",
    ],
}

# Exact cooked-space lamp receivers used by the renderer and fit report.
LAMPS = {
    "headlights": {
        "x": ((-0.91, -0.50), (-0.46, -0.08),
              (0.08, 0.46), (0.50, 0.91)),
        "y": (0.58, 0.74),
        "z": (2.64, 2.70),
    },
    "brakelights": {
        "x": ((-0.91, -0.53), (0.53, 0.91)),
        "y": (0.58, 0.75),
        "z": (-2.70, -2.64),
    },
    "lightbar_red": {
        "x": (-0.70, -0.05), "y": (1.61, 1.77), "z": (-0.35, -0.14),
    },
    "lightbar_blue": {
        "x": (0.05, 0.70), "y": (1.61, 1.77), "z": (-0.35, -0.14),
    },
}

# Inclusive pixel rectangles.  Each final UV is inset two pixels so imagegen
# can change value structure without leaking across semantic receivers.
REGIONS = {
    "SIDE_DRIVER": (4, 4, 252, 46),
    "SIDE_PASSENGER": (4, 50, 252, 92),
    "BODY_TOP": (4, 96, 92, 196),
    "BODY_FRONT": (96, 96, 172, 140),
    "BODY_REAR": (176, 96, 252, 140),
    "GLASS_SIDE": (96, 144, 252, 174),
    "GLASS_FRONT": (96, 178, 170, 204),
    "GLASS_REAR": (174, 178, 252, 204),
    "BLACK": (4, 200, 36, 252),
    "METAL": (40, 200, 72, 252),
    "INTERIOR": (76, 208, 108, 252),
    "SEAT": (112, 208, 144, 252),
    "HEADLIGHT": (148, 208, 180, 228),
    "TAIL_RED": (184, 208, 216, 228),
    "TAIL_AMBER": (220, 208, 252, 228),
    "LIGHTBAR_RED": (148, 232, 180, 252),
    "LIGHTBAR_BLUE": (184, 232, 216, 252),
    "LENS_CLEAR": (220, 232, 252, 252),
}

# Blender source coordinates: X right, Y forward, Z up.
UV_PROJECTIONS = {
    "SIDE_DRIVER": ((1, 2), ((-2.71, 2.71), (0.18, 1.55))),
    "SIDE_PASSENGER": ((1, 2), ((-2.71, 2.71), (0.18, 1.55))),
    "BODY_TOP": ((0, 1), ((-1.05, 1.05), (-2.71, 2.71))),
    "BODY_FRONT": ((0, 2), ((-1.05, 1.05), (0.18, 1.10))),
    "BODY_REAR": ((0, 2), ((-1.05, 1.05), (0.18, 1.10))),
    "GLASS_SIDE": ((1, 2), ((-1.18, 0.86), (0.99, 1.50))),
    "GLASS_FRONT": ((0, 2), ((-0.92, 0.92), (0.99, 1.50))),
    "GLASS_REAR": ((0, 2), ((-0.92, 0.92), (0.99, 1.50))),
}

# Reversing the driver's side receiver keeps generic POLICE lettering readable
# from both exterior side cameras without sharing a mirrored word.
UV_FLIP_U = {"SIDE_DRIVER"}
