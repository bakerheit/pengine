#!/usr/bin/env python3
"""Rebuild the editable Cinder GT scene, body export, checks and design sheet."""
import argparse
import json
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/orison-cinder'
BLENDER='/Applications/Blender.app/Contents/MacOS/Blender'


def design_sheet():
    sheet=Image.new('RGB',(1920,1510),(18,27,30))
    d=ImageDraw.Draw(sheet)
    font_path='/System/Library/Fonts/Supplemental/Arial.ttf'
    title=ImageFont.truetype(font_path,44)
    label=ImageFont.truetype(font_path,22)
    small=ImageFont.truetype(font_path,18)
    d.rectangle((32,31,39,79),fill=(224,151,66))
    d.text((57,29),'ORISON  /  CINDER GT',font=title,fill=(222,231,224))
    d.text((1480,47),'1994  /  SPORTS COUPE',font=label,fill=(153,178,180))
    for i,(view,caption) in enumerate([
        ('front','01  /  FRONT THREE-QUARTER'),('rear','02  /  REAR THREE-QUARTER'),
        ('side','03  /  SIDE PROFILE'),('above','04  /  HOOD AND CANOPY'),
    ]):
        x=32+(i%2)*940;y=114+(i//2)*660
        im=Image.open(OUT/f'orison_cinder_gt_1994-{view}.png').convert('RGB')
        im=im.resize((916,636),Image.Resampling.LANCZOS)
        sheet.paste(im,(x,y))
        d.rectangle((x,y+596,x+916,y+636),fill=(25,37,41))
        d.text((x+16,y+606),caption,font=small,fill=(183,203,202))
    report=json.loads((ROOT/'build/orison-cinder-fit-report.json').read_text())
    tris=report['triangles']
    d.text((33,1458),f'Original fictional design  /  {tris} body triangles  /  256 x 256 atlas  /  Editable Blender source',font=small,fill=(154,178,177))
    sheet.save(OUT/'orison-cinder-design-sheet.png')


def main():
    p=argparse.ArgumentParser();p.add_argument('--sheet-only',action='store_true');args=p.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    if not args.sheet_only:
        subprocess.run([sys.executable,str(ROOT/'tools/make_orison_cinder_texture.py'),'--guide'],check=True,cwd=ROOT)
        subprocess.run([BLENDER,'--background','--factory-startup','--python',str(ROOT/'tools/orison_cinder_blender.py')],check=True,cwd=ROOT)
        subprocess.run([sys.executable,str(ROOT/'tools/validate_orison_cinder.py')],check=True,cwd=ROOT)
    design_sheet()
    print(OUT/'orison_cinder_gt_1994.blend')
    print(OUT/'orison-cinder-design-sheet.png')


if __name__=='__main__':main()
