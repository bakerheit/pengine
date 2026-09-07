"""ALDER PIP — original late-1980s three-door compact city hatchback.

A short sloping bonnet, upright cut-off hatch, tall trapezoid cabin and long
front doors with triangular quarter glass distinguish this from ZIP's targa,
Ridge's tall SUV body and Wayfarer's long wagon. Wheel-less static body.
"""
ATLAS_SIZE=256
WHEELS=dict(x=.72,front_z=1.10,rear_z=-1.26,arch_y=.34,radius=.31)
SHAPE=dict(length=3.80,width=1.94,shell_width=1.72,roof=1.52,
    body_bottom=.19,hood_front=.80,hood_rear=.88,belt=.88,
    cabin=(-1.82,.65),roof_ends=(-1.58,.18),roof_half_width=.69,
    arch_long_radius=.43,arch_height_radius=.39,pocket_inner_x=.38,
    wheelbase=2.36,track=1.44,triangle_budget=(650,1300),
    identity=['short sloping bonnet','upright chopped hatch','long front doors and triangular quarter glass',
    'slim black bumpers','faded mustard paint'])
REGIONS={'SIDE':(4,4,156,76),'TOP':(160,4,252,76),
    'CABIN':(4,80,156,138),'WINDSHIELD':(160,80,252,138),
    'FRONT':(4,142,124,202),'REAR':(128,142,252,202),
    'BACK_GLASS':(4,206,112,250),'CLADDING':(116,206,156,250),
    'BLACK':(160,206,188,250),'METAL':(192,206,220,250),'SHADOW':(224,206,252,250)}
BOUNDS={'SIDE':((-1.82,1.82),(.19,.88)),'TOP':((-.86,.86),(-1.82,1.82)),
    'CABIN':((-1.82,.65),(.88,1.48)),'WINDSHIELD':((-.80,.80),(.88,1.48)),
    'BACK_GLASS':((-.80,.80),(.88,1.48)),
    'FRONT':((-.86,.86),(.19,.88)),'REAR':((-.86,.86),(.19,.88))}
LAMPS={'headlight':(.48,.59,.68,.70,1.82),'rear_red':(.62,.62,.71,.75,-1.82)}

# Articulated override: static closed body plus separately cooked open body/door.
DOOR=dict(hinge=(.824,.32,.58),handle=(.824,.78,-.43),rear_z=-.55,
          front_z=.58,sill_y=.32,inner_x=.73,open_degrees=-65)
DRIVER=dict(left_x=.38,hip=(.38,.56,-.25),wheel_y=.92,wheel_z=.26,
            pedals_x=(.29,.47),pedal_y=.31,pedal_z=.47)
