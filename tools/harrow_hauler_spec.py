"""Original conventional day-cab semi. Native X across, Y up, +Z forward.

Geometry is in meters; runtime must preserve scale instead of fitting to a car.
"""
ATLAS_SIZE = 256
WHEELS = dict(x=1.10, arch_y=.50, front_z=2.05, rear_z=-2.05, radius=.50)
COUPLING = dict(x=0., y=1.28, z=-1.90)
SHAPE = dict(length=6.60, width=2.50, shell_width=2.36, roof=3.25,
             hood_front=1.65, hood_rear=1.80, rear_deck=1.25,
             cabin=(.05,1.40), wheelbase=4.10, track=2.20,
             arch_radius=.64, triangle_budget=(650,1300),
             identity=['long raised hood','upright split windshield','broad front fender crowns',
                       'twin vertical exhausts','exposed fifth wheel and short rear deck'])
REGIONS = {
 'SIDE':(2,2,126,126), 'TOP':(130,2,254,62), 'FRONT':(130,66,254,150),
 'REAR':(2,130,126,190), 'GLASS':(130,154,190,190), 'RED':(194,154,222,190),
 'AMBER':(226,154,254,190), 'METAL':(2,194,62,254),'DARK':(66,194,126,254),
 'PAINT':(130,194,190,254),'CREAM':(194,194,254,254)}
BOUNDS={'SIDE':((-3.3,3.3),(.28,3.25)), 'TOP':((-1.25,1.25),(-3.3,3.3)),
        'FRONT':((-1.25,1.25),(.28,3.25)), 'REAR':((-1.25,1.25),(.28,3.25))}

LAMPS={
 'headlight':dict(face_z=3.03,paint_abs_x=(.84,1.10),paint_y=(1.24,1.47),
                 profile_rect=(.85,1.25,1.09,1.46,3.02,3.04)),
 'rear_red':dict(face_z=-3.31,paint_abs_x=(.85,1.04),paint_y=(.66,.80),
                profile_rect=(.86,.67,1.03,.79,-3.32,-3.30)),
}
