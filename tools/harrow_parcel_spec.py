"""HARROW PARCEL: original early-1990s short-bonnet commercial panel van.

Source coordinates: X across, Y up, Z forward (Blender swaps Y/Z).
No borrowed body silhouette, roof accessories, company text, or wheel geometry.
"""
ATLAS_SIZE = 256
WHEELS = dict(x=.99, front_z=1.62, rear_z=-1.48, arch_y=.43)
SHAPE = dict(length=5.40, width=2.26, shell_width=2.08, roof=2.38,
             hood_front=1.22, hood_rear=1.34, belt=1.25,
             cabin=(-.12, 1.46), cargo=(-2.54, -.12),
             roof_front=.72, arch_long_radius=.52, arch_height_radius=.43,
             pocket_inner_x=.54, triangle_budget=(650, 1300))
REGIONS = {
    "SIDE": (4, 4, 164, 100), "TOP": (168, 4, 252, 100),
    "FRONT": (4, 104, 84, 168), "REAR": (88, 104, 168, 224),
    "SCREEN": (172, 104, 252, 164), "RUBBER": (172, 168, 212, 224),
    "SHADOW": (216, 168, 252, 224), "METAL": (4, 172, 40, 224),
    "TEAL": (44, 172, 84, 224), "CREAM": (4, 228, 124, 252),
    "DARK": (128, 228, 252, 252),
}
BOUNDS = {
    "SIDE": ((-2.54, 2.54), (.26, 2.38)),
    "TOP": ((-1.04, 1.04), (-2.54, 2.54)),
    "FRONT": ((-1.04, 1.04), (.26, 1.34)),
    "REAR": ((-1.04, 1.04), (.26, 2.38)),
    "SCREEN": ((-1.04, 1.04), (1.34, 2.38)),
}

# Native cooked coordinates, mirrored across X. Profile rectangles are inset
# within the actual nearest-filtered pixels so rubber/amber never glows.
LAMPS = {
    'headlight': dict(face_z=2.54, paint_abs_x=(.595,.915), paint_y=(.84,1.09),
                      profile_rect=(.615,.86,.89,1.07,2.53,2.55)),
    'rear_red': dict(face_z=-2.54, paint_abs_x=(.91,1.005), paint_y=(.91,1.16),
                     profile_rect=(.925,.94,.99,1.13,-2.55,-2.53)),
}


def roof(z):
    if z <= .72:
        return 2.38
    if z <= 1.46:
        return 2.38 - (z-.72) * (1.04/.74)
    return 1.34 - (z-1.46) * (.12/1.08)
