"""Marlin Sprint 22. Native metres: X beam, Blender Y bow, Z up.

Boat exception to road-vehicle skill: no axles, tires, or wheel cutouts.
The origin is the design waterline; the export becomes +Z bow, +Y up.
"""
REGIONS = {
    'HULL': (2,2,254,66), 'DECK': (2,70,126,160),
    'GLASS': (130,70,254,118), 'VINYL': (130,122,206,178),
    'TEAK': (2,164,126,210), 'FLOOR': (130,182,206,226),
    'DASH': (210,122,254,178), 'METAL': (210,182,254,206),
    'DARK': (210,210,254,230), 'CREAM': (2,234,62,254),
    'SEAFOAM': (66,234,126,254), 'RED': (130,234,164,254),
    'GREEN': (168,234,202,254),
}
SHAPE = dict(name='Marlin Sprint 22', length=7.15, beam=2.36,
             waterline=0, keel=-.48, deck=.82, windshield=1.43,
             cockpit=(-2.65,.55), floor=.13, triangle_budget=(650,1300),
             atlas=(256,256,'RGBA'), wheel_anchors=[],
             traits=['fine pointed bow', 'continuous deep-V hull and hard chines',
                     'open recessed cockpit', 'split raked windscreen',
                     'cream and seafoam stripe, tan seats and teak swim step'])
STATIONS = [(-3.10,.95,.60),(-2.65,1.08,.65),(-1.80,1.15,.72),
            (-.55,1.18,.80),(.55,1.12,.82),(1.40,.93,.83),
            (2.30,.63,.79),(3.00,.30,.69),(3.50,.015,.55)]
# Future boarding locators only; this asset does not enable boat physics.
ENTRY=(-1.10,.72,-1.55)
PILOT=(.52,.58,-.55)
