# Halberd Field — the north shore air station

Pinatty's second airfield, on the coastal shelf beyond the Kepler Flats
refinery. It is the northernmost thing a player can drive to.

| | |
| --- | --- |
| Site origin | `(-690, -2100)`, basis yaw 0 |
| Plate | 1090 × 240 m, flat to **9 m** at every level of detail |
| Perimeter | x ±530, z −108 … +112, one gate at local x 190 |
| Runway | 09/27, 900 × 45 m, centred on local z −72 |
| Approach | road 244 off the Yard Road (90) at world `(-500, -1930)` |
| Source | [`src/city/north_airbase.h`](../../src/city/north_airbase.h) |
| Terrain | `kHalberdFieldTerrainOp` in [`terrain_ops.h`](../../src/city/terrain_ops.h) |

## Why here

The survey came first. Sampling the north of the island for mean height and
roughness found one large flat dry area outside every district polygon: the
coastal shelf between z −2220 and −1980, west of the Ferrone massif's north
flank and north of Kepler Flats. Everything further north is water; everything
east of local x 0 is the massif.

It is also already served. The Yard Road (id 90) runs along the foot of the
shelf, so the approach is a 52 m stub off an existing junction rather than a new
arterial — which is what makes a single gate credible.

## Three rules the layout follows

**One way in.** The wire is a closed loop and the only opening is the main gate
on the south fence. A gate is only a decision if there is no way round the back,
so the perimeter is authored as four runs and `north_airbase_tests` walks all
four: three must be unbroken, and the fourth may have exactly one hole, no wider
than a gate, centred where road 244 arrives.

**The runway is not a road.** It never appears in `kRoads`. It is paving with
its own ground collision, so the player can drive it and the traffic system can
never route down it. The test asserts no authored road centreline comes within
60 m of the runway centre.

**Military, not civil.** Camber Point is glass, kerbside drop-off and a public
concourse. This is blast walls, three earth-banked aircraft shelters, three
earth-covered magazines, a bunded fuel farm and a hardened tower with a solid
shaft. If the two read the same from the air, the second one was not worth
building.

## The plate

One `Flatten` at 9 m, sharing Kepler Flats' target on purpose: the two
rectangles overlap by 110 m and a different target would put a step across the
south fence. It composes in the site-plate block after the district plates.

Flatness is measured at levels 0–3, not just level 0. A runway that ripples at
level 3 is a runway that ripples the moment you look at it from the Kepler road.
Measured spread over the whole envelope is under 0.02 m at every level.

The op is also in `authored_site_clearance_weight`, which keeps procedural
scatter off the whole earthwork envelope. That is why there is no slab under the
infield: the plate is already flat and already clear, so the terrain's own
surface is the mown grass, and a 1080 × 200 m quad of concrete would only hide
it.

## Paving is layered, and the layer is not decoration

**This is the thing that broke in play and it will break again if the rule is
lost.** An airfield's surfaces genuinely lie on top of one another: a taxiway
runs out across an apron, a revetment's hardstanding is poured on the apron it
opens onto. Authored at one flat height they are coplanar quads fighting for the
depth buffer, and from a car that reads as two road surfaces sliding over each
other as you drive.

So every plane declares a `HalberdLayer` and gets 8 mm of clearance per layer —
under the wheels, invisible; in the depth buffer, decisive:

| Layer | Surfaces |
| --- | --- |
| `Ground` | the big concrete areas: main, east, gate and support aprons |
| `Hard` | poured on them: revetment hardstandings, fuel bund, magazine and barracks walks |
| `Sealed` | asphalt: runway, taxiway, station road, motor pool, parade ground |
| `Paint` | markings |

Planes that share a layer must not overlap, and `north_airbase_tests` checks
that. Like-named parts are exempt: the seven-segment runway numerals share
corners by construction, and identical white on identical white cannot show a
seam.

## And no road runs under the paving

The same failure, one level up. The station shipped with an internal "apron
road" in `kRoads`, and it described the same ground the paving already
described. A road ribbon is **draped and crowned** — it rides up to ~12 cm above
the flat bed it was authored on, measured 8.98–9.18 m over a 9.00 m plate —
while paving is a plane. The two interleaved.

Inside the wire there are now no roads at all, for the same reason the runway is
not one: nothing should route through a military station, so it needs no lanes,
and one description of a piece of ground is the only number that can be right.
Road 244 stops **exactly** where the gate apron begins, at local z 110; they
share an edge and no area.

Paving also sits 13 cm over the plate rather than 6, which is the airport's
`kAirportPavingTopM` convention and exists for this reason: anything lower than
a ribbon's crown does not sit under the road, it fights it.

The test asserts no ribbon vertex lies inside any paving plane. Sharing an edge
is fine and is how the gate works; sharing an area is not.

## Paving and collision

Paving is a **thin plane**, not a slab. A 16 cm box grows lit vertical edges
along every joint and turns an apron into a chequerboard of kerbs.

`halberd_ground_piece()` names the surfaces that carry ground collision, and it
compares names **exactly**. As a prefix, `"halberd runway"` also catches the
centreline, the threshold bars and the edge lights, which would paint forty
overlapping collision planes down the strip.

## Signage

Three sheets, generated by
[`tools/make_halberd_textures.py`](../../tools/make_halberd_textures.py):

* `gate-sign.png` — the board over the gate. The only text on the station a
  player reads at walking pace, so it carries the whole identity.
* `hangar-numbers.png` — a 2 × 2 atlas painted on the hangar door leaves.
* `chain-link.png` — alpha-cut mesh. This one is not decoration: drawn solid,
  2.5 km of perimeter reads as a concrete wall, and a wall is a different
  building.

Runway designators are geometry, not texture: bars on a seven-segment cell,
rotated so the pair at each end is read **from the approach** rather than from
directly above.

## Validation, 2026-09-08

`north_airbase_tests` covers:

- no two structures occupy the same space, and nothing sits outside the wire;
- the perimeter has exactly one opening and it is the main gate;
- no two paving planes share a layer and an area, and no road ribbon runs
  underneath any of them;
- the runway is 900 × 45, carries collision, and is in no road table;
- the plate is flat at levels 0–3 and the north fence stands on dry land;
- **a real `VehicleState` drives off the Yard Road, through the gate, down the
  station road and up between two hangars onto the apron** — 645 m, never more
  than 0 m below the collider;
- the same car drives the full 842 m of runway 09/27.

The last two are the ones that matter. "Gated, but you can drive in" is the
whole design, and a consumer test with a hand-built collider would pass happily
while the runway floated a metre over the plate.

Visual QA used bounded 220-frame runs at `--daylight --clear` and `--night`,
from the Yard Road junction, the gate, the apron, the west threshold and
overhead. GL queues clean, 8.6–8.7 ms mean frame time. Screenshots are in
`build/halberd-shots/`.

Note: `--daylight` before `--night` on the command line wins, so a night shot
needs `--night --clear` with no `--daylight`.

## Not built

Named here so nobody reads the absence as an oversight:

- **No aircraft.** The station has revetments, hangars and a runway and nothing
  parked on any of them. The engine flies aircraft, but the airliner models are
  Camber Point's and would read wrong here.
- **No restricted-area behaviour.** The barrier is up and the guardhouse is
  empty. There is no trespass concept in the game yet, so the gate is a sign and
  a chicane, not a rule.
- **No interior.** Every building is closed. The hangar doors are geometry.
