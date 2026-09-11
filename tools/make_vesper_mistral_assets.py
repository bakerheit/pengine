#!/usr/bin/env python3
"""Reproducible Mistral cook, semantic atlas, wheel-fit checks and QA views."""
import argparse
import json
import math
import subprocess
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
from vesper_mistral_spec import ATLAS_SIZE,REGIONS,WHEELS,SHAPE,LAMPS,DRIVER,DOOR,TOP
from render_firetruck_preview import Part,read_part,raster_view
from bake_vehicle_surfaces import read_mesh,write_mesh

ROOT=Path(__file__).resolve().parents[1]
MODEL=ROOT/'assets/models/vehicles/vesper_mistral'
TEXTURE=ROOT/'assets/textures/vehicles/vesper_mistral/body.png'
BUILD=ROOT/'build'
SCALE=np.array([.78/WHEELS['x'],2.7/2.78,2.7/2.78])


def texture():
    im=Image.new('RGBA',(256,256),(48,33,32,255)); d=ImageDraw.Draw(im)
    palette={'SIDE':(157,66,56),'PAINT':(183,83,70),'FRONT':(160,67,55),
             'REAR':(160,67,55),'GLASS':(78,106,111),'SEAT':(47,44,42),
             'DASH':(33,32,31),'RUBBER':(36,34,33),'SHADOW':(73,42,37),
             'METAL':(123,124,117),'INTERIOR':(43,37,34),'BLACK':(21,23,24),
             'TOP':(44,40,38)}
    for name,box in REGIONS.items():
        d.rectangle(box,fill=palette[name]+(255,))
    def rect(name,a,b,c,e,colour):
        x0,y0,x1,y1=REGIONS[name]
        d.rectangle((x0+2+a*(x1-x0-4),y0+2+b*(y1-y0-4),x0+2+c*(x1-x0-4),y0+2+e*(y1-y0-4)),fill=colour)
    # Broad limited-palette value bands, no random noise or pasted imagery.
    rect('SIDE',0,.12,1,.22,(176,78,66));rect('SIDE',0,.70,1,.83,(137,56,49))
    rect('SIDE',0,.91,1,1,(74,46,40))
    for a in (.315,.60): rect('SIDE',a,.23,a+.009,.85,(98,44,40))
    rect('SIDE',.365,.30,.42,.34,(68,44,39))
    rect('SIDE',.367,.30,.411,.311,(189,99,83))
    rect('PAINT',.19,0,.24,1,(195,96,81));rect('PAINT',.76,0,.81,1,(195,96,81))
    rect('PAINT',.28,.53,.29,.91,(137,56,49));rect('PAINT',.71,.53,.72,.91,(137,56,49))
    rect('PAINT',.29,.91,.71,.925,(137,56,49))
    rect('PAINT',.28,.06,.72,.075,(145,62,53))
    for name in ('FRONT','REAR'):
        rect(name,0,.77,1,.82,(88,47,40));rect(name,0,.92,1,1,(42,33,30))
    # End face atlas uses physical x and height coordinates; lenses stay flush.
    def end(name,x0,z0,x1,z1,colour):
        rect(name,(x0+1.05)/2.10,1-(z1-.20)/.78,(x1+1.05)/2.10,1-(z0-.20)/.78,colour)
    for s in (-1,1):
        a,c=sorted((s*.40,s*.78))
        end('FRONT',a,.48,c,.60,(38,35,32));end('FRONT',a+.025,.495,c-.025,.58,(206,196,151))
        end('FRONT',a+.025,.55,c-.025,.575,(230,220,180))
        end('REAR',a,.48,c,.66,(49,31,28));end('REAR',a+.025,.50,c-.025,.64,(146,32,24))
        end('REAR',a+.06,.59,c-.03,.62,(203,62,39))
    end('FRONT',-.34,.30,.34,.405,(30,29,28))
    end('REAR',-.22,.40,.22,.55,(90,68,54));end('REAR',-.18,.43,.18,.51,(181,166,127))
    rect('GLASS',0,.0,1,.08,(47,58,60));rect('GLASS',0,.91,1,1,(40,49,51))
    rect('GLASS',.16,.17,.27,.74,(106,130,131));rect('GLASS',.27,.17,.32,.53,(106,130,131))
    rect('SEAT',.1,.10,.2,.91,(65,59,52));rect('SEAT',.80,.10,.90,.91,(65,59,52))
    for t in (.25,.38,.51,.64,.77): rect('SEAT',.23,t,.77,t+.025,(32,31,30))
    rect('DASH',.64,.22,.90,.62,(15,19,20))
    for t in (.70,.81): rect('DASH',t,.31,t+.05,.49,(145,139,114))
    rect('DASH',.47,.28,.61,.68,(18,21,22));rect('DASH',.49,.34,.59,.40,(91,91,78))
    # Canvas: bow seams across the width, plus the darker rails the top sits on.
    rect('TOP',0,0,1,.06,(28,26,25));rect('TOP',0,.94,1,1,(28,26,25))
    for t in (.28,.55,.80): rect('TOP',0,t,1,t+.035,(31,29,28))
    rect('TOP',.44,.06,.56,.94,(56,51,48))
    TEXTURE.parent.mkdir(parents=True,exist_ok=True);im.save(TEXTURE)


def door_rotation(fraction):
    angle=math.radians(DOOR['open_degrees'])*min(1,max(0,fraction))
    c,s=math.cos(angle),math.sin(angle)
    return np.array([[c,0,s],[0,1,0],[-s,0,c]])


def top_rotation(axis_angle):
    """Rotation about the lateral axis; the one axis the chassis fit leaves rigid."""
    c,s=math.cos(axis_angle),math.sin(axis_angle)
    return np.array([[1,0,0],[0,c,-s],[0,s,c]])


def top_poses(fraction):
    """(rotation, pivot) for the rear bow and the front bow, in scaled space."""
    f=min(1,max(0,fraction))
    rear=top_rotation(math.radians(TOP['stow_degrees'])*f)
    hinge=np.array(TOP['rear_hinge'])*SCALE
    joint=np.array(TOP['joint_hinge'])*SCALE
    front=top_rotation(math.radians(TOP['stow_degrees']+TOP['fold_degrees'])*f)
    return (rear,hinge,hinge),(front,joint,(joint-hinge)@rear.T+hinge)


def top_parts(fraction):
    out=[]
    for name,(rotation,pivot,moved) in zip(('soft_top_rear','soft_top_front'),top_poses(fraction)):
        part=read_part(MODEL/(name+'.emesh'),TEXTURE)
        part.positions=(part.positions*SCALE-pivot)@rotation.T+moved
        part.normals=(part.normals/SCALE)@rotation.T
        part.normals/=np.linalg.norm(part.normals,axis=1)[:,None]
        out.append(part)
    return out


def parts(steer=0,door_open=None,top=None):
    body=read_part(MODEL/('body.emesh' if door_open is None else 'body_open.emesh'),TEXTURE)
    body.positions*=SCALE;body.normals/=SCALE
    body.normals/=np.linalg.norm(body.normals,axis=1)[:,None]
    wheel=read_part(ROOT/'assets/models/vehicles/common/wheel.emesh',ROOT/'assets/textures/vehicles/common/wheel.png')
    radius=max(np.ptp(wheel.positions[:,1]),np.ptp(wheel.positions[:,2]))*.5
    out=[body]
    for axle in (WHEELS['front_z'],WHEELS['rear_z']):
        for side in (-1,1):
            angle=steer if axle>0 else 0;c,s=math.cos(angle),math.sin(angle)
            rot=np.array([[c,0,-s],[0,1,0],[s,0,c]])
            pts=wheel.positions*(.32/radius)@rot.T
            pts+=np.array([side*.78,.38*SCALE[1],axle*SCALE[2]])
            out.append(Part(pts,wheel.normals@rot.T,wheel.uvs,wheel.indices,wheel.texture))
    if door_open is not None:
        door=read_part(MODEL/'driver_door.emesh',TEXTURE)
        hinge=np.array(DOOR['hinge'])*SCALE
        rotation=door_rotation(door_open)
        door.positions=(door.positions*SCALE-hinge)@rotation.T+hinge
        door.normals=(door.normals/SCALE)@rotation.T
        door.normals/=np.linalg.norm(door.normals,axis=1)[:,None]
        out.append(door)
    if top is not None: out += top_parts(top)
    return out


def qa_mesh(items,stem):
    # Diagnostic fixture only: production body remains wheel-less with one 256 atlas.
    atlas=Image.new('RGBA',(512,256));atlas.paste(Image.fromarray(items[0].texture),(0,0))
    atlas.paste(Image.fromarray(items[1].texture).resize((256,256),Image.Resampling.NEAREST),(256,0))
    atlas.save(BUILD/(stem+'.png'))
    verts=[]
    for no,item in enumerate(items):
        for tri in item.indices:
            for i in tri:
                u,v=item.uvs[i];verts.append((*item.positions[i],*item.normals[i],u*.5+(0 if no==0 else .5),v,1,0,0,1))
    write_mesh(BUILD/(stem+'.emesh'),verts,'vesper_mistral_qa')


def validate():
    v,ids=read_mesh(MODEL/'body.emesh');p=v[:,:3];tris=p[ids]
    assert len(ids) and ids.max()<len(v) and np.isfinite(v).all()
    assert SHAPE['triangle_budget'][0]<=len(ids)<=SHAPE['triangle_budget'][1],len(ids)
    assert ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all()
    lo,hi=p.min(0),p.max(0);assert np.allclose(lo[[0,2]],-hi[[0,2]],atol=.001)
    assert np.allclose((hi-lo)[[0,2]],[2.10,4.70],atol=.001)
    assert np.all(np.linalg.norm(np.cross(tris[:,1]-tris[:,0],tris[:,2]-tris[:,0]),axis=1)>1e-8)
    im=Image.open(TEXTURE);assert im.mode=='RGBA' and im.size==(256,256) and im.getchannel('A').getextrema()==(255,255)
    def projected(point,tri):
        a,b,c=tri;u=b-a;w=c-a;det=u[0]*w[1]-u[1]*w[0]
        if abs(det)<1e-8:return False
        q=np.array(point)-a;s=(q[0]*w[1]-q[1]*w[0])/det;t=(u[0]*q[1]-u[1]*q[0])/det
        return s>=-1e-6 and t>=-1e-6 and s+t<=1+1e-6
    opening_samples=0
    for axle in (1.48,-1.30):
        for side in (-1,1):
            for dz in (-.18,0,.18):
                for dy in (-.12,0,.12):
                    relevant=tris[(tris[:,:,0]*side>.70).all(1)]
                    assert not any(projected((axle+dz,.38+dy),t[:,[2,1]]) for t in relevant),'blocked wheel opening'
                    opening_samples+=1
        for x in (-.65,-.3,0,.3,.65):
            for z in (axle-.04,axle+.04):
                assert any(projected((x,z),t[:,[0,2]]) for t in tris[(tris[:,:,1]>.66).all(1)]),'missing hood/deck'
    for x in (-.40,.40):
        assert not any(projected((x,-.35),t[:,[0,2]]) for t in tris[(tris[:,:,1]>1.12).all(1)]),'roof covers cockpit'
    articulated={}
    for name in ('body_open','driver_door'):
        av,ai=read_mesh(MODEL/(name+'.emesh'));ap=av[:,:3];at=ap[ai]
        assert len(ai) and ai.max()<len(av) and np.isfinite(av).all(),name
        assert ((av[:,6:8]>=0)&(av[:,6:8]<=1)).all(),name
        assert np.all(np.linalg.norm(np.cross(at[:,1]-at[:,0],at[:,2]-at[:,0]),axis=1)>1e-8),name
        articulated[name]=(av,ai,at)
    open_tris=articulated['body_open'][2]
    door_tris=articulated['driver_door'][2]
    assert door_tris[:,:,0].min()>=DOOR['inner_x']-1e-6,'driver door retained deep body wedge'
    closed=np.concatenate((open_tris,door_tris)).reshape(-1,3)
    assert np.allclose(closed.min(0),lo,atol=1e-6) and np.allclose(closed.max(0),hi,atol=1e-6),'closed bounds changed'
    door_samples=0
    side=open_tris[(open_tris[:,:,0]>.70).all(1)]
    for z in np.linspace(DOOR['rear_z']+.06,DOOR['front_z']-.06,9):
        for y in np.linspace(DOOR['sill_y']+.06,.86,5):
            assert not any(projected((z,y),t[:,[2,1]]) for t in side),'driver doorway obstructed'
            assert any(projected((z,y),t[:,[2,1]]) for t in door_tris),'closed door has a hole'
            door_samples+=1
        assert any(projected((z,DOOR['sill_y']-.01),t[:,[2,1]]) for t in side),'fixed sill missing'
    # Export duplicates vertices for flat normals/UVs; count geometric edges
    # after quantization to prove both inner and outer panel volumes are capped.
    edges={}
    for triangle in door_tris:
        keys=[tuple(np.round(p,6)) for p in triangle]
        for a,b in ((0,1),(1,2),(2,0)):
            edge=tuple(sorted((keys[a],keys[b])))
            edges[edge]=edges.get(edge,0)+1
    assert all(count==2 for count in edges.values()),'uncapped driver door'
    hinge=np.array(DOOR['hinge'])*SCALE
    handle=np.array(DOOR['handle'])*SCALE
    opened=(handle-hinge)@door_rotation(1).T+hinge
    assert opened[0]>handle[0]+.6,'driver door opens inward'
    for fraction in np.linspace(0,1,14):
        rotation=door_rotation(fraction)
        assert np.allclose(rotation.T@rotation,np.eye(3),atol=1e-7),'door is not rigid'
        assert np.allclose((hinge-hinge)@rotation.T+hinge,hinge),'hinge moved'
    # The folding canvas. It is separate geometry, which is why the open-body
    # assertions above still hold: raising the top must not close the roadster.
    canvas={}
    for name in ('soft_top_front','soft_top_rear'):
        tv,ti=read_mesh(MODEL/(name+'.emesh'));tp=tv[:,:3];tt=tp[ti]
        assert len(ti) and ti.max()<len(tv) and np.isfinite(tv).all(),name
        assert ((tv[:,6:8]>=0)&(tv[:,6:8]<=1)).all(),name
        assert np.all(np.linalg.norm(np.cross(tt[:,1]-tt[:,0],tt[:,2]-tt[:,0]),axis=1)>1e-8),name
        edges={}
        for triangle in tt:
            keys=[tuple(np.round(point,6)) for point in triangle]
            for a,c in ((0,1),(1,2),(2,0)):
                edge=tuple(sorted((keys[a],keys[c])));edges[edge]=edges.get(edge,0)+1
        assert all(count==2 for count in edges.values()),f'uncapped {name}'
        canvas[name]=tt
    joint_z=TOP['stations'][TOP['joint']][0]
    assert abs(canvas['soft_top_front'][:,:,2].min()-joint_z)<1e-6,'front bow misses the joint'
    assert abs(canvas['soft_top_rear'][:,:,2].max()-joint_z)<1e-6,'rear bow misses the joint'
    # Raised, it is a roof: the cockpit centreline the body deliberately leaves
    # open is covered from the windshield header back to the bulkhead.
    raised=np.concatenate(tuple(canvas.values()))
    top_samples=0
    for x in (-.55,-.25,0,.25,.55):
        for z in (0,-.25,-.50,-.75,-.88):
            assert any(projected((x,z),t[:,[0,2]]) for t in raised),'raised top leaves a hole'
            top_samples+=1
    # Stowed, it is behind the seats, below the raised crown, and inside the
    # rear deck's own plan so nothing pokes out through the bodywork.
    stow_samples=0
    for fraction in np.linspace(0,1,15):
        moved=[]
        for part,(rotation,pivot,anchor) in zip((canvas['soft_top_rear'],canvas['soft_top_front']),
                                                top_poses(fraction)):
            assert np.allclose(rotation.T@rotation,np.eye(3),atol=1e-7),'canvas bow is not rigid'
            moved.append((part.reshape(-1,3)*SCALE-pivot)@rotation.T+anchor)
            stow_samples+=1
        both=np.concatenate(moved)
        assert np.abs(both[:,0]).max()<=.78*SCALE[0]+1e-6,'canvas wider than the deck'
        assert both[:,2].min()>=-2.0*SCALE[2],'canvas overhangs the tail'
    assert both[:,1].max()<1.08*SCALE[1],'stowed canvas stands proud of the seat backs'
    assert both[:,2].max()<-.86*SCALE[2],'stowed canvas still reaches into the cockpit'
    for name,lamp in LAMPS.items():
        faces=tris[np.isclose(tris[:,:,2],lamp['z'],atol=1e-6).all(1)]
        for x0,x1 in lamp['x']:
            for x in np.linspace(x0,x1,5):
                for y in np.linspace(*lamp['y'],4):
                    assert any(projected((x,y),t[:,[0,1]]) for t in faces),f'{name}: missing lamp receiver'
    # Dense triangle samples against the actual shared tire's enclosing cylinder.
    # Check both signs and every intermediate lock, not just axle-center pixels.
    wheel=read_part(ROOT/'assets/models/vehicles/common/wheel.emesh',ROOT/'assets/textures/vehicles/common/wheel.png')
    native=max(np.ptp(wheel.positions[:,1]),np.ptp(wheel.positions[:,2]))*.5
    halfwidth=float(np.max(np.abs(wheel.positions[:,0]))*.32/native)
    scaled=tris*SCALE
    weights=np.array([(i/18,j/18,1-(i+j)/18) for i in range(19) for j in range(19-i)])
    checked=0
    for axle in (1.48,-1.30):
        for side in (-1,1):
            centre=np.array([side*.78,.38*SCALE[1],axle*SCALE[2]])
            near=scaled[(np.abs(scaled-centre).min(1)<np.array([.50,.36,.50])).all(1)]
            pts=np.einsum('ij,tjk->tik',weights,near).reshape(-1,3)-centre
            for angle in (np.linspace(-.82,.82,17) if axle>0 else [0]):
                c,s=math.cos(angle),math.sin(angle)
                local=np.column_stack((pts[:,0]*c+pts[:,2]*s,pts[:,1],-pts[:,0]*s+pts[:,2]*c))
                assert np.isfinite(local).all()
                hits=(np.abs(local[:,0])<halfwidth+.004)&(np.hypot(local[:,1],local[:,2])<.324)
                assert not hits.any(),f'tire/body intersection: axle={axle} side={side} steer={angle}: {local[hits][:4]}'
                checked+=len(pts)
    report=dict(asset='VESPER MISTRAL',mesh=str(MODEL/'body.emesh'),texture=str(TEXTURE),
                vertices=len(v),triangles=len(ids),bounds_min=lo.tolist(),bounds_max=hi.tolist(),bounds_size=(hi-lo).tolist(),
                shape_contract=SHAPE,wheel_anchors=WHEELS,lamp_regions=LAMPS,driver_layout=DRIVER,atlas=[256,256,'RGBA'],palette_colours=len(im.getcolors(65536)),
                driver_door=DOOR,doorway_samples=door_samples,soft_top=TOP,
                soft_top_triangles={name:len(part) for name,part in canvas.items()},
                soft_top_samples=top_samples+stow_samples,
                articulated_triangles={name:len(data[1]) for name,data in articulated.items()},
                uv_range=[v[:,6:8].min(0).tolist(),v[:,6:8].max(0).tolist()],
                player_scale=SCALE.tolist(),shared_wheel_radius=.32,shared_wheel_halfwidth=halfwidth,
                opening_samples=opening_samples,steering_triangle_samples=checked,
                checks=['single joined static BODY (Blender assertion)','no tire objects in body construction',
                        'finite positions, valid indices, nondegenerate triangles','650-1300 triangle budget',
                        'centered bounds and semantic UVs','opaque 256x256 RGBA limited palette',
                        'four open side pockets','broad hood and deck over both axles',
                        'open two-seat cockpit','true driver doorway with fixed sill and capped driver panel',
                        'closed articulated bounds match full body; fitted door swings outward rigidly',
                        'two-bow folding canvas: capped volumes, shared joint station',
                        'raised canvas roofs the open cockpit; stowed canvas folds inside the deck plan',
                        'all declared lamp regions have physical end-face receivers',
                        'shared-wheel cylinder clearance at 17 steering angles including +/-0.82 rad'])
    (BUILD/'vesper_mistral-fit-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
    return report


def previews():
    straight=parts();locked=parts(.82)
    views=[('FRONT',straight,-32,18),('SIDE',straight,-90,0),('REAR',straight,-148,25),
           ('COCKPIT',straight,-30,48),('FULL LOCK',locked,-32,30),('FRONT ORTHO',straight,0,4)]
    sheet=Image.new('RGB',(1440,820),(19,21,25));d=ImageDraw.Draw(sheet)
    for i,(label,items,yaw,pitch) in enumerate(views):
        x,y=(i%3)*480,(i//3)*410
        shot=raster_view(items,yaw,pitch,480,380);sheet.paste(shot,(x,y));d.text((x+12,y+389),label,fill=(225,216,194))
    sheet.save(BUILD/'vesper_mistral-preview.png')
    door_sheet=Image.new('RGB',(1440,820),(19,21,25));door_draw=ImageDraw.Draw(door_sheet)
    for i,(label,fraction,yaw,pitch) in enumerate([
            ('CLOSED',0,-90,12),('HALF OPEN',.5,-90,12),('OPEN 65 DEG',1,-90,12),
            ('FRONT OPEN',1,-32,25),('REAR OPEN',1,-148,25),('DOORWAY',1,-65,42)]):
        x,y=(i%3)*480,(i//3)*410
        door_sheet.paste(raster_view(parts(door_open=fraction),yaw,pitch,480,380),(x,y))
        door_draw.text((x+12,y+389),label,fill=(225,216,194))
    door_sheet.save(BUILD/'vesper_mistral-door-preview.png')
    top_sheet=Image.new('RGB',(1440,820),(19,21,25));top_draw=ImageDraw.Draw(top_sheet)
    for i,(label,fraction,yaw,pitch) in enumerate([
            ('TOP UP',0,-32,14),('RAISING 0.35',.35,-90,10),('RAISING 0.70',.70,-90,10),
            ('TOP DOWN',1,-90,10),('TOP UP SIDE',0,-90,6),('TOP DOWN REAR',1,-148,26)]):
        x,y=(i%3)*480,(i//3)*410
        top_sheet.paste(raster_view(parts(top=fraction),yaw,pitch,480,380),(x,y))
        top_draw.text((x+12,y+389),label,fill=(225,216,194))
    top_sheet.save(BUILD/'vesper_mistral-top-preview.png')
    raster_view(straight,-32,35,1100,900).save(BUILD/'vesper_mistral-detail.png')
    qa_mesh(locked,'vesper_mistral-wheel-qa')
    a=math.radians(-25);c,s=math.cos(a),math.sin(a)
    tilt=np.array([[1,0,0],[0,c,-s],[0,s,c]])
    raised=[Part(np.einsum('ij,kj->ki',tilt,p.positions),np.einsum('ij,kj->ki',tilt,p.normals),p.uvs,p.indices,p.texture) for p in straight]
    qa_mesh(raised,'vesper_mistral-cockpit-qa')
    guide=Image.open(TEXTURE).convert('RGB').resize((768,768),Image.Resampling.NEAREST);d=ImageDraw.Draw(guide)
    body=straight[0]
    for tri in body.indices:
        pts=[(float(body.uvs[i,0]*768),float((1-body.uvs[i,1])*768)) for i in tri]
        d.line(pts+[pts[0]],fill=(80,220,170),width=1)
    guide.save(BUILD/'vesper_mistral-uv-guide.png')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--validate-only',action='store_true');p.add_argument('--preview-only',action='store_true')
    p.add_argument('--blender',default='/Applications/Blender.app/Contents/MacOS/Blender');args=p.parse_args()
    BUILD.mkdir(exist_ok=True)
    if not args.validate_only and not args.preview_only:
        texture()
        subprocess.run([args.blender,'--background','--factory-startup','--python',str(ROOT/'tools/vesper_mistral_blender.py'),
                        '--','--mesh',str(MODEL/'body.emesh'),'--blend',str(MODEL/'source.blend')],check=True)
    if not args.preview_only:validate()
    previews()
