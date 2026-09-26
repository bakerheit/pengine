#!/usr/bin/env python3
"""Generate a deterministic 256px atlas and cook the Harrow Rearloader."""
import json,math,subprocess
from pathlib import Path
from PIL import Image
from harrow_rearloader_spec import ROOT, SLUG, ATLAS, COLORS, REGIONS

def texture():
    im=Image.new('RGBA',(ATLAS,ATLAS));p=im.load()
    for name,(x0,y0,x1,y1) in REGIONS.items():
        rgb=COLORS[name]
        for y in range(y0,y1):
            for x in range(x0,x1):
                u=(x-x0)/63;v=(y-y0)/63;fac=1.0
                if name in ('PAINT','TOP','CREAM'):fac=.94+.09*(1-v)
                elif name in ('METAL','STEEL'):fac=.92+.12*(1-v)+.025*math.sin(y*.4)
                elif name in ('CABIN','HEADLINER','SEAT'):fac=.96+.055*((x*3+y*7)%5)/4
                elif name in ('LAMP','RED','AMBER'):
                    fac=.78+.23*(1-v)+(.12 if (x//3+y//4)%2 else 0)
                    if min(u,v,1-u,1-v)<.035:fac*=.7
                elif name=='WEAR':
                    fac=.86+.035*math.sin(x*.35+y*.09)
                    if y%19==0 and 8<(x+y*3)%53<35:fac=1.20
                    if (x*7+y*31)%127<2:fac=.55
                p[x,y]=tuple(min(255,max(0,round(c*fac))) for c in rgb)+(255,)
    path=ROOT/'assets/textures/vehicles'/SLUG/'body.png';path.parent.mkdir(parents=True,exist_ok=True);im.save(path)

def main():
    texture()
    subprocess.run(['/Applications/Blender.app/Contents/MacOS/Blender','-b','--python-exit-code','1','--python',str(ROOT/'tools/harrow_rearloader_blender.py')],cwd=ROOT,check=True)
    subprocess.run(['/Applications/Blender.app/Contents/MacOS/Blender','-b','--python-exit-code','1','--python',str(ROOT/'tools/harrow_rearloader_inspect.py')],cwd=ROOT,check=True)
    from validate_harrow_rearloader_assets import validate
    report=validate();print(json.dumps(report,indent=2))
if __name__=='__main__':main()
