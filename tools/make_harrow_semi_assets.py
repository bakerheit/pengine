#!/usr/bin/env python3
"""Cook original Hauler tractor / freight trailer; validate actual shared wheels."""
import argparse
import hashlib
import importlib
import json
import math
import struct
import subprocess
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
from make_vesper_vx91_assets import read_emesh
from render_firetruck_preview import Part, read_part, raster_view
ROOT=Path(__file__).resolve().parents[1]


def make_texture(slug,blockout=False):
    spec=importlib.import_module(slug+'_spec')
    trailer=slug.endswith('trailer')
    red=(109,39,38,255);cream=(211,206,184,255);light=(229,225,205,255)
    dark=(32,36,39,255);metal=(136,151,151,255);glass=(42,66,75,255)
    palette={'SIDE':cream if trailer else red,'TOP':light if trailer else (140,57,49,255),
             'FRONT':cream if trailer else red,'REAR':cream if trailer else red,
             'GLASS':glass,'RED':(176,45,36,255),'AMBER':(229,157,49,255),
             'METAL':metal,'DARK':dark,'PAINT':red,'CREAM':light}
    im=Image.new('RGBA',(256,256),dark);d=ImageDraw.Draw(im)
    for k,box in spec.REGIONS.items():d.rectangle(box,fill=palette[k])
    def point(k,a,b):
        (lo,hi),(bottom,top)=spec.BOUNDS[k];x0,y0,x1,y1=spec.REGIONS[k]
        return round(x0+2+(a-lo)/(hi-lo)*(x1-x0-4)),round(y1-2-(b-bottom)/(top-bottom)*(y1-y0-4))
    def rect(k,a,b,c,e,col):d.rectangle([point(k,a,e),point(k,c,b)],fill=col)
    def line(k,ps,col,width=1):d.line([point(k,*p) for p in ps],fill=col,width=width)
    def poly(k,ps,col):d.polygon([point(k,*p) for p in ps],fill=col)
    if not blockout:
        if trailer:
            for k in ('SIDE','FRONT','REAR'):
                (a,c),_=spec.BOUNDS[k]
                rect(k,a,1.43,c,1.61,(108,110,103,255))
                rect(k,a,1.90,c,2.40,red)
                rect(k,a,2.40,c,2.44,(147,73,60,255))
            # Deliberate alternating two-pixel stamped ribs, no random noise.
            for z in np.arange(-4.70,4.8,.46):
                line('SIDE',[(z,1.67),(z,3.66)],(165,167,154,255))
                line('SIDE',[(z+.06,2.48),(z+.06,3.66)],light)
            for z in (-4.5,-3.7,-2.9,-2.1,-1.3,-.5,.3,1.1,1.9,2.7,3.5,4.3):
                rect('SIDE',z,1.45,z+.3,1.54,(197,76,57,255))
            for k in ('SIDE','FRONT','REAR'):
                (a,c),_=spec.BOUNDS[k]
                line(k,[(a,3.64),(c,3.64)],(177,180,163,255))
            for s in (-1,1):
                a,c=sorted((s*.045,s*1.15))
                line('REAR',[(a,1.5),(a,3.65),(c,3.65),(c,1.5)],(91,99,94,255))
            line('REAR',[(0,1.45),(0,3.7)],dark,2)
            for z in np.arange(-4.4,4.8,.8):line('TOP',[(-1.10,z),(1.10,z)],(192,194,177,255))
        else:
            for k in ('SIDE','FRONT','REAR'):
                (a,c),_=spec.BOUNDS[k]
                rect(k,a,1.76,c,1.88,cream)
                rect(k,a,1.88,c,1.94,(156,119,89,255))
            # Driver/passenger glazing is painted directly into the cab shell.
            poly('SIDE',[(.17,2.20),(1.28,2.20),(1.25,2.72),(.83,3.06),(.17,3.06)],dark)
            poly('SIDE',[(.23,2.27),(1.20,2.27),(1.18,2.69),(.79,2.99),(.23,2.99)],glass)
            poly('SIDE',[(.24,2.81),(.95,2.81),(.79,2.99),(.24,2.99)],(69,93,99,255))
            line('SIDE',[(.98,2.25),(.98,2.80)],dark)
            line('SIDE',[(.15,1.17),(.15,3.08),(.81,3.08)],(70,28,30,255))
            line('SIDE',[(1.30,1.20),(1.30,2.17)],(70,28,30,255))
            rect('SIDE',.27,2.04,.49,2.10,metal)
            for z in (1.53,1.70,1.87):line('SIDE',[(z,1.40),(z,1.61)],dark)
            # Front atlas covers grille and split windscreen in source XY.
            rect('FRONT',-.62,.78,.62,1.61,metal)
            rect('FRONT',-.55,.83,.55,1.56,dark)
            for x in np.linspace(-.49,.49,9):line('FRONT',[(x,.87),(x,1.52)],metal)
            rect('FRONT',-.81,2.19,.81,3.14,dark)
            rect('FRONT',-.74,2.26,.74,3.07,glass)
            rect('FRONT',-.73,2.89,.73,3.06,(68,93,99,255))
            rect('FRONT',-.035,2.20,.035,3.15,dark)
            line('FRONT',[(-.68,2.30),(-.15,2.43)],dark,2)
            line('FRONT',[(.11,2.30),(.65,2.43)],dark,2)
            rect('REAR',-.55,2.31,.55,2.88,dark)
            rect('REAR',-.48,2.38,.48,2.81,glass)
            for x in (-.60,.60):line('TOP',[(x,1.50),(x,3.05)],(95,35,34,255))
            # Fifth wheel/tanks use restrained bands, not stretched global noise.
        for k in ('METAL','DARK','CREAM'):
            x0,y0,x1,y1=spec.REGIONS[k]
            base=palette[k];lighter=tuple(min(255,c+16) for c in base[:3])+(255,)
            d.rectangle((x0+2,y0+2,x1-2,y0+7),fill=lighter)
        if trailer:
            # Small original fleet panel remains legible as a pixel motif.
            rect('SIDE',-.7,2.75,1.1,3.30,(189,185,164,255))
            line('SIDE',[(-.45,2.84),(-.45,3.22),(.0,3.22),(.0,2.84)],red,2)
            line('SIDE',[(-.43,3.01),(-.01,3.01)],red,2)
            line('SIDE',[(.25,2.85),(.25,3.21),(.83,3.21)],red,2)
    dest=ROOT/f'assets/textures/vehicles/{slug}/body.png';dest.parent.mkdir(parents=True,exist_ok=True);im.save(dest)


def output(slug,suffix):return ROOT/f'build/{slug}{suffix}'
def paths(slug):return ROOT/f'assets/models/vehicles/{slug}',ROOT/f'assets/textures/vehicles/{slug}/body.png'


def parts(slug,steer=0):
    spec=importlib.import_module(slug+'_spec');model,texture=paths(slug)
    result=[read_part(model/'body.emesh',texture)]
    wheel=read_part(ROOT/'assets/models/vehicles/common/wheel.emesh',ROOT/'assets/textures/vehicles/common/wheel.png')
    radius=max(np.ptp(wheel.positions[:,1]),np.ptp(wheel.positions[:,2]))*.5
    for axle in (spec.WHEELS['front_z'],spec.WHEELS['rear_z']):
        for side in (-1,1):
            a=steer if axle==spec.WHEELS['front_z'] and slug=='harrow_hauler' else 0
            c,s=math.cos(a),math.sin(a);rot=np.array([[c,0,-s],[0,1,0],[s,0,c]])
            p=(wheel.positions*(spec.WHEELS['radius']/radius))@rot.T
            p+=np.array([side*spec.WHEELS['x'],spec.WHEELS['arch_y'],axle])
            result.append(Part(p,wheel.normals@rot.T,wheel.uvs,wheel.indices,wheel.texture))
    return result


def validate(slug):
    spec=importlib.import_module(slug+'_spec');model,texture=paths(slug)
    vertices,indices=read_emesh(model/'body.emesh');v=np.array(vertices);ix=np.array(indices).reshape(-1,3)
    assert np.isfinite(v).all() and ix.min()>=0 and ix.max()<len(v)
    ts=v[ix,:3];areas=np.linalg.norm(np.cross(ts[:,1]-ts[:,0],ts[:,2]-ts[:,0]),axis=1)
    assert (areas>1e-8).all(),'degenerate triangles'
    assert spec.SHAPE['triangle_budget'][0]<=len(ts)<=spec.SHAPE['triangle_budget'][1],len(ts)
    assert ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all()
    lo,hi=v[:,:3].min(0),v[:,:3].max(0)
    assert abs((hi-lo)[2]-spec.SHAPE['length'])<.08,(lo,hi)
    assert abs(lo[0]+hi[0])<1e-5 and abs(hi[0]-1.25)<.011,(lo,hi)
    keys={tuple(np.round(p,4)) for p in v[:,:3]}
    assert all((-x,y,z) in keys for x,y,z in keys),'asymmetric body'
    with Image.open(texture) as im:assert im.size==(256,256) and im.mode=='RGBA' and im.getchannel('A').getextrema()==(255,255)
    wheel=parts(slug)[1];halfwidth=np.ptp(wheel.positions[:,0])*.5
    # Fine surface samples against the actual shared tire's cylinder, including
    # all steering angles. A broad solid hood cannot mask a closed wheel pocket.
    cloud=[]
    for t in ts:
        if t[:,1].min()>1.005:continue
        n=max(1,math.ceil(max(np.linalg.norm(t[i]-t[j]) for i,j in ((0,1),(1,2),(2,0)))/.035))
        for i in range(n+1):
            js=np.arange(n+1-i)[:,None]/n
            cloud.append(t[0]+i/n*(t[1]-t[0])+js*(t[2]-t[0]))
    cloud=np.concatenate(cloud);poses=0
    for axle in (spec.WHEELS['front_z'],spec.WHEELS['rear_z']):
        for side in (-1,1):
            rel=cloud-np.array([side*1.10,.50,axle])
            angles=np.linspace(-.82,.82,33) if slug=='harrow_hauler' and axle>0 else [0]
            for a in angles:
                width=rel[:,0]*math.cos(a)+rel[:,2]*math.sin(a)
                radial=-rel[:,0]*math.sin(a)+rel[:,2]*math.cos(a)
                bad=(np.abs(width)<halfwidth+.002)&(radial**2+rel[:,1]**2<.502**2)
                assert not bad.any(),f'tire overlap {slug} axle {axle} side {side} angle {a}: {cloud[bad][:4]}'
                poses+=1
    # Check broad hood/cargo floor coverage; no accidental removal for arches.
    if slug=='harrow_hauler':
        assert any((t[:,1]>1.6).all() and np.ptp(t[:,0])>.9 for t in ts),'hood lost'
    else:
        assert any((np.abs(t[:,1]-1.4)<1e-5).all() and np.ptp(t[:,0])>2.4 for t in ts),'box floor lost'
    # Verify the inset light profiles land on one real receiver, with the
    # correct nearest-filtered atlas color on both mirrored sides.
    rgba=np.array(Image.open(texture));all_tri=v[ix];lamp_checks={}
    for name,lamp in spec.LAMPS.items():
        x0,y0,x1,y1,z0,z1=lamp['profile_rect']
        faces=all_tri[(np.abs(all_tri[:,:,2]-lamp['face_z'])<1e-5).all(1)]
        count=0
        for side in (-1,1):
            for x in np.linspace(x0+.00031,x1-.00027,7):
                for y in np.linspace(y0+.00037,y1-.00023,7):
                    point=np.array([side*x,y]);hits=[]
                    for t in faces:
                        a,b,c=t[:,:2]
                        cross=lambda u,v:u[0]*v[1]-u[1]*v[0]
                        values=[cross(b-a,point-a),cross(c-b,point-b),cross(a-c,point-c)]
                        if min(values)>1e-9 or max(values)<-1e-9:hits.append(t)
                    assert len(hits)==1,(name,'missing/overlapping receiver',point,len(hits))
                    t=hits[0]
                    w=np.linalg.solve(np.vstack([t[:,:2].T,np.ones(3)]),np.array([*point,1]))
                    uv=w@t[:,6:8]
                    pixel=rgba[min(255,int((1-uv[1])*256)),min(255,int(uv[0]*256)),:3]
                    if name=='rear_red':assert pixel[0]>120 and pixel[1]<80 and pixel[2]<80,(name,pixel)
                    else:assert pixel.min()>170,(name,pixel)
                    count+=1
        lamp_checks[name]=dict(**lamp,samples=count)
    report=dict(slug=slug,vertices=len(v),triangles=len(ts),bounds_min=lo.tolist(),bounds_max=hi.tolist(),
                dimensions=(hi-lo).tolist(),source_to_world_scale=[1,1,1],shape_contract=spec.SHAPE,
                wheels=spec.WHEELS,coupling=spec.COUPLING,atlas=[256,256,'RGBA'],lamps=lamp_checks,
                uv_min=v[:,6:8].min(0).tolist(),uv_max=v[:,6:8].max(0).tolist(),
                tire_validation=dict(radius=.5,halfwidth=float(halfwidth),poses=poses,surface_samples=len(cloud),max_spacing=.035),
                checks=['finite static body; valid indices; nonzero triangles','declared triangle budget',
                'centered symmetric geometry; measured dimensions','opaque 256 RGBA atlas and bounded UVs',
                'actual shared wheels clear all body faces; tractor steering lock +/-0.82',
                'one joined Blender body; four wheel-anchor empties; no baked wheels',
                'hood or box floor coverage retained','both mirrored lamp profiles backed by one receiver and intended atlas colors'],
                mesh_sha256=hashlib.sha256((model/'body.emesh').read_bytes()).hexdigest(),
                atlas_sha256=hashlib.sha256(texture.read_bytes()).hexdigest())
    output(slug,'-fit-report.json').write_text(json.dumps(report,indent=2)+'\n')
    guide=Image.open(texture).copy();d=ImageDraw.Draw(guide)
    for tri in ix:
        points=[(v[i,6]*256,(1-v[i,7])*256) for i in tri];d.line(points+[points[0]],fill=(240,151,52,255))
    guide.save(output(slug,'-uv-guide.png'))
    print(json.dumps(report,indent=2));return report


def qa_fixture(slug,items):
    atlas=Image.new('RGBA',(512,256))
    atlas.paste(Image.fromarray(items[0].texture).convert('RGBA'),(0,0))
    atlas.paste(Image.fromarray(items[1].texture).convert('RGBA').resize((256,256),Image.Resampling.NEAREST),(256,0))
    atlas.save(output(slug,'-qa-atlas.png'))
    values=[]
    for partno,item in enumerate(items):
        for tri in item.indices:
            for i in tri:
                u,v=item.uvs[i];values.append((*item.positions[i],*item.normals[i],u*.5+(.5 if partno else 0),v,1,0,0,1))
    v=np.array(values,dtype='<f4')
    with output(slug,'-steering-qa.emesh').open('wb') as f:
        f.write(struct.pack('<8I',0x48534D45,2,0,len(v),len(v),1,3,0));f.write(v.tobytes())
        f.write(np.arange(len(v),dtype='<u4').tobytes());f.write(struct.pack('<4I',0,len(v),0,0));f.write(b'qa\0')


def previews(slug,blockout=False):
    straight,locked=parts(slug),parts(slug,.82)
    views=[('FRONT 3/4',straight,-32,18),('SIDE',straight,-90,0),('REAR 3/4',straight,-148,18),
           ('ELEVATED FRONT',straight,-32,38),('FULL LOCK',locked,-32,25),('REAR',straight,180,0)]
    sheet=Image.new('RGB',(1440,840),(20,23,26));d=ImageDraw.Draw(sheet)
    for i,(label,items,yaw,pitch) in enumerate(views):
        image=raster_view(items,yaw,pitch,480,386);x,y=(i%3)*480,(i//3)*420
        sheet.paste(image,(x,y));d.text((x+12,y+397),label,fill=(230,230,215))
    sheet.save(output(slug,'-blockout.png' if blockout else '-preview.png'));qa_fixture(slug,locked)


def main(slug=None):
    p=argparse.ArgumentParser();p.add_argument('--slug',default=slug,choices=['harrow_hauler','harrow_freight_trailer'])
    p.add_argument('--blockout',action='store_true');p.add_argument('--texture-only',action='store_true')
    p.add_argument('--asset-lab',action='store_true');args=p.parse_args();slug=args.slug
    output(slug,'').parent.mkdir(parents=True,exist_ok=True);make_texture(slug,args.blockout)
    model,texture=paths(slug)
    if not args.texture_only:
        subprocess.run(['/Applications/Blender.app/Contents/MacOS/Blender','--background','--factory-startup',
                        '--python',str(ROOT/'tools/harrow_semi_blender.py'),'--','--slug',slug,'--mesh',str(model/'body.emesh'),
                        '--blend',str(model/'source.blend')],check=True)
    validate(slug);previews(slug,args.blockout)
    if args.asset_lab:
        for label,mesh,atlas,yaw in [('engine-body',model/'body.emesh',texture,212),
                ('engine-wheels',output(slug,'-steering-qa.emesh'),output(slug,'-qa-atlas.png'),212),
                ('engine-rear',model/'body.emesh',texture,32)]:
            proc=subprocess.run([str(ROOT/'build/bin/apricot_asset_lab'),'--model',str(mesh),'--texture',str(atlas),
                '--yaw',str(yaw),'--frames','60','--screenshot',str(output(slug,'-'+label+'.png'))],cwd=ROOT,capture_output=True,text=True)
            output(slug,'-'+label+'.log').write_text(proc.stdout+proc.stderr);proc.check_returncode()
            assert '0 GL errors' in proc.stdout,proc.stdout
            print(slug,label,'60 frames, 0 GL errors')
if __name__=='__main__':main()
