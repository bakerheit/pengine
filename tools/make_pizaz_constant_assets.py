#!/usr/bin/env python3
"""Rebuild the approved source, create its atlas, and cook its runtime derivative."""
import argparse,subprocess,sys
from PIL import Image,ImageDraw
from pizaz_constant_spec import ROOT,MODEL,TEXTURE,REGIONS,COLORS

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--skip-source',action='store_true');args=parser.parse_args()
    if not args.skip_source:subprocess.run(['npm','run','export'],cwd=ROOT/'tools/pizaz_constant_threejs',check=True)
    im=Image.new('RGBA',(256,256));d=ImageDraw.Draw(im)
    for key,(x0,y0,x1,y1) in REGIONS.items():
        for y in range(y0,y1):
            t=(y-y0)/max(1,y1-y0-1)
            gain=1 if key in ('rubber','trim','glass') else (1.08-.16*t)
            d.line((x0,y,x1-1,y),fill=tuple(round(c*gain) for c in COLORS[key])+(255,))
    TEXTURE.parent.mkdir(parents=True,exist_ok=True);im.save(TEXTURE)
    subprocess.run(['/Applications/Blender.app/Contents/MacOS/Blender','-b','--python-exit-code','1','--python',str(ROOT/'tools/pizaz_constant_blender.py')],check=True)
    subprocess.run([sys.executable,str(ROOT/'tools/validate_pizaz_constant_assets.py')],check=True)
if __name__=='__main__':main()
