# Halberd Gunship sources

The one vehicle in this tree that Apricot did NOT author. Every other model
under `assets/models/vehicles/` is built by a script in `tools/`; this one
arrived finished, as a low-poly PSX-style attack helicopter with its own atlas,
and `tools/cook_psx_helicopter.py` converts rather than models. That difference
is why this file exists.

- Supplied as: `psx_helicopter.zip`, given to the build on 2026-09-12
- Archive SHA-256: `39fd398a568d0359c5a993c77faa7518da09cc7907f9d8771916a35b0712f26c`
- 8 files, authored in Blender 4.0.2 and dated 2025-03-20: `.blend`, `.obj` +
  `.mtl`, `.fbx`, `.glb`, two PNG atlases and a reference collage
- Cooked from the `.obj`. The `.blend`, `.fbx` and `.glb` are not used.

## What ships, and what does not

| Runtime file | From | SHA-256 |
|---|---|---|
| `body.png` | `psx_helicopter_texture.png`, 256x256 RGBA | `9ed58f9e591ae31c57604a5caa848865ace283fde0c2cb63040373700e2f5c44` |
| `rotor.png` | `psx_helicopter_rotors.png`, 128x128 RGBA | `f8cf8b207d4689e04cf2d08cdf58e2d61b172d8102116356ebc840209b987de7` |

Both are copied through unchanged apart from an RGBA convert. The cooked
geometry lands in `assets/models/vehicles/psx_helicopter/`, which is gitignored
along with the rest of that tree — the source package is kept beside it under
`source/`, so the cook is reproducible on any machine that has the zip.

> **LICENCE AND ATTRIBUTION ARE NOT YET RECORDED, AND THAT IS A GAP, NOT AN
> OMISSION.** The archive carries no licence file, no readme and no author
> metadata. The only licence text anywhere in it belongs to an embedded ICC
> colour profile (Elle Stone, CC BY-SA 3.0) that describes the PNG's colour
> space and says nothing about the artwork. Unlike the cooked meshes, these two
> PNGs ARE tracked in git. Before this ships in a build that leaves this
> machine, fill in:
>
> - Creator: **unknown — to be supplied**
> - Source page: **unknown — to be supplied**
> - Licence: **unknown — to be supplied**

## Re-cooking it

```sh
python3 tools/cook_psx_helicopter.py --source ~/Downloads/psx_helicopter.zip
```

The `--source` argument takes the zip, a directory, or the `.obj` itself; with
no argument it reads `assets/models/vehicles/psx_helicopter/source/`. The cook
prints the airframe's bounds and the rotor hub offset, and those numbers are
pinned as constants in `src/city/halberd_helicopter.h` — if the source model
ever changes, the cook's output and that header have to be reconciled, and
`north_airbase_tests` is what notices when the stand no longer fits the field.
