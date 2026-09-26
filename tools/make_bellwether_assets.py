#!/usr/bin/env python3
"""Author Bellwether's original low-poly town meshes and matching collision.

Run with Python 3 + numpy/Pillow. Models use engine Y-up metres, +Z south,
relative to (480, 11, -700). Reference turnarounds live in
docs/design/references/bellwether. All topology and material patterns are
authored here; this is not image-to-mesh extraction.
"""
from pathlib import Path
import json
import math
import shutil
import struct
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets/models/world/bellwether'
TEX = ROOT / 'assets/textures/world/bellwether'
MATS = ['brick', 'stone', 'slate', 'wood', 'steel', 'rust', 'black', 'cream',
        'red', 'glass', 'amber', 'concrete', 'sign', 'grass', 'lamp', 'darkbrick']
COLORS = [(103,65,47),(136,130,111),(50,58,65),(72,59,43),
          (62,72,79),(90,66,43),(23,25,27),(173,166,137),
          (121,38,30),(43,59,61),(235,173,72),(111,110,101),
          (153,143,110),(76,78,49),(185,127,51),(69,56,48)]


def atlas():
    """Small hand-authored material patterns, baked as a nearest-era atlas."""
    rng = np.random.default_rng(1989)
    img = Image.new('RGB', (1024,1024))
    for idx,(name,color) in enumerate(zip(MATS,COLORS)):
        noise = rng.integers(-12,13,(64,64,1))
        tile = np.clip(np.array(color)[None,None,:]+noise,0,255).astype('uint8')
        im = Image.fromarray(tile).resize((256,256),Image.Resampling.NEAREST)
        d = ImageDraw.Draw(im)
        if name in ('brick','darkbrick'):
            for y in range(0,256,20):
                d.line((0,y,256,y),fill=(64,61,53),width=2)
                for x in range(-22 if y//20%2 else 0,256,44):
                    d.line((x,y,x,y+20),fill=(64,61,53),width=2)
        elif name == 'slate':
            for y in range(0,256,25):
                d.line((0,y,256,y),fill=(29,35,39),width=3)
                for x in range(-17 if y//25%2 else 0,256,35):
                    d.line((x,y,x,y+25),fill=(35,42,47),width=2)
        elif name == 'wood':
            for _ in range(52):
                x=int(rng.integers(0,256));y=int(rng.integers(0,220))
                d.line((x,y,x,y+int(rng.integers(20,80))),fill=(53,44,33),width=1)
        elif name in ('steel','rust'):
            for x in range(0,256,64):d.line((x,0,x,256),fill=(43,49,51),width=2)
            for _ in range(35):
                x,y=rng.integers(4,251,2);d.rectangle((x,y,x+3,y+7),fill=(98,65,42))
        elif name == 'glass':
            d.rectangle((10,10,245,245),outline=(96,101,84),width=9)
            d.line((0,130,255,130),fill=(23,29,29),width=10)
        elif name == 'amber':
            for y in range(0,256,48):
                for x in range(0,256,40):
                    c=(182,113,42) if (x//40+y//48)%3 else (107,111,77)
                    d.rectangle((x+3,y+3,x+37,y+44),fill=c)
            d.line((128,0,128,256),fill=(62,53,33),width=8)
        elif name == 'sign':
            d.rectangle((3,3,252,252),fill=(173,160,121),outline=(84,51,33),width=9)
            try:
                font=ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial Bold.ttf',27)
            except OSError:
                try:font=ImageFont.truetype('DejaVuSans-Bold.ttf',27)
                except OSError:font=ImageFont.load_default()
            for text,y in [('BELLWETHER',75),('SERVICE',124)]:
                d.text((128,y),text,font=font,fill=(67,40,27),anchor='mt')
        # 4px gutters prevent neighbouring materials bleeding through mips.
        img.paste(im,(idx%4*256,idx//4*256))
    TEX.mkdir(parents=True,exist_ok=True);OUT.mkdir(parents=True,exist_ok=True)
    img.save(TEX/'town-atlas.png');shutil.copy2(TEX/'town-atlas.png',OUT/'town-atlas.png')


class Mesh:
    def __init__(self,name):self.name=name;self.v=[];self.i=[]
    def face(self,points,mat,uv=None,reverse=False):
        p=[np.array(a,dtype=float) for a in points]
        if reverse:p=p[::-1]
        n=np.cross(p[1]-p[0],p[2]-p[0]);length=np.linalg.norm(n)
        if length<1e-9:return
        n/=length;tile=MATS.index(mat);base=len(self.v)
        if uv is None:uv=[(0,0),(1,0),(1,1),(0,1)][:len(p)]
        for a,t in zip(p,uv):
            u=(tile%4+(0.02+0.96*t[0]))/4
            # stb image loader retains top-first rows; conventional mesh V is inverted.
            v=1-(tile//4+(0.02+0.96*(1-t[1])))/4
            self.v.append((*a,*n,u,v,1,0,0,1))
        for j in range(1,len(p)-1):self.i += [base,base+j,base+j+1]
    def box(self,c,size,mat):
        c=np.array(c);h=np.array(size)*.5
        for axis in range(3):
            for side in (-1,1):
                a=(axis+1)%3;b=(axis+2)%3
                pts=[]
                for u,v in [(-1,-1),(1,-1),(1,1),(-1,1)]:
                    p=c.copy().astype(float);p[axis]+=side*h[axis];p[a]+=u*h[a];p[b]+=v*h[b];pts.append(p)
                # Divide broad surfaces to keep brick/metal texel density sensible.
                nu=max(1,math.ceil(size[a]/3));nv=max(1,math.ceil(size[b]/3))
                for i in range(nu):
                    for j in range(nv):
                        p0,p1,p2,p3=pts
                        q=lambda u,v:p0+(p1-p0)*u+(p3-p0)*v
                        self.face([q(i/nu,j/nv),q((i+1)/nu,j/nv),q((i+1)/nu,(j+1)/nv),q(i/nu,(j+1)/nv)],mat,reverse=side<0)
    def beam(self,a,b,r,mat,sides=6,r2=None):
        a=np.array(a,dtype=float);b=np.array(b,dtype=float);d=b-a;d/=np.linalg.norm(d)
        ref=np.array([0.,1.,0.]) if abs(d[1])<.95 else np.array([1.,0.,0.])
        u=np.cross(d,ref);u/=np.linalg.norm(u);v=np.cross(d,u)
        lower=[a+r*(u*math.cos(t*2*math.pi/sides)+v*math.sin(t*2*math.pi/sides)) for t in range(sides)]
        upper=[b+(r if r2 is None else r2)*(u*math.cos(t*2*math.pi/sides)+v*math.sin(t*2*math.pi/sides)) for t in range(sides)]
        for j in range(sides):
            k=(j+1)%sides;self.face([lower[j],lower[k],upper[k],upper[j]],mat)
            self.face([a,lower[k],lower[j]],mat);self.face([b,upper[j],upper[k]],mat)
    def ring(self,c,r,y,thick,mat,n=20):
        for j in range(n):
            a=j*2*math.pi/n;b=(j+1)*2*math.pi/n
            self.beam((c[0]+r*math.cos(a),y,c[1]+r*math.sin(a)),(c[0]+r*math.cos(b),y,c[1]+r*math.sin(b)),thick,mat,5)
    def save(self):
        if not self.i:return
        name=b'town\0';p=OUT/(self.name+'.emesh')
        with p.open('wb') as f:
            f.write(struct.pack('<8I',0x48534d45,2,0,len(self.v),len(self.i),1,len(name),0))
            for v in self.v:f.write(struct.pack('<12f',*v))
            f.write(struct.pack('<%dI'%len(self.i),*self.i));f.write(struct.pack('<4I',0,len(self.i),0,0));f.write(name)


parts=[];boxes=[];grounds=[];lights=[];covers=[]
def model(name,glow=0):
    m=Mesh(name);parts.append((m,glow));return m
def solid(m,c,size,mat):
    m.box(c,size,mat);boxes.append([*c,*(np.array(size)*.5)])
def floor(m,c,size,mat):
    m.box(c,size,mat);grounds.append([c[0],c[1]+size[1]*.5,c[2],size[0]*.5,size[2]*.5])


def water_tower():
    m=model('water-tower');x,z=-102,45
    pos=lambda dx,y,dz:(x+dx,y,z+dz)
    for sx in (-1,1):
        for sz in (-1,1):
            solid(m,pos(3.4*sx,.35,3.4*sz),(1.15,.7,1.15),'stone')
            m.beam(pos(3.4*sx,.7,3.4*sz),pos(2.2*sx,17,2.2*sz),.19,'steel',6)
    for axis in (0,1):
        for side in (-1,1):
            for lo,hi in [(1.,8.6),(8.6,17.)]:
                def at(u,y):
                    h=3.4-1.2*(y-.7)/16.3
                    return pos(u*h,y,side*h) if axis==0 else pos(side*h,y,u*h)
                for a,b in [(-1,1),(1,-1)]:m.beam(at(a,lo),at(b,hi),.06,'steel',5)
                m.beam(at(-1,lo),at(1,lo),.10,'rust',5)
    m.beam(pos(0,16.8,0),pos(0,17.6,0),2.2,'steel',20,r2=3.5)
    m.beam(pos(0,17.6,0),pos(0,23.0,0),3.5,'steel',20)
    m.beam(pos(0,23,0),pos(0,25,0),3.72,'slate',20,r2=.15)
    m.beam(pos(0,25,0),pos(0,25.4,0),.22,'steel',8)
    for y in (17.5,20.4,23):m.ring((x,z),3.54,y,.055,'rust')
    m.ring((x,z),3.92,17.25,.13,'steel');m.ring((x,z),3.92,18.3,.045,'steel')
    m.ring((x,z),3.92,17.8,.035,'steel')
    for j in range(20):
        a=j*2*math.pi/20;dx=3.92*math.cos(a);dz=3.92*math.sin(a)
        m.beam(pos(dx,17.2,dz),pos(dx,18.3,dz),.035,'steel',5)
    for dx in (-.3,.3):m.beam(pos(dx,.5,3.85),pos(dx,24,3.85),.04,'steel',5)
    for y in np.arange(.65,24,.34):m.beam(pos(-.3,y,3.85),pos(.3,y,3.85),.03,'steel',5)
    m.beam(pos(2.65,.1,-.6),pos(2.65,20.8,-.6),.16,'rust',8)
    floor(m,pos(0,.025,0),(11,.05,11),'concrete')


def church():
    m=model('church');g=model('church-glass',.55);x,z=-42,-43
    def box(dx,y,dz,size,mat='brick',collision=True):
        (solid if collision else lambda a,b,c,d:a.box(b,c,d))(m,(x+dx,y,z+dz),size,mat)
    floor(m,(x,.20,z),(13,.40,23),'stone')
    # Real open centre doorway, wall thickness shared with collision.
    for sx in (-1,1):box(sx*6.35,3.85,0,(.3,7.3,23))
    box(0,3.85,-11.35,(13,7.3,.3))
    for sx in (-1,1):box(sx*3.85,3.85,11.35,(5.3,7.3,.3))
    box(0,5.45,11.35,(2.4,4.1,.3))
    # Gable ends and two separate slate planes.
    for dz,rev in [(-11.5,False),(11.5,True)]:
        m.face([(x-6.5,7.5,z+dz),(x,11.2,z+dz),(x+6.5,7.5,z+dz)],'brick',reverse=rev)
        inner_z=z+dz-math.copysign(.03,dz)
        m.face([(x-6.5,7.5,inner_z),(x,11.2,inner_z),(x+6.5,7.5,inner_z)],'brick',reverse=not rev)
    m.face([(x-6.9,7.45,z+12),(x,11.45,z+12),(x,11.45,z-12),(x-6.9,7.45,z-12)],'slate')
    m.face([(x,11.45,z+12),(x+6.9,7.45,z+12),(x+6.9,7.45,z-12),(x,11.45,z-12)],'slate')
    # Inward faces give the nave an actual ceiling when backface culling is on.
    m.face([(x-6.5,7.4,z+11.5),(x,11.2,z+11.5),(x,11.2,z-11.5),(x-6.5,7.4,z-11.5)],'wood',reverse=True)
    m.face([(x,11.2,z+11.5),(x+6.5,7.4,z+11.5),(x+6.5,7.4,z-11.5),(x,11.2,z-11.5)],'wood',reverse=True)
    for dz in (-8,-3,2,7):m.box((x,7.2,z+dz),(12.7,.3,.28),'wood')
    covers.append([x,7.6,z,6.9,.2,12])
    # Bell tower, open porch underneath, capped by a four-sided spire.
    for sx in (-1,1):box(sx*1.55,3.6,12.2,(.7,6.4,2.3))
    box(0,11.2,12.2,(3.8,9.6,3.8))
    for y in (6.65,11.6,16):box(0,y,12.2,(4.15,.3,4.15),'stone',False)
    for a in range(4):
        t=a*math.pi/2;dx,dz=1.94*math.sin(t),1.94*math.cos(t)
        if abs(dx)<.1:
            box(0,14,12.2+dz,(1.35,2.7,.08),'black',False)
            for yy in np.arange(12.85,15.2,.3):box(0,yy,12.2+dz*1.01,(1.35,.10,.12),'wood',False)
        else:
            box(dx,14,12.2,(.08,2.7,1.35),'black',False)
            for yy in np.arange(12.85,15.2,.3):box(dx*1.01,yy,12.2,(.12,.10,1.35),'wood',False)
    base=[(x-1.96,16.15,z+10.24),(x+1.96,16.15,z+10.24),(x+1.96,16.15,z+14.16),(x-1.96,16.15,z+14.16)]
    for i in range(4):m.face([base[i],(x,25.7,z+12.2),base[(i+1)%4]],'slate')
    m.box((x,26.15,z+12.2),(.14,1.2,.14),'wood');m.box((x,26.4,z+12.2),(.7,.13,.14),'wood')
    def lancet(cx,cz,side=False):
        outline=[(-.7,1.9),(.7,1.9),(.7,4.4),(0,5.25),(-.7,4.4)]
        points=[(cx,yy,cz+u) if side else (cx+u,yy,cz) for u,yy in outline]
        g.face(points,'amber',uv=[(0,0),(1,0),(1,.8),(.5,1),(0,.8)])
        g.face(points[::-1],'amber',uv=[(0,.8),(.5,1),(1,.8),(1,0),(0,0)])
        for j in range(len(points)):m.beam(points[j],points[(j+1)%len(points)],.11,'stone',5)
    for sx in (-1,1):
        for dz in (-8.3,-3.1,2.1,7.3):
            lancet(x+sx*6.54,z+dz,True)
            lancet(x+sx*6.18,z+dz,True)
            box(sx*6.7,3.5,dz-2.25,(.55,6.3,.6),'brick')
            box(sx*6.7,6.65,dz-2.25,(.68,.25,.75),'stone',False)
        lancet(x+sx*4.0,z+11.54)
        # Open wood door leaves sit against porch sides.
        box(sx*1.22,1.85,12.6,(.10,2.8,1.5),'wood')
    for i in range(3):
        y=.07*(i+1);floor(m,(x,y,z+15.4-.5*i),(3.1,.14*(i+1),1.0),'stone')
    floor(m,(x,.20,z+13.0),(3.1,.40,4.0),'stone')
    floor(m,(x,.055,z+24),(3,.11,16),'concrete')
    for dz in (-6,-3,0,3,6):
        for sx in (-1,1):
            box(sx*3.45,.85,dz,(3.1,.20,.6),'wood')
            box(sx*3.45,1.2,dz-.32,(3.1,.85,.13),'wood')
    box(0,.85,-8.9,(3,1.0,1.2),'stone')
    box(0,4,-11.13,(.22,2.6,.16),'wood',False)
    box(0,4.6,-11.13,(1.5,.2,.16),'wood',False)
    lights.extend([(x,5.5,z-4,12,2.8),(x,5.5,z+5,12,2.8),(x,3.6,z+14.1,8,2.2)])


def pole_and_lines():
    m=model('utility-poles');w=model('overhead-wires')
    zs=-10.8;xs=list(range(-120,151,30))
    for i,x in enumerate(xs):
        solid(m,(x,5,zs),(.34,10,.34),'wood')
        m.box((x,9.6,zs),(.22,.22,2.8),'wood')
        for dz in (-1.05,0,1.05):
            m.beam((x,9.7,zs+dz),(x,10.05,zs+dz),.105,'cream',6)
            for y in (9.78,9.95):m.beam((x,y-.03,zs+dz),(x,y+.03,zs+dz),.16,'cream',6)
        for s in (-1,1):m.beam((x,8.75,zs),(x,9.5,zs+s*1.15),.05,'wood',5)
        if i%3==0:
            m.beam((x+.4,7.3,zs),(x+.4,8.3,zs),.30,'steel',8)
            for y in (7.25,8.35):m.beam((x+.4,y-.04,zs),(x+.4,y+.04,zs),.33,'cream',8)
        if i==len(xs)-1:continue
        for dz,y0,sag in [(-1.05,10.05,.58),(0,10.05,.6),(1.05,10.05,.58),(0,8.15,.75)]:
            for j in range(16):
                at=lambda t:(x+30*t,y0-sag*4*t*(1-t),zs+dz)
                w.beam(at(j/16),at((j+1)/16),.024,'black',4)


def gas_station():
    m=model('gas-station');g=model('gas-station-lights',1.65)
    x,z=90,-54
    floor(m,(x,.055,-29),(38,.11,28),'concrete')
    floor(m,(x,.15,z),(28,.3,12),'concrete')
    solid(m,(x,2.35,z-5.85),(28,4.4,.3),'brick')
    for s in (-1,1):solid(m,(x+s*13.85,2.35,z),(.3,4.4,12),'brick')
    # Storefront display windows either side of a 2m clear entrance.
    for sx in (-1,1):
        solid(m,(x+sx*7.5,.55,z+5.85),(12.8,.8,.3),'brick')
        m.box((x+sx*7.5,2.1,z+6.03),(12.5,2.2,.08),'glass')
        solid(m,(x+sx*1.15,2.0,z+5.85),(.3,3.4,.3),'cream')
        for xx in (4,8,12):m.box((x+sx*xx,2.1,z+6.12),(.12,2.2,.1),'cream')
    solid(m,(x,4.05,z+5.85),(28,.95,.4),'cream')
    m.box((x,3.55,z+6.1),(28,.30,.10),'red')
    floor(m,(x,4.65,z),(29,.28,13),'slate')
    # Canopy and two period pump islands.
    for sx in (-1,1):solid(m,(x+sx*10,2.75,-26),(.38,5.5,.38),'steel')
    m.box((x,5.6,-26),(25,.5,13),'cream');m.box((x,5.62,-19.42),(25,.30,.12),'red')
    m.box((x,5.85,-19.33),(9,.85,.12),'sign')
    covers.extend([[x,5.6,-26,12.5,.25,6.5],[x,4.65,z,14.5,.14,6.5]])
    for sx in (-1,1):
        px=x+sx*7.4
        floor(m,(px,.12,-25.5),(2.4,.24,5),'concrete')
        solid(m,(px,.95,-25.5),(.85,1.65,.72),'cream')
        m.box((px,.68,-25.1),(.82,.85,.07),'red')
        m.box((px,1.45,-25.1),(.66,.40,.07),'black')
        m.box((px,1.48,-25.04),(.40,.16,.035),'cream')
        for j in range(14):
            def at(t):return (px+.55+.23*math.sin(t*math.pi),1.6-1.28*math.sin(t*math.pi),-25.5+.28*t)
            m.beam(at(j/14),at((j+1)/14),.032,'black',5)
        g.box((px,5.33,-26),(1.45,.06,.7),'lamp');lights.append((px,5.15,-26,15,3.0))
    for xx in (x-7,x+7):
        g.box((xx,4.32,z),(2,.06,.6),'lamp');lights.append((xx,4.12,z,10,2.6))
    solid(m,(x+8,.75,z+3.9),(7,1.25,.8),'cream')
    for zz in (z-2,z-4):solid(m,(x-7,.8,zz),(6,1.4,.6),'wood')
    # Narrow concrete path connects canopy and the supported store threshold.
    floor(m,(x,.045,-41),(5,.09,15),'concrete')


def town_details():
    m=model('shop-row');glow=model('shop-windows',.9)
    for k,(x,width,high) in enumerate([(-6,19,7.5),(15,19,8.2),(36,19,6.9)]):
        z=34;mat='brick' if k!=1 else 'darkbrick'
        solid(m,(x,high/2,z),(width,high,19),mat)
        m.box((x,high+.13,z),(width+.6,.28,19.6),'stone')
        covers.append([x,high+.13,z,(width+.6)/2,.14,9.8])
        for xx in (-5.7,0,5.7):
            m.box((x+xx,2.0,z-9.58),(3.8,2.5,.10),'glass')
            for y in (1.0,3.4):m.box((x+xx,y,z-9.68),(4.0,.14,.1),'cream')
            glow.box((x+xx,5.3,z-9.61),(1.6,1.8,.08),'amber')
            m.box((x+xx,6.3,z-9.68),(2,.2,.2),'stone')
        m.box((x,3.9,z-9.7),(width,.45,.35),'red' if k==0 else 'cream')
        floor(m,(x,.065,z-13),(width+1,.13,6.5),'concrete')
    street=model('street-furniture');lens=model('street-lamp-lenses',2.3)
    for x in range(-120,151,30):
        z=10.2
        solid(street,(x,.35,z),(.7,.7,.7),'concrete')
        street.beam((x,.7,z),(x,7.7,z),.105,'steel',8)
        street.beam((x,7.7,z),(x,7.7,z-1.4),.07,'steel',6)
        street.box((x,7.6,z-1.6),(.52,.2,.85),'steel')
        lens.box((x,7.48,z-1.6),(.42,.045,.7),'lamp')
        lights.append((x,7.3,z-1.6,19,3.2))
    for x,z in [(-64,-17),(-17,-20),(57,19),(77,-40)]:
        solid(street,(x,.65,z),(1.8,.20,.55),'wood')
        solid(street,(x,1.,z+.25),(1.8,.70,.1),'wood')
        for sx in (-.65,.65):street.box((x+sx,.32,z),(.1,.64,.4),'steel')
    # Bare deciduous trees frame the street without hiding landmark silhouettes.
    trees=model('autumn-trees')
    for k,(x,z) in enumerate([(-77,-45),(-9,-40),(40,-42),(135,-53),(-125,60),(77,61)]):
        solid(trees,(x,1.8,z),(.45,3.6,.45),'wood')
        trees.beam((x,3.2,z),(x+.3,8.5,z-.2),.20,'wood',6,r2=.045)
        for i in range(8):
            a=i*2.399+k;h=3.5+i*.45;length=3.4-i*.20
            tip=(x+math.cos(a)*length,h+1.7,z+math.sin(a)*length)
            trees.beam((x,h,z),tip,.09,'wood',5,r2=.018)
            for s in (-1,1):trees.beam(tip,(tip[0]+s*.7,tip[1]+.9,tip[2]+.25*s),.025,'wood',4,r2=.006)


def main():
    atlas();water_tower();church();pole_and_lines();gas_station();town_details()
    for m,_ in parts:m.save()
    (OUT/'parts.txt').write_text(''.join(f'{m.name}.emesh {glow}\n' for m,glow in parts))
    for name,rows in [('collision',boxes),('grounds',grounds),('lights',lights),('covers',covers)]:
        (OUT/(name+'.txt')).write_text(''.join(' '.join(f'{float(v):.6f}' for v in row)+'\n' for row in rows))
    report={'origin':[480,11,-700],'parts':{m.name:len(m.i)//3 for m,_ in parts},'collision_boxes':len(boxes),'ground_surfaces':len(grounds),'lights':len(lights),'rain_covers':len(covers)}
    (OUT/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
