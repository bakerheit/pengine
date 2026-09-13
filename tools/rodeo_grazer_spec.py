"""Grazer: original compact extended-cab 4x4, dimensions in metres."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MODEL=ROOT/'assets/models/vehicles/rodeo_grazer'
TEXTURE=ROOT/'assets/textures/vehicles/rodeo_grazer/body.png'
WHEELS=dict(x=.82,arch_y=.405,front_z=1.43,rear_z=-1.50,radius=.405)
SHAPE=dict(length=5.06,width=1.90,roof=1.78,hood=1.065,bed_rail=1.09,
           bed_floor=.65,cabin=(-.70,.94),arch_radius=.49)
# Rounded edges stay deliberately low-poly: enough curvature to read like a
# GTA-era vehicle, without turning the compact truck into a modern smooth CAD
# model. Values are metres except the fender arc tessellation.
ROUNDING=dict(body=.032,trim=.018,bumper=.050,window=.021,
              fender_arc_steps=15)
DOOR=dict(hinge=(.90,.43,.92),handle=(.916,1.015,-.19),rear_z=-.28,
          front_z=.92,sill_y=.43,top_y=1.75)
DRIVER=dict(hip=(.40,.81,.07),wheel=(.40,1.13,.61),floor_y=.43)
# Runtime axes: X driver-left, Y up, Z forward. Blank mounting surfaces for
# future per-vehicle plate text; no registration is baked into the atlas.
PLATE_MOUNTS=dict(width=.305,height=.152,
    front=dict(center=(0,.625,2.446),normal=(0,0,1)),
    rear=dict(center=(0,.560,-2.646),normal=(0,0,-1)))
# Approved round-01 Rodeo R: black bowl/stem, orange kicked leg, metal edges.
# Badge-only shape contract; the truck bounds, cab and wheel rig stay unchanged.
# Dimensions include the silhouette before its 1.4 mm edge bevel. Runtime axes.
EMBLEMS=dict(
    front=dict(center=(0,.872,2.381),width=.195,height=.165,normal=1),
    rear=dict(center=(0,.824,-2.531),width=.218,height=.185,normal=-1))
EMBLEM_DEPTH=.006
EMBLEM_BEVEL=.0014
REGIONS={'SIDE':(0,0,128,128),'PAINT':(128,0,256,128),
         'GLASS':(0,128,64,192),'CHROME':(64,128,128,192),
         'BLACK':(128,128,192,192),'LAMP':(192,128,256,192),
         'AMBER':(0,192,32,224),'RED':(32,192,64,224),
         'DASH':(64,192,128,256),'BED':(128,192,192,256),
         'FOURBY':(0,224,64,256),'GAUGE':(192,192,256,224),
         'BADGE_DARK':(192,224,214,256),'BADGE_RUST':(214,224,236,256),
         'BADGE_METAL':(236,224,256,256)}
COLORS={'SIDE':(76,91,101),'PAINT':(91,107,117),'GLASS':(45,64,72),
        'CHROME':(182,195,203),'BLACK':(22,26,28),'LAMP':(218,225,218),
        'AMBER':(225,133,27),'RED':(166,26,23),'DASH':(58,64,67),
        'BED':(31,38,42),'GAUGE':(24,33,37),'FOURBY':(65,80,91),
        'BADGE_DARK':(28,29,26),'BADGE_RUST':(210,83,27),'BADGE_METAL':(205,216,220)}

# Early-PS2 direction: crowned hood/roof, rolled fenders, bowed door skins,
# period graphics and restrained pixel wear. Keep the approved physical rig.
ART_DIRECTION='Early PS2 realism with a limited-color 256px atlas'
