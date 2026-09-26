#!/usr/bin/env python3
"""Rebuild the approved source, create its atlas, and cook its runtime derivative."""
import argparse,subprocess,sys,math
from PIL import Image,ImageDraw
from pizaz_constant_spec import ROOT,MODEL,TEXTURE,REGIONS,COLORS

def interior_atlas(d):
    # Restrained woven cloth and carpet, with dedicated controls rather than
    # painting the dashboard with the exterior's global height gradient.
    for key in ('upholstery','carpet'):
        x0,y0,x1,y1=REGIONS[key]
        for y in range(y0+2,y1-2):
            for x in range(x0+2,x1-2):
                amount=(3 if (x+y)%4==0 else -2 if x%4==0 else 0)
                d.point((x,y),fill=tuple(max(0,c+amount) for c in COLORS[key])+(255,))
    for cx,cy,r in [(206,160,10),(229,160,10),(246,155,4),(246,165,4)]:
        d.ellipse((cx-r,cy-r,cx+r,cy+r),fill=(8,12,15),outline=(76,83,84))
        for i in range(11):
            a=math.radians(140+i*26)
            d.line((round(cx+math.cos(a)*(r-1)),round(cy+math.sin(a)*(r-1)),
                    round(cx+math.cos(a)*(r-3)),round(cy+math.sin(a)*(r-3))),fill=(193,199,188))
        d.line((cx,cy,cx-r+3,cy-3),fill=(218,104,65),width=1)
        d.point((cx,cy),fill=(221,225,207))
    d.rectangle((202,166,210,168),fill=(67,91,74));d.line((225,169,233,169),fill=(130,150,131))
    for x,color in [(195,(169,78,48)),(199,(195,150,54)),(240,(116,157,106))]:d.rectangle((x,173,x+2,174),fill=color)
    d.rectangle((214,131,232,135),fill=(69,104,83));d.line((216,132,229,132),fill=(151,188,153))
    d.rectangle((216,138,230,140),fill=(4,8,10))
    for x in [211,235]:d.ellipse((x-2,136,x+2,140),fill=(108,114,112))
    for x in [198,207,216]:
        d.ellipse((x-3,180,x+3,187),fill=(68,75,80),outline=(14,18,21))
        d.line((x,181,x,184),fill=(202,203,187))
    d.line((196,179,208,179),fill=(78,125,161));d.line((209,179,219,179),fill=(182,93,68))
    for y in range(179,190,2):
        for x in range(227,254,2):d.point((x,y),fill=(9,14,18))
    for i in range(5):
        y=131+i*2
        d.line((244,y,248,y),fill=(181,190,174));d.point((251,y),fill=(95,126,100) if i==0 else (63,67,65))

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--skip-source',action='store_true');args=parser.parse_args()
    if not args.skip_source:subprocess.run(['npm','run','export'],cwd=ROOT/'tools/pizaz_constant_threejs',check=True)
    im=Image.new('RGBA',(256,256));d=ImageDraw.Draw(im)
    for key,(x0,y0,x1,y1) in REGIONS.items():
        for y in range(y0,y1):
            t=(y-y0)/max(1,y1-y0-1)
            gain=1 if key in ('rubber','trim','glass') else (1.08-.16*t)
            d.line((x0,y,x1-1,y),fill=tuple(round(c*gain) for c in COLORS[key])+(255,))
    interior_atlas(d)
    TEXTURE.parent.mkdir(parents=True,exist_ok=True);im.save(TEXTURE)
    subprocess.run(['/Applications/Blender.app/Contents/MacOS/Blender','-b','--python-exit-code','1','--python',str(ROOT/'tools/pizaz_constant_blender.py')],check=True)
    subprocess.run([sys.executable,str(ROOT/'tools/validate_pizaz_constant_assets.py')],check=True)
if __name__=='__main__':main()
