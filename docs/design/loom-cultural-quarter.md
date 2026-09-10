# Loom Way museum and garden

Loom Museum / Pinatty Museum is a two-story general museum in the northeast
block of the southwest Pinatty crescent. Its public entrance faces Loom Way.
Sable Garden remains diagonally across Loom, with its gazebo and paths.

| Site | World origin | Basis yaw | Lot | Ground |
| --- | --- | --- | --- | --- |
| Pinatty Museum | `(-286, 270)` | -5.906 degrees | 60 × 88 m | 13 m |
| Sable Garden | `(-365, 329)` | 8.596 degrees | 30 × 22 m | 13 m |

## Cincinnati reference

The design draws on the Cincinnati Art Museum at **953 Eden Park Drive,
Cincinnati, OH 45202**, while keeping original Loom branding and fitting the
existing Pinatty block. Research checked the museum's own architectural history
and a contemporary exterior photograph:

- [Museum history](https://www.cincinnatiartmuseum.org/about/museum-history/):
  Daniel Burnham's 1907 Schmidlapp Wing introduced the Doric facade that later
  became the main entrance. This informed the four-column portico, broad
  entablature and triangular pediment.
- [Schmidlapp Wing archive article](https://www.cincinnatiartmuseum.org/about/blog/library-post-1072017/):
  the wing developed into exhibition space for the sculpture collection.
- [Romanesque Revival architecture](https://www.cincinnatiartmuseum.org/about/blog/roman-what-romanesque-revival-part-1-cincinnati-art-museum-1886/):
  James W. McLaughlin's original buildings used rough stone and rounded arches.
  The stone construction informed Loom's masonry treatment.
- [Contemporary facade photograph](https://www.everythingcincy.com/listings/cincinnati-art-museum-museums-cincinnati-oh):
  used to inspect the column spacing, capitals, cornice and entrance stair.

These are architectural references, not a claim that Loom reproduces the
museum's real plan or collection. No reference photograph is shipped as a
runtime texture.

## Building and circulation

The 54 × 72 m building has a 3,888 m² ground floor and 3,456 m² of upper
floor and balcony area. It retains the four fluted Doric columns, broad
entrance steps, layered stone facade and roughly 15.5 m high pediment.
Raised **LOOM MUSEUM** lettering and the **Pinatty Museum** map marker identify
its general collection. Both rows of exterior windows are real openings.

| Floor | Gallery | Displays |
| --- | --- | --- |
| Ground | Art | Eight framed canvases on the wall and a double-sided hanging spine |
| Ground | Antiquities | Amphorae in tall cases, an offering group, a votive stele |
| Ground | Natural History | Mounted sauropod, a bedding-plane block, wall cases |
| Ground | Pinatty History | Tabletop city model under glass, survey table |
| Upper | Science | Seconds pendulum with a floor dial, a cut gear train |
| Upper | Space | Sounding rocket with a gantry, the solar sequence |
| Upper | Transport | 0-6-0 locomotive and tender on ballasted track |
| Upper | Design | Two chairs, a glass service, a lamp, a study wall |

The ground floor is at world Y 13.8 m and the upper floor at 19.8 m. A five-metre
wide stair rises north through forty 15 cm risers over a 20 m run. The upper
floor leaves the stairwell open. Separate side walks bypass it, while two-metre
balcony walks serve the front rooms around the open entrance hall. Solid
parapets protect the balcony and stairwell. The front entrance stays on Loom
Way, with reception and the armillary in the double-height hall.

## Interior finish

The interior lives in [`src/city/loom_museum_interior.h`](../../src/city/loom_museum_interior.h),
split out from `loom_cultural.h` because dressing rooms is a different job from
the shell. Two conventions run through it.

**Walls are lined, not retextured.** One stone run is both the Loom Way facade
and a gallery's inside face, so a plaster material on the wall itself would put
plaster on the street. Each room gets a skirting, a plaster field split around
whatever the wall baker cut, a picture rail and a cornice, applied to the inside
face. The lining is not solid: it stands 8 cm proud of a wall the character
already stops 30 cm short of, so it adds no collision boxes. The hall runs both
storeys as one face, broken by a moulding under the balcony slab and a skirting
on top of it — both clear of the slab, because a band buried inside it only
z-fights. Galleries get an oak parquet plane 2 cm over the slab; at 4 mm the two
fell inside the depth buffer's resolution and swapped in hard-edged wedges right
across the room.

**Graphics are atlas cells named by index.** The sim side places
`museum panel 3` and never learns a texture exists. Sheets and cell order are in
[asset provenance](../assets/loom-museum-interior.md). Eight portal plaques, one
large wall graphic per gallery, 24 object labels and a lobby directory replaced
2,239 raised-letter parts that spelled the gallery names in a pixel font. The
building bakes 2,486 parts now against 3,408 before, so the whole interior
rework came in cheaper than the signage it removed. The 160 remaining letters
are the **LOOM MUSEUM** inscription on the pediment, where raised metal capitals
are the right answer.

Each gallery takes its own wall colour from one near-white plaster sheet.

## Lighting

Two rows of three pendants per gallery, 5.4 m either side of the centre line,
plus one cove uplight; eight picture lights over the art; and in the hall, four
pendants under the balcony soffits, eight wall sconces through the atrium, four
stair newel lamps and a chandelier ring. 84 sources in total, up from 33.

The rows are off-centre on purpose. The tiled light grid's cone has a **hard**
outer edge — no inner falloff — so a fitting on the centre line of a 19 m room
cuts a visible straight line across the floor where its cone ends and leaves the
walls unlit entirely. Two rows 4.2 m off each wall put every surface inside a
cone. Six narrow cones also cost the grid *less* than three wide ones: its span
radius grows as `range / outer`, so widening a cone is the expensive way to
cover a room and adding fittings is the cheap one.

Range is 9 m. That covers a gallery from the two rows and stops well short of
the 12 m between an upper fitting and the ground floor: the grid has no
occlusion, and that gap is the only thing keeping the upstairs fittings from
lighting the room below straight through the slab. Gallery light is near
neutral; the tungsten default used elsewhere reads as an orange cast over an oak
floor and eight authored wall colours.

`tests/loom_cultural_tests.cpp` caps the source count. The budget is a real
cost, paid every frame the player is anywhere near Loom Way.

## First Street closure and site support

The **First Street segment between Briar Street and Mercer Avenue is removed**.
Its former junctions are now T junctions. Stable road ID **30** remains on First
Street east of Mercer; new ID **210** preserves the short west section from
Sable Crescent to Briar. Both endpoints match the existing junction heights.
The shared road graph, traffic lanes, road ribbons/collision and maps use the
same split, so there is no invisible traffic connection through the museum.
Briar, Mercer, Loom Way and the rest of the crescent retain their routes.

The museum origin remains `(-286,270)`, but its lot center moves to local
`(0,-22)`, expanding north into the reclaimed street. The body spans local
X `[-27,27]`, Z `[-62,10]`; the lot spans X `[-30,30]`, Z `[-66,22]`.
The earthwork bench is 12.6 m, with a raised terrace and retaining edges carrying
the remaining height to the lot paving. This keeps coarse terrain triangles
from introducing a bump under the adjacent Briar/Mercer roads.

Both access records remain pinned to **road 209, Loom Way**. The museum's
five-metre inlet is centered on the entrance axis; the garden's inlet is 3.2 m.
Neither plot uses automatic lot expansion. Access paving and curb cuts use
matching triangle collision. The earthwork also excludes procedural scatter.
The full map and minimap follow the expanded footprint and removed street.

The garden retains its green-roofed timber gazebo, open north/south ends,
side rails, benches, trees and flower beds. The gazebo light runs after dark.

## Validation, 2026-09-08

`tools/ci.sh` green: guard, configure, `-Werror` build and the full `ctest` run.
`loom_cultural_tests` covers, in addition to the existing walks:

- every gallery and the hall carry lined wall panels; all eight plaques, all
  eight room panels, all 24 label cells, the directory and the parquet are
  placed; fixture counts and the light-source budget hold;
- exhibits are sized against a 1.8 m visitor — a wheel-thrown vessel is about a
  metre tall, a driving wheel is round and 0.9–1.6 m, the rocket is at least
  2.5× taller than it is wide, and nothing reaches the roof;
- the six exhibit meshes stay inside the unit volume the placement convention
  assumes and are wound so face and vertex normals agree;
- the ground tour now walks through the art room's hanging spine, whose gap
  lines up with the doorway. If the two drift apart the walk dead-ends.

Circulation was also checked offline against every tour lane before running the
suite, which caught two conflicts the new furniture introduced: a hall bench
level with a portal, and the city model reaching to within three metres of its
doorway. Both are moved.

Visual QA used bounded 200-frame runs at `--daylight --clear` and `--night`,
from inside all eight galleries and the hall; GL queues clean, 8.3–8.7 ms mean
frame time in the galleries. Screenshots are in `build/loom-shots/`.

`--start-player-height Y` supplements `--start-player-at X Z` for upper-floor
visual QA. It requires supporting ground within 0.25 m and a clear character
capsule. Ordinary starts are unchanged. Stair traversal is measured separately
by the character tests; a height-selected screenshot does not prove a live climb.

Note for anyone doing the same QA: the chase camera does not push out of
interior geometry, so a start point within a few metres of a case or a wall puts
the camera inside it and fills the lower screen with a back face. That is the
camera, not the room — pick a start with open floor behind it.

Example upper-floor view:

```sh
./build/bin/apricot --start-at -320 306 --start-player-at -303.3214 272.2295 \
  --start-player-height 19.8 --start-heading -5.9 --road-start --frames 200 \
  --night --clear --save-file /tmp/loom-science.json \
  --screenshot build/loom-shots/science.bmp
```
