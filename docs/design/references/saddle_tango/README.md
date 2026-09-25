# 1991 Saddle Tango references

Selected direction: [concept B](concept-selected.png), renamed
**Saddle Tango**. The seven images here are modeling references for that car.

| Feature | Controlling view |
| --- | --- |
| Wheelbase, overhangs, door seams, roof and beltline | [left](left.png), checked against [right](right.png) |
| Grille, rectangular lamps, amber corners and bumper width | [front](front.png) |
| Tail lamps, plate recess and bumper | [rear](rear.png) |
| Hood, roof and deck taper | [top](top.png) |
| Overall front and rear appearance | [front three-quarter](front-three-quarter.png), [rear three-quarter](rear-three-quarter.png) |

The generated views vary slightly in spoiler height, side window shape and rear
lamp segmentation. Use the side views for proportions, the straight front/rear
views for fascia placement, and the selected concept for overall identity.
Keep the spoiler low and the lamps segmented. Treat image measurements as design
estimates, not recovered vehicle dimensions.

## Construction benchmark

The Pizaz Constant source uses one curved contour for its fenders, doors,
rockers, and arch returns; separate pressed door skins with thickness; glass
inside frames; and crowned hood, deck, and roof stampings. Tango now uses the
same construction ideas through `tools/saddle_tango_surface.py` and
`tools/saddle_tango_blender.py`, with its own dimensions and panel layout.

Tango stays a distinct 1991 Saddle: 5.04 m long, 1.47 m high, with a long
falling hood, taller and more formal four-door cabin, long rear deck, paired
rectangular lamps and amber corners, horizontal grille with S badge, two-tone
side molding, small deck lip, and basketweave wheels. Pizaz is a shorter,
lower sport sedan with a five-spoke look and a different window/door outline.

The Apricot runtime body is a joined static mesh; its doors are modeled as
separate solid source components, but they do not open in game yet. The source
and seven fixed inspection renders can be rebuilt with
`python3 tools/make_saddle_tango_assets.py` and
`/Applications/Blender.app/Contents/MacOS/Blender -b --python-exit-code 1 --python tools/render_saddle_tango.py`.

The six window groups are separate from the opaque body and use Apricot's
transparent glass pass. The inward-facing skins of the front and rear doors
have their own upholstery texture, inserts, armrests, latches, and pockets.
The dash, seats, carpet, headliner, rear quarter trim, and inner wheelhouses
also have assigned atlas regions. The inspection renderer includes front and
rear cabin views so these surfaces can be checked from inside as well as
through the windows.
