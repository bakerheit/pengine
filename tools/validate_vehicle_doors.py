"""Validate/render independently articulated static body and entry door assets."""
import json
import math
import numpy as np
from PIL import Image, ImageDraw
from render_firetruck_preview import Part,read_part,raster_view
from make_vesper_vx91_assets import read_emesh


def validate_and_preview(root,slug,door,driver,closed_parts,contains,make_preview=True):
    model=root/'assets/models/vehicles'/slug
    texture=root/'assets/textures/vehicles'/slug/'body.png'
    report={}
    meshes={}
    for name in ('body_open','driver_door'):
        vertices,indices=read_emesh(model/(name+'.emesh'))
        v=np.asarray(vertices);t=v[np.asarray(indices).reshape(-1,3),:3]
        assert len(indices)>0 and np.isfinite(v).all()
        assert ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all()
        assert np.all(np.linalg.norm(np.cross(t[:,1]-t[:,0],t[:,2]-t[:,0]),axis=1)>1e-9)
        report[name]={'triangles':len(t),'bounds_min':v[:,:3].min(0).tolist(),'bounds_max':v[:,:3].max(0).tolist()}
        meshes[name]=t
    body=meshes['body_open']
    moving=meshes['driver_door']
    tested=0
    def projected_x(t,p):
        weights=np.linalg.solve(np.vstack([t[:,[2,1]].T,np.ones(3)]),np.array([*p,1]))
        return float(weights@t[:,0])
    for z in np.linspace(door['rear_z']+.10,door['front_z']-.14,5):
        for y in (door['sill_y']+.12,door['sill_y']+.32,1.0):
            p=np.array([z,y]);covers=[t for t in moving if contains(p,t[:,[2,1]])]
            # A raked door's upper front corner is not an opening at every
            # height. Test only points actually occupied by its moving skin.
            if not covers:continue
            surface=max(projected_x(t,p) for t in covers)
            blockers=[t for t in body if contains(p,t[:,[2,1]]) and projected_x(t,p)>surface-.045]
            assert not blockers,('stationary doorway obstruction',z,y)
            tested+=1
    assert tested>=10,'not enough real aperture probes'
    report['aperture_probes']=tested
    # The opening has a retained floor beneath the driver's feet and seat.
    p=np.array([driver['left_x'],driver['hip'][2]])
    assert any(contains(p,t[:,[0,2]]) for t in body if (t[:,1]<.38).all()),'missing cabin floor'
    if not make_preview:
        report.update(door=door,driver=driver,checks=['finite static open body and separate door','clear stationary doorway','retained cabin floor'])
        (root/'build'/f'{slug}-door-report.json').write_text(json.dumps(report,indent=2)+'\n')
        return report
    body_part=read_part(model/'body_open.emesh',texture)
    door_part=read_part(model/'driver_door.emesh',texture)
    hinge=np.array(door['hinge']);angle=math.radians(door['open_degrees'])
    c,s=math.cos(angle),math.sin(angle);rotation=np.array([[c,0,s],[0,1,0],[-s,0,c]])
    opened=Part((door_part.positions-hinge)@rotation.T+hinge,
        door_part.normals@rotation.T,door_part.uvs,door_part.indices,door_part.texture)
    shut=[body_part,door_part]+closed_parts[1:]
    swung=[body_part,opened]+closed_parts[1:]
    # Driver is +X. Negative renderer yaw exposes the doorway instead of the passenger side.
    views=[('CLOSED ASSEMBLY',shut,-32,18),('OPEN DOOR',swung,-32,18),
           ('CABIN / FLOOR',swung,-75,32),('REAR DOOR VIEW',swung,-145,20)]
    sheet=Image.new('RGB',(1200,850),(20,23,26));draw=ImageDraw.Draw(sheet)
    for i,(label,parts,yaw,pitch) in enumerate(views):
        image=raster_view(parts,yaw,pitch,600,392)
        x=(i%2)*600;y=(i//2)*425;sheet.paste(image,(x,y));draw.text((x+12,y+402),label,fill=(235,235,220))
    sheet.save(root/'build'/f'{slug}-door-preview.png')
    report.update(door=door,driver=driver,checks=['finite static open body and separate door','clear stationary doorway','retained cabin floor','closed and swung door visual preview'])
    (root/'build'/f'{slug}-door-report.json').write_text(json.dumps(report,indent=2)+'\n')
    return report
