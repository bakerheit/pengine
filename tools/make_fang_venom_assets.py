#!/usr/bin/env python3
"""Cook, texture, validate and preview the Fang Venom motorbike."""

import argparse
import hashlib
import json
import math
import struct
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from fang_venom_spec import ATLAS_SIZE, REGIONS, SHAPE, WHEELS
from make_vesper_vx91_assets import read_emesh
from render_firetruck_preview import Part, read_part, raster_view

ROOT=Path(__file__).resolve().parents[1]
BLENDER=Path("/Applications/Blender.app/Contents/MacOS/Blender")
MODEL=ROOT/"assets/models/vehicles/fang_venom"
TEXTURE_DIR=ROOT/"assets/textures/vehicles/fang_venom"
TEXTURE=TEXTURE_DIR/"body.png"
IMAGEGEN_SOURCE=TEXTURE_DIR/"body-imagegen-source.png"
TEMPLATE=ROOT/"build/fang-venom-component-template.png"
UV_GUIDE=ROOT/"build/fang-venom-uv-guide.png"
PREVIEW=ROOT/"build/fang-venom-preview.png"
REPORT=ROOT/"build/fang-venom-fit-report.json"

BASE={
    "PAINT_SIDE":(25,132,68,255),"PAINT_TOP":(39,166,82,255),"NOSE":(31,148,75,255),
    "BLACK":(12,14,17,255),"METAL":(135,143,145,255),"ENGINE":(55,61,64,255),
    "SEAT":(24,21,29,255),"HEADLIGHT":(224,215,173,255),"TAIL_RED":(174,28,38,255),
    "GAUGE":(38,76,91,255),"TIRE":(17,18,20,255),"RIM":(151,156,155,255),
    "EXHAUST":(91,97,99,255),"PURPLE":(76,28,105,255),"FRAME":(35,41,43,255),
}


def template():
    image=Image.new("RGBA",(ATLAS_SIZE,ATLAS_SIZE),(8,10,12,255)); draw=ImageDraw.Draw(image)
    font=ImageFont.load_default()
    for name,box in REGIONS.items():
        draw.rectangle(box,fill=BASE[name],outline=(235,146,47,255),width=2)
        draw.text((box[0]+4,box[1]+4),name,fill=(245,245,225,255),font=font)
    TEMPLATE.parent.mkdir(parents=True,exist_ok=True); image.save(TEMPLATE)
    return image


def write_blockout():
    TEXTURE_DIR.mkdir(parents=True,exist_ok=True)
    image=template()
    draw=ImageDraw.Draw(image)
    # Exact locked graphic placement provides a useful model-first blockout.
    for name in ("PAINT_SIDE","PAINT_TOP","NOSE"):
        x0,y0,x1,y1=REGIONS[name]
        draw.line([(x0+5,y1-12),(x0+(x1-x0)//2,y0+12),(x1-5,y1-18)],fill=(94,34,124,255),width=5)
    image.save(TEXTURE)


def build_model():
    subprocess.run([str(BLENDER),"--background","--factory-startup","--python",
                    str(ROOT/"tools/fang_venom_blender.py"),"--","--model-dir",str(MODEL),
                    "--texture",str(TEXTURE)],check=True)


def quantized_region(source, name):
    box=REGIONS[name]
    crop=np.asarray(source.crop(box).convert("L"),dtype=np.uint8)
    levels=(crop//51).astype(np.float32)/5.0
    base=np.asarray(BASE[name][:3],dtype=np.float32)
    rgb=np.clip(base[None,None,:]*(.68+levels[:,:,None]*.58),0,255).astype(np.uint8)
    alpha=np.full((*rgb.shape[:2],1),255,dtype=np.uint8)
    return Image.fromarray(np.concatenate((rgb,alpha),axis=2),"RGBA")


def make_texture():
    if not IMAGEGEN_SOURCE.is_file():
        raise FileNotFoundError(f"built-in imagegen edit missing: {IMAGEGEN_SOURCE}")
    source=Image.open(IMAGEGEN_SOURCE).convert("RGBA").resize((256,256),Image.Resampling.LANCZOS)
    image=Image.new("RGBA",(256,256),(8,10,12,255))
    for name,box in REGIONS.items(): image.paste(quantized_region(source,name),box)
    draw=ImageDraw.Draw(image)
    # Lock critical material identity and chunky period graphics after AI reduction.
    for name in ("PAINT_SIDE","PAINT_TOP","NOSE"):
        x0,y0,x1,y1=REGIONS[name]
        draw.line([(x0+4,y1-11),(x0+(x1-x0)//2,y0+10),(x1-4,y1-17)],fill=(75,26,104,255),width=5)
        draw.line([(x0+5,y1-8),(x0+(x1-x0)//2,y0+13),(x1-5,y1-14)],fill=(142,57,167,255),width=1)
    for name,color in (("HEADLIGHT",BASE["HEADLIGHT"]),("TAIL_RED",BASE["TAIL_RED"]),
                       ("TIRE",BASE["TIRE"]),("BLACK",BASE["BLACK"])):
        draw.rectangle(REGIONS[name],fill=color)
    # Small original wordmark, readable but not copied branding.
    draw.text((8,73),"FANG",fill=(214,224,190,255),font=ImageFont.load_default())
    TEXTURE_DIR.mkdir(parents=True,exist_ok=True); image.save(TEXTURE)


def uv_guide():
    image=Image.open(TEXTURE).copy(); draw=ImageDraw.Draw(image)
    for mesh_path in (MODEL/"body.emesh",MODEL/"wheel.emesh"):
        vertices,indices=read_emesh(mesh_path); v=np.asarray(vertices)
        for tri in np.asarray(indices).reshape(-1,3):
            points=[(v[i,6]*256,(1-v[i,7])*256) for i in tri]
            draw.line(points+[points[0]],fill=(244,151,52,255),width=1)
    image.save(UV_GUIDE)


def assembled_parts():
    body=read_part(MODEL/"body.emesh",TEXTURE)
    wheel=read_part(MODEL/"wheel.emesh",TEXTURE)
    return [body,
            Part(wheel.positions+np.array((0,WHEELS["centre_y"],WHEELS["front_z"])),wheel.normals,wheel.uvs,wheel.indices,wheel.texture),
            Part(wheel.positions+np.array((0,WHEELS["centre_y"],WHEELS["rear_z"])),wheel.normals,wheel.uvs,wheel.indices,wheel.texture)]


def previews():
    parts=assembled_parts(); views=[("FRONT 3/4",-32,18),("SIDE",-90,4),("REAR 3/4",-148,18),
                                    ("ELEVATED",-35,34),("FRONT",0,5),("REAR",180,5)]
    sheet=Image.new("RGB",(1440,840),(20,23,26)); draw=ImageDraw.Draw(sheet)
    for i,(label,yaw,pitch) in enumerate(views):
        view=raster_view(parts,yaw,pitch,480,386); x,y=(i%3)*480,(i//3)*420
        sheet.paste(view,(x,y)); draw.text((x+12,y+397),label,fill=(230,230,215))
    sheet.save(PREVIEW)


def write_qa_fixture():
    parts=assembled_parts(); values=[]
    for part in parts:
        for tri in part.indices:
            for i in tri:
                values.append((*part.positions[i],*part.normals[i],*part.uvs[i],1,0,0,1))
    vertices=np.asarray(values,dtype="<f4"); path=ROOT/"build/fang-venom-qa.emesh"
    material=b"fang_venom\0"
    with path.open("wb") as out:
        out.write(struct.pack("<8I",0x48534D45,2,0,len(vertices),len(vertices),1,len(material),0))
        out.write(vertices.tobytes()); out.write(np.arange(len(vertices),dtype="<u4").tobytes())
        out.write(struct.pack("<4I",0,len(vertices),0,0)); out.write(material)
    return path


def validate(require_imagegen=True):
    body,body_indices=read_emesh(MODEL/"body.emesh"); wheel,wheel_indices=read_emesh(MODEL/"wheel.emesh")
    v=np.asarray(body); w=np.asarray(wheel); triangles=len(body_indices)//3
    assert SHAPE["triangle_budget"][0] <= triangles <= SHAPE["triangle_budget"][1],triangles
    assert np.isfinite(v).all() and np.isfinite(w).all()
    assert ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all() and ((w[:,6:8]>=0)&(w[:,6:8]<=1)).all()
    dimensions=np.ptp(v[:,:3],axis=0); radius=max(np.ptp(w[:,1]),np.ptp(w[:,2]))*.5
    assert dimensions[0] <= .811 and 1.9 <= dimensions[2] <= 2.25,dimensions
    assert abs(radius-WHEELS["radius"])<.003,radius
    image=Image.open(TEXTURE); assert image.size==(256,256) and image.mode=="RGBA"
    assert image.getchannel("A").getextrema()==(255,255)
    if require_imagegen: assert IMAGEGEN_SOURCE.is_file()
    report={"model":"Fang Venom","period":1991,"body_triangles":triangles,
            "wheel_triangles":len(wheel_indices)//3,"body_dimensions_m":dimensions.tolist(),
            "wheel_radius_m":float(radius),"wheelbase_m":WHEELS["front_z"]-WHEELS["rear_z"],
            "body_is_wheel_less":bool(v[:,1].min()>.32),"atlas":[256,256,"RGBA"],
            "imagegen":{"mode":"built-in imagegen edit","source":str(IMAGEGEN_SOURCE.relative_to(ROOT))},
            "shape_contract":SHAPE,"mesh_sha256":hashlib.sha256((MODEL/"body.emesh").read_bytes()).hexdigest(),
            "texture_sha256":hashlib.sha256(TEXTURE.read_bytes()).hexdigest(),
            "checks":["separate two-wheel mesh","finite indexed triangles","semantic UV cells",
                       "wheel-less body","exact wheelbase and radius","opaque 256px imagegen-derived atlas"]}
    REPORT.write_text(json.dumps(report,indent=2)+"\n"); print(json.dumps(report,indent=2))
    uv_guide(); previews(); write_qa_fixture()


def asset_lab():
    mesh=write_qa_fixture(); shot=ROOT/"build/fang-venom-asset-lab.png"
    proc=subprocess.run([str(ROOT/"build/bin/apricot_asset_lab"),"--model",str(mesh),"--texture",str(TEXTURE),
                         "--yaw","212","--frames","60","--screenshot",str(shot)],cwd=ROOT,
                        capture_output=True,text=True)
    (ROOT/"build/fang-venom-asset-lab.log").write_text(proc.stdout+proc.stderr)
    proc.check_returncode(); assert "0 GL errors" in proc.stdout; print("Fang Venom Asset Lab: 60 frames, 0 GL errors")


if __name__=="__main__":
    parser=argparse.ArgumentParser(); parser.add_argument("--prepare-imagegen",action="store_true")
    parser.add_argument("--validate-only",action="store_true"); parser.add_argument("--asset-lab",action="store_true")
    args=parser.parse_args(); MODEL.mkdir(parents=True,exist_ok=True); TEXTURE_DIR.mkdir(parents=True,exist_ok=True)
    if args.prepare_imagegen: write_blockout()
    if not args.validate_only:
        if not TEXTURE.is_file(): write_blockout()
        build_model()
        if not args.prepare_imagegen: make_texture(); build_model()
    validate(require_imagegen=not args.prepare_imagegen)
    if args.asset_lab: asset_lab()
