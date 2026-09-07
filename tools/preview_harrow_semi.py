#!/usr/bin/env python3
"""Measured joint preview using cooked bodies and the actual shared wheel mesh."""
import math
from PIL import Image,ImageDraw
import numpy as np
from make_harrow_semi_assets import parts, ROOT
from render_firetruck_preview import raster_view, Part
from harrow_hauler_spec import COUPLING as FIFTH
from harrow_freight_trailer_spec import COUPLING as KINGPIN


def coupled(angle=0):
    c,s=math.cos(angle),math.sin(angle)
    rot=np.array([[c,0,s],[0,1,0],[-s,0,c]],dtype=np.float32)
    fifth=np.array([FIFTH[k] for k in ('x','y','z')]);king=np.array([KINGPIN[k] for k in ('x','y','z')])
    shift=fifth-rot@king
    result=parts('harrow_hauler')
    for item in parts('harrow_freight_trailer'):
        positions=np.einsum('ij,kj->ik',item.positions,rot)+shift
        normals=np.einsum('ij,kj->ik',item.normals,rot)
        assert np.isfinite(positions).all() and np.isfinite(normals).all()
        result.append(Part(positions.astype(np.float32),normals.astype(np.float32),item.uvs,item.indices,item.texture))
    for item in result:
        item.positions=item.positions.astype(np.float32)
        item.normals=item.normals.astype(np.float32)
    return result


def main():
    views=[('HARROW HAULER + FREIGHT TRAILER',coupled(),-32,20),
           ('SIDE / MEASURED COUPLING',coupled(),-90,0),
           ('ARTICULATION / 35 DEGREES',coupled(math.radians(35)),-32,35)]
    sheet=Image.new('RGB',(1440,1440),(20,23,26));d=ImageDraw.Draw(sheet)
    for i,(title,items,yaw,pitch) in enumerate(views):
        im=raster_view(items,yaw,pitch,1440,440);sheet.paste(im,(0,i*480));d.text((18,i*480+454),title,fill=(230,230,215))
    target=ROOT/'build/harrow-semi-combined-preview.png';sheet.save(target);print(target)
if __name__=='__main__':main()
