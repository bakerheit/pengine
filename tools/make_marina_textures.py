#!/usr/bin/env python3
"""Small repeatable PSX pixel textures for the Ostend dock, no random noise."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'assets/textures/world/marina'
OUT.mkdir(parents=True,exist_ok=True)

im=Image.new('RGBA',(128,128),(119,99,71,255));d=ImageDraw.Draw(im)
for i in range(8):
    x=i*16;c=[(132,111,79),(110,94,71),(144,122,87),(123,108,85)][i%4]
    d.rectangle((x,0,x+14,127),fill=c+(255,))
    d.line((x,0,x,127),fill=(173,150,109,255))
    d.line((x+15,0,x+15,127),fill=(62,58,47,255))
    for j in range(6):
        y=(i*21+j*23)%128
        d.line((x+3,y,x+3,y+9),fill=(91,79,59,255),width=1)
        d.line((x+8,y+4,x+8,y+15),fill=(153,131,96,255),width=2)
    for y in (5,121):
        d.rectangle((x+4,y,x+5,y+1),fill=(54,59,56,255))
im.save(OUT/'weathered-timber.png')
im=Image.new('RGBA',(128,128),(176,184,177,255));d=ImageDraw.Draw(im)
for x in range(0,128,16):
    d.line((x,0,x,127),fill=(121,139,135,255),width=2)
    d.line((x+2,0,x+2,127),fill=(207,211,195,255),width=2)
    for y in (7,119):d.rectangle((x+7,y,x+8,y+1),fill=(82,103,102,255))
for x,y in ((12,90),(38,33),(82,104),(105,52)):
    d.rectangle((x,y,x+7,y+2),fill=(131,147,135,255))
im.save(OUT/'painted-boards.png')
im=Image.new('RGBA',(128,128),(61,72,75,255));d=ImageDraw.Draw(im)
for x in range(0,128,8):
    d.rectangle((x,0,x+2,127),fill=(89,100,101,255))
    d.line((x+6,0,x+6,127),fill=(40,53,59,255))
for y in (5,122):
    for x in range(3,128,16):d.point((x,y),fill=(143,149,133,255))
im.save(OUT/'corrugated-roof.png')
im=Image.new('RGBA',(512,64),(22,65,66,255));d=ImageDraw.Draw(im)
d.rectangle((1,1,510,62),outline=(197,178,127,255),width=2)
font=ImageFont.load_default(size=31)
text='OSTEND BAIT & TACKLE'
box=d.textbbox((0,0),text,font=font)
d.text(((512-(box[2]-box[0]))/2,6),text,font=font,fill=(242,231,195,255))
font=ImageFont.load_default(size=12)
text='LIVE BAIT   /   TACKLE   /   HARBOUR SUPPLIES'
box=d.textbbox((0,0),text,font=font)
d.text(((512-(box[2]-box[0]))/2,44),text,font=font,fill=(186,207,186,255))
im.save(OUT/'boatworks-sign.png')
for path in OUT.glob('*.png'):
    im=Image.open(path);assert im.mode=='RGBA';print(path.relative_to(ROOT),im.size)
