"""Original 1994 Orison Cinder GT: shared shape and 256-pixel atlas contract.

Blender coordinates: X across the car, +Y toward the nose, Z up.  REGIONS
are inclusive image pixel bounds (left, top, right, bottom).  Broad body
panels share a continuous projection; semantic details use their full cell.
"""

ATLAS_SIZE = 256
ATLAS_INSET = 1.0

REGIONS = {
    "SIDE": (4, 4, 171, 77),
    "TOP": (176, 4, 251, 171),
    "GLASS": (4, 84, 107, 137),
    "SHADOW": (114, 84, 137, 107),
    "BLACK": (144, 84, 167, 107),
    "RUBBER": (114, 114, 167, 137),
    "METAL": (4, 144, 51, 171),
    "HEADLIGHT": (58, 144, 109, 171),
    "AMBER": (116, 144, 139, 171),
    "RED": (146, 144, 169, 171),
    "REVERSE": (4, 178, 51, 205),
    "BADGE": (58, 178, 81, 201),
    "PLATE": (88, 178, 167, 205),
}

# SIDE uses (Blender Y, Z), TOP uses (Blender X, Y).  The second axis
# increases upwards within the image cell, matching Blender's UV convention.
BOUNDS = {
    "SIDE": ((-2.21, 2.21), (0.15, 0.90)),
    "TOP": ((-0.95, 0.95), (-2.21, 2.21)),
}

SHAPE = {
    "make": "Orison",
    "model": "Cinder GT",
    "year": 1994,
    "body_length": 4.42,
    "body_width": 1.90,
    "roof_height": 1.245,
    "wheelbase": 2.54,
    "half_track": 0.80,
    "front_axle_y": 1.30,
    "rear_axle_y": -1.24,
    "wheel_center_z": 0.34,
    "wheel_radius": 0.326,
    "front_arch_radius_y": 0.44,
    "front_arch_radius_z": 0.405,
    "rear_arch_radius_y": 0.415,
    "rear_arch_radius_z": 0.390,
    "body_triangle_budget": (650, 1600),
    "signature": (
        "Long low front-engine hood with closed pop-up headlight doors",
        "Compact fastback cabin and broad rear shoulders",
        "Deep petrol teal paint, black lower trim and a small rear wing",
        "Amber front markers, red rear lamps and an original O emblem",
    ),
}


def cell_xy(name, u, v, inset=ATLAS_INSET):
    """Image pixel coordinate for normalized UV (0..1, bottom to top)."""
    x0, y0, x1, y1 = REGIONS[name]
    return (
        x0 + inset + u * (x1 - x0 - 2 * inset),
        y1 - inset - v * (y1 - y0 - 2 * inset),
    )


def pixel_to_uv(x, y):
    """Map a pixel center in a top-left image to Blender UV space."""
    return ((x + 0.5) / ATLAS_SIZE, 1.0 - (y + 0.5) / ATLAS_SIZE)


def cell_uv(name, u, v, inset=ATLAS_INSET):
    """Map a whole small semantic face into its cell with a safe gutter."""
    return pixel_to_uv(*cell_xy(name, u, v, inset))


def project_xy(name, a, b, inset=ATLAS_INSET):
    """Image pixel coordinate for the shared broad-panel projection."""
    (a0, a1), (b0, b1) = BOUNDS[name]
    return cell_xy(name, (a - a0) / (a1 - a0), (b - b0) / (b1 - b0), inset)


def project_uv(name, a, b, inset=ATLAS_INSET):
    """Continuous Blender UV for SIDE(y,z) or TOP(x,y)."""
    return pixel_to_uv(*project_xy(name, a, b, inset))
