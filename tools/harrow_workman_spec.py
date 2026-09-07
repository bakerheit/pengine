"""Shape/UV contract for an ordinary single-cab, long-bed work pickup."""

ATLAS_SIZE = 256
WHEELS = dict(x=0.94, front_z=1.65, rear_z=-1.55, arch_y=0.45)
# Source X/up-Y/forward-Z; door constants mirrored by app/workman_door.h.
DOOR = dict(hinge=(.964,.52,.92), handle=(.97,1.09,-.20), rear_z=-.43,
            front_z=.92, sill_y=.52, top_y=1.93, open_degrees=-65)
DRIVER = dict(hip=(.43,.94,-.12), wrists=((.32,1.20,.44),(.54,1.20,.44)),
              ankles=((.33,.635,.60),(.53,.635,.60)), floor_y=.52,
              cushion_top_y=.80, back_z=-.37, dashboard_z=.70, roof_inner_y=1.93)
SHAPE = dict(length=5.40, width=2.22, roof=1.99, hood=1.25,
             bed_rail=1.22, bed_floor=0.65, arch_radius=0.58,
             cabin=(-0.50, 0.99), triangle_budget=(650, 1300))
REGIONS = {
    "BODY_SIDE": (4, 4, 124, 68), "BODY_TOP": (128, 4, 252, 68),
    "DOOR": (4, 72, 84, 144), "GLASS": (88, 72, 164, 144),
    "BED": (168, 72, 252, 144), "TAILGATE": (4, 148, 104, 196),
    "GRILLE": (108, 148, 188, 176), "METAL": (108, 180, 188, 196),
    "HEADLIGHT": (192, 148, 220, 172), "AMBER": (224, 148, 252, 172),
    "TAIL": (192, 176, 220, 208), "REVERSE": (224, 176, 252, 208),
    "BLACK": (4, 200, 48, 252), "CLADDING": (52, 200, 104, 252),
    "DASH": (108, 200, 160, 252), "SEAM": (164, 212, 204, 252),
    "SHADOW": (208, 212, 252, 252),
}
