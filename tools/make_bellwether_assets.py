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
# Service station detail tiles, painted by service_tiles() after the base set
# so the original sixteen keep their exact noise.
MATS += ['fascia', 'pumpface', 'price', 'shelf', 'cooler', 'storefront',
         'poster', 'lino', 'stain', 'ice', 'yellow']
ROWS = 8


def atlas():
    """Small hand-authored material patterns, baked as a nearest-era atlas."""
    rng = np.random.default_rng(1989)
    img = Image.new('RGB', (1024,256*ROWS))
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
            # One line at the boards' ~10:1 aspect (canopy, shop front).
            def board(im,d,size):
                w,h=size;d.rectangle((0,0,w,h),fill=(173,160,121),outline=(84,51,33),width=2)
                d.text((w/2,h/2+1),'BELLWETHER SERVICE',font=bold(int(h*.62)),fill=(67,40,27),anchor='mm')
            im=painted(9,.86,board)
        # 4px gutters prevent neighbouring materials bleeding through mips.
        img.paste(im,(idx%4*256,idx//4*256))
    for idx,im in enumerate(service_tiles(),start=len(COLORS)):
        img.paste(im,(idx%4*256,idx//4*256))
    TEX.mkdir(parents=True,exist_ok=True);OUT.mkdir(parents=True,exist_ok=True)
    img.save(TEX/'town-atlas.png');shutil.copy2(TEX/'town-atlas.png',OUT/'town-atlas.png')


def bold(size,mono=False):
    names=(['/System/Library/Fonts/Supplemental/Courier New Bold.ttf','DejaVuSansMono-Bold.ttf']
           if mono else ['/System/Library/Fonts/Supplemental/Arial Bold.ttf','DejaVuSans-Bold.ttf'])
    for name in names:
        try:return ImageFont.truetype(name,size)
        except OSError:pass
    return ImageFont.load_default()


def painted(w_m,h_m,draw):
    """Paint at the face's real aspect, then fit the square tile, so lettering
    on a tall pump or a wide price board is not stretched once it is mapped."""
    scale=256/max(w_m,h_m);size=(max(1,round(w_m*scale)),max(1,round(h_m*scale)))
    im=Image.new('RGB',size);draw(im,ImageDraw.Draw(im),size)
    return im.resize((256,256),Image.Resampling.NEAREST)


def noisy(color,seed,amp=12):
    rng=np.random.default_rng(seed)
    tile=np.clip(np.array(color)[None,None,:]+rng.integers(-amp,amp+1,(64,64,1)),0,255).astype('uint8')
    return Image.fromarray(tile).resize((256,256),Image.Resampling.NEAREST)


def stocked(d,rng,box,rows,warm=1.0):
    """Rows of mixed packaging on shelves; used for gondolas, windows, coolers."""
    x0,y0,x1,y1=box;pitch=(y1-y0)/rows
    palette=[(170,42,34),(196,160,52),(52,92,150),(222,214,190),(64,120,70),
             (205,110,40),(120,48,98),(38,40,44),(180,176,160)]
    for r in range(rows):
        top=y0+r*pitch;base=top+pitch
        d.rectangle((x0,base-3,x1,base),fill=(70,72,70))
        x=x0+1
        while x<x1-3:
            w=int(rng.integers(5,15));h=int(pitch*rng.uniform(.45,.85))
            c=tuple(int(min(255,v*warm)) for v in palette[int(rng.integers(0,len(palette)))])
            d.rectangle((x,base-3-h,min(x+w,x1),base-4),fill=c)
            if w>8:d.line((x+2,base-3-h*.6,min(x+w,x1)-2,base-3-h*.6),fill=(235,228,200))
            x+=w+int(rng.integers(0,3))


def service_tiles():
    """Station-only tiles: canopy fascia, pump face, price board, stocked
    shelving, lit coolers and windows, linoleum, oil-stained concrete."""
    rng=np.random.default_rng(1990);tiles=[]
    # Canopy fascia, one tile per ~3 m run: red crown, cream band, red pinstripe.
    im=noisy((174,166,138),7);d=ImageDraw.Draw(im)
    d.rectangle((0,0,256,86),fill=(150,40,32));d.rectangle((0,204,256,222),fill=(150,40,32))
    d.line((0,86,256,86),fill=(92,28,24),width=3);d.line((0,253,256,253),fill=(60,58,52),width=5)
    tiles.append(im)
    # Late-80s dispenser face (0.95 x 1.75 m): brand cap, lit readout window,
    # grade buttons and a red lower door with the grade name.
    def pump(im,d,size):
        w,h=size;d.rectangle((0,0,w,h),fill=(178,170,142))
        d.rectangle((0,0,w,int(h*.11)),fill=(150,40,32))
        d.text((w/2,h*.035),'BELLWETHER',font=bold(max(8,w//9)),fill=(238,226,190),anchor='mt')
        d.rectangle((8,h*.15,w-8,h*.40),fill=(18,20,20),outline=(80,82,78),width=3)
        mono=bold(max(10,w//6),True)
        for text,y,label in [('12.40',.17,'SALE'),(' 11.3',.245,'GAL'),('1.099',.32,'PER')]:
            d.text((w-14,h*y),text,font=mono,fill=(235,150,48),anchor='rt')
            d.text((14,h*y+3),label,font=bold(max(7,w//14)),fill=(150,146,126),anchor='lt')
        for k,c in enumerate([(170,42,34),(52,92,150),(64,120,70)]):
            cx=w*(.22+.28*k);d.rectangle((cx-10,h*.44,cx+10,h*.49),fill=c,outline=(40,40,38),width=2)
        d.rectangle((0,h*.53,w,h),fill=(132,36,30))
        d.rectangle((10,h*.58,w-10,h*.66),fill=(214,204,172))
        d.text((w/2,h*.595),'REGULAR',font=bold(max(8,w//9)),fill=(110,30,26),anchor='mt')
        for y in (.78,.9):d.line((10,h*y,w-10,h*y),fill=(96,28,24),width=2)
    tiles.append(painted(.95,1.75,pump))
    # Roadside price board (3.2 x 2.4 m), dark frame and slide-in numerals.
    def price(im,d,size):
        w,h=size;d.rectangle((0,0,w,h),fill=(60,40,28))
        d.rectangle((8,8,w-8,h-8),fill=(226,218,190))
        d.rectangle((8,8,w-8,h*.22),fill=(150,40,32))
        d.text((w/2,h*.055),'GAS',font=bold(h//8),fill=(240,230,196),anchor='mt')
        for k,(grade,cost) in enumerate([('REGULAR','1.09'),('UNLEADED','1.17'),('SUPER','1.25')]):
            y=h*(.29+.23*k)
            d.text((18,y+8),grade,font=bold(h//15),fill=(40,32,26),anchor='lt')
            d.rectangle((w*.55,y,w-18,y+h*.18),fill=(250,246,232),outline=(40,32,26),width=2)
            d.text((w-38,y+h*.09),cost,font=bold(h//8,True),fill=(20,20,20),anchor='rm')
            d.text((w-22,y+6),'9',font=bold(h//18,True),fill=(20,20,20),anchor='rt')
    tiles.append(painted(3.2,2.4,price))
    # Gondola / wall shelving face (3 x 1.5 m).
    def shelf(im,d,size):
        w,h=size;d.rectangle((0,0,w,h),fill=(196,190,168))
        stocked(d,rng,(2,4,w-2,h-18),4);d.rectangle((0,h-18,w,h),fill=(48,50,48))
        for x in (0,w//2,w-3):d.rectangle((x,0,x+3,h),fill=(150,150,140))
    tiles.append(painted(3,1.5,shelf))
    # Reach-in cooler bank (2.4 x 2.0 m): three lit glass doors.
    def cooler(im,d,size):
        w,h=size;d.rectangle((0,0,w,h),fill=(150,154,150))
        for k in range(3):
            x0=k*w/3+5;x1=(k+1)*w/3-5
            d.rectangle((x0,8,x1,h-14),fill=(214,226,222))
            stocked(d,rng,(x0+3,12,x1-3,h-18),5,1.08)
            d.rectangle((x0,8,x1,h-14),outline=(96,100,98),width=4)
            d.rectangle((x1-10,h*.35,x1-7,h*.6),fill=(200,204,200))
        d.rectangle((0,0,w,7),fill=(150,40,32))
        d.text((w/2,1),'COLD DRINKS',font=bold(7),fill=(240,230,196),anchor='mt')
    tiles.append(painted(2.4,2.0,cooler))
    # Display window seen from the forecourt (2.75 x 2.2 m): a warm lit store
    # behind the glass, with a clean variant and a posted variant.
    def window(posters):
        def paint(im,d,size):
            w,h=size;d.rectangle((0,0,w,h),fill=(226,206,150))
            d.rectangle((0,0,w,h*.1),fill=(248,240,206))
            d.rectangle((0,h*.84,w,h),fill=(170,164,138))
            stocked(d,rng,(4,h*.30,w-4,h*.84),3,.95)
            if posters:
                d.rectangle((w*.08,h*.12,w*.46,h*.52),fill=(176,40,32),outline=(240,230,200),width=3)
                d.text((w*.27,h*.18),'COLD',font=bold(h//9),fill=(250,240,210),anchor='mt')
                d.text((w*.27,h*.32),'POP 99¢',font=bold(h//12),fill=(250,220,90),anchor='mt')
                d.rounded_rectangle((w*.58,h*.16,w*.9,h*.3),radius=6,outline=(255,70,60),width=4)
                d.text((w*.74,h*.23),'OPEN',font=bold(h//13),fill=(255,96,80),anchor='mm')
            d.rectangle((0,0,w-1,h-1),outline=(96,101,84),width=5)
        return paint
    tiles.append(painted(2.75,2.2,window(False)))
    tiles.append(painted(2.75,2.2,window(True)))
    # Worn store linoleum, eight squares per 3 m.
    im=noisy((198,192,168),11,8);d=ImageDraw.Draw(im)
    for i in range(8):
        for j in range(8):
            if (i+j)%2:d.rectangle((i*32,j*32,i*32+31,j*32+31),fill=(118,126,110))
    for _ in range(9):
        x,y=rng.integers(0,240,2);d.line((x,y,x+int(rng.integers(8,30)),y+int(rng.integers(-4,5))),fill=(92,90,80))
    tiles.append(im)
    # Oil stain decal on forecourt concrete: dark centre, feathered out to the
    # concrete tile's own colour so the patch edge does not read.
    base=np.array(noisy((111,110,101),5),dtype=float)
    yy,xx=np.mgrid[0:256,0:256]
    blot=np.zeros((256,256))
    for _ in range(9):
        cx,cy=rng.uniform(60,196,2);r=rng.uniform(18,46);sq=rng.uniform(.6,1.4)
        blot+=np.clip(1-np.hypot(xx-cx,(yy-cy)*sq)/r,0,1)**1.5*rng.uniform(.3,.6)
    # Blocky like the rest of the atlas, but soft-edged: drips, not holes.
    blot=np.asarray(Image.fromarray((np.clip(blot,0,1)*255).astype('uint8'))
                    .resize((32,32),Image.Resampling.BILINEAR)
                    .resize((256,256),Image.Resampling.NEAREST),dtype=float)/255*.55
    tiles.append(Image.fromarray(np.clip(base*(1-blot[...,None])+18*blot[...,None],0,255).astype('uint8')))
    # Party ice chest front.
    im=noisy((212,216,212),13,6);d=ImageDraw.Draw(im)
    d.rectangle((0,0,255,255),outline=(120,126,128),width=10)
    d.text((128,52),'ICE',font=bold(96),fill=(40,92,160),anchor='mt')
    d.text((128,176),'PARTY SIZE',font=bold(26),fill=(40,92,160),anchor='mt')
    tiles.append(im)
    # Safety yellow, scuffed.
    im=noisy((196,158,42),17,10);d=ImageDraw.Draw(im)
    for _ in range(14):
        x,y=rng.integers(0,250,2);d.line((x,y,x+int(rng.integers(3,18)),y+int(rng.integers(-3,4))),fill=(84,74,50),width=2)
    tiles.append(im)
    return tiles


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
            v=1-(tile//4+(0.02+0.96*(1-t[1])))/ROWS
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
    def panel(self,a,b,y0,y1,mat,pieces=1):
        """Upright single-sided quad from a to b (x,z), seen from the side where
        a is on the left. The tile stays upright however the wall is turned."""
        a=np.array(a,dtype=float);b=np.array(b,dtype=float)
        for k in range(pieces):
            p=a+(b-a)*k/pieces;q=a+(b-a)*(k+1)/pieces
            self.face([(p[0],y0,p[1]),(q[0],y0,q[1]),(q[0],y1,q[1]),(p[0],y1,p[1])],mat)
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


def deck(m,x0,z0,x1,z1,y,mat,down=False):
    """Flat single-sided surface, split to ~3 m so tiles keep their density."""
    nx=max(1,math.ceil((x1-x0)/3));nz=max(1,math.ceil((z1-z0)/3))
    for i in range(nx):
        for j in range(nz):
            a=x0+(x1-x0)*i/nx;b=x0+(x1-x0)*(i+1)/nx
            c=z0+(z1-z0)*j/nz;e=z0+(z1-z0)*(j+1)/nz
            m.face([(a,y,e),(b,y,e),(b,y,c),(a,y,c)],mat,reverse=down)


def gas_station():
    m=model('gas-station');g=model('gas-station-lights',1.65)
    signs=model('gas-station-signs',.6);lit=model('gas-station-glass',.55)
    x,z=90,-54
    floor(m,(x,.055,-29),(38,.11,28),'concrete')
    floor(m,(x,.15,z),(28,.3,12),'concrete')
    deck(m,x-13.7,z-5.7,x+13.7,z+5.7,.302,'lino')
    solid(m,(x,2.35,z-5.85),(28,4.4,.3),'brick')
    for s in (-1,1):solid(m,(x+s*13.85,2.35,z),(.3,4.4,12),'brick')
    # Storefront: lit display glass either side of a 2m clear entrance. The
    # glass is solid, so nobody vaults the 0.8m sill into the store.
    for sx in (-1,1):
        solid(m,(x+sx*7.5,.55,z+5.85),(12.8,.8,.3),'brick')
        boxes.append([x+sx*7.5,2.1,z+5.97,6.25,1.1,.05])
        solid(m,(x+sx*1.15,2.0,z+5.85),(.3,3.4,.3),'cream')
        m.box((x+sx*7.5,3.39,z+5.95),(12.5,.38,.2),'cream')
        for k,(a,b) in enumerate([(1.3,4),(4,8),(8,12),(12,13.7)]):
            lo,hi=sorted((x+sx*a,x+sx*b));pieces=max(1,round((hi-lo)/2.2))
            step=(hi-lo)/pieces
            for i in range(pieces):
                # One posted pane each side, on the piece nearest the door.
                posted=k==(1 if sx<0 else 0) and i==(pieces-1 if sx<0 else 0)
                lit.panel((lo+i*step,z+6.06),(lo+(i+1)*step,z+6.06),1.0,3.2,
                          'poster' if posted else 'storefront')
            m.panel((hi,z+5.95),(lo,z+5.95),1.0,3.2,'glass',pieces)
        for xx in (4,8,12):m.box((x+sx*xx,2.1,z+6.12),(.12,2.2,.1),'cream')
    solid(m,(x,4.05,z+5.85),(28,.95,.4),'cream')
    m.box((x,3.55,z+6.1),(28,.30,.10),'red')
    m.box((x,4.08,z+6.12),(7.4,.78,.1),'black')
    signs.panel((x-3.55,z+6.18),(x+3.55,z+6.18),3.74,4.42,'sign')
    floor(m,(x,4.65,z),(29,.28,13),'slate');covers.append([x,4.65,z,14.5,.14,6.5])
    deck(m,x-13.7,z-5.7,x+13.7,z+5.7,4.50,'cream',down=True)
    # Painted interior walls over the brick.
    m.panel((x-13.69,z-5.69),(x+13.69,z-5.69),.3,4.5,'cream',9)
    m.panel((x-13.69,z+5.7),(x-13.69,z-5.7),.3,4.5,'cream',4)
    m.panel((x+13.69,z-5.7),(x+13.69,z+5.7),.3,4.5,'cream',4)
    interior(m,g,lit,x,z)
    canopy(m,g,signs,x)
    for sx in (-1,1):pump_island(m,x+sx*7.4,-25.5)
    price_board(m,signs,x+17,-17.5)
    # Concrete apron along the shop front, with the clutter a 1989 station
    # keeps by its door: party ice, an oil rack and a payphone.
    floor(m,(x,.06,z+7.4),(30,.12,2.8),'concrete')
    solid(m,(x-4.2,.67,z+7.35),(1.7,1.1,.75),'cream')
    m.box((x-4.2,1.25,z+7.35),(1.76,.08,.8),'steel')
    m.panel((x-5.02,z+7.735),(x-3.38,z+7.735),.2,1.16,'ice')
    boxes.append([x+4,.77,z+7.1,.7,.65,.25])
    for sx in (-.65,.65):m.box((x+4+sx,.77,z+7.1),(.05,1.3,.45),'steel')
    for k,y in enumerate((.3,.72,1.14)):
        m.box((x+4,y,z+7.1),(1.35,.04,.45),'steel')
        for i in range(6):
            m.box((x+3.46+i*.215,y+.13,z+7.05),(.15,.22,.1),('red','yellow','cream')[(i+k)%3])
    solid(m,(x+12.8,1.55,z+6.33),(.45,.7,.26),'steel')
    m.box((x+12.8,2.02,z+6.42),(.62,.08,.46),'steel')
    m.box((x+12.62,1.62,z+6.48),(.08,.36,.07),'black')
    m.box((x+12.88,1.66,z+6.47),(.2,.24,.02),'cream')
    # Air and water post at the far corner of the forecourt.
    solid(m,(x-17.5,.76,-41.2),(.36,1.3,.3),'red')
    m.box((x-17.5,1.2,-41.03),(.26,.2,.04),'cream')
    m.ring((x-17.5,-40.9),.2,.62,.035,'black',10)
    # Oil drips where cars stand at the pumps and idle in the lanes.
    for px in (x-7.4,x+7.4):
        for side in (-1,1):deck(m,px+side*2.4-1.1,-27.2,px+side*2.4+1.1,-23.8,.122,'stain')
    for sx,sz in [(x,-31),(x+2.5,-20.5),(x-3,-37.5)]:
        deck(m,sx-1.4,sz-1.1,sx+1.4,sz+1.1,.122,'stain')
    # Narrow concrete path connects canopy and the supported store threshold.
    floor(m,(x,.045,-41),(5,.09,15),'concrete')


def interior(m,g,lit,x,z):
    """Stocked 27 x 11 m sales floor. The centre aisle from the door stays
    clear; every fixture a shopper can bump into is solid."""
    # Reach-in coolers along the back-left wall.
    solid(m,(x-6.9,1.45,z-5.3),(12,2.3,.8),'steel')
    lit.panel((x-12.9,z-4.89),(x-.9,z-4.89),.45,2.45,'cooler',5)
    # Three double-sided gondolas and one wall run of shelving.
    for gz in (-3.15,-.55,2.05):
        solid(m,(x-7.75,1.05,z+gz),(8.5,1.5,.9),'steel')
        m.panel((x-12,z+gz+.455),(x-3.5,z+gz+.455),.32,1.78,'shelf',3)
        m.panel((x-3.5,z+gz-.455),(x-12,z+gz-.455),.32,1.78,'shelf',3)
        for sx,a,b in [(-12.005,-.45,.45),(-3.495,.45,-.45)]:
            m.panel((x+sx,z+gz+a),(x+sx,z+gz+b),.32,1.78,'red')
        m.box((x-7.75,1.84,z+gz),(8.5,.08,.5),'cream')
    solid(m,(x-13.45,1.25,z+.25),(.5,1.9,9.9),'steel')
    m.panel((x-13.19,z+5.2),(x-13.19,z-4.7),.32,2.18,'shelf',3)
    # Sales counter by the door: register, candy rack facing the aisle.
    solid(m,(x+8,.75,z+3.9),(7,1.25,.8),'wood')
    m.box((x+8,1.405,z+3.95),(7.2,.06,1.0),'black')
    m.panel((x+11.4,z+3.49),(x+4.6,z+3.49),.36,1.3,'shelf',2)
    m.box((x+6.5,1.59,z+4.05),(.5,.3,.45),'black')
    m.box((x+6.5,1.8,z+3.95),(.34,.13,.06),'cream')
    m.box((x+9.5,1.9,z+4.1),(1.6,.9,.3),'steel')
    m.panel((x+10.3,z+3.94),(x+8.7,z+3.94),1.5,2.3,'shelf')
    # Coffee counter and an ice-cream chest on the right.
    solid(m,(x+4,.775,z-5.35),(6,.95,.7),'cream')
    m.box((x+4,1.27,z-5.33),(6.1,.04,.76),'black')
    for bx in (2.4,3.3,4.2):
        m.box((x+bx,1.57,z-5.45),(.42,.56,.4),'steel')
        m.box((x+bx,1.38,z-5.25),(.2,.18,.18),'amber')
    solid(m,(x+5,.73,z-1.5),(2.2,.85,.9),'cream')
    m.box((x+5,1.16,z-1.5),(2.0,.03,.75),'glass')
    solid(m,(x+13.475,1.1,z-1.25),(.45,1.6,6.5),'steel')
    m.panel((x+13.24,z-4.5),(x+13.24,z+2.0),.36,1.86,'shelf',2)
    for sx in (-8,0,8):
        for sz in (-2.8,2.8):g.box((x+sx,4.47,z+sz),(2.4,.05,.5),'lamp')
    for sx in (-8,0,8):lights.append((x+sx,4.12,z,10,2.4))


def canopy(m,g,signs,x):
    cz=-26
    for sx in (-1,1):
        solid(m,(x+sx*10,2.65,cz),(.6,5.3,.6),'cream')
        m.box((x+sx*10,.45,cz),(.66,.9,.66),'yellow')
    m.box((x,5.6,cz),(25,.5,13),'cream')
    # Deep fascia on all four sides, like the reference: red crown, cream band.
    for sz in (-1,1):
        m.box((x,5.75,cz+sz*6.55),(25.2,1.0,.1),'cream')
        a,b=(x-12.6,x+12.6) if sz>0 else (x+12.6,x-12.6)
        m.panel((a,cz+sz*6.605),(b,cz+sz*6.605),5.25,6.25,'fascia',8)
    for sx in (-1,1):
        m.box((x+sx*12.55,5.75,cz),(.1,1.0,13),'cream')
        a,b=(cz+6.6,cz-6.6) if sx>0 else (cz-6.6,cz+6.6)
        m.panel((x+sx*12.605,a),(x+sx*12.605,b),5.25,6.25,'fascia',4)
    # Brand board stands proud of the fascia over the street and the shop.
    for sz in (-1,1):
        m.box((x,5.75,cz+sz*6.7),(9.2,1.1,.16),'black')
        a,b=(x-4.4,x+4.4) if sz>0 else (x+4.4,x-4.4)
        signs.panel((a,cz+sz*6.785),(b,cz+sz*6.785),5.32,6.18,'sign')
    covers.append([x,5.6,cz,12.5,.25,6.5])
    for px in (x-7.4,x+7.4):
        for dz in (-3.4,0,3.4):g.box((px,5.33,cz+dz),(1.45,.06,.7),'lamp')
        lights.append((px,5.15,cz,15,3.0))
    for dz in (-3.4,3.4):g.box((x,5.33,cz+dz),(1.45,.06,.7),'lamp')
    lights.append((x,5.15,cz,13,2.2))


def pump_island(m,px,pz):
    """Curbed island: one dispenser with a face and hose to each lane, yellow
    bollards at both ends, a bin and a squeegee bucket."""
    floor(m,(px,.12,pz),(2.4,.24,5),'concrete')
    m.box((px,.3,pz),(.72,.12,1.12),'concrete')
    solid(m,(px,1.235,pz),(.62,1.75,1.0),'cream')
    m.box((px,2.18,pz),(.7,.14,1.08),'red')
    for side in (-1,1):
        fx=px+side*.316;a,b=(pz+.5,pz-.5) if side>0 else (pz-.5,pz+.5)
        m.panel((fx,a),(fx,b),.36,2.11,'pumpface')
        m.box((px+side*.36,1.18,pz+.3),(.08,.26,.12),'black')
        start=np.array([px+side*.33,1.95,pz-.36]);end=np.array([px+side*.4,1.1,pz+.3])
        def at(t,start=start,end=end,side=side):
            p=start+(end-start)*t;s=math.sin(t*math.pi)
            return (p[0]+side*.24*s,p[1]-1.0*s,p[2])
        for j in range(14):m.beam(at(j/14),at((j+1)/14),.03,'black',5)
    for dz in (-2.2,2.2):
        boxes.append([px,.74,pz+dz,.11,.5,.11])
        m.beam((px,.24,pz+dz),(px,1.24,pz+dz),.1,'yellow',8)
        m.beam((px,1.24,pz+dz),(px,1.3,pz+dz),.1,'yellow',8,r2=.04)
    solid(m,(px,.69,pz+1.3),(.5,.9,.5),'steel')
    m.box((px,1.17,pz+1.3),(.56,.06,.56),'black')
    m.beam((px,.24,pz-1.3),(px,.62,pz-1.3),.16,'black',8)
    m.beam((px,.62,pz-1.3),(px,1.25,pz-1.3),.015,'steel',4)


def price_board(m,signs,x,z):
    """Roadside price board, turned to face drivers on Main Street."""
    solid(m,(x,.25,z),(1.1,.5,3.8),'brick')
    m.box((x,.51,z),(1.0,.04,3.7),'grass')
    for dz in (-1.3,1.3):
        boxes.append([x,3.4,z+dz,.12,3.1,.12])
        m.beam((x,.5,z+dz),(x,6.3,z+dz),.12,'steel',8)
    m.box((x,4.9,z),(.45,2.6,3.4),'black')
    m.box((x,6.29,z),(.5,.18,3.5),'red')
    signs.panel((x+.23,z+1.62),(x+.23,z-1.62),3.68,6.12,'price')
    signs.panel((x-.23,z-1.62),(x-.23,z+1.62),3.68,6.12,'price')


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
