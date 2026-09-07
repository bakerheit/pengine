"""VESPER SCYTHE: original angular mid-engine exotic with raised rear wing."""
ATLAS_SIZE=256
WHEELS=dict(x=.87,front_z=1.30,rear_z=-1.25,arch_y=.36,radius=.34)
SHAPE=dict(length=4.65,width=2.18,shell_width=2.04,roof=1.19,body_bottom=.18,
    hood_front=.53,hood_rear=.76,belt=.86,cabin=(-1.20,1.15),roof_ends=(-.63,.12),
    roof_half_width=.62,arch_long_radius=.46,arch_height_radius=.40,pocket_inner_x=.43,
    wheelbase=2.55,track=1.74,triangle_budget=(650,1300),
    identity=['sharp low wedge nose','broad rear haunches','short angular canopy',
    'sculpted side intake pockets','louvered engine deck and raised wing'])
REGIONS={'SIDE':(4,4,156,76),'TOP':(160,4,252,76),
    'CABIN':(4,80,156,138),'WINDSHIELD':(160,80,252,138),
    'FRONT':(4,142,124,202),'REAR':(128,142,252,202),
    'BACK_GLASS':(4,206,112,250),'CLADDING':(116,206,156,250),
    'BLACK':(160,206,188,250),'METAL':(192,206,220,250),'SHADOW':(224,206,252,250)}
BOUNDS={'SIDE':((-2.245,2.245),(.18,.91)),'TOP':((-1.02,1.02),(-2.245,2.245)),
    'CABIN':((-1.20,1.15),(.688,1.13)),'WINDSHIELD':((-.88,.88),(.688,1.13)),
    'BACK_GLASS':((-.88,.88),(.688,1.13)),
    'FRONT':((-1.02,1.02),(.18,.64)),'REAR':((-1.02,1.02),(.18,.91))}
LAMPS={'headlight':(.53,.36,.77,.43,2.245),'rear_red':(.62,.57,.83,.68,-2.245)}

DOOR=dict(hinge=(.985,.29,.78),handle=(.97,.68,-.43),rear_z=-.54,
          front_z=.78,sill_y=.29,inner_x=.85,open_degrees=-65)
DRIVER=dict(left_x=.43,hip=(.43,.42,-.24),wheel_y=.72,wheel_z=.15,
            pedals_x=(.33,.53),pedal_y=.26,pedal_z=.55)
