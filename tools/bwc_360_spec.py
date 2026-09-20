"""BWC 360 compact four-door sedan. Dimensions in metres; runtime axes."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MODEL=ROOT/'assets/models/vehicles/bwc_360'
TEXTURE=ROOT/'assets/textures/vehicles/bwc_360/body.png'
SHAPE=dict(length=4.50,width=1.76,roof=1.43,hood=.97,trunk=.96,
           wheelbase=2.57,arch_radius=.405,body_triangle_budget=19000)
WHEELS=dict(x=.755,arch_y=.325,front_z=1.27,rear_z=-1.30,radius=.315)
DRIVER=dict(hip=(.38,.58,.22),wheel=(.38,.94,.63),floor_y=.29)
PLATE_MOUNTS=dict(width=.305,height=.152,
    front=dict(center=(0,.482,2.244),normal=(0,0,1)),
    rear=dict(center=(0,.650,-2.233),normal=(0,0,-1)))
REGIONS={'SIDE':(0,0,128,128),'PAINT':(128,0,256,128),
         'GLASS':(0,128,64,192),'METAL':(64,128,128,192),
         'BLACK':(128,128,192,192),'LAMP':(192,128,256,192),
         'BADGE':(0,192,64,256),'CLOTH':(64,192,128,256),
         'DASH':(128,192,192,224),'GAUGE':(128,224,192,256),'AMBER':(192,192,224,224),
         'RED':(224,192,256,224),'LABEL':(192,224,256,256)}
COLORS={'SIDE':(97,115,139),'PAINT':(119,138,159),'GLASS':(29,40,48),
        'METAL':(166,175,177),'BLACK':(20,23,24),'LAMP':(203,214,208),
        'BADGE':(17,19,21),'CLOTH':(69,75,76),'DASH':(37,43,45),'GAUGE':(14,18,19),
        'AMBER':(224,136,28),'RED':(168,31,39),'LABEL':(185,194,194)}
