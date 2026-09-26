# Probable Cause road surface

`psx-asphalt.png` and `psx-paving.png` are seamless in-game tiles derived from the purchased
[PSX Modular Road Asset Pack by VadaCross](https://vadacross.itch.io/psx-modular-road-asset-pack).
The sources are `Textures/2WayBaseRoad.png` and `Textures/Path.png` in the user's ZIP. Run
`python3 tools/make_psx_road_surface.py /path/to/PSX\ Modular\ Road\ Asset\ Pack.zip`
to rebuild it. The fixed-width road meshes and painted lines are not imported:
Apricot's road ribbons supply the road shape, curb, junctions, collision and
lane paint. That lets this surface cover straight and curved roads alike.

The creator allows use and modification in games, including commercial games,
but does not allow redistribution as a standalone asset pack. These tiles are
included only as part of Probable Cause.

For editable curves in the purchased pack, run Blender with
`tools/extend_psx_road_pack.py` against the original `.blend`. It adds 45 and
90 degree left/right pieces at the pack's 6 m road width and writes a new
`.blend` plus a GLB containing only those four pieces. Each curve stores its
entry and exit centre and heading as custom properties. The original pack
file is left alone. In-game roads remain the authored ribbons in
`src/city/roads.h`; the curve meshes are for modular scene editing.
