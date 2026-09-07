#!/usr/bin/env python3
"""Reproducible pickup cook, pixel atlas and geometry/steering regression gate."""
import argparse
import json
import math
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from harrow_workman_spec import ATLAS_SIZE, REGIONS, SHAPE, WHEELS, DOOR, DRIVER
from make_vesper_vx91_assets import read_emesh

ROOT=Path(__file__).resolve().parents[1]
MODEL=ROOT/"assets/models/vehicles/harrow_workman"
TEXTURE=ROOT/"assets/textures/vehicles/harrow_workman/body.png"


def make_texture(blockout=False):
    im=Image.new("RGBA",(256,256),(15,22,28,255))
    d=ImageDraw.Draw(im)
    palette={"BODY_SIDE":(64,101,126),"BODY_TOP":(82,122,145),
        "DOOR":(64,101,126),"GLASS":(24,44,56),"BED":(47,74,89),
        "TAILGATE":(64,101,126),"GRILLE":(22,28,31),"METAL":(147,155,154),
        "HEADLIGHT":(223,221,194),"AMBER":(219,137,42),"TAIL":(164,43,37),
        "REVERSE":(213,215,196),"BLACK":(11,17,22),"CLADDING":(30,38,42),
        "DASH":(29,36,40),"SEAM":(26,49,62),"SHADOW":(26,36,42)}
    for key,box in REGIONS.items():
        d.rectangle(box,fill=palette[key]+(255,))
    if not blockout:
        def rect(box,colour): d.rectangle(box,fill=colour+(255,))
        # Quiet paint bands; no random speckle or baked dramatic lighting.
        for name in ("BODY_SIDE","DOOR","TAILGATE"):
            x0,y0,x1,y1=REGIONS[name]
            rect((x0,y0,x1,y0+5),(89,126,147))
            rect((x0,y1-11,x1,y1),(49,77,95))
            rect((x0,y1-6,x1,y1-4),(61,83,94))
        x0,y0,x1,y1=REGIONS["BODY_TOP"]
        rect((x0+9,y0,x0+13,y1),(100,137,155))
        rect((x1-13,y0,x1-9,y1),(100,137,155))
        # Door seams, stamping and handle are painted, not box geometry.
        x0,y0,x1,y1=REGIONS["DOOR"]
        d.rectangle((x0+3,y0+3,x1-3,y1-3),outline=(30,58,73,255),width=1)
        rect((x0+8,y0+9,x0+23,y0+12),(23,33,38))
        rect((x0+9,y0+9,x0+22,y0+10),(161,170,165))
        rect((x0+5,y0+22,x1-5,y0+23),(93,125,142))
        rect((x0+5,y0+24,x1-5,y0+25),(46,77,97))
        # Tint + a restrained sky reflection and dark bench/dashboard silhouette.
        x0,y0,x1,y1=REGIONS["GLASS"]
        rect((x0,y0,x1,y0+15),(51,77,88))
        d.polygon(((x0+5,y0+16),(x1-5,y0+6),(x1-5,y0+10),(x0+5,y0+21)),fill=(83,111,119,255))
        rect((x0,y1-17,x1,y1),(14,26,34))
        rect((x0+14,y1-23,x1-14,y1-16),(32,44,49))
        # Longitudinal pressed bed ribs, concentrated in this dedicated UV tile.
        x0,y0,x1,y1=REGIONS["BED"]
        for x in range(x0+7,x1-4,9):
            rect((x,y0+3,x+1,y1-3),(66,92,104))
            rect((x+2,y0+3,x+3,y1-3),(30,52,66))
        rect((x0+3,y1-6,x1-3,y1-4),(91,92,81))
        x0,y0,x1,y1=REGIONS["TAILGATE"]
        d.rectangle((x0+7,y0+11,x1-7,y1-8),outline=(38,70,90,255),width=2)
        rect((x0+9,y0+13,x1-9,y0+14),(101,134,151))
        rect((x0+41,y0+5,x0+59,y0+9),(24,38,45))
        rect((x0+43,y0+5,x0+57,y0+6),(148,161,163))
        x0,y0,x1,y1=REGIONS["GRILLE"]
        d.rectangle((x0+2,y0+2,x1-2,y1-2),outline=(146,158,158,255),width=2)
        for y in range(y0+7,y1-4,5): rect((x0+4,y,x1-4,y+1),(116,133,139))
        rect((x0+39,y0+4,x0+41,y1-4),(71,88,96))
        for name in ("HEADLIGHT","AMBER","TAIL","REVERSE"):
            x0,y0,x1,y1=REGIONS[name]
            d.rectangle((x0+2,y0+2,x1-2,y1-2),outline=(40,46,45,255),width=2)
            base=palette[name]
            bright=tuple(min(255,c+25) for c in base)
            for x in range(x0+6,x1-3,4): rect((x,y0+5,x,y1-5),bright)
            rect((x0+5,y0+5,x1-5,y0+6),bright)
        x0,y0,x1,y1=REGIONS["METAL"]
        rect((x0,y0,x1,y0+5),(190,195,183))
        rect((x0,y1-5,x1,y1),(57,69,76))
    TEXTURE.parent.mkdir(parents=True,exist_ok=True)
    im.save(TEXTURE)


def contains(p,tri):
    a,b,c=tri
    def cross(u,v): return u[0]*v[1]-u[1]*v[0]
    signs=[cross(b-a,p-a),cross(c-b,p-b),cross(a-c,p-c)]
    return min(signs)>1e-7 or max(signs)<-1e-7


def validate_rear_surface_layers(triangles):
    """Each exposed rail/lamp sample must have exactly one coplanar skin."""
    def layers(point, axis, plane, axes):
        candidates=triangles[(np.abs(triangles[:,:,axis]-plane)<1e-5).all(axis=1)]
        return sum(contains(np.array(point),t[:,axes]) for t in candidates)

    for side in (-1,1):
        for x in (.883,.919,.977,1.007):
            for z in (-2.503,-2.317,-1.873,-1.283,-.683):
                assert layers((side*x,z),1,1.22,[0,2])==1,"overlapping or missing bed rail skin"
        for height in (.673,.721,.763,.827,1.033):
            assert layers((side*.951,height),2,-2.547,[0,1])==1,"overlapping or missing rear lamp skin"
        for x in (.853,.865,.913):
                assert layers((side*x,1.183),2,-2.54,[0,1])==1,"overlapping or missing tailgate corner skin"


def covers(point, triangle):
    # Inclusive barycentric test, including the shared diagonal of a quad.
    a,b,c=triangle;u=b-a;v=c-a;q=np.asarray(point)-a
    determinant=u[0]*v[1]-u[1]*v[0]
    if abs(determinant)<1e-9: return False
    s=(q[0]*v[1]-q[1]*v[0])/determinant
    t=(u[0]*q[1]-u[1]*q[0])/determinant
    return s>=-1e-6 and t>=-1e-6 and s+t<=1+1e-6


def validate_cab(legacy):
    meshes={}
    for name in ("body_open","driver_door"):
        vertices,indices=read_emesh(MODEL/(name+".emesh"))
        v=np.asarray(vertices); ids=np.asarray(indices).reshape(-1,3)
        assert len(ids) and ids.min()>=0 and ids.max()<len(v),name
        assert np.isfinite(v).all() and ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all(),name
        triangles=v[ids,:3]
        areas=np.linalg.norm(np.cross(triangles[:,1]-triangles[:,0],triangles[:,2]-triangles[:,0]),axis=1)
        assert (areas>1e-9).all(),name+": degenerate triangles"
        assert np.allclose(np.linalg.norm(v[:,3:6],axis=1),1,atol=1e-4),name+": invalid normals"
        meshes[name]=triangles
    body,door=meshes['body_open'],meshes['driver_door']
    joined=np.concatenate((body,door)).reshape(-1,3)
    assert np.allclose(joined.min(0),legacy.min(0),atol=1e-5),'articulated bounds minimum changed'
    assert np.allclose(joined.max(0),legacy.max(0),atol=1e-5),'articulated bounds maximum changed'
    assert len(body)+len(door)<=SHAPE['triangle_budget'][1],'articulated budget exceeded'
    outside=body[(body[:,:,0]>.78).all(1)]
    doorway_samples=0
    for z in np.linspace(-.35,.65,9):
        for y in np.linspace(.58,1.18,7):
            assert not any(covers((z,y),t[:,[2,1]]) for t in outside),'driver doorway still solid'
            assert any(covers((z,y),t[:,[2,1]]) for t in door),'door panel missing'
            doorway_samples+=1
    for z in (-.30,0,.25):
        for y in (1.40,1.60,1.80):
            assert not any(covers((z,y),t[:,[2,1]]) for t in outside),'cab window still solid'
            assert not any(covers((z,y),t[:,[2,1]]) for t in door),'driver window not rolled down'
            doorway_samples+=1
    # Rays through the shoulder/head volume must not encounter the old solid
    # CabUpper floor or roof. Feet retain real support below the footwell.
    ceiling=body[(body[:,:,1]>1.40).all(1) & (body[:,:,1]<1.929).all(1)]
    floor=body[np.isclose(body[:,:,1],DRIVER['floor_y'],atol=1e-6).all(1)]
    roof=body[np.isclose(body[:,:,1],DRIVER['roof_inner_y'],atol=1e-6).all(1)]
    for x in (.20,.43,.66):
        for z in (-.26,-.10,.10):
            assert not any(covers((x,z),t[:,[0,2]]) for t in ceiling),'cab headroom obstructed'
            assert any(covers((x,z),t[:,[0,2]]) for t in roof),'roof inner surface missing'
        for z in (.40,.60):
            assert any(covers((x,z),t[:,[0,2]]) for t in floor),'footwell floor missing'
    cushions=body[np.isclose(body[:,:,1],DRIVER['cushion_top_y'],atol=1e-6).all(1)]
    assert any(covers((.43,-.12),t[:,[0,2]]) for t in cushions),'driver cushion misplaced'
    # Ignore the two intentional legacy paint/glass cards. Every remaining
    # panel/frame/mirror volume must have paired edges, including shared seams.
    solids=door[~(np.isclose(door[:,:,0],.964,atol=1e-6).all(1) |
                  np.isclose(door[:,:,0],1.112,atol=1e-6).all(1))]
    edges={}
    for triangle in solids:
        keys=[tuple(np.round(p,6)) for p in triangle]
        for a,b in ((0,1),(1,2),(2,0)):
            edge=tuple(sorted((keys[a],keys[b])))
            edges[edge]=edges.get(edge,0)+1
    assert all(n%2==0 for n in edges.values()),'uncapped door/frame/mirror'
    assert door[:,:,0].min()>.80,'door includes thick cab interior'
    angle=math.radians(DOOR['open_degrees']); c,s=math.cos(angle),math.sin(angle)
    rotation=np.array([[c,0,s],[0,1,0],[-s,0,c]])
    scale=np.array([.78/WHEELS['x'],2.7/3.2,2.7/3.2])
    hinge=np.array(DOOR['hinge'])*scale; handle=np.array(DOOR['handle'])*scale
    opened=(handle-hinge)@rotation.T+hinge
    assert opened[0]>handle[0]+.6,'door opens inward'
    validate_rear_surface_layers(body)
    return dict(triangles={k:len(v) for k,v in meshes.items()},doorway_samples=doorway_samples,
        driver=DRIVER,door=DOOR,texture=str(TEXTURE),checks=[
            'hollow cab and true driver doorway','rolled-down driver window',
            'floor, cushion and roof match agreed anchors','capped thin panel, frame and mirror',
            'outward fitted hinge rotation','legacy outer bounds and rear surfaces preserved'])


def preview_cab():
    from render_firetruck_preview import Part,read_part,raster_view
    scale=np.array([.78/WHEELS['x'],2.7/3.2,2.7/3.2])
    def fitted(mesh,texture):
        part=read_part(mesh,texture)
        part.positions*=scale; part.normals/=scale
        part.normals/=np.linalg.norm(part.normals,axis=1)[:,None]
        return part
    old=fitted(MODEL/'body_surface.emesh',TEXTURE.with_name('body_surface.png'))
    body=fitted(MODEL/'body_open.emesh',TEXTURE)
    door=fitted(MODEL/'driver_door.emesh',TEXTURE)
    wheel=read_part(ROOT/'assets/models/vehicles/common/wheel.emesh',
                    ROOT/'assets/textures/vehicles/common/wheel.png')
    radius=max(np.ptp(wheel.positions[:,1]),np.ptp(wheel.positions[:,2]))*.5
    wheels=[]
    for x in (-WHEELS['x'],WHEELS['x']):
        for z in (WHEELS['front_z'],WHEELS['rear_z']):
            centre=np.array([x,WHEELS['arch_y'],z])*scale
            wheels.append(Part(wheel.positions*(.32/radius)+centre,wheel.normals,
                               wheel.uvs,wheel.indices,wheel.texture))
    angle=math.radians(DOOR['open_degrees']);c,s=math.cos(angle),math.sin(angle)
    rotation=np.array([[c,0,s],[0,1,0],[-s,0,c]])
    hinge=np.array(DOOR['hinge'])*scale
    opened=Part((door.positions-hinge)@rotation.T+hinge,door.normals@rotation.T,
                door.uvs,door.indices,door.texture)
    sheet=Image.new('RGB',(1440,820),(19,21,25));draw=ImageDraw.Draw(sheet)
    views=[('LEGACY BAKED PASSENGER',[old],32,20),('HOLLOW CAB PASSENGER',[body,door],32,20),
           ('OPEN DRIVER DOOR',[body,opened],-32,25),
           ('LEGACY BAKED DRIVER',[old],-90,12),('ROLLED-DOWN DRIVER WINDOW',[body,door],-90,12),
           ('CAB / DOORWAY',[body,opened],-65,35)]
    for i,(label,parts,yaw,pitch) in enumerate(views):
        x,y=(i%3)*480,(i//3)*410
        sheet.paste(raster_view(parts+wheels,yaw,pitch,480,380),(x,y))
        draw.text((x+12,y+389),label,fill=(230,230,230))
    sheet.save(ROOT/'build/harrow-workman-door-preview.png')


def validate():
    verts,indices=read_emesh(MODEL/"body.emesh")
    v=np.asarray(verts)
    assert np.isfinite(v).all() and min(indices)>=0 and max(indices)<len(v)
    triangles=v[np.asarray(indices).reshape(-1,3),:3]
    validate_rear_surface_layers(triangles)
    areas=np.linalg.norm(np.cross(triangles[:,1]-triangles[:,0],triangles[:,2]-triangles[:,0]),axis=1)
    assert (areas>1e-9).all(),"degenerate triangles"
    assert SHAPE["triangle_budget"][0]<=len(triangles)<=SHAPE["triangle_budget"][1],len(triangles)
    assert ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all()
    lo,hi=v[:,:3].min(axis=0),v[:,:3].max(axis=0)
    assert abs(lo[0]+hi[0])<.001 and abs(lo[2]+hi[2])<.001
    assert 5.3<hi[2]-lo[2]<5.5 and 1.9<hi[1]<2.1
    # Mirrored positions including all tiny detail panels.
    keys={tuple(np.round(p,4)) for p in v[:,:3]}
    assert all((-x,y,z) in keys for x,y,z in keys)
    for side in (-1,1):
        doors=v[np.abs(v[:,0]-side*.964)<1e-5]
        assert len(doors)==6 and (side*doors[:,3]>.99).all(),"inward door panels"
    for axle in (WHEELS["front_z"],WHEELS["rear_z"]):
        for s in (-1,1):
            side_tris=triangles[(s*triangles[:,:,0]>.85).all(axis=1)]
            for dz,dy in ((0,0),(.12,0),(-.12,0),(0,.15),(0,-.12)):
                p=np.array([axle+dz,WHEELS["arch_y"]+dy])
                assert not any(contains(p,t[:,[2,1]]) for t in side_tris),"closed wheel opening"
    for x in (-.75,0,.75):
        for z in (1.60,1.70):
            assert any(contains(np.array([x,z]),t[:,[0,2]]) for t in triangles
                       if (t[:,1]>1.10).all()),"missing hood"
    for x in (-.3,.3):
        for z in (-2.3,-1.8,-1.2,-.8):
            assert not any(contains(np.array([x,z]),t[:,[0,2]]) for t in triangles
                           if (t[:,1]>.70).all()),"bed is capped"
    # Sample body surfaces at <=2 cm spacing against the fitted, finite-width
    # tire cylinders through the entire +/-0.82 rad steering range. This is a
    # sampled intersection gate, not an exact collision/physics proof.
    sx=.78/WHEELS["x"]
    sy=2.7/(WHEELS["front_z"]-WHEELS["rear_z"])
    scaled=triangles*np.array([sx,sy,sy])
    cloud=[]
    for t in scaled:
        if t[:,1].min()>(WHEELS["arch_y"]*sy+.33): continue
        n=max(1,math.ceil(max(np.linalg.norm(t[i]-t[j]) for i,j in ((0,1),(1,2),(2,0)))/.02))
        for i in range(n+1):
            js=np.arange(n+1-i)[:,None]/n
            cloud.append(t[0]+(i/n)*(t[1]-t[0])+js*(t[2]-t[0]))
    cloud=np.concatenate(cloud)
    samples=0
    for axle in (WHEELS["front_z"],WHEELS["rear_z"]):
        for s in (-1,1):
            rel=cloud-np.array([s*.78,WHEELS["arch_y"]*sy,axle*sy])
            for angle in (np.linspace(-.82,.82,33) if axle>0 else [0]):
                width=rel[:,0]*math.cos(angle)+rel[:,2]*math.sin(angle)
                radial_z=-rel[:,0]*math.sin(angle)+rel[:,2]*math.cos(angle)
                overlap=(np.abs(width)<.097)&(radial_z**2+rel[:,1]**2<.322**2)
                assert not overlap.any(),(f"tire/body intersection: axle={axle}, side={s}, steer={angle}, "
                    f"source samples={(cloud[overlap][:8]/np.array([sx,sy,sy])).tolist()}")
                samples+=1
    with Image.open(TEXTURE) as im:
        assert im.mode=="RGBA" and im.size==(256,256)
        assert im.getchannel("A").getextrema()==(255,255)
        colours=len(set(im.getdata()))
        assert colours<=96
    report=dict(asset="Harrow Workman",triangles=len(triangles),vertices=len(v),
        articulated=validate_cab(v[:,:3]),
        bounds_min=lo.tolist(),bounds_max=hi.tolist(),wheel_anchors=WHEELS,
        atlas=[256,256,"RGBA"],palette_colours=colours,
        checks=["mirrored body", "outward door faces", "four side openings", "wide intact hood",
                "open cargo bed", "single-layer rear rails, corners and lamp lenses",
                "finite geometry and valid UVs"],
        steering_test=dict(method="body surface samples <=2cm vs finite tire cylinder",
                           poses=samples,surface_samples=len(cloud),max_lock_radians=.82))
    (ROOT/"build/harrow-workman-fit-report.json").write_text(json.dumps(report,indent=2)+"\n")
    guide=Image.open(TEXTURE).copy()
    d=ImageDraw.Draw(guide)
    for ids in np.asarray(indices).reshape(-1,3):
        pts=[(v[i,6]*256,(1-v[i,7])*256) for i in ids]
        d.line(pts+[pts[0]],fill=(240,165,63,255))
    guide.save(ROOT/"build/harrow-workman-uv-guide.png")
    print(json.dumps(report,indent=2))


if __name__=="__main__":
    p=argparse.ArgumentParser()
    p.add_argument("--blockout",action="store_true")
    p.add_argument("--texture-only",action="store_true")
    p.add_argument("--validate-only",action="store_true")
    p.add_argument("--preview-only",action="store_true")
    args=p.parse_args()
    if args.preview_only:
        preview_cab()
        raise SystemExit(0)
    if not args.validate_only: make_texture(args.blockout)
    if not args.texture_only and not args.validate_only:
        subprocess.run(["/Applications/Blender.app/Contents/MacOS/Blender",
            "--background","--factory-startup","--python",str(ROOT/"tools/harrow_workman_blender.py"),
            "--","--mesh",str(MODEL/"body.emesh"),"--blend",str(MODEL/"source.blend")],check=True)
    validate()
    if not args.validate_only:
        from bake_vehicle_surfaces import bake_if_canonical
        bake_if_canonical("harrow_workman", MODEL/"body.emesh", TEXTURE)
