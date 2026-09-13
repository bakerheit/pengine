"""Stable metric fit and controls for the reference-authored Ember GT."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT/'assets/models/vehicles/ember_gt_study/ember_gt.blend'
MODEL = ROOT/'assets/models/vehicles/ember_gt'
TEXTURE = ROOT/'assets/textures/vehicles/ember_gt/body.png'
WHEELS = dict(x=.88, arch_y=.375, front_z=1.40, rear_z=-1.36, radius=.365)
DOOR = dict(hinge=(.93,.32,.86), handle=(.953,.757,-.41),
            rear_z=-.60, front_z=.86, sill_y=.24, open_degrees=-65)
# Runtime X-left/Y-up/Z-forward. Controls belong to the same left footwell.
DRIVER = dict(hip=(.36,.44,-.15), left_x=.36,
              wheel=(.36,.765,.43), brake=(.46,.325,.79),
              accelerator=(.26,.31,.81))
# Semantic cells preserve original color/material identity in one runtime atlas.
COLORS = {
    'Tangerine pearl':(234,83,12), 'Sculpted orange':(202,61,8),
    'Satin carbon':(24,29,34), 'Intake darkness':(8,12,16),
    'Smoked blue glass':(52,82,94), 'Performance rubber':(28,29,31),
    'Graphite forged alloy':(62,72,80), 'Machined alloy':(160,171,175),
    'Brake rotor':(104,112,116), 'Vermilion caliper':(197,40,18),
    'Ice LED':(225,237,237), 'Red LED':(186,12,18),
    'Cabin charcoal':(38,38,40), 'Cabin stitch':(180,73,19),
    'Gauge face':(60,129,157), 'Interior alloy':(88,97,104),
}

def cell(name):
    index=list(COLORS).index(name)
    return (index%4*64,index//4*64,64,64)
