#!/usr/bin/env python3
"""Verify cooked BWC geometry; --door-sweep adds a bounded section report."""
import json,math,sys
import numpy as np
from PIL import Image
from bwc_360_spec import ROOT,MODEL,TEXTURE,SHAPE,WHEELS,PLATE_MOUNTS,REGIONS
from make_vesper_vx91_assets import read_emesh
parts={};report={};uv_parts={}
for path in MODEL.glob('*.emesh'):
    vertices,ids=read_emesh(path);v=np.asarray(vertices);ids=np.asarray(ids).reshape(-1,3)
    assert len(ids)>0 and np.isfinite(v).all() and ids.max()<len(v),path
    assert ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all(),path
    tri=v[ids,:3];area=np.linalg.norm(np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]),axis=1)
    assert (area>1e-10).all(),(path,'degenerate face')
    parts[path.stem]=tri;uv_parts[path.stem]=v[ids,6:8];report[path.stem]={'triangles':len(ids),'minimum':v[:,:3].min(0).tolist(),'maximum':v[:,:3].max(0).tolist()}
assert len(parts['body'])<SHAPE['body_triangle_budget'],len(parts['body'])
with Image.open(TEXTURE) as im:assert im.size==(256,256) and im.mode=='RGBA'
for side in ['driver','passenger']:
    for row in ['front','rear']:
        assert len(parts[f'{side}_{row}_door'])>50
        assert len(parts[f'{side}_{row}_glass'])>=2
assert len(parts['windshield'])>=2 and len(parts['rear_glass'])>=2
for name,t in parts.items():
    if 'glass' not in name and name!='windshield':continue
    outward=np.array((0,0,1) if name=='windshield' else ((0,0,-1) if name=='rear_glass' else (1 if name.startswith('driver') else -1,0,0)))
    normals=np.cross(t[:,1]-t[:,0],t[:,2]-t[:,0])
    assert (normals@outward>0).all(),(name,'inward glazing winding')
def hit_x(tri,y,z,s):
    q=tri[:,:,[1,2]];a=q[:,1]-q[:,0];b=q[:,2]-q[:,0];d=a[:,0]*b[:,1]-a[:,1]*b[:,0];valid=abs(d)>1e-10
    p=np.array([y,z])-q[:,0];u=np.zeros(len(tri));v=u.copy()
    np.divide(p[:,0]*b[:,1]-p[:,1]*b[:,0],d,out=u,where=valid)
    np.divide(a[:,0]*p[:,1]-a[:,1]*p[:,0],d,out=v,where=valid)
    keep=valid&(u>=-1e-6)&(v>=-1e-6)&(u+v<=1+1e-6)
    depth=tri[:,0,0]+u*(tri[:,1,0]-tri[:,0,0])+v*(tri[:,2,0]-tri[:,0,0])
    return bool((keep&(s*depth>.69)).any())

def atlas_material(uv):
    for key,(x0,y0,x1,y1) in REGIONS.items():
        if x0/256<uv[0]<x1/256 and 1-y1/256<uv[1]<1-y0/256:return key
    return 'UNKNOWN'

# Every wheel cavity must have a near-side opaque inboard wall.
# These rays used to escape through the body to the opposite wheel opening.
tri=parts['body_open'];q=tri[:,:,[1,2]];a=q[:,1]-q[:,0];b=q[:,2]-q[:,0]
den=a[:,0]*b[:,1]-a[:,1]*b[:,0];valid=abs(den)>1e-10
for side in (-1,1):
    for axle in (WHEELS['front_z'],WHEELS['rear_z']):
        for h in (.43,.56,.67):
            for dz in (-.10,0,.10):
                p=np.array([h,axle+dz])-q[:,0];u=np.zeros(len(tri));v=u.copy()
                np.divide(p[:,0]*b[:,1]-p[:,1]*b[:,0],den,out=u,where=valid)
                np.divide(a[:,0]*p[:,1]-a[:,1]*p[:,0],den,out=v,where=valid)
                depth=side*(tri[:,0,0]+u*(tri[:,1,0]-tri[:,0,0])+v*(tri[:,2,0]-tri[:,0,0]))
                keep=valid&(u>=-1e-7)&(v>=-1e-7)&(u+v<=1+1e-7)&(depth>.45)&(depth<.55)
                ids=np.flatnonzero(keep)
                assert len(ids) and any(atlas_material(uv_parts['body_open'][i].mean(0))=='BLACK' for i in ids),('missing opaque wheel tub',side,axle,h,dz)

def section_segments(tri,axis,value):
    """Slice exported triangles; return 3D line segments and source face IDs."""
    ids=np.flatnonzero((tri[:,:,axis].min(1)<=value)&(tri[:,:,axis].max(1)>value))
    lines=[];faces=[]
    for idx in ids:
        points=[]
        for a,c in [(0,1),(1,2),(2,0)]:
            lo=tri[idx,a];hi=tri[idx,c]
            if (lo[axis]<=value<hi[axis]) or (hi[axis]<=value<lo[axis]):
                points.append(lo+(hi-lo)*(value-lo[axis])/(hi[axis]-lo[axis]))
        if len(points)==2 and np.linalg.norm(points[0]-points[1])>1e-8:
            lines.append(points);faces.append(idx)
    return np.asarray(lines).reshape(-1,2,3),np.asarray(faces,dtype=int)

def validate_door_geometry(meshes,uvs):
    """Check actual side surfaces without importing the door generator."""
    cache={};metrics={'flush_samples':[],'center_gaps':[],'roof_gaps':[],'pillar_backing':[],'clear_rear_skin_samples':0}
    keys={name:np.array([atlas_material(uv.mean(0)) for uv in part]) for name,part in uvs.items()}
    for name,tri in meshes.items():
        q=tri[:,:,[1,2]];a=q[:,1]-q[:,0];b=q[:,2]-q[:,0]
        den=a[:,0]*b[:,1]-a[:,1]*b[:,0]
        cache[name]=(tri,q,a,b,den,abs(den)>1e-10)
    def ray(name,h,z,s):
        tri,q,a,b,den,valid=cache[name];p=np.array([h,z])-q[:,0]
        u=np.zeros(len(tri));v=u.copy()
        np.divide(p[:,0]*b[:,1]-p[:,1]*b[:,0],den,out=u,where=valid)
        np.divide(a[:,0]*p[:,1]-a[:,1]*p[:,0],den,out=v,where=valid)
        keep=valid&(u>=-1e-7)&(v>=-1e-7)&(u+v<=1+1e-7)
        if not keep.any():return -np.inf,'NONE'
        depth=s*(tri[:,0,0]+u*(tri[:,1,0]-tri[:,0,0])+v*(tri[:,2,0]-tri[:,0,0]))
        idx=np.argmax(np.where(keep,depth,-np.inf))
        return float(depth[idx]),keys[name][idx]
    def boundary(name,h,s):
        lines,ids=section_segments(meshes[name],1,h)
        keep=(keys[name][ids]=='SIDE')&(s*lines[:,:,0].mean(1)>.77)
        assert keep.any(),('missing door skin section',name,h)
        return float(lines[keep,:,2].min()),float(lines[keep,:,2].max())
    def groove(values,depths,reference,center,label):
        recessed=np.asarray(depths)<np.asarray(reference)-.008
        k=int(np.argmin(abs(values-center)));assert recessed[k],('missing shut line',label,center)
        lo=hi=k
        while lo>0 and recessed[lo-1]:lo-=1
        while hi+1<len(values) and recessed[hi+1]:hi+=1
        assert lo>0 and hi+1<len(values),('unbounded panel gap',label)
        width=float(values[hi]-values[lo]+values[1]-values[0])
        assert .0015<width<.010,('wide or sealed panel gap',label,width)
        return width
    for s,side in [(1,'driver'),(-1,'passenger')]:
        for row in ['front','rear']:
            name=side+'_'+row+'_door'
            for h in [.34,.42,.62,.74,.86]:
                low,high=boundary(name,h,s);edge=high if row=='front' else low;direction=1 if row=='front' else -1
                inside=ray(name,h,edge-direction*.014,s)[0]
                # The rear door's lower edge borders the wheel aperture, not
                # another painted panel; compare the upper quarter seam only.
                if row=='front' or h>.72:
                    outside,key=ray('body_open',h,edge+direction*.020,s)
                    assert key in ('PAINT','SIDE') and outside>.77,('missing quarter beside door',name,h,edge,key,outside)
                    step=outside-inside
                    assert abs(step)<.004,('door quarter profile step',name,h,step)
                    metrics['flush_samples'].append(dict(door=name,height=h,step_m=step))
                if row=='rear':
                    for dz in [.016,.040,.075]:
                        z=low+dz;skin=ray(name,h,z,s)[0];fixed=ray('body_open',h,z,s)[0]
                        assert fixed<skin-.012,('fixed quarter covers rear door',name,h,z,fixed-skin)
                        metrics['clear_rear_skin_samples']+=1
        for h in [.34,.62,.86,1.08,1.22]:
            a=ray(side+'_front_door',h,-.045,s)[0];c=ray(side+'_rear_door',h,-.105,s)[0];reference=(a+c)/2
            values=np.linspace(-.110,-.040,141);depths=[ray('body',h,z,s)[0] for z in values]
            width=groove(values,depths,reference,-.075,(side,'center',h))
            backing,key=ray('body',h,-.075,s)
            assert key=='BLACK' and .012<reference-backing<.045,('center seam backing',side,h,key,reference-backing)
            metrics['center_gaps'].append(dict(side=side,height=h,width_m=width,backing_m=reference-backing))
        for row,z in [('front',.12),('rear',-.40)]:
            name=side+'_'+row+'_door';lines,ids=section_segments(meshes[name],2,z)
            keep=(keys[name][ids]=='PAINT')&(s*lines[:,:,0].mean(1)>.65)
            top=float(lines[keep,:,1].max());a=ray(name,top-.020,z,s)[0];c=ray(name,top-.010,z,s)[0]
            heights=np.linspace(top-.012,top+.018,61)
            reference=a+(c-a)*(heights-(top-.020))/.010
            depths=[ray('body',h,z,s)[0] for h in heights]
            width=groove(heights,depths,reference,top+.002,(side,row,'roof'))
            # A 4 mm roof shut line gets 2 mm allowance for the edge return
            # and sampling; the old 7.5-8.5 mm roof grooves must not pass.
            assert width<.006,('wide roof shut line',name,width)
            backing,key=ray('body',top+.002,z,s)
            expected=a+(c-a)*.022/.010
            # The painted inner return may be ahead of the black weather seal.
            # Either must provide close backing instead of a view into the cabin.
            assert key in ('PAINT','BLACK') and .006<expected-backing<.040,('roof seam backing',name,key,expected-backing)
            metrics['roof_gaps'].append(dict(door=name,width_m=width,backing_m=expected-backing))
        for row,direction in [('front',1),('rear',-1)]:
            name=side+'_'+row+'_door'
            for h in [1.10,1.20]:
                lines,ids=section_segments(meshes[name],1,h)
                painted=keys[name][ids]=='PAINT'
                assert painted.any(),('missing upper door frame',name,h)
                reference=float((s*lines[painted,:,0]).max())
                # Follow the exported outside frame edge, excluding the inner
                # return. These diagonal A/C gaps can hide on a dark backdrop.
                outer=painted&((s*lines[:,:,0]).min(1)>reference-.004)
                assert outer.any(),('missing outside door frame',name,h)
                edge=float(lines[outer,:,2].max() if direction>0 else lines[outer,:,2].min())
                for offset in [.0005,.0020,.0035]:
                    z=edge+direction*offset;backing,key=ray('body',h,z,s);inset=reference-backing
                    assert key in ('PAINT','BLACK') and -.002<inset<.060,('unbacked A or C pillar seam',name,h,z,key,inset)
                    metrics['pillar_backing'].append(dict(door=name,height=h,longitudinal_m=z,backing_m=inset))
    return metrics

def door_sweep_report(meshes,hinges):
    """Optional bounded mesh-section sweep, not a full solid collision proof.

    Tests each door against the fixed shell and the adjacent closed door. A
    1 mm endpoint tolerance excludes touching tessellation edges; reported
    crossings require inspection rather than being silently called clearance.
    """
    heights=[.282,.305,.345,.40,.70,1.10,1.26,1.30,1.33,1.35];slices={}
    for name,tri in meshes.items():
        if name=='body_open' or name.endswith('_door'):
            slices[name]={h:section_segments(tri,1,h)[0][:,:,[0,2]] for h in heights}
    def crossings(a,b):
        if not len(a) or not len(b):return []
        p=a[:,0,None,:];r=(a[:,1]-a[:,0])[:,None,:]
        q=b[None,:,0,:];v=(b[:,1]-b[:,0])[None,:,:]
        den=r[:,:,0]*v[:,:,1]-r[:,:,1]*v[:,:,0];valid=abs(den)>1e-10;d=q-p
        t=np.zeros_like(den);u=t.copy()
        np.divide(d[:,:,0]*v[:,:,1]-d[:,:,1]*v[:,:,0],den,out=t,where=valid)
        np.divide(d[:,:,0]*r[:,:,1]-d[:,:,1]*r[:,:,0],den,out=u,where=valid)
        ma=.001/np.maximum(np.linalg.norm(r,axis=2),1e-9);mb=.001/np.maximum(np.linalg.norm(v,axis=2),1e-9)
        keep=valid&(t>ma)&(t<1-ma)&(u>mb)&(u<1-mb)
        points=(p+t[:,:,None]*r)[keep]
        if not len(points):return []
        # Neighboring triangulation fragments can report the same crossing.
        _,ids=np.unique(np.round(points/.002).astype(int),axis=0,return_index=True)
        return points[ids].tolist()
    result={'scope':'horizontal mesh sections with 1 mm endpoint tolerance',
            'heights_m':heights,'poses':0,'contacts':[]}
    for side,s in [('driver',1),('passenger',-1)]:
        for row in ['front','rear']:
            name=side+'_'+row;other=side+('_rear_door' if row=='front' else '_front_door')
            # Cook report stores Blender construction X/Y/Z; this slice is X/Z
            # in runtime coordinates, so construction Y is its second axis.
            pivot=np.array(hinges[name][:2]);maximum=62 if row=='front' else 58
            for angle in [0,2,5,10,20,35,maximum]:
                theta=s*math.radians(angle);rotation=np.array([[math.cos(theta),-math.sin(theta)],[math.sin(theta),math.cos(theta)]])
                for h in heights:
                    moving=(slices[name+'_door'][h]-pivot)@rotation.T+pivot
                    for obstacle in ['body_open',other]:
                        fixed=slices[obstacle][h];fixed=fixed[s*fixed[:,:,0].mean(1)>.60]
                        hits=crossings(moving,fixed)
                        if hits:result['contacts'].append(dict(door=name,angle_deg=angle,height_m=h,obstacle=obstacle,crossings=len(hits),points_xz=hits[:12]))
                    result['poses']+=1
    result['status']='review crossings' if result['contacts'] else 'no section crossings found'
    return result

report['door_geometry']=validate_door_geometry(parts,uv_parts)
if '--door-sweep' in sys.argv:
    report['door_sweep']=door_sweep_report(parts,json.loads((MODEL/'cook_report.json').read_text())['hinges'])
for s in (-1,1):
    for y,z in [(.71,.34),(.72,-.57),(1.16,.11),(1.16,-.48)]:
        assert not hit_x(parts['body_open'],y,z,s),('blocked door or window aperture',s,y,z)
    for y,z in [(.70,-.075),(1.37,-.075),(1.12,.61)]:
        assert hit_x(parts['body_open'],y,z,s),('missing pillar or rail',s,y,z)
# Check the actual exposed surface, so two-sided preview shading cannot hide
# a fender cap in front of a lens or a bumper protruding through a blank plate.
def frontmost(x,h,s,material=None):
    tri=parts['body'];q=tri[:,:,[0,1]];a=q[:,1]-q[:,0];b=q[:,2]-q[:,0]
    d=a[:,0]*b[:,1]-a[:,1]*b[:,0];valid=abs(d)>1e-10;p=np.array([x,h])-q[:,0]
    u=np.zeros(len(tri));v=u.copy()
    np.divide(p[:,0]*b[:,1]-p[:,1]*b[:,0],d,out=u,where=valid)
    np.divide(a[:,0]*p[:,1]-a[:,1]*p[:,0],d,out=v,where=valid)
    keep=valid&(u>=-1e-6)&(v>=-1e-6)&(u+v<=1+1e-6)
    if material is not None:
        # Supporting paint may be behind a thin molding on an adjacent patch.
        # Furniture visibility checks still use the unfiltered first hit.
        mean_uv=uv_parts['body'].mean(1);x0,y0,x1,y1=REGIONS[material]
        keep&=(mean_uv[:,0]>x0/256)&(mean_uv[:,0]<x1/256)&(mean_uv[:,1]>1-y1/256)&(mean_uv[:,1]<1-y0/256)
    depth=tri[:,0,2]+u*(tri[:,1,2]-tri[:,0,2])+v*(tri[:,2,2]-tri[:,0,2])
    assert keep.any(),('missing exterior ray hit',x,h,s)
    idx=np.argmax(np.where(keep,s*depth,-np.inf));uv=uv_parts['body'][idx]
    return s*depth[idx],uv[0]+u[idx]*(uv[1]-uv[0])+v[idx]*(uv[2]-uv[0])
fascia_checks=[]
def exposed(x,h,key,s=1):
    """Read the first actual triangle hit, including its atlas material."""
    depth,uv=frontmost(x,h,s);x0,y0,x1,y1=REGIONS[key]
    assert x0/256<uv[0]<x1/256 and 1-y1/256<uv[1]<1-y0/256,(
        'wrong exposed fascia material',x,h,s,key,uv.tolist())
    return depth

def paint_tangent(x,h,lo,hi,s=1):
    # Estimate the adjacent stamping from exported paint hits, not from the
    # producer's profile/deformation functions. Samples avoid inserted furniture.
    a=exposed(x,lo,'PAINT',s);c=exposed(x,hi,'PAINT',s)
    return a+(c-a)*(h-lo)/(hi-lo)

def record_depth(name,x,h,reference,depth,minimum,maximum,s=1):
    inset=reference-depth
    assert minimum<inset<maximum,('fascia depth',name,x,h,s,inset,minimum,maximum)
    fascia_checks.append(dict(part=name,x=x,height=h,end=s,inset_m=float(inset)))

def validate_rear_furniture():
    """Fit rear inserts to measured paint rather than producer coordinates."""
    samples=[]
    def paint(x,h):return frontmost(x,h,-1,material='PAINT')[0]
    def rear_surface(x,h):
        # Same-X vertical neighbors avoid assuming a flat panel across the
        # curved tail. Both patches sit below the lamp apertures and script.
        a=paint(x,.617);c=paint(x,.630)
        return a+(c-a)*(h-.617)/.013
    def check(part,x,h,reference,key,minimum,maximum):
        depth=exposed(x,h,key,-1);inset=reference-depth
        assert minimum<inset<maximum,('rear furniture stand-off',part,x,h,key,inset)
        samples.append(dict(part=part,x=x,height=h,inset_m=float(inset)))
    for side in (-1,1):
        for x,h in [(.470,.667),(.470,.807),(.580,.765),(.700,.667),(.800,.711)]:
            x*=side;check('red rear lens',x,h,rear_surface(x,h),'RED',-.001,.008)
        for x in [.480,.550,.630]:
            for h in [.704,.716]:
                x1=side*x;check('reverse lens',x1,h,rear_surface(x1,h),'LAMP',-.001,.008)
        for x in [.738,.773]:
            for h in [.766,.785]:
                x1=side*x;check('amber rear lens',x1,h,rear_surface(x1,h),'AMBER',-.001,.008)
        x=side*.633;h=.822
        check('rear lamp rolled border',x,h,rear_surface(x,h),'BLACK',-.001,.004)
    # The roundel occupies the almost-flat center of the transverse curve.
    # Nearby paint on both sides supplies its actual vertical pitch.
    for x,dh in [(0,0),(-.033,0),(.033,0),(0,-.033),(0,.033)]:
        h=.802+dh;reference=(paint(-.075,h)+paint(.075,h))/2
        check('fitted rear roundel',x,h,reference,'BADGE',-.004,.001)
    for x in [.295,.340,.390]:
        for h in [.813,.835]:
            check('fitted 360 script',x,h,rear_surface(x,h),'LABEL',-.003,.0015)
    # At the plate, use paint immediately beside the pocket at matching height.
    # This follows the rounded bumper below the flatter tail; a flat plate can
    # be deeper at its lower edge, but it must never float outside the stamping.
    mount=PLATE_MOUNTS['rear'];_,height,z=mount['center'];anchor=-z
    for h in [height-.076,height,height+.076]:
        reference=(paint(-.215,h)+paint(.215,h))/2
        anchor_inset=reference-anchor
        assert -.001<anchor_inset<.035,('rear plate anchor outside body or too deep',h,anchor_inset)
        for x in [-.12,0,.12]:
            check('rear plate backing',x,h,reference,'BLACK',.002,.041)
        samples.append(dict(part='rear plate anchor',x=0,height=h,inset_m=float(anchor_inset)))
    for x in [-.172,.172]:
        reference=(paint(-.215,height)+paint(.215,height))/2
        check('rear plate rolled lip',x,height,reference,'PAINT',-.001,.005)
    return samples

report['rear_furniture_samples']=validate_rear_furniture()

# The uncut painted bridge beside each grille gives the nose's vertical slope.
# A paint sample immediately above each insert anchors that slope at its X,
# retaining the actual plan-view curvature. No Blender producer is imported.
for side in (-1,1):
    slope=(exposed(side*.350,.780,'PAINT')-exposed(side*.350,.640,'PAINT'))/.140
    def nose_surface(x,h):
        return exposed(x,.815,'PAINT')+slope*(h-.815)
    for off in [-.054,0,.054]:
        x=side*(.172+off);h=.792
        record_depth('grille rolled lip',x,h,nose_surface(x,h),exposed(x,h,'METAL'),-.004,.007)
    for off in [-.054,0,.054]:
        for h in [.660,.700,.740]:
            x=side*(.172+off);depth=exposed(x,h,'METAL')
            record_depth('recessed grille vane',x,h,nose_surface(x,h),depth,.010,.025)
            floor_x=side*(.172+off+.0135);floor=exposed(floor_x,h,'BLACK')
            record_depth('grille pocket floor',floor_x,h,nose_surface(floor_x,h),floor,.021,.041)
            assert .006<depth-floor<.023,('grille vane to backing separation',side,off,h,depth-floor)

# Fog lamps must remain visible inside the aperture, behind the neighboring
# bumper paint. Multiple points catch a painted cap covering only part of a lens.
for side in (-1,1):
    for dx in [-.040,0,.040]:
        for h in [.412,.430]:
            x=side*(.577+dx)
            reference=paint_tangent(x,h,.376,.477)
            record_depth('recessed fog lens',x,h,reference,exposed(x,h,'LAMP'),.010,.031)

# Sample each end of the molding's height against the nearby paint tangent.
# This catches the old shelf that grew from 6 to 28 mm proud of the bumper.
for end in (-1,1):
    for x in [-.700,-.420,.420,.700]:
        for h,lo,hi in [(.553,.516,.539),(.576,.586,.601)]:
            reference=paint_tangent(x,h,lo,hi,end)
            record_depth('conforming impact strip',x,h,reference,exposed(x,h,'BLACK',end),-.005,.001,end)

for s in (-1,1):
    for cx in [.486,.640]:
        _,uv=frontmost(s*(cx+.030),.755,1)
        assert .75<uv[0]<1 and .25<uv[1]<.50,('occluded front optical lens',s,cx,uv.tolist())
for name in ['front','rear']:
    x,h,z=PLATE_MOUNTS[name]['center'];s=PLATE_MOUNTS[name]['normal'][2]
    for dx in np.linspace(-.305/2,.305/2,3):
        for dh in np.linspace(-.152/2,.152/2,3):
            depth,_=frontmost(x+dx,h+dh,s);gap=s*z-depth
            assert .002<gap<.008,('plate backing clearance',name,dx,dh,gap)
# Swept tire/body proximity, retaining full tire width through front steering.
cloud=[]
for t in parts['body']:
    if t[:,1].min()>.72:continue
    n=max(1,math.ceil(max(np.linalg.norm(t[i]-t[j]) for i,j in [(0,1),(0,2),(1,2)])/.025))
    for i in range(n+1):
        j=np.arange(n+1-i)[:,None]/n
        cloud.append(t[0]+i/n*(t[1]-t[0])+j*(t[2]-t[0]))
cloud=np.concatenate(cloud);poses=0
for axle in [WHEELS['front_z'],WHEELS['rear_z']]:
    for s in (-1,1):
        p=cloud-[s*WHEELS['x'],WHEELS['arch_y'],axle]
        for angle in (np.linspace(-.48,.48,25) if axle>0 else [0]):
            x=p[:,0]*math.cos(angle)+p[:,2]*math.sin(angle);z=-p[:,0]*math.sin(angle)+p[:,2]*math.cos(angle)
            r=np.hypot(p[:,1],z);bad=(abs(x)<.093)&(r<.304)&(r>.219)
            assert not bad.any(),('tire intersection',s,axle,float(angle),cloud[bad][:3].tolist())
            poses+=1
report['fascia_depth_samples']=fascia_checks
report['checks']=['four doors and four moving panes','fixed canopy seams and clear apertures','flush door quarter profiles and clear rear door skins','narrow backed center and roof shut lines','backed A and C pillar seams','finite nondegenerate UV-mapped assets','52 wheel steering poses','four exposed optical lenses','recessed grille lips vanes and pocket floors','recessed exposed fog lenses','conforming front and rear bumper molding','flush exposed rear lenses roundel and script','rear plate pocket fitted behind painted stamping','front and rear plate clearance']
(ROOT/'build/bwc-360-fit.json').write_text(json.dumps(report,indent=2)+'\n')
print('BWC 360 passed:',poses,'wheel poses;',len(cloud),'body samples')
if 'door_sweep' in report:
    sweep=report['door_sweep'];print('Door sweep:',sweep['status'],';',sweep['poses'],'section poses;',len(sweep['contacts']),'contact groups')
