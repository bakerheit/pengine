#!/usr/bin/env python3
"""Build the Ember atlas and cook the retained Blender design for Apricot."""
import subprocess
from PIL import Image,ImageDraw
from ember_gt_spec import ROOT,SOURCE,TEXTURE,COLORS,cell

im=Image.new('RGBA',(256,256),(0,0,0,255));draw=ImageDraw.Draw(im)
for key,color in COLORS.items():
    x,y,_,_=cell(key)
    for row in range(64):
        gain=.92+.12*(1-row/63)
        c=tuple(min(255,round(v*gain)) for v in color)
        draw.line((x,y+row,x+63,y+row),fill=(*c,255))
TEXTURE.parent.mkdir(parents=True,exist_ok=True);im.save(TEXTURE)
subprocess.run(['/Applications/Blender.app/Contents/MacOS/Blender','-b','--python-exit-code','1','--python',str(ROOT/'tools/ember_gt_blender.py'),'--','--source',str(SOURCE)],check=True)
from validate_ember_gt_assets import validate
validate()
