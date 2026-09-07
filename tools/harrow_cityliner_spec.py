"""HARROW CITYLINER: original 11-metre two-axle urban transit bus.

Shape contract: tall flat-front cab, subtly crowned roof, long passenger saloon,
short front engine-free nose and a long rear engine compartment. Four shared
wheel anchors; body contains no wheel geometry. Native X across, Y up, Z front.
"""
ATLAS_SIZE = 256
WHEELS = dict(x=1.12, front_z=3.0, rear_z=-2.6, arch_y=.55, radius=.48)
SHAPE = dict(length=11.0, width=2.88, shell_width=2.50, roof=3.10,
             hood_front=1.32, hood_rear=1.32, belt=1.45,
             cabin=(3.65,5.40), passenger_saloon=(-4.7,3.65),
             roof_front=5.02, front_overhang=2.50, rear_overhang=2.90,
             arch_long_radius=.68, arch_height_radius=.57,
             pocket_inner_x=.62, triangle_budget=(650,1300))
REGIONS = {'LEFT':(4,4,252,66),'RIGHT':(4,70,252,132),
           'FRONT':(4,136,100,252),'REAR':(104,136,192,252),
           'TOP':(196,136,252,196),'RUBBER':(196,200,220,224),
           'SHADOW':(224,200,252,224),'METAL':(196,228,220,252),
           'CREAM':(224,228,252,252)}
BOUNDS = {'LEFT':((-5.40,5.40),(.22,3.10)),
          'RIGHT':((-5.40,5.40),(.22,3.10)),
          'TOP':((-1.25,1.25),(-5.40,5.40)),
          'FRONT':((-1.25,1.25),(.22,3.10)),
          'REAR':((-1.25,1.25),(.22,3.10))}
def roof(z):
    return 3.10 if z<=5.02 else 3.10-(z-5.02)*(.20/.38)

# Safe physical receiver masks inside painted lens cells (native coordinates).
LAMPS = {
    'headlight':(.76,.82,1.03,.96,5.40),
    'rear_red':(1.04,.91,1.11,1.05,-5.40),
}
