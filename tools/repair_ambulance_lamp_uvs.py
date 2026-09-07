#!/usr/bin/env python3
"""Restore the rear atlas footprint cropped out by the legacy ambulance cook.

Only rear-face UVs change. Geometry, front/sides, indices and materials are
untouched. Idempotent; refuses an unrecognised UV range. Original is backed up
under build before applying this mechanical repair to the private cooked mesh.

The model-first Municipal ambulance supersedes this repair. Its generated cook
has dedicated rear-lamp UVs and exits here without touching the asset.
"""
import argparse
import shutil
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    path = ROOT / 'assets/models/vehicles/ambulance/body.emesh'
    data = bytearray(path.read_bytes())
    magic, version, flags, count = struct.unpack_from('<4I', data)
    assert (magic,version,flags) == (0x48534D45,2,0)
    if count != 108:
        print('generated Municipal ambulance already has fitted rear lamp UVs')
        return
    selected = []
    for i in range(count):
        v = struct.unpack_from('<12f',data,32+i*48)
        if v[2] < -9.49 and v[5] < -.9:
            selected.append((i,v))
    assert selected, 'rear cap missing'
    lo = min(v[6] for _,v in selected), min(v[7] for _,v in selected)
    hi = max(v[6] for _,v in selected), max(v[7] for _,v in selected)
    target_lo, target_hi = (.5605,.6172), (.9082,.9609)
    if max(abs(lo[j]-target_lo[j])+abs(hi[j]-target_hi[j]) for j in range(2)) < .0001:
        print('ambulance rear atlas already repaired')
        return
    assert abs(lo[0]-.5872)<.001 and abs(hi[0]-.8746)<.001, 'unrecognised rear UVs'
    assert abs(lo[1]-.7008)<.001 and abs(hi[1]-.9472)<.001, 'unrecognised rear UVs'
    if not args.apply:
        print('rear atlas crops both tail lamps; run with --apply to repair')
        return
    backup = ROOT / 'build/ambulance-before-brake-uv.emesh'
    if not backup.exists():
        shutil.copyfile(path,backup)
    for i,v in selected:
        uv = [target_lo[j]+(v[6+j]-lo[j])/(hi[j]-lo[j])*(target_hi[j]-target_lo[j]) for j in range(2)]
        struct.pack_into('<2f',data,32+i*48+24,*uv)
    path.write_bytes(data)
    print(f'repaired {len(selected)} rear UVs; original: {backup}')


if __name__ == '__main__':
    main()
