"""Original 1988 ALDER RIDGE two-door short-wheelbase SUV shape contract."""
ATLAS_SIZE = 256
WHEELS = dict(x=.98, front_z=1.24, rear_z=-1.40, arch_y=.39)
SHAPE = dict(length=4.40, width=2.20, roof=2.04, sill=.32,
             hood_front=1.16, hood_rear=1.25, cabin=(-2.08,.65),
             roof_ends=(-1.96,.36), roof_half_width=.86,
             arch_long_radius=.49, arch_height_radius=.43,
             triangle_budget=(650,1300),
             signature=['short two-door body', 'upright enclosed rear cabin',
                        'integrated flared arches', 'square lamps',
                        'split side glass and plain practical tailgate'])
REGIONS = {
    'SIDE': (4,4,156,76), 'TOP': (160,4,252,76),
    'CABIN': (4,80,156,138), 'WINDSHIELD': (160,80,252,138),
    'FRONT': (4,142,124,202), 'REAR': (128,142,252,202),
    'BACK_GLASS': (4,206,112,250), 'CLADDING': (116,206,156,250),
    'BLACK': (160,206,188,250), 'METAL': (192,206,220,250),
    'SHADOW': (224,206,252,250),
}
BOUNDS = {
    'SIDE': ((-2.08,2.08),(.32,1.25)),
    'TOP': ((-1.06,1.06),(-2.08,2.08)),
    'CABIN': ((-2.08,.65),(1.25,1.99)),
    'WINDSHIELD': ((-.95,.95),(1.25,1.99)),
    'BACK_GLASS': ((-.95,.95),(1.25,1.99)),
    'FRONT': ((-1.06,1.06),(.32,1.25)),
    'REAR': ((-1.06,1.06),(.32,1.25)),
}

# Model-space mask proposals for the shared profile owner. X is absolute;
# both sides mirror it. The real receiver faces lie at Z = +/-2.08.
# Masks sit just inside the painted texel boundaries to exclude dark bezels.
LAMPS = {
    'front_face_z': 2.08, 'rear_face_z': -2.08,
    'headlight_rects': [(.66,.845,.91,1.045,2.07,2.09)],
    'rear_red_rects': [(.83,.735,.94,.83,-2.09,-2.07),
                       (.83,.96,.94,1.10,-2.09,-2.07)],
    'painted_headlights': [(.645,.825,.925,1.065)],
    'painted_rear_red': [(.82,.72,.95,.85),(.82,.94,.95,1.12)],
    'painted_reverse': [(.82,.85,.95,.94)],
}
