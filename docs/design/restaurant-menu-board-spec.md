# Restaurant menu-board contract

This is the authored physical-and-raster contract for every Cloggers and
Tacomaco menu receiver. It exists before the next ImageGen pass so no menu art
is stretched, cropped, or reused across incompatible boards.

## Physical receivers

| Receiver | Count per restaurant | Face size | Full diffuse canvas | Safe inset |
| --- | ---: | ---: | ---: | ---: |
| Interior counter board | 2 | 4.80 m x 1.20 m | 2048 x 512 px, landscape 4:1 | 64 px on every edge |
| Drive-through order board | 1 | 2.00 m x 3.00 m | 1024 x 1536 px, portrait 2:3 | 64 px on every edge |

The thin dark backing is deliberately larger than the face by its authored
border. Generated art fills the white face edge to edge; the safe inset is for
all readable type, food crops, price badges, and brand marks.

The geometry lives in `src/city/restaurant_menu_boards.h` and is used by the
shared Cloggers/Tacomaco fixture records in `src/city/start_area.h`. Both
sites inherit exactly these receivers through the shared restaurant plan.

At the customer counter, the earlier menu sequence belongs on the left board
and the later sequence belongs on the right. For Cloggers that is combos 1-4
on the left and 5-7 on the right.

## Required generated deliverables

Do not generate a single image for every receiver. The current one-texture
binding is legacy content and is allowed to stay in place only until this
replacement set is ready.

| Restaurant | Interior board A | Interior board B | Drive-through board |
| --- | --- | --- | --- |
| Cloggers | `menu-board-interior-a-v2-albedo.png` | `menu-board-interior-b-v2-albedo.png` | `menu-board-drive-through-v2-albedo.png` |
| Tacomaco | `menu-board-interior-a-v2-albedo.png` | `menu-board-interior-b-v2-albedo.png` | `menu-board-drive-through-v2-albedo.png` |

Each file is opaque, front-on, full-bleed diffuse/albedo artwork. Keep every
letter and price inside the stated safe inset. Interior A/B may divide a large
menu across the two physical boards; the portrait order board gets a compact,
driver-readable selection rather than a squeezed copy of the whole counter
menu.

## ImageGen handoff

Use a separate image-generation call for each deliverable. The prompt must
state the relevant aspect and canvas, exact restaurant spelling, exact menu
copy, and these constraints: original fictional restaurant only; flat
orthographic albedo; no bezel, pole, building, room, perspective, shadow,
glare, real brand, watermark, or extra text. Preserve the 64 px safe inset.

Before replacing runtime materials, inspect each generated result for exact
text and aspect, copy it as a new versioned project asset, then split material
routing by receiver. Do not overwrite the current menu textures.

## Implemented v2 set

The six v2 files now exist under `assets/textures/world/quickbite/` and
`assets/textures/world/tacomaco/` and are routed to the explicitly named
interior A, interior B, and drive-through faces. The two counter boards are
each 2048 x 512 px; their two matching generated 2:1 panels were assembled
side-by-side at native scale, then uniformly downsampled. The drive-through
boards are direct 1024 x 1536 px outputs. No old menu texture was overwritten.
