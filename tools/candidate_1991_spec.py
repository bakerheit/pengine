"""Shared shape and atlas contract for the three selected 1991 concepts."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ATLAS = 256
BODY_TRIANGLE_LIMIT = 60000  # Close-view refinement requested after the blockout.
REGIONS = {name: ((i % 4) * 64, (i // 4) * 64,
                  (i % 4 + 1) * 64, (i // 4 + 1) * 64)
           for i, name in enumerate((
               'PAINT', 'TOP', 'CREAM', 'CLAD',
               'DARK', 'CABIN', 'SEAT', 'HEADLINER',
               'METAL', 'LAMP', 'AMBER', 'RED',
               'WINDOW', 'TYRE', 'STEEL', 'STRIPE'))}

# Blender axes: X across, Y forward, Z up. Exported runtime axes: X, Y up, Z forward.
# Distances are metres, deliberately scaled to each approved reference set.
SHAPES = {
    'glm_meridian': dict(brand='GLM', model='MERIDIAN', kind='van',
        half_length=2.43, half_width=.91, belt=1.14, roof=1.84,
        roof_rear=-2.02, roof_front=.64, windshield_base=1.56,
        front_axle=1.39, rear_axle=-1.39, wheel_radius=.365,
        wheel_x=.80, arch_radius=.45, door_rear=.04, door_front=1.16,
        door_sill=.33, cab_floor=.39, seat_y=.74, seat_fore=.49, steering_fore=1.08,
        paint=(153, 180, 176), top=(181, 204, 200), cream=(218, 217, 197),
        clad=(68, 69, 69), seat=(105, 106, 105)),
    'rodeo_switchback': dict(brand='RODEO', model='SWITCHBACK', kind='suv',
        half_length=2.29, half_width=.92, belt=1.14, roof=1.86,
        roof_rear=-2.14, roof_front=.72, windshield_base=1.15,
        front_axle=1.38, rear_axle=-1.35, wheel_radius=.46,
        wheel_x=.83, arch_radius=.55, door_rear=-.35, door_front=.76,
        door_sill=.41, cab_floor=.48, seat_y=.78, seat_fore=.155, steering_fore=.36,
        paint=(182, 73, 29), top=(209, 95, 44), cream=(218, 213, 186),
        clad=(36, 39, 41), seat=(85, 84, 79)),
    'harrow_hookline': dict(brand='HARROW', model='HOOKLINE', kind='wrecker',
        half_length=2.70, half_width=1.02, belt=1.36, roof=2.14,
        roof_rear=.47, roof_front=2.29, windshield_base=2.66,
        front_axle=1.62, rear_axle=-1.58, wheel_radius=.46,
        wheel_x=.90, arch_radius=.55, door_rear=.47, door_front=2.40,
        door_sill=.48, cab_floor=.54, seat_y=.92, seat_fore=1.12, steering_fore=1.87,
        paint=(220, 159, 24), top=(242, 189, 39), cream=(222, 219, 195),
        clad=(36, 37, 39), seat=(75, 78, 78)),
}
