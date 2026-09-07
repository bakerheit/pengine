"""HALCYON SOVEREIGN: original formal 8.2m white stretch limousine."""
ATLAS_SIZE=256
WHEELS=dict(x=.89,front_z=2.85,rear_z=-2.70,arch_y=.41,radius=.36)
SHAPE=dict(length=8.20,width=2.26,shell_width=2.06,roof=1.68,
    body_bottom=.23,hood_front=.94,hood_rear=1.07,belt=1.07,
    cabin=(-2.72,1.70),roof_ends=(-2.26,1.22),roof_half_width=.86,
    arch_long_radius=.49,arch_height_radius=.43,pocket_inner_x=.48,
    wheelbase=5.55,track=1.78,triangle_budget=(650,1300),
    identity=['formal three-box profile','long passenger cabin insert','dark vinyl roof',
    'chrome upright grille and bumpers','separate chauffeur and rear seating'])
REGIONS={'SIDE':(4,4,156,76),'TOP':(160,4,252,76),
    'CABIN':(4,80,156,138),'WINDSHIELD':(160,80,252,138),
    'FRONT':(4,142,124,202),'REAR':(128,142,252,202),
    'BACK_GLASS':(4,206,112,250),'CLADDING':(116,206,156,250),
    'BLACK':(160,206,188,250),'METAL':(192,206,220,250),'SHADOW':(224,206,252,250)}
BOUNDS={'SIDE':((-4.0,4.0),(.23,1.07)),'TOP':((-1.03,1.03),(-4.0,4.0)),
    'CABIN':((-2.72,1.70),(1.07,1.64)),'WINDSHIELD':((-.97,.97),(1.07,1.64)),
    'BACK_GLASS':((-.97,.97),(1.07,1.64)),
    'FRONT':((-1.03,1.03),(.23,1.07)),'REAR':((-1.03,1.03),(.23,1.07))}
LAMPS={'headlight':(.59,.73,.84,.85,4.0),'rear_red':(.72,.67,.86,.83,-4.0)}
DOOR=dict(hinge=(1.014,.37,1.60),handle=(1.012,.96,.30),rear_z=.18,
    front_z=1.60,sill_y=.37,inner_x=.86,open_degrees=-65)
DRIVER=dict(left_x=.46,hip=(.46,.65,.67),wheel_y=1.06,wheel_z=1.20,
    pedals_x=(.36,.56),pedal_y=.37,pedal_z=1.47)
