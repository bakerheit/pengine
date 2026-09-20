#!/usr/bin/env python3
"""Matched views of cooked Shū body, separate panes and actual shared wheels."""
import argparse
import math
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
from render_firetruck_preview import Part,read_part,raster_view
from spagatti_shu_spec import WHEEL_ANCHORS
ROOT=Path(__file__).resolve().parents[1]

def parts(model,texture,lock=False,clay=False):
    body=read_part(model/'body.emesh',texture)
    if clay:body.texture=np.full_like(body.texture,[151,151,148])
    result=[body]
    for name in ['windshield','rear_glass','driver_glass','passenger_glass','driver_rear_glass','passenger_rear_glass']:
        path=model/(name+'.emesh')
        if path.exists():
            pane=read_part(path,texture);pane.texture=np.full_like(pane.texture,[38,51,57]);result.append(pane)
    wheel=read_part(ROOT/'assets/models/vehicles/common/wheel.emesh',ROOT/'assets/textures/vehicles/common/wheel.png')
    scale=.86/(np.ptp(wheel.positions[:,1]))*.999
    for x in [-.94,.94]:
        for z in [1.45,-1.35]:
            angle=.60 if lock and z>0 else 0
            c,s=math.cos(angle),math.sin(angle)
            matrix=np.array([[c,0,s],[0,1,0],[-s,0,c]])
            result.append(Part(wheel.positions@matrix.T*scale+[x,.43,z],wheel.normals@matrix.T,wheel.uvs,wheel.indices,wheel.texture))
    return result

def main():
    p=argparse.ArgumentParser();p.add_argument('--model',type=Path,default=ROOT/'assets/models/vehicles/spagatti_shu');p.add_argument('--texture',type=Path,default=ROOT/'assets/textures/vehicles/spagatti_shu/body.png');p.add_argument('--output',type=Path,default=ROOT/'build/spagatti-shu-preview.png');p.add_argument('--clay',action='store_true');a=p.parse_args()
    sheet=Image.new('RGB',(1800,1000),(18,20,23));d=ImageDraw.Draw(sheet)
    views=[('FRONT 3/4',-32,18),('SIDE',-90,0),('REAR 3/4',-148,18),('ELEVATED FRONT',-32,38),('FULL LOCK',-32,25),('REAR',180,0)]
    for i,(label,yaw,pitch) in enumerate(views):
        x=(i%3)*600;y=(i//3)*500
        sheet.paste(raster_view(parts(a.model,a.texture,i==4,a.clay),yaw,pitch,600,465),(x,y));d.text((x+15,y+480),label,fill=(225,227,229))
    a.output.parent.mkdir(parents=True,exist_ok=True);sheet.save(a.output)
if __name__=='__main__':main()
