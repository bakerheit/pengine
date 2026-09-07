#!/usr/bin/env python3
"""Pickup QA views using the actual player fit, separate wheels and full lock.

The combined diagnostic mesh is saved only in build/, never in game assets.
"""
import math
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from harrow_workman_spec import WHEELS
from render_firetruck_preview import Part, raster_view, read_part

ROOT=Path(__file__).resolve().parents[1]


def parts(steer=0):
    body=read_part(ROOT/"assets/models/vehicles/harrow_workman/body.emesh",
                   ROOT/"assets/textures/vehicles/harrow_workman/body.png")
    scale=np.array([.78/WHEELS["x"],2.7/3.2,2.7/3.2])
    body.positions*=scale
    body.normals/=scale
    body.normals/=np.linalg.norm(body.normals,axis=1)[:,None]
    result=[body]
    wheel=read_part(ROOT/"assets/models/vehicles/common/wheel.emesh",
                    ROOT/"assets/textures/vehicles/common/wheel.png")
    radius=max(np.ptp(wheel.positions[:,1]),np.ptp(wheel.positions[:,2]))*.5
    for axle in (WHEELS["front_z"],WHEELS["rear_z"]):
        for side in (-1,1):
            angle=steer if axle>0 else 0
            c,s=math.cos(angle),math.sin(angle)
            rot=np.array([[c,0,-s],[0,1,0],[s,0,c]])
            p=(wheel.positions*(.32/radius))@rot.T
            p+=np.array([side*.78,WHEELS["arch_y"]*scale[1],axle*scale[2]])
            result.append(Part(p,wheel.normals@rot.T,wheel.uvs,wheel.indices,wheel.texture))
    return result


def diagnostic_mesh(items, filename="harrow-workman-steering-qa.emesh"):
    # Repack the wheel pixels beside the body atlas for a one-material engine
    # QA fixture. The real body still uses its single 256x256 texture.
    atlas=Image.new("RGB",(512,256))
    atlas.paste(Image.fromarray(items[0].texture),(0,0))
    atlas.paste(Image.fromarray(items[1].texture).resize((256,256),Image.Resampling.NEAREST),(256,0))
    atlas.save(ROOT/"build/harrow-workman-qa-atlas.png")
    verts=[]
    for item_no,item in enumerate(items):
        for tri in item.indices:
            for i in tri:
                u,v=item.uvs[i]
                verts.append((*item.positions[i],*item.normals[i],
                              u*.5+(0 if item_no==0 else .5),v,1,0,0,1))
    material=b"qa\0"
    with (ROOT/"build"/filename).open("wb") as f:
        f.write(struct.pack("<8I",0x48534D45,2,0,len(verts),len(verts),1,len(material),0))
        for v in verts: f.write(struct.pack("<12f",*v))
        f.write(struct.pack(f"<{len(verts)}I",*range(len(verts))))
        f.write(struct.pack("<4I",0,len(verts),0,0))
        f.write(material)


if __name__=="__main__":
    straight=parts()
    locked=parts(.82)
    views=[("FRONT",straight,-32,18),("SIDE",straight,-90,0),
           ("OPEN BED",straight,-148,30),("FULL STEERING LOCK",locked,-32,30)]
    sheet=Image.new("RGB",(1440,388),(19,21,25))
    d=ImageDraw.Draw(sheet)
    for i,(name,items,yaw,pitch) in enumerate(views):
        sheet.paste(raster_view(items,yaw,pitch,360,350),(i*360,0))
        d.text((i*360+12,363),name,fill=(220,225,220))
    sheet.save(ROOT/"build/harrow-workman-preview.png")
    diagnostic_mesh(locked)
    # A raised rear view matches the rail/lamp regression reported in game.
    raster_view(straight,180,40,850,750).save(ROOT/"build/harrow-workman-rear-detail.png")
    angle=math.radians(-35)
    c,s=math.cos(angle),math.sin(angle)
    tilt=np.array([[1,0,0],[0,c,-s],[0,s,c]])
    raised=[Part(p.positions@tilt.T,p.normals@tilt.T,p.uvs,p.indices,p.texture)
            for p in straight]
    diagnostic_mesh(raised,"harrow-workman-rear-qa.emesh")
