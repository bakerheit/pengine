#!/usr/bin/env python3
"""Original readable civic sign textures for the neighborhood stations."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
out=Path(__file__).resolve().parents[1]/'assets/textures/world/neighborhood'
out.mkdir(parents=True,exist_ok=True)
font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',78)
for slug,label,color in [('fire-station','HALLOWAY FIRE STATION',(111,28,24)),
                          ('police-station','HALLOWAY POLICE STATION',(24,48,66))]:
    im=Image.new('RGBA',(1536,128),(*color,255)); d=ImageDraw.Draw(im)
    d.rectangle((3,3,1532,124),outline=(204,181,126),width=3)
    box=d.textbbox((0,0),label,font=font)
    d.text(((1536-(box[2]-box[0]))/2,(128-(box[3]-box[1]))/2-box[1]),label,font=font,fill=(241,229,195))
    im.save(out/(slug+'.png'))

# Hand-painted neighborhood bar board, deliberately less pristine than civic signage.
import random
im=Image.new('RGBA',(1280,256),(69,22,23,255));d=ImageDraw.Draw(im);rng=random.Random(18)
for _ in range(2800):
    x,y=rng.randrange(1280),rng.randrange(256)
    color=rng.choice([(83,32,29),(45,18,19),(110,52,40)])
    d.line((x,y,x+rng.randrange(1,9),y),fill=color,width=1)
d.rectangle((8,8,1271,247),outline=(159,127,86),width=4)
for label,size,top,color in [('THE BENT ELBOW',124,66,(205,182,132)),
                            ('COLD BEER   •   POOL   •   OPEN LATE',27,205,(172,145,104))]:
    font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',size)
    box=d.textbbox((0,0),label,font=font)
    d.text(((1280-box[2])/2,top-box[1]),label,font=font,fill=color)
im.save(out/'bent-elbow.png')
