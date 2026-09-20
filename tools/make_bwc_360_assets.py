#!/usr/bin/env python3
"""Deterministic BWC 360 atlas, native Blender model and static mesh cook."""
import math,random,subprocess
from PIL import Image,ImageDraw,ImageFont
from bwc_360_spec import ROOT,MODEL,TEXTURE,REGIONS,COLORS

im=Image.new('RGBA',(256,256));draw=ImageDraw.Draw(im)
for key,(x0,y0,x1,y1) in REGIONS.items():
    for y in range(y0,y1):
        t=(y-y0)/(y1-y0);k=1.03-.18*t
        if key=='METAL':k=.65+.4*abs(t*2-1)
        if key in ('BLACK','BADGE'):k=1
        draw.line((x0,y,x1-1,y),fill=tuple(round(v*k) for v in COLORS[key])+(255,))
# Continuous restrained lower cladding and shoulder line.
draw.line((1,118,126,118),fill=(61,76,95,255))
draw.line((1,117,126,117),fill=(136,152,169,255))
for y in range(197,254,4):draw.line((66,y,125,y),fill=(57,63,64,255))
for x in range(196,254,12):draw.line((x,131,x,189),fill=(150,163,162,255))
for y in range(138,187,16):draw.line((194,y,254,y),fill=(174,186,183,255))
rng=random.Random(360)
for key in ['SIDE','PAINT','CLOTH','BLACK']:
    x0,y0,x1,y1=REGIONS[key]
    for _ in range(75):
        x=rng.randrange(x0+2,x1-2);y=rng.randrange(y0+2,y1-2)
        base=im.getpixel((x,y));d=rng.choice([-2,-1,1])
        draw.point((x,y),fill=tuple(max(0,c+d) for c in base[:3])+(255,))
# Rasterize the established roundel with the repository's Roboto font.
# This is a production atlas cell; no fallback system font is required.
scale=4;badge=Image.new('RGBA',(64*scale,64*scale),(17,19,21,255));b=ImageDraw.Draw(badge)
def ellipse(bounds,color):b.ellipse(tuple(round(v*scale) for v in bounds),fill=color)
ellipse((1,1,63,63),'#aeb1ae');ellipse((3,3,61,61),'#111315')
ellipse((10,12,54,56),'#d8d9d4');ellipse((12,14,52,54),'#17191b')
b.pieslice((12*scale,14*scale,52*scale,54*scale),270,90,fill='#f3c51b')
b.pieslice((12*scale,14*scale,52*scale,54*scale),90,180,fill='#d22831')
b.line((32*scale,14*scale,32*scale,54*scale),fill='#e6e7e2',width=scale)
b.line((12*scale,34*scale,52*scale,34*scale),fill='#e6e7e2',width=scale)
ellipse((14.5,16,17.5,19),'#f3c51b');ellipse((46.5,16,49.5,19),'#d22831')
font=ImageFont.truetype(str(ROOT/'assets/fonts/Roboto-Variable.ttf'),9*scale)
if hasattr(font,'set_variation_by_name'):font.set_variation_by_name('Black')
for ch,x,y,angle in [('B',23,9,23),('W',32,7,0),('C',42,10,-24)]:
    glyph=Image.new('RGBA',(18*scale,18*scale));g=ImageDraw.Draw(glyph)
    g.text((9*scale,9*scale),ch,font=font,anchor='mm',fill='#f1f1ec')
    glyph=glyph.rotate(angle,resample=Image.Resampling.BICUBIC)
    badge.alpha_composite(glyph,(int((x-9)*scale),int((y-9)*scale)))
im.paste(badge.resize((64,64),Image.Resampling.LANCZOS),(0,192))
font=ImageFont.truetype(str(ROOT/'assets/fonts/Roboto-Variable.ttf'),16)
draw.text((202,228),'360',font=font,fill=(225,231,225,255))
for x,r in [(140,8),(160,11),(181,8)]:
    draw.ellipse((x-r,240-r,x+r,240+r),outline=(184,199,190,255),width=1)
    draw.line((x,240,x+3,234),fill=(220,116,54,255),width=1)
TEXTURE.parent.mkdir(parents=True,exist_ok=True);im.save(TEXTURE)
MODEL.mkdir(parents=True,exist_ok=True);badge.save(MODEL/'emblem-preview.png')
subprocess.run(['/Applications/Blender.app/Contents/MacOS/Blender','-b','--python-exit-code','1',
                '--python',str(ROOT/'tools/bwc_360_blender.py')],check=True)
subprocess.run(['python3',str(ROOT/'tools/validate_bwc_360_assets.py')],check=True)
from make_vesper_vx91_assets import read_emesh
guide=im.resize((1024,1024),Image.Resampling.NEAREST);g=ImageDraw.Draw(guide)
for part in ['body_open','driver_front_door','driver_rear_door']:
    vertices,indices=read_emesh(MODEL/(part+'.emesh'))
    for start in range(0,len(indices),3):
        pts=[(vertices[i][6]*1024,(1-vertices[i][7])*1024) for i in indices[start:start+3]]
        g.line(pts+[pts[0]],fill=(239,123,49,255),width=1)
guide.save(ROOT/'build/bwc-360-uv-guide.png')
