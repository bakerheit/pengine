#!/usr/bin/env python3
"""Reproduce RIDGE's atlas, static Blender cook, structural QA and wheel views."""
import argparse
import hashlib
import json
import math
import subprocess
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
from alder_ridge_spec import ATLAS_SIZE, REGIONS, BOUNDS, SHAPE, WHEELS, LAMPS
from make_vesper_vx91_assets import read_emesh
from make_harrow_workman_assets import contains
from render_firetruck_preview import Part, read_part, raster_view

ROOT=Path(__file__).resolve().parents[1]
MODEL=ROOT/'assets/models/vehicles/alder_ridge'
TEXTURE=ROOT/'assets/textures/vehicles/alder_ridge/body.png'
OUT=ROOT/'build'
GREEN=(63,87,67,255)
LIGHT=(83,107,80,255)
GRAY=(133,128,113,255)
DARK=(24,30,29,255)
GLASS=(32,53,62,255)
MAX_CENTRAL_STEER=.82
# The inside front tire turns farther than the central steering angle.
MAX_WHEEL_LOCK=math.atan(2.7/(2.7/math.tan(MAX_CENTRAL_STEER)-.78))


def make_texture(blockout=False):
    image=Image.new('RGBA',(ATLAS_SIZE,ATLAS_SIZE),DARK)
    draw=ImageDraw.Draw(image)
    for name,rect in REGIONS.items():
        color={'TOP':LIGHT,'CLADDING':GRAY,'BLACK':DARK,'SHADOW':(35,40,33,255),
               'METAL':(161,165,155,255)}.get(name,GREEN)
        draw.rectangle(rect,fill=color)
    def point(name,a,b):
        x0,y0,x1,y1=REGIONS[name];(a0,a1),(b0,b1)=BOUNDS[name]
        return (round(x0+2+(a-a0)/(a1-a0)*(x1-x0-4)),round(y1-2-(b-b0)/(b1-b0)*(y1-y0-4)))
    def poly(name,points,color): draw.polygon([point(name,*p) for p in points],fill=color)
    def rect(name,a,b,c,d,color): poly(name,[(a,b),(c,b),(c,d),(a,d)],color)
    def line(name,points,color,width=1):draw.line([point(name,*p) for p in points],fill=color,width=width)
    # Every side facet uses the same coordinates, so trim never restarts at
    # an arch seam. Fender crowns and quarter panels use the same green.
    rect('SIDE',-2.08,.32,2.08,.76,GRAY)
    rect('SIDE',-2.08,.32,2.08,.40,(91,89,79,255))
    line('SIDE',[(-2.08,.77),(2.08,.77)],DARK,2)
    line('SIDE',[(-2.08,1.16),(2.08,1.16)],LIGHT)
    if not blockout:
        line('SIDE',[(-.57,1.24),(-.57,.43),(.62,.43),(.62,1.24)],(36,54,43,255))
        line('SIDE',[(-.42,1.07),(-.19,1.07)],DARK,3)
        line('SIDE',[(-.40,1.09),(-.20,1.09)],(158,164,143,255))
        line('SIDE',[(-1.97,1.08),(-1.74,1.08),(-1.74,.9),(-1.97,.9),(-1.97,1.08)],(42,61,45,255))
        rect('SIDE',1.85,.94,2.02,1.02,(205,135,53,255))
    # Windows are painted directly onto the cabin receiver. A thick B pillar
    # separates the long door from the rear quarter, with a slim vent divider.
    windows=[[(.50,1.33),(.25,1.90),(-.45,1.90),(-.45,1.33)],
             [(-.67,1.33),(-.67,1.90),(-1.84,1.90),(-1.95,1.33)]]
    for shape in windows:
        poly('CABIN',shape,DARK)
        coords=np.asarray(shape);center=coords.mean(0)
        poly('CABIN',(center+(coords-center)*.91).tolist(),GLASS)
    if not blockout:
        line('CABIN',[(.14,1.34),(-.01,1.87)],DARK,2)
        line('CABIN',[(-1.35,1.34),(-1.35,1.87)],DARK)
        for a,b in ((-1.78,-.76),(-.37,-.04)):
            poly('CABIN',[(a,1.77),(b,1.85),(b,1.80),(a,1.72)],(71,91,94,255))
    for name in ('WINDSHIELD','BACK_GLASS'):
        poly(name,[(-.85,1.33),(.85,1.33),(.76,1.91),(-.76,1.91)],DARK)
        poly(name,[(-.80,1.36),(.80,1.36),(.73,1.88),(-.73,1.88)],GLASS)
        if not blockout:
            poly(name,[(-.70,1.76),(.68,1.86),(.68,1.81),(-.70,1.71)],(72,94,97,255))
            if name=='BACK_GLASS':
                for h in (1.47,1.57,1.67): line(name,[(-.71,h),(.71,h)],(56,72,71,255))
                line(name,[(.0,1.36),(.45,1.53)],DARK,2)
            else:
                line(name,[(-.72,1.38),(-.10,1.42)],DARK)
                line(name,[(.1,1.38),(.65,1.42)],DARK)
    for name in ('FRONT','REAR'):
        rect(name,-1.06,.32,1.06,.70,GRAY)
        line(name,[(-1.06,.71),(1.06,.71)],DARK,2)
    rect('FRONT',-.52,.80,.52,1.08,DARK)
    if not blockout:
        for h in (.84,.91,.98,1.05):line('FRONT',[(-.48,h),(.48,h)],(113,125,111,255))
        rect('FRONT',-.05,.89,.05,.98,GRAY)
    for s in (-1,1):
        a,c=sorted((s*.61,s*.96))
        rect('FRONT',a,.79,c,1.10,DARK)
        rect('FRONT',a+.035,.825,c-.035,1.065,(225,224,193,255))
        rect('FRONT',a,.59,c,.68,(208,130,45,255))
        if not blockout:
            for x in np.linspace(a+.065,c-.065,4):line('FRONT',[(x,.85),(x,1.04)],(172,183,162,255))
        a,c=sorted((s*.80,s*.97))
        rect('REAR',a,.69,c,1.15,DARK)
        for h0,h1,color in ((.72,.85,(165,39,32,255)),(.85,.94,(204,209,182,255)),(.94,1.12,(177,47,33,255))):
            rect('REAR',a+.02,h0,c-.02,h1,color)
    if not blockout:
        line('REAR',[(-.70,1.21),(-.70,.59),(.70,.59),(.70,1.21)],(36,53,41,255))
        rect('REAR',-.20,.98,.20,1.025,DARK)
        rect('REAR',-.28,.66,.28,.81,DARK)
        rect('REAR',-.23,.685,.23,.78,(167,171,146,255))
        # Fictional plain RIDGE badge on the tailgate, no borrowed marks.
        glyphs=['110/101/110/101/101','111/010/010/010/111','110/101/101/101/110',
                '111/100/101/101/111','111/100/110/100/111']
        badge=Image.new('RGBA',(19,5),GREEN)
        bd=ImageDraw.Draw(badge)
        for i,glyph in enumerate(glyphs):
            for y,row in enumerate(glyph.split('/')):
                for x,pixel in enumerate(row):
                    if pixel=='1':bd.point((i*4+x,y),fill=(171,183,155,255))
        # Looking at the rear reverses model X; compensate only this text.
        image.paste(badge.transpose(Image.Transpose.FLIP_LEFT_RIGHT),point('REAR',-.34,.925))
        for x in (-.43,.43):line('TOP',[(x,.74),(x,1.96)],(99,119,87,255))
    TEXTURE.parent.mkdir(parents=True,exist_ok=True);image.save(TEXTURE)


def wheel_and_scale():
    wheel=read_part(ROOT/'assets/models/vehicles/common/wheel.emesh',ROOT/'assets/textures/vehicles/common/wheel.png')
    radius=max(np.ptp(wheel.positions[:,1]),np.ptp(wheel.positions[:,2]))*.5
    scale=np.array([.78/WHEELS['x'],2.7/(WHEELS['front_z']-WHEELS['rear_z']),2.7/(WHEELS['front_z']-WHEELS['rear_z'])])
    return wheel,float(radius),scale


def validate():
    vertices,indices=read_emesh(MODEL/'body.emesh');v=np.asarray(vertices)
    assert len(indices)%3==0 and min(indices)>=0 and max(indices)<len(v)
    assert np.isfinite(v).all() and ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all()
    tri=v[np.asarray(indices).reshape(-1,3),:3]
    assert SHAPE['triangle_budget'][0]<=len(tri)<=SHAPE['triangle_budget'][1],len(tri)
    area=np.linalg.norm(np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]),axis=1)
    assert (area>1e-9).all(),'degenerate triangle'
    lo,hi=v[:,:3].min(0),v[:,:3].max(0)
    assert np.allclose([hi[0]-lo[0],hi[2]-lo[2],hi[1]],[2.20,4.40,2.04],atol=1e-5)
    keys={tuple(np.round(p,4)) for p in v[:,:3]}
    assert all((-x,y,z) in keys for x,y,z in keys),'asymmetric geometry'
    for axle in (1.24,-1.40):
        for s in (-1,1):
            outside=tri[(s*tri[:,:,0]>.55).all(1)]
            for dz,dy in ((0,0),(.15,0),(-.15,0),(0,.17),(0,-.04)):
                assert not any(contains(np.array([axle+dz,.39+dy]),t[:,[2,1]]) for t in outside),'blocked side opening'
    for x in (-.8,-.4,0,.4,.8):
        for z in (1.19,1.29,-1.45,-1.35):
            assert any(contains(np.array([x,z]),t[:,[0,2]]) for t in tri if (t[:,1]>1.05).all()),'missing hood/deck'
    for x in (-.6,0,.6):
        for z in (-1.8,-1.0,0):
            assert any(contains(np.array([x,z]),t[:,[0,2]]) for t in tri if (t[:,1]>1.99).all()),'missing roof'
    # End/cabin details must use one receiver surface, never offset cards.
    for end in (-1,1):
        faces=tri[(np.abs(tri[:,:,2]-end*2.08)<1e-5).all(1)]
        for x in (-.863,-.31,.17,.863):
            for y in (.79,.93,1.09):
                assert sum(contains(np.array([x,y]),t[:,[0,1]]) for t in faces)==1,'layered fascia'
    for s in (-1,1):
        faces=tri[(s*tri[:,:,0]>.7).all(1)]
        for z in (-1.71,-.88,-.17):
            for y in (1.41,1.69,1.84):
                assert sum(contains(np.array([z,y]),t[:,[2,1]]) for t in faces)==1,'layered cabin'
    atlas_pixels=np.asarray(Image.open(TEXTURE))
    for name,regions in (('FRONT',LAMPS['headlight_rects']),('REAR',LAMPS['rear_red_rects'])):
        z=LAMPS['front_face_z' if name=='FRONT' else 'rear_face_z']
        faces=tri[(np.abs(tri[:,:,2]-z)<1e-5).all(1)]
        a,b,c,d=REGIONS[name];(x0,x1),(y0,y1)=BOUNDS[name]
        for region in regions:
            for s in (-1,1):
                for x in np.linspace(region[0],region[2],11):
                    for y in np.linspace(region[1],region[3],11):
                        assert any(contains(np.array([s*x,y]),t[:,[0,1]]) for t in faces),'lamp mask over empty air'
                        tx=int(a+2+(s*x-x0)/(x1-x0)*(c-a-4));ty=int(d-2-(y-y0)/(y1-y0)*(d-b-4))
                        r,g,bl,_=map(int,atlas_pixels[ty,tx])
                        assert (r>150 and g>150 and bl>140) if name=='FRONT' else (r>150 and g<70 and bl<70),'lamp mask crosses bezel or wrong lens color'
    a,b,c,d=REGIONS['SIDE']
    side=v[(v[:,6]>(a+1)/256)&(v[:,6]<(c-1)/256)&(v[:,7]>1-(d-1)/256)&(v[:,7]<1-(b+1)/256)]
    assert len(side)>100
    assert np.allclose(side[:,6],(a+2+(side[:,2]+2.08)/4.16*(c-a-4))/256,atol=1e-6)
    assert np.allclose(side[:,7],1-(d-2-(side[:,1]-.32)/.93*(d-b-4))/256,atol=1e-6)
    # Actual shared tire dimensions, enlarged by 2 mm, tested against a dense
    # sample of the fitted body throughout both steering directions.
    wheel,radius,scale=wheel_and_scale()
    wheel_radius=.32
    half_width=np.max(np.abs(wheel.positions[:,0]))*(wheel_radius/radius)
    cloud=[]
    for t in tri*scale:
        if t[:,1].min()>.39*scale[1]+wheel_radius+.005:continue
        n=max(1,math.ceil(max(np.linalg.norm(t[a]-t[b]) for a,b in ((0,1),(1,2),(2,0)))/.015))
        for i in range(n+1):
            js=np.arange(n+1-i)[:,None]/n
            cloud.append(t[0]+i/n*(t[1]-t[0])+js*(t[2]-t[0]))
    cloud=np.concatenate(cloud);poses=0
    for axle in (1.24,-1.40):
        for s in (-1,1):
            rel=cloud-np.array([s*.78,.39*scale[1],axle*scale[2]])
            for angle in (np.linspace(-MAX_WHEEL_LOCK,MAX_WHEEL_LOCK,41) if axle>0 else [0]):
                width=rel[:,0]*math.cos(angle)+rel[:,2]*math.sin(angle)
                radial=-rel[:,0]*math.sin(angle)+rel[:,2]*math.cos(angle)
                overlap=(np.abs(width)<half_width+.002)&(radial**2+rel[:,1]**2<(wheel_radius+.002)**2)
                assert not overlap.any(),f'wheel collision: axle={axle}, side={s}, angle={angle}, count={overlap.sum()}'
                poses+=1
    with Image.open(TEXTURE) as atlas:
        assert atlas.mode=='RGBA' and atlas.size==(256,256)
        assert atlas.getchannel('A').getextrema()==(255,255)
        colors=len(atlas.getcolors(65536));assert colors<=64
    report=dict(asset='ALDER RIDGE',era=1988,shape_contract=SHAPE,vertices=len(v),triangles=len(tri),
                bounds_min=lo.tolist(),bounds_max=hi.tolist(),dimensions=(hi-lo).tolist(),wheel_anchors=WHEELS,lamps=LAMPS,
                atlas=[256,256,'RGBA'],palette_colors=colors,uv_min=v[:,6:8].min(0).tolist(),uv_max=v[:,6:8].max(0).tolist(),
                player_scale=scale.tolist(),player_roof_height=float(.32+(2.04-.39)*scale[1]),
                source_wheel_radius=wheel_radius/scale[1],
                checks=['one joined static wheel-less body','symmetric geometry','four open side pockets',
                        'continuous hood and rear waist','intact roof','single-layer baked fascia and glass',
                        'continuous side UVs','lamp masks on actual faces and correct colored texels',
                        'valid indices and UVs','nondegenerate triangles','measured shared-wheel full-lock clearance'],
                steering=dict(poses=poses,max_central_steer=MAX_CENTRAL_STEER,max_tire_lock=MAX_WHEEL_LOCK,
                              sample_spacing_m=.015,samples=len(cloud),
                              radius_m=wheel_radius,half_width_m=float(half_width),safety_margin_m=.002),
                mesh_sha256=hashlib.sha256((MODEL/'body.emesh').read_bytes()).hexdigest(),
                atlas_sha256=hashlib.sha256(TEXTURE.read_bytes()).hexdigest())
    (OUT/'alder_ridge-fit-report.json').write_text(json.dumps(report,indent=2)+'\n')
    guide=Image.open(TEXTURE).copy();draw=ImageDraw.Draw(guide)
    for ids in np.asarray(indices).reshape(-1,3):
        points=[(v[i,6]*256,(1-v[i,7])*256) for i in ids]
        draw.line(points+[points[0]],fill=(237,164,73,255))
    guide.save(OUT/'alder_ridge-uv-guide.png')
    print(json.dumps(report,indent=2))


def preview(blockout=False):
    body=read_part(MODEL/'body.emesh',TEXTURE)
    wheel,radius,scale=wheel_and_scale()
    body.positions*=scale;body.normals/=scale;body.normals/=np.linalg.norm(body.normals,axis=1)[:,None]
    def parts(lock):
        result=[body]
        for axle in (1.24,-1.40):
            for side in (-1,1):
                a=0
                if axle>0 and lock:
                    a=math.atan(2.7/(2.7/math.tan(lock)+side*.78))
                c,s=math.cos(a),math.sin(a)
                rotation=np.array([[c,0,-s],[0,1,0],[s,0,c]])
                p=wheel.positions*(.32/radius)@rotation.T+np.array([side*.78,.39*scale[1],axle*scale[2]])
                result.append(Part(p,wheel.normals@rotation.T,wheel.uvs,wheel.indices,wheel.texture))
        return result
    views=[('FRONT',-32,18,0),('SIDE',-90,0,0),('REAR',-148,18,0),
           ('ELEVATED FRONT',-32,34,0),('FRONT ORTHO',0,0,0),('FULL LOCK',-32,28,.82)]
    sheet=Image.new('RGB',(1440,832),(20,23,26));draw=ImageDraw.Draw(sheet)
    for i,(label,yaw,pitch,lock) in enumerate(views):
        panel=raster_view(parts(lock),yaw,pitch,480,390)
        suffix='blockout' if blockout else label.lower().replace(' ','-')
        if not blockout:panel.save(OUT/f'alder_ridge-{suffix}.png')
        x=(i%3)*480;y=(i//3)*416;sheet.paste(panel,(x,y));draw.text((x+12,y+399),label,fill=(229,231,213))
    sheet.save(OUT/('alder_ridge-blockout.png' if blockout else 'alder_ridge-preview.png'))


def asset_lab():
    results=[]
    for label,yaw in (('front',190),('rear',32)):
        screenshot=OUT/f'alder_ridge-engine-{label}.png'
        cmd=[str(ROOT/'build/bin/apricot_asset_lab'),'--model','models/vehicles/alder_ridge/body.emesh',
             '--texture','textures/vehicles/alder_ridge/body.png','--yaw',str(yaw),'--zoom','.58',
             '--frames','120','--screenshot',str(screenshot)]
        result=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True)
        log=result.stdout+result.stderr
        (OUT/f'alder_ridge-engine-{label}.log').write_text(log)
        result.check_returncode();assert '0 GL errors' in log
        results.append(dict(view=label,frames=120,gl_errors=0,screenshot=str(screenshot)))
    (OUT/'alder_ridge-renderer-report.json').write_text(json.dumps(results,indent=2)+'\n')
    print('Asset Lab: front/rear passed, 120 frames each, 0 GL errors')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--blockout',action='store_true');p.add_argument('--texture-only',action='store_true')
    p.add_argument('--asset-lab',action='store_true')
    args=p.parse_args();OUT.mkdir(parents=True,exist_ok=True)
    make_texture(args.blockout)
    if not args.texture_only:
        subprocess.run(['/Applications/Blender.app/Contents/MacOS/Blender','--background','--factory-startup','--python',
                        str(ROOT/'tools/alder_ridge_blender.py'),'--','--mesh',str(MODEL/'body.emesh'),
                        '--blend',str(MODEL/'source.blend')],check=True)
    preview(args.blockout)
    validate()
    if args.asset_lab:asset_lab()
