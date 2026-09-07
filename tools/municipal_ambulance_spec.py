"""Shape and semantic-atlas contract for the Municipal ambulance."""

ATLAS_SIZE = 256

# Source coordinates are X/right, Y/up, Z/forward after the Blender cook.
WHEELS = dict(x=0.98, front_z=1.78, rear_z=-1.58, arch_y=0.47)
SHAPE = dict(
    length=6.10,
    width=2.30,
    roof=2.62,
    hood=1.22,
    module=(-3.00, 0.20),
    arch_radius=0.60,
    triangle_budget=(650, 1300),
)

REGIONS = {
    "CAB_BODY": (4, 4, 84, 64),
    "MODULE_WHITE": (88, 4, 172, 64),
    "ROOF": (176, 4, 252, 64),
    "STRIPE": (4, 68, 116, 94),
    "GLASS": (120, 68, 196, 136),
    "REAR_DOOR": (200, 68, 252, 136),
    "SIDE_DOOR": (4, 98, 72, 160),
    "MEDICAL": (76, 140, 140, 204),
    "EMS_TEXT": (144, 140, 252, 164),
    "GRILLE": (4, 164, 72, 196),
    "METAL": (4, 200, 72, 220),
    "HEADLIGHT": (76, 208, 112, 236),
    "AMBER": (116, 208, 144, 236),
    "TAIL_RED": (148, 168, 176, 204),
    "REVERSE": (180, 168, 208, 204),
    "LIGHT_RED": (212, 168, 232, 204),
    "LIGHT_BLUE": (236, 168, 252, 204),
    "BLACK": (76, 240, 116, 252),
    "CLADDING": (120, 240, 164, 252),
    "INTERIOR": (168, 208, 208, 236),
    "SEAM": (212, 208, 232, 236),
    "SHADOW": (236, 208, 252, 236),
}
