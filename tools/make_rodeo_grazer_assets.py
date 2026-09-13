#!/usr/bin/env python3
"""Generate the reference-inspired paint atlas and editable/cooked Grazer."""
import subprocess,random
from PIL import Image,ImageDraw
from rodeo_grazer_spec import ROOT,TEXTURE,REGIONS,COLORS
im=Image.new('RGBA',(256,256),(22,26,28,255));d=ImageDraw.Draw(im)
for key,(x0,y0,x1,y1) in REGIONS.items():
    color=COLORS[key]
    for y in range(y0,y1):
        k=.85+.25*(1-(y-y0)/(y1-y0))
        if key=='CHROME':k=.52+.58*abs((y-y0)/(y1-y0)*2-1)
        if key.startswith('BADGE_'):k=1.0  # Solid enamel; bevels supply the metal edge.
        d.line((x0,y,x1-1,y),fill=tuple(round(c*k) for c in color)+(255,))
# Continuous grey period graphics, projected over both doors and bed sides.
d.polygon([(2,57),(68,57),(92,77),(126,81),(126,91),(84,84),(65,65),(2,65)],fill=(41,50,56,255))
d.polygon([(2,71),(62,71),(83,88),(126,96),(126,101),(77,93),(58,77),(2,77)],fill=(48,58,65,255))
d.line((2,113,126,113),fill=(34,42,48,255),width=3)
for x in range(132,191,6):d.line((x,195,x,252),fill=(49,58,61,255),width=2)
for x in range(197,253,8):d.line((x,133,x,186),fill=(157,172,176,255))
d.text((18,234),'4x4',fill=(190,204,211,255))
for x in [203,224,245]:
    d.ellipse((x-7,196,x+7,210),outline=(185,193,178,255),width=1)
    d.line((x,204,x+3,199),fill=(208,98,33,255))
# Deliberate pixel clusters: baked reflections, rubber grain and lower-panel dust.
rng=random.Random(41)
for key in ['SIDE','PAINT','DASH','BED','BLACK']:
    x0,y0,x1,y1=REGIONS[key]
    for _ in range(160 if key=='SIDE' else 80):
        x=rng.randrange(x0+2,x1-3);y=rng.randrange(y0+2,y1-2)
        base=im.getpixel((x,y));delta=rng.choice([-4,-3,3])
        d.line((x,y,x+rng.choice([1,1,2]),y),fill=tuple(max(0,min(255,c+delta)) for c in base[:3])+(255,))
# Muted dust is clustered near the sill, not scattered over the whole vehicle.
for _ in range(170):
    x=rng.randrange(3,125);y=rng.randrange(105,125)
    if rng.random()<(y-103)/24:
        base=im.getpixel((x,y));dust=(94,88,74)
        d.point((x,y),fill=tuple(round(c*.70+t*.30) for c,t in zip(base[:3],dust))+(255,))
# A compact horizon reflection gives chrome depth without modern PBR shine.
for yy,col in [(137,(218,225,225,255)),(145,(156,173,184,255)),(152,(63,77,87,255)),(156,(48,59,67,255)),(160,(170,181,182,255)),(174,(223,225,217,255))]:
    d.rectangle((66,yy,125,yy+2),fill=col)
# Ribbed cloth and lamp cells remain readable at gameplay distance.
for y in range(205,251,4):d.line((68,y,123,y),fill=(48,54,57,255))
for y in range(138,185,8):d.line((197,y,252,y),fill=(181,192,191,255))
TEXTURE.parent.mkdir(parents=True,exist_ok=True);im.save(TEXTURE)
subprocess.run(['/Applications/Blender.app/Contents/MacOS/Blender','-b','--python-exit-code','1',
                '--python',str(ROOT/'tools/rodeo_grazer_blender.py')],check=True)

# Validate the cooked producer on every regeneration.
import runpy
runpy.run_path(str(ROOT/"tools/validate_rodeo_grazer_assets.py"),run_name="__main__")

# Keep a model-derived UV inspection sheet alongside the preview evidence.
from make_vesper_vx91_assets import read_emesh
from rodeo_grazer_spec import MODEL
uvguide=im.copy().resize((1024,1024),Image.Resampling.NEAREST)
ud=ImageDraw.Draw(uvguide)
for part in ['body_open','driver_door','passenger_door']:
    vertices,indices=read_emesh(MODEL/(part+'.emesh'))
    for start in range(0,len(indices),3):
        pts=[(vertices[i][6]*1024,(1-vertices[i][7])*1024) for i in indices[start:start+3]]
        ud.line(pts+[pts[0]],fill=(240,114,66,255),width=1)
uvguide.save(ROOT/'build/grazer-sa-uv-guide.png')
