"""Aster A-80: fictional 1980s regional jet. Native metres, +Z forward in game."""
ATLAS_SIZE = 256
REGIONS = {
    'FUSELAGE': (2, 2, 254, 94),
    'WING': (2, 98, 126, 170),
    'TAIL': (130, 98, 254, 170),
    'ENGINE': (2, 174, 126, 214),
    'FAN': (132, 176, 172, 216),
    'RUBBER': (180, 178, 210, 208),
    'METAL': (216, 178, 252, 210),
    'SHADOW': (2, 222, 42, 254),
    'WHITE': (48, 222, 88, 254),
    'TEAL': (94, 222, 134, 254),
}
SHAPE = dict(name='Aster A-80', length=32.0, span=28.0, height=9.2,
             fuselage_radius=1.65, fuselage_height=3.7,
             body_triangle_budget=(600, 1500), atlas=(256,256,'RGBA'),
             traits=['tapered round nose', 'low swept wings', 'two open nacelles',
                     'tall swept fin', 'cream paint with teal and gold cheatline'])
# Blender uses X lateral, Y forward, Z up. The cooker swaps Y/Z for Apricot.
STATIONS = [(-16,.06,4.5),(-14,.50,4.2),(-11,1.20,3.7),(-8,1.65,3.7),
            (5,1.65,3.7),(8,1.65,3.7),(11,1.58,3.7),
            (13,1.40,3.70),(15,.85,3.60),(16,.22,3.48)]
GEAR = [(-.24,.42,11.1,.42,.26),(.24,.42,11.1,.42,.26),
        (-2.65,.58,-1.5,.58,.38),(-1.95,.58,-1.5,.58,.38),
        (1.95,.58,-1.5,.58,.38),(2.65,.58,-1.5,.58,.38)]
# Entry/seat locators only: interaction, ownership, and flight are future work.
ENTRY = (-1.65,2.30,10.3)
PILOT = (-.55,3.55,12.0)
