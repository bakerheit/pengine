#!/usr/bin/env python3
"""Reproducible Wayfarer atlas, Blender cook, fit checks and shared-wheel views."""
import argparse
import json
import math
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from alder_wayfarer_spec import (ATLAS_SIZE, REGIONS, SHAPE, WHEELS,
                                SURFACE_REGIONS, SURFACE_BOUNDS, surface_patches)
from make_harrow_workman_assets import contains
from make_vesper_vx91_assets import read_emesh
from render_firetruck_preview import Part, read_part, raster_view

ROOT=Path(__file__).resolve().parents[1]
MODEL=ROOT/"assets/models/vehicles/alder_wayfarer"
TEXTURE=ROOT/"assets/textures/vehicles/alder_wayfarer/body.png"


def make_texture(blockout=False):
    image=Image.new("RGBA",(ATLAS_SIZE,ATLAS_SIZE),(24,15,20,255))
    draw=ImageDraw.Draw(image)
    colors={"SIDE":(125,44,55),"PAINT":(147,55,64),"GLASS":(23,38,46),
            "CREAM":(211,200,173),"RUBBER":(29,30,32),"GRILLE":(25,26,28),
            "TAILGATE":(125,44,55),"HEADLIGHT":(222,222,197),"AMBER":(220,135,37),
            "RED":(187,39,35),"REVERSE":(211,213,196),"CHROME":(149,153,151),
            "SHADOW":(37,25,31),"BLACK":(10,15,19),"WINDSHIELD":(23,38,46),
            "REAR_GLASS":(23,38,46)}
    for key,region in REGIONS.items():
        draw.rectangle(region,fill=colors[key]+(255,))
    if not blockout:
        def rect(box,color): draw.rectangle(box,fill=color+(255,))
        # Pixel landmarks use precisely the same source projection as SIDE UVs.
        def side(y,z):
            x0,y0,x1,y1=REGIONS["SIDE"]
            return (round(x0+2+(y+2.62)/5.24*(x1-x0-4)),
                    round(y1-2-(z-.25)/.88*(y1-y0-4)))
        def side_line(a,b,color,width=1): draw.line([side(*a),side(*b)],fill=color+(255,),width=width)
        x0,y0,x1,y1=REGIONS["SIDE"]
        rect((x0,y0,x1,y0+8),(147,62,70))
        rect((x0,y1-11,x1,y1),(76,31,40))
        for z,col,w in ((.99,(186,127,111),1),(.60,(26,26,28),3),(.62,(179,169,144),1)):
            side_line((-2.62,z),(2.62,z),col,w)
        for y in (-1.40,-.44,.72):
            side_line((y,.31),(y,1.06),(61,27,36))
        side_line((-1.4,.31),(.72,.31),(58,25,34))
        for y in (-1.08,-.17):
            side_line((y,.93),(y+.19,.93),(22,24,28),3)
            side_line((y,.945),(y+.17,.945),(177,179,164))
        # A small petrol flap, no logos or readable labels.
        draw.rectangle((*side(-2.20,.89),*side(-2.00,.73)),outline=(66,28,36,255),width=1)
        x0,y0,x1,y1=REGIONS["PAINT"]
        rect((x0+6,y0,x0+9,y1),(177,82,88))
        rect((x1-9,y0,x1-6,y1),(177,82,88))
        for name in ("GLASS","WINDSHIELD","REAR_GLASS"):
            x0,y0,x1,y1=REGIONS[name]
            rect((x0,y0,x1,y0+5),(53,74,82))
            draw.polygon(((x0+4,y0+7),(x1-4,y0+3),(x1-4,y0+6),(x0+4,y0+10)),fill=(80,100,102,255))
            rect((x0,y1-7,x1,y1),(10,23,31))
            if name=="WINDSHIELD":
                rect((x0+7,y1-9,x1-7,y1-6),(35,40,43))
            elif name=="REAR_GLASS":
                # Fine heater wires, not separate coplanar strips.
                for y in range(y0+13,y1-5,5): rect((x0+3,y,x1-3,y),(50,58,57))
        x0,y0,x1,y1=REGIONS["CREAM"]
        rect((x0,y0,x0+4,y1),(170,159,134))
        rect((x1-4,y0,x1,y1),(170,159,134))
        rect((x0+7,y0+5,x1-7,y0+7),(226,215,187))
        x0,y0,x1,y1=REGIONS["TAILGATE"]
        draw.rectangle((x0+5,y0+6,x1-5,y1-6),outline=(65,26,35,255),width=1)
        rect((x0+7,y0+8,x1-7,y0+9),(172,85,89))
        rect((x0+39,y0+13,x0+62,y0+17),(18,24,27))
        rect((x0+40,y0+13,x0+61,y0+14),(179,180,162))
        rect((x0,y1-13,x1,y1-10),(27,27,29))
        x0,y0,x1,y1=REGIONS["GRILLE"]
        draw.rectangle((x0+2,y0+2,x1-2,y1-2),outline=(146,155,150,255),width=2)
        for y in range(y0+8,y1-4,7): rect((x0+4,y,x1-4,y+1),(124,135,134))
        for x in range(x0+9,x1-4,9): rect((x,y0+4,x,y1-4),(72,80,84))
        for name in ("HEADLIGHT","AMBER","RED","REVERSE"):
            x0,y0,x1,y1=REGIONS[name]
            draw.rectangle((x0+2,y0+2,x1-2,y1-2),outline=(32,30,31,255),width=2)
            bright=tuple(min(255,c+24) for c in colors[name])
            for x in range(x0+6,x1-4,4): rect((x,y0+5,x,y1-5),bright)
            rect((x0+5,y0+5,x1-5,y0+6),bright)
        x0,y0,x1,y1=REGIONS["CHROME"]
        rect((x0,y0,x1,y0+5),(204,202,185))
        rect((x0,y1-5,x1,y1),(65,75,82))
    bake_surface_details(image)
    TEXTURE.parent.mkdir(parents=True,exist_ok=True)
    image.save(TEXTURE)


def bake_surface_details(image):
    """Bake semantic swatches onto the shell's continuous planar UV islands.

    Nearest sampling preserves the original pixel palette. No floating skin,
    depth bias, extra render pass, or change to collision deformation is used.
    """
    source=np.asarray(image.copy())
    result=np.asarray(image).copy()
    for name, patches in surface_patches().items():
        x0,y0,x1,y1=SURFACE_REGIONS.get(name,REGIONS.get(name))
        (a0,a1),(b0,b1)=SURFACE_BOUNDS[name]
        base=(147,55,64,255) if name in ("GLASS","WINDSHIELD","REAR_GLASS") else (125,44,55,255)
        if name=="BUMPER_FACE":base=(29,30,32,255)
        result[y0:y1+1,x0:x1+1]=base
        yy,xx=np.mgrid[y0:y1+1,x0:x1+1]
        a=a0+(xx-(x0+2))/(x1-x0-4)*(a1-a0)
        b=b0+((y1-2)-yy)/(y1-y0-4)*(b1-b0)
        for material, polygon in patches:
            # Pixel-center inclusion; convex authored detail boundaries.
            edges=[]
            for p,q in zip(polygon,polygon[1:]+polygon[:1]):
                edges.append((q[0]-p[0])*(b-p[1])-(q[1]-p[1])*(a-p[0]))
            mask=np.all(np.asarray(edges)>=-1e-7,axis=0)|np.all(np.asarray(edges)<=1e-7,axis=0)
            points=np.asarray(polygon)
            lo,hi=points.min(axis=0),points.max(axis=0)
            sx0,sy0,sx1,sy1=REGIONS[material]
            sx=np.clip(np.floor(sx0+2+(a-lo[0])/(hi[0]-lo[0])*(sx1-sx0-4)),0,255).astype(int)
            sy=np.clip(np.floor(sy1-2-(b-lo[1])/(hi[1]-lo[1])*(sy1-sy0-4)),0,255).astype(int)
            result[y0:y1+1,x0:x1+1][mask]=source[sy,sx][mask]
    # The new full fascia island reuses the old small lens/chrome space.
    # Relocate chrome and shadow so unrelated rack/chassis materials survive.
    for name in ("CHROME","SHADOW"):
        dst=SURFACE_REGIONS[name]
        swatch=Image.fromarray(source).crop(REGIONS[name]).resize(
            (dst[2]-dst[0]+1,dst[3]-dst[1]+1),Image.Resampling.NEAREST)
        result[dst[1]:dst[3]+1,dst[0]:dst[2]+1]=np.asarray(swatch)
    image.paste(Image.fromarray(result))


def validate():
    vertices,indices=read_emesh(MODEL/"body.emesh")
    v=np.asarray(vertices)
    assert len(indices)%3==0 and min(indices)>=0 and max(indices)<len(v)
    assert np.isfinite(v).all() and ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all()
    triangles=v[np.asarray(indices).reshape(-1,3),:3]
    assert SHAPE["triangle_budget"][0]<=len(triangles)<=SHAPE["triangle_budget"][1],len(triangles)
    area=np.linalg.norm(np.cross(triangles[:,1]-triangles[:,0],triangles[:,2]-triangles[:,0]),axis=1)
    assert (area>1e-9).all(),"degenerate triangle"
    lo,hi=v[:,:3].min(axis=0),v[:,:3].max(axis=0)
    assert abs(lo[0]+hi[0])<1e-5 and abs(lo[2]+hi[2])<1e-5
    assert 5.45<hi[2]-lo[2]<5.55 and 1.86<hi[1]<1.90
    keys={tuple(np.round(p,4)) for p in v[:,:3]}
    assert all((-x,y,z) in keys for x,y,z in keys),"asymmetric geometry"
    # Outer side faces must share one model-wide projection, including arch
    # facets; otherwise each facet repeats the door trim at a different scale.
    side=v[(np.abs(v[:,0])>.979)&(np.abs(v[:,2])<2.5)&
           (v[:,1]>.27)&(v[:,1]<.93)&(np.abs(v[:,3])>.85)]
    x0,y0,x1,y1=REGIONS["SIDE"]
    expected_u=(x0+2+(side[:,2]+2.62)/5.24*(x1-x0-4))/256
    expected_v=1-(y1-2-(side[:,1]-.25)/.88*(y1-y0-4))/256
    assert len(side)>50
    assert np.allclose(side[:,6],expected_u,atol=1e-6),"side trim U discontinuity"
    assert np.allclose(side[:,7],expected_v,atol=1e-6),"side trim V discontinuity"
    for axle in (WHEELS["front_z"],WHEELS["rear_z"]):
        for s in (-1,1):
            outside=triangles[(s*triangles[:,:,0]>.66).all(axis=1)]
            for dz,dy in ((0,0),(.12,0),(-.12,0),(0,.15),(0,-.12)):
                assert not any(contains(np.array([axle+dz,WHEELS["arch_y"]+dy]),t[:,[2,1]])
                               for t in outside),"blocked wheel opening"
    for x in (-.65,0,.65):
        for z in (1.65,1.75):
            assert any(contains(np.array([x,z]),t[:,[0,2]]) for t in triangles
                       if (t[:,1]>1.02).all()),"missing hood"
    # Roof coverage and actual empty space below the roof-rack rails.
    for x in (-.55,.13,.55):
        for z in (-1.90,-1.30,-.60,.10):
            assert any(contains(np.array([x,z]),t[:,[0,2]]) for t in triangles
                       if (t[:,1]>1.70).all()),"missing wagon roof"
    # There must be no independent optical skins offset from the shell.
    for end in (-1,1):
        assert not (np.abs(triangles[:,:,2]-end*2.625)<1e-5).all(axis=1).any()
        end_faces=triangles[(np.abs(triangles[:,:,2]-end*2.62)<1e-5).all(axis=1)]
        for x in (-.863,-.591,.137,.591,.863):
            for h in (.573,.743,.913):
                assert sum(contains(np.array([x,h]),t[:,[0,1]]) for t in end_faces)==1,"layered end skin"
    for side in (-1,1):
        cabin=triangles[(side*triangles[:,:,0]>.7).all(axis=1)]
        for z in (-1.80,-.90,-.10):
            for h in (1.31,1.47):
                assert sum(contains(np.array([z,h]),t[:,[2,1]]) for t in cabin)==1,"layered cabin skin"
    # Both sloped end windows use the original cabin shell plane, with no
    # offset glass rectangle hiding behind it during nonlinear deformation.
    for face_name, plane in (("WINDSHIELD", lambda p: .8-(p[:,1]-1.13)*(.54/.57)),
                             ("REAR_GLASS", lambda p: -2.46+(p[:,1]-1.13)*(.38/.57))):
        x0,y0,x1,y1=REGIONS[face_name]
        glass=v[(v[:,6]>x0/256)&(v[:,6]<x1/256)&
                (v[:,7]>1-y1/256)&(v[:,7]<1-y0/256)]
        assert len(glass)==6, "window is not the two shell triangles"
        assert np.allclose(glass[:,2],plane(glass),atol=1e-6),"offset glass skin"
    sx=.78/WHEELS["x"]
    sy=2.7/(WHEELS["front_z"]-WHEELS["rear_z"])
    scale=np.array([sx,sy,sy])
    cloud=[]
    for t in triangles*scale:
        if t[:,1].min()>WHEELS["arch_y"]*sy+.33: continue
        n=max(1,math.ceil(max(np.linalg.norm(t[a]-t[b]) for a,b in ((0,1),(1,2),(2,0)))/.02))
        for i in range(n+1):
            js=np.arange(n+1-i)[:,None]/n
            cloud.append(t[0]+i/n*(t[1]-t[0])+js*(t[2]-t[0]))
    cloud=np.concatenate(cloud)
    poses=0
    for axle in (WHEELS["front_z"],WHEELS["rear_z"]):
        for s in (-1,1):
            rel=cloud-np.array([s*.78,WHEELS["arch_y"]*sy,axle*sy])
            for angle in (np.linspace(-.82,.82,33) if axle>0 else [0]):
                width=rel[:,0]*math.cos(angle)+rel[:,2]*math.sin(angle)
                radial=-rel[:,0]*math.sin(angle)+rel[:,2]*math.cos(angle)
                overlap=(np.abs(width)<.097)&(radial**2+rel[:,1]**2<.322**2)
                assert not overlap.any(),f"wheel collision at {axle}, {s}, {angle}"
                poses+=1
    with Image.open(TEXTURE) as texture:
        assert texture.mode=="RGBA" and texture.size==(256,256)
        assert texture.getchannel("A").getextrema()==(255,255)
        colors=len(set(texture.getdata()))
        assert colors<=96
    report=dict(asset="Alder Wayfarer",era=1984,triangles=len(triangles),vertices=len(v),
                bounds_min=lo.tolist(),bounds_max=hi.tolist(),wheel_anchors=WHEELS,
                player_roof_height=.32+(1.77-WHEELS["arch_y"])*sy,
                atlas=[256,256,"RGBA"],palette_colors=colors,
                checks=["symmetric body only", "four open arches", "wide intact hood",
                        "long wagon roof", "single-layer baked end details", "single-layer baked windows",
                        "continuous side trim",
                        "valid geometry and UVs"],
                steering=dict(poses=poses,max_lock=.82,sample_spacing_m=.02,samples=len(cloud)))
    (ROOT/"build/alder-wayfarer-fit-report.json").write_text(json.dumps(report,indent=2)+"\n")
    guide=Image.open(TEXTURE).copy()
    draw=ImageDraw.Draw(guide)
    for ids in np.asarray(indices).reshape(-1,3):
        points=[(v[i,6]*256,(1-v[i,7])*256) for i in ids]
        draw.line(points+[points[0]],fill=(248,176,55,255))
    guide.save(ROOT/"build/alder-wayfarer-uv-guide.png")
    print(json.dumps(report,indent=2))


def preview():
    body=read_part(MODEL/"body.emesh",TEXTURE)
    scale=np.array([.78/WHEELS["x"],2.7/3.25,2.7/3.25])
    body.positions*=scale
    body.normals/=scale
    body.normals/=np.linalg.norm(body.normals,axis=1)[:,None]
    wheel=read_part(ROOT/"assets/models/vehicles/common/wheel.emesh",ROOT/"assets/textures/vehicles/common/wheel.png")
    radius=max(np.ptp(wheel.positions[:,1]),np.ptp(wheel.positions[:,2]))*.5
    def parts(lock):
        items=[body]
        for axle in (WHEELS["front_z"],WHEELS["rear_z"]):
            angle=lock if axle>0 else 0
            c,s=math.cos(angle),math.sin(angle)
            rotation=np.array([[c,0,-s],[0,1,0],[s,0,c]])
            for side in (-1,1):
                p=wheel.positions*(.32/radius)@rotation.T
                p+=np.array([side*.78,WHEELS["arch_y"]*scale[1],axle*scale[2]])
                items.append(Part(p,wheel.normals@rotation.T,wheel.uvs,wheel.indices,wheel.texture))
        return items
    sheet=Image.new("RGB",(1440,388),(19,21,25))
    draw=ImageDraw.Draw(sheet)
    for i,(label,yaw,pitch,lock) in enumerate((
            ("FRONT",-32,18,0),("SIDE",-90,0,0),
            ("REAR / ROOF RACK",-148,24,0),("FULL STEERING LOCK",-32,30,.82))):
        sheet.paste(raster_view(parts(lock),yaw,pitch,360,350),(i*360,0))
        draw.text((i*360+12,363),label,fill=(225,216,194))
    sheet.save(ROOT/"build/alder-wayfarer-preview.png")


if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--blockout",action="store_true")
    parser.add_argument("--texture-only",action="store_true")
    args=parser.parse_args()
    make_texture(args.blockout)
    if not args.texture_only:
        subprocess.run(["/Applications/Blender.app/Contents/MacOS/Blender","--background",
                        "--factory-startup","--python",str(ROOT/"tools/alder_wayfarer_blender.py"),
                        "--","--mesh",str(MODEL/"body.emesh"),"--blend",str(MODEL/"source.blend")],check=True)
    validate()
    preview()
