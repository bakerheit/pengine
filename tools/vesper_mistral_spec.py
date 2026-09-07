"""VESPER MISTRAL: an original 1991 two-seat roadster, source axes X/up/Z."""
ATLAS_SIZE = 256
WHEELS = dict(x=.91, front_z=1.48, rear_z=-1.30, arch_y=.38)
DRIVER = dict(left_x=.43, wheel_y=1.0, wheel_z=.135,
              hip=(.43,.74,-.37), pedals_x=(.33,.53))
# Source X/up-Y/forward-Z; mirrored by src/app/mistral_door.h.
DOOR = dict(hinge=(1.0143,.44,.31), handle=(1.0175,.76,-.52),
            rear_z=-.86, front_z=.31, sill_y=.44, inner_x=.74, open_degrees=-65)
SHAPE = dict(length=4.70, width=2.10, windshield_top=1.45,
             body_bottom=.20, hood_front=.69, hood_rear=.94,
             cockpit=(-.93,.50), rear_deck=.94,
             arch_long_radius=.43, arch_height_radius=.40,
             wheelbase=2.78, track=1.82, triangle_budget=(650,1300),
             identity=['long dished hood and broad rising fenders',
                       'short rounded rear deck with no wing',
                       'swept windshield and completely open two-seat cockpit',
                       'faded warm red paint and small flush lamps'])
REGIONS = {
    'SIDE':(4,4,124,100), 'PAINT':(128,4,252,100),
    'FRONT':(4,104,124,152), 'REAR':(128,104,252,152),
    'GLASS':(4,156,124,196), 'SEAT':(128,156,188,220),
    'DASH':(192,156,252,196), 'RUBBER':(4,200,60,252),
    'SHADOW':(64,200,124,252), 'METAL':(192,200,252,220),
    'INTERIOR':(128,224,188,252), 'BLACK':(192,224,252,252),
}

# Cooked X/up-Y/forward-Z coordinates. Lenses are atlas paint on physical
# planar end-cap triangles, not offset overlay geometry.
LAMPS = dict(
    headlights=dict(z=2.35, y=(.495,.58), x=((- .755,-.425),(.425,.755)),
                    receiver_y=(.22,.635), receiver_x=(-.78,.78)),
    rear_red=dict(z=-2.35, y=(.50,.64), x=((- .755,-.425),(.425,.755)),
                  receiver_y=(.22,.685), receiver_x=(-.78,.78)))
