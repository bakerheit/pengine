"""1991 Harrow Rearloader contract, derived from reviewed generated references.
Blender X = driver-left, Y = forward, Z = up; runtime X = driver-left,
Y = up, Z = forward. Metres. Working name; target Class 3, review pending.
"""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
SLUG = 'harrow_rearloader'
ATLAS = 256
BODY_TRIANGLE_LIMIT = 65000
SHAPE = dict(half_length=3.8, half_width=1.20, cab_width=1.16,
    roof=3.12, body_roof=3.55, belt=2.04, cab_rear=1.48,
    windshield_base=3.53, windshield_top=3.28,
    front_axle=2.27, rear_axle=-1.53, wheel_x=1.00, wheel_radius=.60,
    arch_radius=.72, rear_dual_offsets=(-.17,.17),
    door_front=3.22, door_rear=1.65, door_sill=.91,
    cab_floor=1.13, driver_seat=(.58,1.58,2.16),
    steering_wheel=(.58,2.09,2.82), driver_hinge=(1.155,2.04,3.22),
    cab_step_forward=3.17, step_exit=(1.52,.42,3.10), body_front=1.40, body_rear=-2.10,
    hopper_rear=-3.70, hopper_sill=1.00,
    plate_front=(0,.790,3.846), plate_rear=(0,.790,-3.834),
    headlamps=((-.91,1.169,3.674),(.91,1.169,3.674)),
    tail_lamps=((-1.10,1.27,-3.731),(1.10,1.27,-3.731)))
COLORS = {'PAINT':(67,94,74),'TOP':(88,112,92),'CREAM':(222,220,202),
    'CLAD':(48,51,51),'DARK':(20,23,24),'CABIN':(68,71,70),
    'SEAT':(75,79,78),'HEADLINER':(151,150,136),'METAL':(159,164,157),
    'LAMP':(226,227,210),'AMBER':(225,143,32),'RED':(176,32,25),
    'WINDOW':(82,103,106),'TYRE':(24,26,26),'STEEL':(90,97,96),
    'WEAR':(75,72,61)}
REGIONS = {name: ((i%4)*64,(i//4)*64,(i%4+1)*64,(i//4+1)*64)
           for i,name in enumerate(COLORS)}
