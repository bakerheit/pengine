#!/usr/bin/env python3
"""Bake an original articulated trench outfit onto the owned PSX player rig.

The supplied pack contributes head, hands, trousers, shoes, and the unchanged
animation skeleton. Coat shell, split tails, sleeves, collar, lapels, belt,
storm flap and tie are original meshes. Requires the locally staged PSX pack.
"""
from pathlib import Path
import json, math, shutil, struct
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'assets/models/characters/psx_pack/player_male_01'
OUT=ROOT/'assets/models/characters/psx_pack/trench_detective'


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    data=(SRC/'skin.emesh').read_bytes();h=struct.unpack_from('<8I',data)
    vertices=[list(struct.unpack_from('<12f4B4f',data,32+i*68)) for i in range(h[3])]
    source_indices=list(struct.unpack_from('<%dI'%h[4],data,32+h[3]*68))
    tex=Image.new('RGB',(1024,1024))
    tex.paste(Image.open(SRC/'body.png').convert('RGB').resize((512,512),Image.Resampling.NEAREST),(0,0))
    rng=np.random.default_rng(1989)
    palette={'coat':(512,0,(85,71,45)),'lapel':(768,0,(101,85,55)),
             'lining':(512,256,(49,46,35)),'belt':(768,256,(61,53,36)),
             'pants':(512,512,(31,34,36)),'shoes':(768,512,(34,29,24)),
             'shirt':(512,768,(167,161,140)),'tie':(768,768,(32,35,38))}
    for name,(x,y,col) in palette.items():
        coarse=rng.integers(-8,9,(32,32,1));tile=np.clip(np.array(col)+coarse,0,255).astype('uint8')
        tex.paste(Image.fromarray(tile).resize((256,256),Image.Resampling.NEAREST),(x,y))
    tex.save(OUT/'body.png')
    def uv(mat,u,v):
        x,y,_=palette[mat];return ((x+5+246*u)/1024,1-(y+5+246*(1-v))/1024)
    for v in vertices:
        v[6]*=.5;v[7]=.5+v[7]*.5
        if 370<v[1]<398:
            v[6],v[7]=uv('coat',(v[2]+85)/170,(v[1]-370)/33)
        if v[1]<267 and abs(v[2])<55:
            v[6],v[7]=uv('pants' if v[1]>25 else 'shoes',(v[2]+55)/110,max(0,v[1])/267)
    indices=[]
    for i in range(0,len(source_indices),3):
        ids=source_indices[i:i+3];p=np.mean([vertices[j][:3] for j in ids],axis=0)
        # Remove the old torso and sleeves where the custom coat replaces them.
        # Retain the source head, hands, trousers and shoes.
        hand=sum(sum(v[16+k] for k in range(4) if v[12+k] in (11,12,16,17,21,22,23,27)) for v in [vertices[j] for j in ids])/3
        if 263<p[1]<386 and hand<.55:continue
        if abs(p[2])>42 and 248<p[1]<382 and hand<.55:continue
        indices+=ids
    source_vertex_count=len(vertices)
    def weights(p):
        y=p[1];side=0 if p[2]>0 else 4
        if y<268:
            t=min(.88,max(0,(268-y)/110));return [(2,1-t),(side,t)]
        if y<305:
            t=(y-268)/37;return [(2,1-t),(1,t)]
        if y<340:
            t=(y-305)/35;return [(1,1-t),(3,t)]
        if y<374:
            t=(y-340)/34;return [(3,1-t),(6,t)]
        return [(6,1)]
    def face(points,mat='coat',custom=None,back=False):
        ps=[np.array(p,dtype=float) for p in points]
        n=np.cross(ps[1]-ps[0],ps[2]-ps[0]);n/=max(1e-9,np.linalg.norm(n))
        base=len(vertices)
        for j,p in enumerate(ps):
            bi=custom[j] if custom else weights(p)
            bi=[(b,w) for b,w in bi if w>1e-8]
            ids=[b for b,w in bi]+[0]*(4-len(bi));ws=[w for b,w in bi]+[0]*(4-len(bi))
            u,v=uv(mat,(j in (1,2))*.95+.025,(j>=2)*.95+.025)
            vertices.append([*p,*n,u,v,1,0,0,1,*ids,*ws])
        for j in range(1,len(ps)-1):indices.extend([base,base+j,base+j+1])
        if back:
            face(list(reversed(points)),'lining',list(reversed(custom)) if custom else None)
    # Connected cloth panels; split at front and below the back waist.
    rings=[(392,-20,17,18,1.05),(377,-17,31,46,.65),(350,-12,34,46,.40),
           (307,-6,34,44,.24),(268,-4,33,44,.12),(235,-6,39,47,.16),
           (190,-9,44,50,.21),(145,-12,45,52,.25)]
    for side in (-1,1):
        points=[]
        for y,xc,deep,wide,gap in rings:
            end=math.pi-(.065*max(0,268-y)/123)
            points.append([(xc+deep*math.cos(t),y,side*wide*math.sin(t)) for t in np.linspace(gap,end,11)])
        for row in range(len(points)-1):
            for j in range(10):
                ps=[points[row][j],points[row+1][j],points[row+1][j+1],points[row][j+1]]
                if side<0:ps.reverse()
                face(ps,back=True)
    # Sleeves have shoulder/elbow/wrist rings and explicit matching arm weights.
    for side,bone,fore in [(1,5,10),(-1,19,20)]:
        rings_arm=[((-20.5,370,side*43.3),17,[(bone,.85),(6,.15)]),
                   ((-17,341,side*52),15.6,[(bone,1)]),
                   ((-12.6,308,side*59.7),14.5,[(bone,.5),(fore,.5)]),
                   ((0,276,side*65.7),12.5,[(fore,1)]),
                   ((12,244,side*71.5),10,[(fore,.95),(11 if side>0 else 21,.05)])]
        points=[]
        for j,(c,r,w) in enumerate(rings_arm):
            c=np.array(c);a=np.array(rings_arm[max(0,j-1)][0]);b=np.array(rings_arm[min(j+1,len(rings_arm)-1)][0]);d=b-a;d/=np.linalg.norm(d)
            u=np.cross(d,[0,0,1]);u/=np.linalg.norm(u);v=np.cross(d,u)
            points.append([c+r*(math.cos(t)*u+math.sin(t)*v) for t in np.linspace(0,2*math.pi,11)[:-1]])
        for row in range(len(points)-1):
            for j in range(10):
                k=(j+1)%10;ps=[points[row][j],points[row][k],points[row+1][k],points[row+1][j]]
                w0=rings_arm[row][2];w1=rings_arm[row+1][2]
                face(ps,'coat',[w0,w0,w1,w1],True)
    # A tapered undershirt closes the front opening without leaving the old
    # pack's broad shirt surface poking through the custom cloth shell.
    face([(0,388,-12),(0,388,12),(30,275,10),(30,275,-10)],'shirt',back=True)
    # Front lapels and tie follow spine weights, never rigid props at the root.
    for s in (-1,1):
        face([(4,387,s*12),(19,368,s*27),(22,337,s*11),(25,316,s*5)],'lapel',back=True)
        face([(24,277,s*21),(28,277,s*32),(30,269,s*32),(26,269,s*21)],'belt',back=True)
    face([(13,378,-4),(13,378,4),(27,299,3),(27,287,0),(27,299,-3)],'tie',back=True)
    # Back storm flap. Its corners sit on the actual curved back surface.
    face([(-36,375,-30),(-43,353,-36),(-37,332,-32),(-43,332,0),(-37,332,32),(-43,353,36),(-36,375,30)],'coat',back=True)
    for j in range(20):
        a=2*math.pi*j/20;b=2*math.pi*(j+1)/20
        if math.cos((a+b)/2)>.96:continue
        ps=[(-4+29*math.cos(a),273,37*math.sin(a)),(-4+29*math.cos(a),264,37*math.sin(a)),
            (-4+29*math.cos(b),264,37*math.sin(b)),(-4+29*math.cos(b),273,37*math.sin(b))]
        face(ps,'belt',back=True)
    name=b'trench_detective\0'
    with (OUT/'skin.emesh').open('wb') as f:
        f.write(struct.pack('<8I',0x48534d45,2,1,len(vertices),len(indices),1,len(name),0))
        for v in vertices:f.write(struct.pack('<12f4B4f',*v))
        f.write(struct.pack('<%dI'%len(indices),*indices));f.write(struct.pack('<4I',0,len(indices),0,0));f.write(name)
    shutil.copy2(SRC/'skin.eskel',OUT/'skin.eskel')
    result={'vertices':len(vertices),'triangles':len(indices)//3,'original_coat_vertices':len(vertices)-source_vertex_count,
            'bone_count':28,'source':'locally owned PSX pack player rig; original coat and cloth atlas',
            'reference':'docs/design/references/bellwether/detective.png'}
    (OUT/'manifest.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':main()
