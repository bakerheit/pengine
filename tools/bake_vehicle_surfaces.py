#!/usr/bin/env python3
"""Fleet surface bake: preserve source paint while removing floating skins.

All work is initially staged in build/vehicle-surfaces, never over live assets.
Charts are planar receiver surfaces, not a new silhouette or a deformation hack.
"""
import argparse
import json
import math
import struct
import hashlib
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import distance_transform_edt

ROOT=Path(__file__).resolve().parents[1]
OUTPUT=ROOT/'build/vehicle-surfaces'

# Reviewed non-planar thin skins. Round lamps, bumpers, mirrors, fenders,
# roof gear and engine hardware are deliberately NOT flattened.
EXTRA_SKINS={
    # Rear lamp housings (24/25) span open air below the apparatus body. They
    # have no receiver there: baking them erased the actual brake lenses.
    'firetruck': [5,6,8,9,*range(10,24)],
    'montrose_regent_eight': [20,22,34,35],
}
BAKED_MODELS=('vesper_vx91','glm_lunge','glm_zip','harrow_workman',
              'halcyon_six','montrose_regent_eight','firetruck')
KEEP_PLANAR={'glm_zip': [6,20,59]}  # separate rocker blades and exhaust mouth
# Real protruding lamp housings retain their own atlas cells. Body stripes
# behind them must not get projected over the lens faces during the bake.
PROTECTED_RECEIVERS={'firetruck': [24,25]}
EXPECTED_COMPONENTS={'vesper_vx91':48,'glm_lunge':51,'glm_zip':60,
                     'harrow_workman':42,'halcyon_six':30,
                     'montrose_regent_eight':38,'firetruck':43}


def read_mesh(path):
    data=path.read_bytes()
    magic,version,flags,nv,ni,ns,_,_=struct.unpack_from('<8I',data)
    assert (magic,version,flags,ns)==(0x48534D45,2,0,1)
    v=np.frombuffer(data,dtype='<f4',count=nv*12,offset=32).reshape(-1,12).copy()
    ids=np.frombuffer(data,dtype='<u4',count=ni,offset=32+nv*48).reshape(-1,3).copy()
    return v,ids


def components(v,ids):
    parent=list(range(len(ids)))
    def root(i):
        while parent[i]!=i:
            parent[i]=parent[parent[i]]
            i=parent[i]
        return i
    edges={}
    for i, tri in enumerate(v[ids,:3]):
        keys=[tuple(np.round(p,5)) for p in tri]
        for a,b in ((0,1),(1,2),(2,0)):
            key=tuple(sorted((keys[a],keys[b])))
            if key in edges: parent[root(i)]=root(edges[key])
            else: edges[key]=i
    result={}
    for i in range(len(ids)): result.setdefault(root(i),[]).append(i)
    return list(result.values())


def audit(name):
    v,ids=read_mesh(ROOT/f'assets/models/vehicles/{name}/body.emesh')
    items=[]
    for i,faces in enumerate(components(v,ids)):
        p=v[ids[faces],:3].reshape(-1,3)
        lo,hi=p.min(0),p.max(0)
        eigen=np.linalg.svd(p-p.mean(0),compute_uv=False)
        planar=eigen[-1]<max(eigen[0]*1e-5,1e-6)
        items.append(dict(id=i,triangles=len(faces),centre=np.round((lo+hi)/2,3).tolist(),
                          size=np.round(hi-lo,3).tolist(),planar=bool(planar),
                          uv=np.round(v[ids[faces],6:8].mean(axis=(0,1)),3).tolist()))
    return items


def write_mesh(path,vertices,name):
    material=(name+'\0').encode()
    vertices=np.asarray(vertices,dtype='<f4').reshape(-1,12)
    n=len(vertices)
    with path.open('wb') as f:
        f.write(struct.pack('<8I',0x48534D45,2,0,n,n,1,len(material),0))
        f.write(vertices.tobytes())
        f.write(np.arange(n,dtype='<u4').tobytes())
        f.write(struct.pack('<4I',0,n,0,0))
        f.write(material)


def pack(charts,density):
    free=[(0,0,256,256)]
    boxes={}
    sizes=[]
    for i,c in enumerate(charts):
        w,h=np.maximum(3,np.ceil((c['hi']-c['lo'])*density*c.get('priority',1.)).astype(int))+4
        sizes.append((i,int(w),int(h)))
    for i,w,h in sorted(sizes,key=lambda t:max(t[1:])*1000+t[1]*t[2],reverse=True):
        fits=[(min(fw-w,fh-h),fw*fh-w*h,j) for j,(_,_,fw,fh) in enumerate(free) if fw>=w and fh>=h]
        if not fits:return None
        _,_,j=min(fits)
        x,y,_,_=free[j]
        boxes[i]=(x,y,w,h)
        split=[]
        for fx,fy,fw,fh in free:
            if x+w<=fx or x>=fx+fw or y+h<=fy or y>=fy+fh:
                split.append((fx,fy,fw,fh));continue
            if x>fx: split.append((fx,fy,x-fx,fh))
            if x+w<fx+fw: split.append((x+w,fy,fx+fw-x-w,fh))
            if y>fy: split.append((fx,fy,fw,y-fy))
            if y+h<fy+fh: split.append((fx,y+h,fw,fy+fh-y-h))
        free=[r for k,r in enumerate(split) if not any(k!=j and
              s[0]<=r[0] and s[1]<=r[1] and s[0]+s[2]>=r[0]+r[2] and s[1]+s[3]>=r[1]+r[3]
              and (s!=r or j<k) for j,s in enumerate(split))]
    return boxes


def barycentric(points,tri):
    a,b,c=tri
    e=b-a;f=c-a
    det=e[0]*f[1]-e[1]*f[0]
    if abs(det)<1e-10:return None
    p=points-a
    u=(p[...,0]*f[1]-p[...,1]*f[0])/det
    v=(e[0]*p[...,1]-e[1]*p[...,0])/det
    return np.stack((1-u-v,u,v),axis=-1)


def bake_model(name,install=False):
    model=ROOT/f'assets/models/vehicles/{name}'
    texture=ROOT/f'assets/textures/vehicles/{name}'
    v,ids=read_mesh(model/'body.emesh')
    tris=v[ids].astype(float)
    comps=components(v,ids)
    assert len(comps)==EXPECTED_COMPONENTS[name],f'{name}: source topology changed; review the bake recipe'
    audit_items=audit(name)
    skin_ids=[i for i,c in enumerate(audit_items) if c['planar'] and i not in KEEP_PLANAR.get(name,[])]+EXTRA_SKINS.get(name,[])
    for i in EXTRA_SKINS.get(name,[]):
        assert min(audit_items[i]['size'])<.09, 'reviewed thin skin changed shape'
    removed=sorted({face for i in skin_ids for face in comps[i]})
    keep=sorted(set(range(len(ids)))-set(removed))
    protected={face for i in PROTECTED_RECEIVERS.get(name,[]) for face in comps[i]}
    low=tris[keep]
    layers=tris[removed]
    # The GLM lamp cards extended above/outside their actual nose caps.
    # Bring only the end station up to their existing outline so the bake
    # has real metal to land on. No wheel/roof/collision bounds change.
    if name in ('glm_zip','glm_lunge'):
        z,width,old_width = (2.5,.98,.82) if name=='glm_zip' else (2.75,1.08,.94)
        end=np.abs(low[:,:,2]-z)<1e-5
        # Limit the change to the main shell, not the splitter at that station.
        shell=end&(low[:,:,1]>.25)&(np.abs(low[:,:,0])<=old_width+1e-5)
        low[:,:,0][shell]*=width/old_width
        if name=='glm_zip':
            for old,new in ((.39,.58),(.43,.40),(.47,.61)):
                mask=shell&(np.abs(low[:,:,1]-old)<1e-5)
                low[:,:,1][mask]=new
        else:
            for old,new in ((.40,.58),(.43,.61)):
                mask=shell&(np.abs(low[:,:,1]-old)<1e-5)
                low[:,:,1][mask]=new
        for t in low:
            n=np.cross(t[1,:3]-t[0,:3],t[2,:3]-t[0,:3])
            if np.linalg.norm(n)>1e-9:
                n/=np.linalg.norm(n)
                if (np.abs(t[:,2]-z)<1e-5).all():
                    if n[2]<0:n=-n
                elif n@t[:,3:6].mean(0)<0:n=-n
                t[:,3:6]=n
    # Coplanar receiver triangles share a chart, even across primitive seams.
    groups={}
    for i,t in enumerate(low):
        n=np.cross(t[1,:3]-t[0,:3],t[2,:3]-t[0,:3])
        if np.linalg.norm(n)<1e-10: continue
        n/=np.linalg.norm(n)
        if n@t[:3,3:6].mean(0)<0:n=-n
        d=n@t[0,:3]
        key=tuple(np.round(n,4))+(round(d,4),)
        groups.setdefault(key,[]).append(i)
    charts=[]
    for key,faces in groups.items():
        n=np.array(key[:3]);n/=np.linalg.norm(n)
        omitted=int(np.argmax(np.abs(n)))
        axes=[i for i in range(3) if i!=omitted]
        pts=low[faces,:,:3].reshape(-1,3)[:,axes]
        charts.append(dict(faces=faces,n=n,d=float(n@low[faces[0],0,:3]),axes=axes,
                           omitted=omitted,lo=pts.min(0),hi=pts.max(0),
                           protected=all(keep[fi] in protected for fi in faces)))
    reach=np.ptp(v[:,2])*.065
    for c in charts:
        c['priority']=1.8 if c['protected'] else .65
        for t in layers:
            normal=t[:,3:6].mean(0)
            if abs(normal@c['n'])<.45:continue
            if np.abs(t[:,:3]@c['n']-c['d']).min()>reach:continue
            p=t[:,c['axes']]
            overlap=np.minimum(p.max(0),c['hi'])-np.maximum(p.min(0),c['lo'])
            if (overlap>1e-5).all():
                c['priority']=1.8
                break
    density=36/(np.ptp(v[:,2])/5)
    while (boxes:=pack(charts,density)) is None:
        density*=.92
        assert density>3,'atlas cannot fit'
    source=np.asarray(Image.open(texture/'body.png').convert('RGBA'))
    image=np.zeros((256,256,4),dtype=np.uint8);image[:]=(30,30,30,255)
    output=[]
    painted=set()
    reach=np.ptp(v[:,2])*.065
    for ci,c in enumerate(charts):
        x,y,w,h=boxes[ci]
        yy,xx=np.mgrid[0:h-4,0:w-4]
        points=np.stack((xx/(w-5),1-yy/(h-5)),axis=-1)*(c['hi']-c['lo'])+c['lo']
        world=np.zeros((*xx.shape,3));world[...,c['axes']]=points
        world[...,c['omitted']]=(c['d']-np.sum(points*c['n'][c['axes']],axis=-1))/c['n'][c['omitted']]
        pixels=np.zeros((*xx.shape,4),dtype=np.uint8);pixels[:]=(30,30,30,255)
        covered=np.zeros(xx.shape,dtype=bool)
        for fi in c['faces']:
            t=low[fi]
            weights=barycentric(points,t[:,c['axes']])
            if weights is None:continue
            mask=(weights>=-1e-5).all(-1)
            uv=np.sum(weights[...,None]*t[:,6:8],axis=-2)
            tx=np.clip((uv[...,0]*source.shape[1]).astype(int),0,source.shape[1]-1)
            ty=np.clip(((1-uv[...,1])*source.shape[0]).astype(int),0,source.shape[0]-1)
            pixels[mask]=source[ty,tx][mask];covered|=mask
        depth=np.full(xx.shape,-np.inf)
        # Some legacy cooks have inward shading normals on end caps. Optical
        # stacking still follows exterior depth, never the sign of that normal.
        outward=1 if np.dot(world.mean(axis=(0,1))-v[:,:3].mean(0),c['n'])>=0 else -1
        for li,t in enumerate([] if c['protected'] else layers):
            normal=t[:,3:6].mean(0)
            normal/=max(np.linalg.norm(normal),1e-8)
            if abs(normal@c['n'])<.45:continue
            # Orthogonal ray projection along the RECEIVER normal, not a
            # global view axis. Sloped glass keeps its authored footprint.
            tn=np.cross(t[1,:3]-t[0,:3],t[2,:3]-t[0,:3])
            denom=tn@c['n']
            if abs(denom)<1e-9:continue
            distance=np.sum((t[0,:3]-world)*tn,axis=-1)/denom
            hit=world+distance[...,None]*c['n']
            ax=[i for i in range(3) if i!=np.argmax(np.abs(tn))]
            weights=barycentric(hit[...,ax],t[:,ax])
            if weights is None:continue
            # Conservative one-pixel coverage keeps tiny seams/markers from
            # vanishing between texel centres at the required PSX resolution.
            dx=(weights[0,-1]-weights[0,0])/max(w-5,1)
            dy=(weights[-1,0]-weights[0,0])/max(h-5,1)
            slack=.5*(np.abs(dx)+np.abs(dy))+1e-5
            layer_depth=distance*outward
            mask=covered&(weights>=-slack).all(-1)&(distance>=-reach)&(distance<=reach)&(layer_depth>=depth-1e-5)
            if not mask.any():continue
            weights=np.maximum(weights,0)
            weights/=np.maximum(weights.sum(-1,keepdims=True),1e-8)
            uv=np.sum(weights[...,None]*t[:,6:8],axis=-2)
            tx=np.clip((uv[...,0]*source.shape[1]).astype(int),0,source.shape[1]-1)
            ty=np.clip(((1-uv[...,1])*source.shape[0]).astype(int),0,source.shape[0]-1)
            pixels[mask]=source[ty,tx][mask];depth[mask]=layer_depth[mask]
            painted.add(removed[li])
        # Dilate chart contents into unused triangle corners BEFORE gutter
        # padding. Otherwise nearest filtering at sloped UV edges hits black.
        if covered.any():
            nearest=distance_transform_edt(~covered,return_distances=False,return_indices=True)
            pixels=pixels[tuple(nearest)]
        image[y:y+h,x:x+w]=np.pad(pixels,((2,2),(2,2),(0,0)),mode='edge')
        for fi in c['faces']:
            t=low[fi].copy()
            ab=(t[:,c['axes']]-c['lo'])/(c['hi']-c['lo'])
            t[:,6]=(x+2.5+ab[:,0]*(w-5))/256
            t[:,7]=1-(y+2.5+(1-ab[:,1])*(h-5))/256
            # Tangents are not used for vehicle normal mapping.
            output.extend(t)
    dest=OUTPUT/name;dest.mkdir(parents=True,exist_ok=True)
    write_mesh(dest/'body.emesh',output,name)
    # Vehicle bodies use the opaque pass. Some old reference swatches contain
    # unused transparent texels; never carry those into a receiver island.
    image[:,:,3]=255
    Image.fromarray(image).save(dest/'body.png')
    out=np.asarray(output)
    assert np.isfinite(out).all() and ((out[:,6:8]>=0)&(out[:,6:8]<=1)).all()
    assert np.allclose(out[:,:3].min(0),v[:,:3].min(0),atol=.08)
    assert np.allclose(out[:,:3].max(0),v[:,:3].max(0),atol=.08)
    report=dict(model=name,original_triangles=len(ids),triangles=len(output)//3,
                removed_components=skin_ids,removed_triangles=len(removed),
                painted_source_triangles=len(painted),charts=len(charts),texels_per_source_unit=float(density),
                unpainted_components=[i for i in skin_ids if not any(f in painted for f in comps[i])],
                source_mesh_sha256=hashlib.sha256((model/'body.emesh').read_bytes()).hexdigest(),
                source_texture_sha256=hashlib.sha256((texture/'body.png').read_bytes()).hexdigest(),
                atlas=[256,256,'RGBA'])
    assert not report['unpainted_components'],f'{name}: a removed detail has no baked receiver'
    (dest/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))
    if install:
        import shutil
        shutil.copyfile(dest/'body.emesh',model/'body_surface.emesh')
        shutil.copyfile(dest/'body.png',texture/'body_surface.png')
        shutil.copyfile(dest/'report.json',texture/'body_surface.json')
    return report


def bake_if_canonical(name,mesh,texture):
    """Called by source cookers; custom/test output paths never touch the fleet."""
    if (Path(mesh).resolve()==ROOT/f'assets/models/vehicles/{name}/body.emesh' and
        Path(texture).resolve()==ROOT/f'assets/textures/vehicles/{name}/body.png'):
        OUTPUT.mkdir(parents=True,exist_ok=True)
        bake_model(name,install=True)


def preview_fleet(names):
    from render_firetruck_preview import read_part,raster_view
    from PIL import ImageDraw
    sheet=Image.new('RGB',(1200,len(names)*290),(22,25,29))
    draw=ImageDraw.Draw(sheet)
    for row,name in enumerate(names):
        source=read_part(ROOT/f'assets/models/vehicles/{name}/body.emesh',ROOT/f'assets/textures/vehicles/{name}/body.png')
        baked=read_part(OUTPUT/name/'body.emesh',OUTPUT/name/'body.png')
        for col,(part,yaw,label) in enumerate(((source,-32,'BEFORE'),(baked,-32,'BAKED FRONT'),(baked,-148,'BAKED REAR'))):
            sheet.paste(raster_view([part],yaw,18,400,265),(col*400,row*290))
            draw.text((col*400+8,row*290+269),name+' '+label,fill=(240,225,205))
    sheet.save(OUTPUT/'fleet-preview.png')


if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('models',nargs='*')
    p.add_argument('--bake',action='store_true')
    p.add_argument('--install',action='store_true')
    p.add_argument('--preview',action='store_true')
    args=p.parse_args()
    OUTPUT.mkdir(parents=True,exist_ok=True)
    if args.preview:
        preview_fleet(args.models)
        raise SystemExit(0)
    for name in args.models:
        if args.bake or args.install:
            bake_model(name,args.install)
            continue
        items=audit(name)
        (OUTPUT/f'{name}-components.json').write_text(json.dumps(items,indent=2)+'\n')
        print(name,len(items),'components')
        for item in items:
            if item['planar'] or min(item['size'])<.07:
                print(item)
