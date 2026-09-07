"""Original ribbed box trailer. Meters, X across, Y up, +Z forward.

Two tandem rear axles share the standard four-wheel renderer. Kingpin is toward
+Z. Landing gear mounts only; gameplay draws the retractable legs separately.
"""
from harrow_hauler_spec import ATLAS_SIZE, REGIONS
WHEELS=dict(x=1.10, arch_y=.50, front_z=-3.0, rear_z=-4.2, radius=.50)
COUPLING=dict(x=0.,y=1.28,z=4.0)
SHAPE=dict(length=10.,width=2.50,roof=3.80,floor=1.40,
           wheelbase=1.20,track=2.20,landing_gear_z=2.40,landing_gear_x=.85,
           triangle_budget=(450,1300),
           identity=['long ivory ribbed box','oxblood freight stripe','tandem rear axles',
                     'double rear cargo doors','front kingpin and stowed landing gear'])
BOUNDS={'SIDE':((-5.,5.),(.28,3.8)), 'TOP':((-1.25,1.25),(-5.,5.)),
        'FRONT':((-1.25,1.25),(.28,3.8)), 'REAR':((-1.25,1.25),(.28,3.8))}

LAMPS={
 'rear_red':dict(face_z=-5.03,paint_abs_x=(.55,.91),paint_y=(1.13,1.27),
                profile_rect=(.56,1.14,.90,1.26,-5.04,-5.02)),
}
