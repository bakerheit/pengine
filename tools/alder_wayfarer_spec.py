"""1984 Alder Wayfarer: original family wagon, not a converted truck body."""
ATLAS_SIZE = 256
WHEELS = dict(x=1.0, front_z=1.70, rear_z=-1.55, arch_y=0.43)
SHAPE = dict(length=5.50, width=2.30, roof=1.77, rack=1.88,
             hood_front=1.04, hood_rear=1.13, cabin=(-2.46, 0.80),
             arch_long_radius=0.59, arch_height_radius=0.51,
             triangle_budget=(650, 1300))
REGIONS = {
    "SIDE": (4, 4, 128, 100), "PAINT": (132, 4, 252, 100),
    "GLASS": (4, 104, 96, 160), "CREAM": (100, 104, 164, 160),
    "RUBBER": (168, 104, 208, 160), "GRILLE": (212, 104, 252, 160),
    "TAILGATE": (4, 164, 108, 216), "HEADLIGHT": (112, 164, 152, 188),
    "AMBER": (156, 164, 184, 188), "RED": (188, 164, 216, 188),
    "REVERSE": (220, 164, 252, 188), "CHROME": (112, 192, 180, 216),
    "SHADOW": (184, 192, 252, 216), "BLACK": (4, 224, 80, 252),
    "WINDSHIELD": (84, 224, 164, 252), "REAR_GLASS": (168, 224, 252, 252),
}

# Final atlas islands for details baked into the shell, not floating polygons.
# Source cells above remain the small editable lens/glass swatches for baking.
SURFACE_REGIONS = {
    "BUMPER_FACE": (212, 104, 252, 160),
    "FRONT_FACE": (112, 164, 252, 216),
    "CHROME": (4, 224, 40, 252),
    "SHADOW": (44, 224, 80, 252),
}
SURFACE_BOUNDS = {
    "BUMPER_FACE": ((-1.07, 1.07), (.23, .42)),
    "GLASS": ((-2.46, .80), (1.13, 1.70)),
    "WINDSHIELD": ((-.90, .90), (1.13, 1.70)),
    "REAR_GLASS": ((-.90, .90), (1.13, 1.70)),
    "FRONT_FACE": ((-1.04, 1.04), (.25, 1.13)),
    "TAILGATE": ((-1.04, 1.04), (.25, 1.13)),
}


def surface_patches():
    def rectangle(x0, y0, x1, y1):
        return [(x0,y0),(x1,y0),(x1,y1),(x0,y1)]
    patches = {
        "BUMPER_FACE": [("CHROME", rectangle(-1.,.30,1.,.35))],
        "GLASS": [("GLASS", p) for p in (
            [(.66,1.18),(.24,1.65),(-.38,1.65),(-.38,1.18)],
            [(-.50,1.18),(-.50,1.65),(-1.32,1.65),(-1.32,1.18)],
            [(-1.44,1.18),(-1.44,1.65),(-2.03,1.65),(-2.35,1.18)])],
        "WINDSHIELD": [("WINDSHIELD", [(-.81,1.19),(.81,1.19),(.74,1.65),(-.74,1.65)])],
        "REAR_GLASS": [("REAR_GLASS", [(-.80,1.19),(.80,1.19),(.73,1.65),(-.73,1.65)])],
        "FRONT_FACE": [("GRILLE", rectangle(-.45,.60,.45,.91))],
        "TAILGATE": [("TAILGATE", rectangle(-.66,.43,.66,1.07))],
    }
    for side in (-1, 1):
        for mat, a, b, lo, hi in (("HEADLIGHT",.49,.83,.66,.93), ("AMBER",.86,1.,.66,.93)):
            a,b=sorted((side*a,side*b))
            patches["FRONT_FACE"].append((mat,rectangle(a,lo,b,hi)))
        for mat, lo, hi in (("RED",.49,.70),("REVERSE",.70,.79),("AMBER",.79,1.01)):
            a,b=sorted((side*.74,side*.99))
            patches["TAILGATE"].append((mat,rectangle(a,lo,b,hi)))
    return patches
