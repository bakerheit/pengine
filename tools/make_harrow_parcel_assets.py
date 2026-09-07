#!/usr/bin/env python3
"""Cook HARROW PARCEL, its direct receiver atlas, and measured wheel-fit QA.

No fleet bake recipe is required: glass, seams and lamps are already painted
onto their final receiver surfaces. No floating decal skins are exported.
"""
import argparse
import hashlib
import json
import math
import struct
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from harrow_parcel_spec import ATLAS_SIZE, REGIONS, BOUNDS, WHEELS, SHAPE, LAMPS
from make_vesper_vx91_assets import read_emesh
from render_firetruck_preview import Part, read_part, raster_view

ROOT=Path(__file__).resolve().parents[1]
MODEL=ROOT/'assets/models/vehicles/harrow_parcel'
TEXTURE=ROOT/'assets/textures/vehicles/harrow_parcel/body.png'
PREFIX=ROOT/'build/harrow_parcel'
SCALE=np.array([.78/WHEELS['x'],2.7/(WHEELS['front_z']-WHEELS['rear_z']),2.7/(WHEELS['front_z']-WHEELS['rear_z'])])


def output(suffix):
    return Path(str(PREFIX)+suffix)


def make_texture(blockout=False):
    cream=(214,211,187,255)
    light=(231,228,205,255)
    teal=(71,109,105,255)
    dark=(35,42,42,255)
    seam=(129,138,125,255)
    im=Image.new('RGBA',(ATLAS_SIZE,ATLAS_SIZE),dark)
    d=ImageDraw.Draw(im)
    palette={'SIDE':cream,'TOP':light,'FRONT':cream,'REAR':cream,'SCREEN':cream,
             'RUBBER':(36,40,40,255),'SHADOW':(29,33,33,255),'METAL':(122,145,145,255),
             'TEAL':teal,'CREAM':cream,'DARK':dark}
    for name,box in REGIONS.items():d.rectangle(box,fill=palette[name])
    def point(name,p):
        (a,b),(c,e)=BOUNDS[name]
        x0,y0,x1,y1=REGIONS[name]
        return (round(x0+2+(p[0]-a)/(b-a)*(x1-x0-4)),round(y1-2-(p[1]-c)/(e-c)*(y1-y0-4)))
    def poly(name,pts,color):d.polygon([point(name,p) for p in pts],fill=color)
    def rect(name,a,b,c,e,color):poly(name,[(a,b),(c,b),(c,e),(a,e)],color)
    def line(name,pts,color=seam,width=1):d.line([point(name,p) for p in pts],fill=color,width=width)
    for name in ('SIDE','FRONT','REAR'):
        (a,b),_=BOUNDS[name]
        rect(name,a,.26,b,.79,teal)
        rect(name,a,.79,b,.825,(99,132,122,255))
        rect(name,a,.26,b,.33,(52,80,78,255))
    if not blockout:
        # Cab glass follows the actual sloped roof and is painted on the shell.
        window=[(.02,1.40),(1.34,1.40),(.77,2.20),(.02,2.20)]
        poly('SIDE',window,dark)
        poly('SIDE',[(.09,1.46),(1.23,1.46),(.72,2.12),(.09,2.12)],(43,66,72,255))
        poly('SIDE',[(.10,1.93),(.85,1.93),(.72,2.12),(.10,2.12)],(70,93,98,255))
        line('SIDE',[(.10,1.87),(.75,2.04)],(110,128,126,255),2)
        line('SIDE',[(.90,1.44),(.62,2.17)],dark,1)
        # Both flanks have a sliding panel. Subtle stamp line, rail and handles.
        line('SIDE',[(-1.75,1.07),(-1.75,2.18),(-.17,2.18),(-.17,.38)],seam)
        line('SIDE',[(-.10,.36),(-.10,2.23),(.77,2.23)],seam)
        line('SIDE',[(-2.40,1.16),(-.22,1.16)],(155,162,146,255))
        line('SIDE',[(-2.40,1.13),(-.22,1.13)],(106,123,113,255))
        rect('SIDE',-.48,1.24,-.24,1.29,dark)
        rect('SIDE',.02,1.23,.23,1.28,dark)
        rect('SIDE',2.22,.98,2.41,1.05,(214,148,58,255))
        # Hood pressings and quiet roof gutters, continuous across stations.
        for x in (-.78,.78):line('TOP',[(x,-2.44),(x,.62)],(204,203,181,255))
        for x in (-.63,.63):line('TOP',[(x,1.59),(x,2.37)],(199,200,179,255))
        # Broad single windshield with rubber seal and restrained pixel glare.
        poly('SCREEN',[(-.88,1.41),(.88,1.41),(.80,2.28),(-.80,2.28)],dark)
        poly('SCREEN',[(-.82,1.47),(.82,1.47),(.75,2.22),(-.75,2.22)],(39,61,68,255))
        poly('SCREEN',[(-.78,2.04),(.77,2.04),(.75,2.22),(-.75,2.22)],(66,89,94,255))
        line('SCREEN',[(-.72,1.95),(.62,2.12)],(98,116,117,255),2)
        line('SCREEN',[(-.69,1.49),(-.15,1.57)],dark,2)
        line('SCREEN',[(.12,1.49),(.65,1.57)],dark,2)
        # Square sealed beams, small grille, amber turn cells. No badge text.
        rect('FRONT',-.49,.75,.49,1.05,dark)
        for y in (.80,.88,.96):line('FRONT',[(-.43,y),(.43,y)],(86,97,92,255))
        for s in (-1,1):
            a,c=sorted((s*.57,s*.94))
            rect('FRONT',a,.81,c,1.12,dark)
            rect('FRONT',a+.025,.84,c-.025,1.09,(225,222,191,255))
            for x in np.linspace(a+.05,c-.05,4):line('FRONT',[(x,.86),(x,1.07)],light)
            rect('FRONT',a,.64,c,.74,(217,147,56,255))
        # Tall paired cargo doors. All seams and hinges lie in the rear atlas.
        for s in (-1,1):
            a,c=sorted((s*.055,s*.83))
            line('REAR',[(a,.57),(a,2.25),(c,2.25),(c,.57)],seam)
            a,c=sorted((s*.90,s*1.015))
            rect('REAR',a,.79,c,1.38,dark)
            rect('REAR',a+.01,1.18,c-.01,1.36,(206,141,58,255))
            rect('REAR',a+.01,.91,c-.01,1.16,(162,50,40,255))
            rect('REAR',a+.01,.81,c-.01,.89,(221,214,181,255))
            for h in (.78,1.95):
                a,c=sorted((s*.76,s*.91))
                rect('REAR',a,h,c,h+.055,(122,132,120,255))
        line('REAR',[(0,.54),(0,2.30)],(84,101,94,255))
        rect('REAR',.09,1.03,.15,1.27,dark)
        rect('REAR',-.42,.59,.06,.73,(162,169,145,255))
        for name in ('RUBBER','METAL'):
            x0,y0,x1,y1=REGIONS[name]
            d.rectangle((x0,y0,x1,y0+5),fill=(65,72,69,255) if name=='RUBBER' else (158,173,162,255))
    TEXTURE.parent.mkdir(parents=True,exist_ok=True)
    im.save(TEXTURE)


def contains(p,t):
    a,b,c=t
    cross=lambda x,y:x[0]*y[1]-x[1]*y[0]
    values=[cross(b-a,p-a),cross(c-b,p-b),cross(a-c,p-c)]
    return min(values)>1e-7 or max(values)<-1e-7


def wheel_part():
    return read_part(ROOT/'assets/models/vehicles/common/wheel.emesh',ROOT/'assets/textures/vehicles/common/wheel.png')


def validate(blockout=False):
    vertices,indices=read_emesh(MODEL/'body.emesh')
    v=np.asarray(vertices)
    assert len(indices)%3==0 and len(indices)>0
    assert np.isfinite(v).all() and min(indices)>=0 and max(indices)<len(v)
    tris=v[np.asarray(indices).reshape(-1,3),:3]
    areas=np.linalg.norm(np.cross(tris[:,1]-tris[:,0],tris[:,2]-tris[:,0]),axis=1)
    assert (areas>1e-9).all(),'degenerate triangles'
    assert SHAPE['triangle_budget'][0]<=len(tris)<=SHAPE['triangle_budget'][1],len(tris)
    assert ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all()
    lo,hi=v[:,:3].min(0),v[:,:3].max(0)
    assert np.allclose(hi-lo,[2.26,2.13,5.40],atol=.001),(lo,hi)
    keys={tuple(np.round(p,4)) for p in v[:,:3]}
    assert all((-x,y,z) in keys for x,y,z in keys),'asymmetric body'
    for axle in (WHEELS['front_z'],WHEELS['rear_z']):
        for s in (-1,1):
            outer=tris[(s*tris[:,:,0]>.53).all(1)]
            for dz,dy in ((.013,.017),(.15,.017),(-.15,.017),(.013,.16),(.013,-.14)):
                p=np.array([axle+dz,WHEELS['arch_y']+dy])
                assert not any(contains(p,t[:,[2,1]]) for t in outer),'closed wheel opening'
    # Connected broad roof/hood and the central cargo floor cannot disappear
    # when changing wheel pockets. Offset points avoid triangulation diagonals.
    for x in (-.80,-.32,.023,.37,.81):
        for z in (-2.31,-1.51,-.71,.31,1.59,1.83,2.23):
            assert any(contains(np.array([x,z]),t[:,[0,2]]) for t in tris if (t[:,1]>1.1).all()),'missing roof or hood'
    for x in (-.32,.23):
        for z in (-1.71,-.81,.31,1.63):
            assert any(contains(np.array([x,z]),t[:,[0,2]]) for t in tris if (t[:,1]<.40).all()),'missing central floor'
    # Exact shared-wheel measured bounds set the finite tire-cylinder envelope.
    wheel=wheel_part()
    radius=max(np.ptp(wheel.positions[:,1]),np.ptp(wheel.positions[:,2]))*.5
    wheel_scale=.32/radius
    halfwidth=np.max(np.abs(wheel.positions[:,0]))*wheel_scale
    scaled=tris*SCALE
    cloud=[]
    for t in scaled:
        if t[:,1].min()>WHEELS['arch_y']*SCALE[1]+.324:continue
        n=max(1,math.ceil(max(np.linalg.norm(t[i]-t[j]) for i,j in ((0,1),(1,2),(2,0)))/.02))
        for i in range(n+1):
            js=np.arange(n+1-i)[:,None]/n
            cloud.append(t[0]+i/n*(t[1]-t[0])+js*(t[2]-t[0]))
    cloud=np.concatenate(cloud)
    poses=0
    for axle in (WHEELS['front_z'],WHEELS['rear_z']):
        for side in (-1,1):
            rel=cloud-np.array([side*.78,WHEELS['arch_y']*SCALE[1],axle*SCALE[2]])
            for angle in (np.linspace(-.82,.82,33) if axle>0 else [0]):
                width=rel[:,0]*math.cos(angle)+rel[:,2]*math.sin(angle)
                radial=-rel[:,0]*math.sin(angle)+rel[:,2]*math.cos(angle)
                bad=(np.abs(width)<halfwidth+.002)&(radial**2+rel[:,1]**2<.322**2)
                assert not bad.any(),f'tire contact: {axle}, {side}, {angle}, {(cloud[bad][:5]/SCALE).tolist()}'
                poses+=1
    with Image.open(TEXTURE) as atlas:
        assert atlas.size==(256,256) and atlas.mode=='RGBA'
        assert atlas.getchannel('A').getextrema()==(255,255)
    # Verify real receiver coverage and exact sampled atlas colors for the
    # proposed shared shader profiles, on both mirrored sides.
    rgba=np.asarray(Image.open(TEXTURE))
    lamp_checks={}
    all_tri=v[np.asarray(indices).reshape(-1,3)]
    for name,lamp in LAMPS.items():
        x0,y0,x1,y1,z0,z1=lamp['profile_rect']
        faces=all_tri[(np.abs(all_tri[:,:,2]-lamp['face_z'])<1e-5).all(1)]
        count=0
        for side in (-1,1):
            for x in np.linspace(x0+.00031,x1-.00027,7):
                for y in np.linspace(y0+.00037,y1-.00023,7):
                    point=np.array([side*x,y])
                    hits=[t for t in faces if contains(point,t[:,[0,1]])]
                    assert len(hits)==1,(name,'missing/overlapping lamp receiver',point,len(hits))
                    t=hits[0]
                    w=np.linalg.solve(np.vstack([t[:,:2].T,np.ones(3)]),np.array([*point,1]))
                    uv=w@t[:,6:8]
                    pixel=rgba[min(255,int((1-uv[1])*256)),min(255,int(uv[0]*256)),:3]
                    if not blockout:
                        if name=='rear_red':assert pixel[0]>120 and pixel[1]<80 and pixel[2]<80,(name,pixel)
                        else:assert pixel.min()>170,(name,pixel)
                    count+=1
        lamp_checks[name]=dict(**lamp,samples=count,receiver_triangles=len(faces))
    report=dict(model='harrow_parcel',vertices=len(v),triangles=len(tris),bounds_min=lo.tolist(),bounds_max=hi.tolist(),
                dimensions=(hi-lo).tolist(),shape_contract=SHAPE,wheels=WHEELS,
                player_fit_scale=SCALE.tolist(),fitted_dimensions=((hi-lo)*SCALE).tolist(),
                wheel_native_radius=float(radius),wheel_native_halfwidth=float(halfwidth/wheel_scale),
                fitted_wheel_radius=.32,fitted_wheel_halfwidth=float(halfwidth),
                atlas=[256,256,'RGBA'],lamps=lamp_checks,uv_min=v[:,6:8].min(0).tolist(),uv_max=v[:,6:8].max(0).tolist(),
                checks=['finite static mesh, valid indices, no degenerate faces','triangle budget','symmetric geometry and measured bounds',
                        'four real open side pockets','continuous roof, hood and central floor','direct receiver atlas; no floating glass/lamp/seam skins',
                        'four anchor empties; one joined body; no wheels in exported source','opaque 256x256 RGBA atlas',
                        'sampled shared-tire envelope clear through full steering range',
                        'both mirrored shader lamp rectangles backed by one receiver and correct atlas pixels'],
                steering_test=dict(surface_samples=len(cloud),max_surface_spacing=.02,poses=poses,max_lock=.82),
                mesh_sha256=hashlib.sha256((MODEL/'body.emesh').read_bytes()).hexdigest(),
                atlas_sha256=hashlib.sha256(TEXTURE.read_bytes()).hexdigest())
    output('-fit-report.json').write_text(json.dumps(report,indent=2)+'\n')
    guide=Image.open(TEXTURE).copy()
    d=ImageDraw.Draw(guide)
    for ids in np.asarray(indices).reshape(-1,3):
        pts=[(v[i,6]*256,(1-v[i,7])*256) for i in ids]
        d.line(pts+[pts[0]],fill=(240,151,52,255))
    guide.save(output('-uv-guide.png'))
    print(json.dumps(report,indent=2))
    return report


def parts(steer=0):
    body=read_part(MODEL/'body.emesh',TEXTURE)
    body.positions*=SCALE
    body.normals/=SCALE
    body.normals/=np.linalg.norm(body.normals,axis=1)[:,None]
    result=[body]
    wheel=wheel_part()
    radius=max(np.ptp(wheel.positions[:,1]),np.ptp(wheel.positions[:,2]))*.5
    for axle in (WHEELS['front_z'],WHEELS['rear_z']):
        for side in (-1,1):
            angle=steer if axle>0 else 0
            c,s=math.cos(angle),math.sin(angle)
            rot=np.array([[c,0,-s],[0,1,0],[s,0,c]])
            p=(wheel.positions*(.32/radius))@rot.T
            p+=np.array([side*.78,WHEELS['arch_y']*SCALE[1],axle*SCALE[2]])
            result.append(Part(p,wheel.normals@rot.T,wheel.uvs,wheel.indices,wheel.texture))
    return result


def previews(blockout=False):
    straight,locked=parts(),parts(.82)
    views=[('FRONT 3/4',straight,-32,18),('SIDE',straight,-90,0),('REAR 3/4',straight,-148,18),
           ('ELEVATED FRONT',straight,-32,38),('FULL LOCK',locked,-32,25),('REAR',straight,180,0)]
    sheet=Image.new('RGB',(1440,840),(20,23,26))
    d=ImageDraw.Draw(sheet)
    for i,(label,items,yaw,pitch) in enumerate(views):
        image=raster_view(items,yaw,pitch,480,386)
        image.save(output('-'+label.lower().replace(' ','-').replace('/','')+('.blockout' if blockout else '')+'.png'))
        x,y=(i%3)*480,(i//3)*420
        sheet.paste(image,(x,y))
        d.text((x+12,y+397),label,fill=(230,230,215))
    sheet.save(output('-blockout.png' if blockout else '-preview.png'))
    # One-material engine QA fixture combines the real shared wheels with body.
    # It lives only in build; production asset remains wheel-less and 256 square.
    atlas=Image.new('RGBA',(512,256))
    atlas.paste(Image.fromarray(locked[0].texture).convert('RGBA'),(0,0))
    atlas.paste(Image.fromarray(locked[1].texture).convert('RGBA').resize((256,256),Image.Resampling.NEAREST),(256,0))
    atlas.save(output('-qa-atlas.png'))
    values=[]
    for partno,item in enumerate(locked):
        for tri in item.indices:
            for i in tri:
                u,v=item.uvs[i]
                values.append((*item.positions[i],*item.normals[i],u*.5+(.5 if partno else 0),v,1,0,0,1))
    v=np.asarray(values,dtype='<f4')
    with output('-steering-qa.emesh').open('wb') as f:
        f.write(struct.pack('<8I',0x48534D45,2,0,len(v),len(v),1,3,0))
        f.write(v.tobytes())
        f.write(np.arange(len(v),dtype='<u4').tobytes())
        f.write(struct.pack('<4I',0,len(v),0,0))
        f.write(b'qa\0')


if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--blockout',action='store_true')
    p.add_argument('--texture-only',action='store_true')
    p.add_argument('--asset-lab',action='store_true',help='use existing binary only; no rebuild or game launch')
    args=p.parse_args()
    PREFIX.parent.mkdir(parents=True,exist_ok=True)
    make_texture(args.blockout)
    if not args.texture_only:
        subprocess.run(['/Applications/Blender.app/Contents/MacOS/Blender','--background','--factory-startup',
                        '--python',str(ROOT/'tools/harrow_parcel_blender.py'),'--','--mesh',str(MODEL/'body.emesh'),
                        '--blend',str(MODEL/'source.blend')],check=True)
    validate(args.blockout)
    previews(args.blockout)
    if args.asset_lab:
        for label,mesh,atlas,yaw in [('engine-body',MODEL/'body.emesh',TEXTURE,212),
                                     ('engine-wheels',output('-steering-qa.emesh'),output('-qa-atlas.png'),212),
                                     ('engine-rear',MODEL/'body.emesh',TEXTURE,32)]:
            proc=subprocess.run([str(ROOT/'build/bin/apricot_asset_lab'),'--model',str(mesh),'--texture',str(atlas),
                                 '--yaw',str(yaw),'--frames','60','--screenshot',str(output('-'+label+'.png'))],
                                cwd=ROOT,capture_output=True,text=True)
            output('-'+label+'.log').write_text(proc.stdout+proc.stderr)
            proc.check_returncode()
            assert '0 GL errors' in proc.stdout,proc.stdout
            print(label,'60 frames, 0 GL errors')
