# Runtime assets

Keep runtime-ready files grouped by kind, then gameplay family:

```
assets/
  audio/vehicles/player/        licensed player-car WAV overrides + sources
  models/vehicles/<model>/       cooked `.emesh` geometry
  models/vehicles/common/        geometry shared by several vehicles
  textures/vehicles/<model>/     paint matching one vehicle's UV layout
  textures/vehicles/common/      shared vehicle paint
  textures/world/gas_station/    seamless authored environment albedo maps
  textures/world/bank/           authored bank identity art
  textures/world/airport/        runway/apron and terminal-facade albedo maps
  textures/world/billboards/     full-colour roadside advertising faces
  shaders/                       GLSL source
```

## Player car audio

`audio/vehicles/player/engine_0.wav` through `engine_5.wav` are retained CC0
racing-car loops. `SOURCES.md` beside them records the author, source URLs,
license, retrieval date, hashes, and former runtime processing. They are not
connected to normal driving; neither are the procedural engine, tyre, surface,
or impact placeholders.

`audio/vehicles/player/auditions/` contains five supplied per-gear acceleration
sounds plus 20 short CC0 real-car recordings for the F1 sound lab. They cover
acceleration, braking, crashes, tyre maneuvers, and road surfaces. They never
fall back to synth audio; `SOURCES.md` records provenance and edits, and
`AUDITIONS_SHA256SUMS` pins the shipped PCM.

`audio/vehicles/player/runtime/` contains the attack, seamless held-throttle,
and release cuts used during normal driving. They come from one real Pixabay
recording, so all three transitions keep the same engine character.

Do not put source `.blend`, `.fbx`, `.obj` or `.mtl` files in this runtime
tree. Apricot currently consumes cooked `.emesh` plus PNG paint directly.

## Imported from the Probable Cause alpha

`models/vehicles/car5/body.emesh`, `car8/body.emesh`, and
`ambulance/body.emesh` are the **wheel-less** legacy body cooks. The player and
traffic visual rigs attach four separate wheel nodes, so a body must never have
baked wheels or the car will double-render them.

`textures/vehicles/car8/ambulance.png` is a fourth Car 8 paint, alongside the
imported `grey`, `purple` and `mail`. Car 8's step-van shell already reads as an
ambulance body, so the paint is all it takes. It is cooked, not imported: run
`python3 tools/make_car8_ambulance_texture.py` from the repository root. The
cook folds two generated panels -- `ambulance-flank-reference.png` and
`ambulance-rear-reference.png`, both edits *of the stock atlas crops* -- back
onto `body.png` as a ratio against the crop each came from, so pixels the edit
left alone survive bit-for-bit and a downsample of painted artwork cannot smear
the one-pixel panel lines a 128x128 atlas is made of. The roof, nose and cab
front carry no generated panel and are lifted to the same white by a tone curve
measured off the flank panel. `docs/assets/car8-ambulance-generated-texture.md`
holds the prompts and the inspection record.

It is selectable as LEGACY / CAR 8 AMBULANCE, from Car 8's own mesh -- a
repaint, not a second vehicle, so the catalog fit numbers must stay equal to Car
8's. Because the two rows share a mesh folder, `--player-car car8` resolves to
plain Car 8 and the ambulance has no `--player-car` spelling; the dev menu is
the way in.

Car 8's flank is **one UV island shared mirrored by both sides**, so the word
AMBULANCE reads correctly on one flank and backwards on the other, and the rear
island's single Star of Life lands on both doors. That is the imported atlas's
decision, not the cook's -- `mail.png`'s envelope mirrors the same way.

`models/vehicles/car5_next/` is Car 5 again, cut up so its driver door swings
and its windows are see-through. Every other articulated car in this tree is
generated in Blender and simply never joins its door or its panes to its body;
Car 5 is an import with no generator, so `tools/make_car5_next_assets.py` clips
both out of the finished mesh instead. It wears Car 5's own paint rather than a
copy: the cut moves no UV, so a second atlas could only drift away from the
body it wraps. Regenerate with `python3 tools/make_car5_next_assets.py`.

It writes `body.emesh`, a byte copy of Car 5's, which is still the shell the
catalog fits, the lamps sample and the snow pass tags; `body_open.emesh`, that
shell minus the door and the glass, plus the cabin an open door reveals -- an
inset inner skin, a floor, seats and a dashboard; `driver_door.emesh`, the
panel with its own inner skin and rim; and six pane files.

Nothing about the cut is styled. The door's Z bounds are the painted shut lines
in `textures/vehicles/car5/body.png` -- the front fender seam and the B-pillar
-- and its raked upper edge is a least-squares fit of the A-pillar taken off
the mesh, because a straight vertical cut there saws through the windscreen
header. The window outlines came off the same atlas: Car 5 paints its glass
onto the shell, which is why it is opaque, and the replacement panes have to
land where that paint was. The greenhouse makes that tractable -- it is a tent,
a belt ring at y=1.44, x=+/-1.15 and a roof ring at y=2.02, x=+/-0.87, so
windscreen, sides and backlight separate cleanly by face orientation and each
window only needs its own outline.

Two ordering facts are load bearing. `driver_glass` is cut out of the door, not
the body, because it is the only pane that has to swing with it, and
`player_car_visual` expects it in slot 3. And the glass comes out before the
cabin lining is built, or the lining stands as a wall behind every window.

`tools/validate_car5_next_door.py` is the gate. It rebuilds the shut skin,
probes the doorway for stationary obstructions, checks the glazing mirrors left
to right, and rasterises 90 directions at 3 door angles with back-face culling
to prove every hole the cut makes is filled by a pane or a rim, with no crack
left over. It writes a QA sheet next to its report; look at it.

`models/vehicles/car5_next_police/` is that same cut in police livery, made by
`tools/make_car5_next_police_assets.py`, which adds the two things a patrol car
needs and nothing else.

The livery is painted through the UVs rather than by hand. Car 5's atlas has no
labelled charts, so every texel is mapped back to the position and normal of
the triangle that owns it and the panels are recognised by where they are on
the car: the doors go white, everything else black, and a gold star lands on
whatever texels the front door turns out to occupy. That survives a re-cook of
the body, which hand-painting would not. Car 5's own red mask is reused so the
amber indicators and red tail lenses come through untouched, and the source's
baked panel shading is carried over as a relative value so the creases survive.

One constraint decides the whole design: Car 5's flanks, bonnet, roof and boot
each map BOTH halves of the car onto one chart. Every texel is painted onto a
left-hand panel and its right-hand mirror at once, so anything asymmetric --
lettering above all -- comes out reversed on one side. Hence a two-tone split
and a five-pointed star, both of which mirror cleanly. The atlas is written at
256 rather than Car 5's 128 because the star needs the texels; the upscale is
NEAREST, so nothing of the original is blended and the extra resolution only
carries what the script draws.

The lightbar takes the Municipal Cruiser 91-C's proportions -- converted into
this body's source units, which are not metres -- but is built up rather than
copied. It is a stack of slabs: two mounting feet with a siren speaker slung
between them, a dark base shell, a band of six lens cells (three per bank) with
chrome ribs, spine and end caps, and a chrome top rail. The cells stand proud
of the frame and the frame is recessed, which is what makes it read as parts
rather than one painted block at the distance you see it from. Its width does
NOT scale over from the 91-C: that bar is 1.52 m across a body half a metre
wider than this one, so it is re-fitted to Car 5's narrower roof.

Two things about it are easy to break later. It is welded onto the body mesh
rather than loaded as a part, because the emergency glow pass redraws the body
and lets the lit shader keep only what falls inside a glow box. And there is
one box per lens CELL, not one for the whole bank -- a single box would light
the chrome ribs along with the lenses. That is also why the patrol car needs
its own headlight profile id, 29, despite sharing Car 5's actual lenses: the id
is what `lit.frag` matches to find a lightbar, and Car 5 has none.

`tools/car5_next_police_spec.py` owns the geometry and prints the matching
`lit.frag` clause from `glsl()`. Nothing in the build forces those two files to
agree, so the cook validator parses the boxes back out of the shader and checks
every lens cell falls inside one and no chrome part does.

`tools/validate_car5_next_door.py car5_next_police` holds it to the same four
properties as the plain car.

`models/vehicles/common/wheel.emesh` is the shared wheel model. The player-car
visual drives its four instances from real suspension, steering, and sim-owned
spin. Each traffic car gets the same four-node setup; those wheels spin from
lane speed and the front pair follows the lane bend.

`models/vehicles/firetruck/body.emesh` is an original wheel-less low-poly body
generated by `tools/make_firetruck_assets.py`. Its 256x256 PSX paint atlas is
derived from `textures/vehicles/firetruck/body-reference.png`, generated with
OpenAI image generation on 2026-09-01 as an orthographic pixel-art material
sheet. The cook adds exact bitmap `FIRE` door lettering, a cab-over front,
diamond-plate lockers, hose gear, emergency lamps, and a roof ladder. Run
`python3 tools/make_firetruck_assets.py` from the repository root to regenerate
both runtime assets.

`models/vehicles/halcyon_six/body.emesh` is the original fictional 1936
Halcyon Six Sedan. `tools/make_halcyon_six_assets.py` builds its single
658-triangle wheel-less body with four open arches, separate low-poly fenders,
running boards, a tall cream roof, four painted door panels, two modeled round
headlights, a three-bar grille, and a flat rear spare-wheel suggestion. The
script also reduces `textures/vehicles/halcyon_six/body-reference.png`, made
with OpenAI image generation on 2026-09-02, into the final 256x256 RGBA PSX
atlas. Its traffic-fit authoring points are arch Y `0.68`, wheel X `1.06`,
front Z `2.18`, and rear Z `1.86` in source-model units.

`models/vehicles/montrose_regent_eight/body.emesh` is the original fictional
1931 Montrose Regent Eight formal sedan. The reproducible
`tools/make_montrose_regent_eight_assets.py` cook produces one 850-triangle
wheel-less body with four open wheel arches, independent bulbous fenders, broad
running boards, a long hood, a tall flat-roof cabin, a squared trunk, separate
supported headlights, a tall grille, and a fitted rear spare cover. It reduces
the clean factory-finish material sheet at
`textures/vehicles/montrose_regent_eight/body-reference.png`, generated with
OpenAI image generation on 2026-09-02, to one 256x256 RGBA PSX atlas. Its
traffic-fit authoring points are arch Y `0.68`, wheel X `1.10`, front Z `2.30`,
and rear Z `2.05` in source-model units. The rear spare cover is a body-mounted
period accessory; none of the four separately attached road wheels are baked
into this mesh. Normal sedan traffic deterministically splits between the
original sedan, Halcyon Six, and Montrose without changing truck or emergency
vehicle weights.

## Vehicle surface details and collision damage

### Aster A-80 airport aircraft

The original cream/teal Aster A-80 replaces the old seven-box plane on the
Gate 1 apron at Pinatty International. It is 32 m long with a 28 m wingspan.
`python3 tools/make_aster_a80_assets.py` builds its 256x256 RGBA pixel atlas,
884-triangle wheel-free airframe, separate six-wheel landing assembly, packed
editable `source.blend`, and structural/UV reports. Source/cooked models stay
private under `models/vehicles/aster_a80/`; the recipe and atlas are public.
`preview.emesh` joins the two pieces only for inspection; the game loads
`body.emesh` plus `gear.emesh` using the same atlas.

Placement, a stable instance ID, future entry/pilot locators, and compound
collision live in `src/city/airport_aircraft.h`. This is parked scenery with
collision, not yet enterable, stealable, drivable or flyable. Do not add it to
the road-car menu as a shortcut. Later interaction work should replace the
static actor/collision ownership explicitly.

Inspect with `build/bin/apricot_asset_lab --model
models/vehicles/aster_a80/preview.emesh --texture
textures/vehicles/aster_a80/body.png --yaw 170 --zoom .57`, and check placement
with `build/bin/apricot --start-at -2 2100 --start-heading -150`.

### Body-surface detail workflow

Window glass, grilles, lens paint, seams, and flat trim belong in the body
atlas, not on offset polygons. Independently deformed polygons can sink into
their backing panel after a collision. Keep real lamp housings, mirrors,
bumpers, and roof hardware as geometry.

Seven older original vehicles use derived `body_surface.emesh` and
`body_surface.png` assets: Vesper, GLR Lunge, GLR ZIP, Harrow, Halcyon,
Montrose, and the firetruck. Their canonical generators run
`tools/bake_vehicle_surfaces.py` automatically. Original `body.emesh` and
`body.png` authoring cooks remain intact. The matching `body_surface.json`
records source hashes and the reviewed skin components removed by the bake;
topology changes require reviewing that recipe again. Wayfarer's details are
baked directly by its generator; Car 5, Car 8, and ambulance retain their
existing body atlases, including paint variants.

All player and traffic lamp glow reuses the body mesh and its deformation.
The reserved negative UV scale selects a surface mask in the lit shader; a
tiny depth bias prevents coplanar fighting. Do not bring back a separate
four-corner glowing patch. Beam origins are separate from glow draw bounds.

After rebuilding, run `python3 tools/check_vehicle_surfaces.py --render`.
This verifies source hashes, retained geometry and UVs, then renders all 11
bodies with front/side/rear damage, glow, and repair. It checks glow silhouette
containment and frame stability, and saves the gallery and report under
`build/vehicle-surface-qa/`. Inspect the gallery too; numeric checks do not
prove every detail looks right under every possible dent.

## Player and pedestrian characters

The live player is Character 01 with `Character_01.png` from the supplied
`Characters_psx_1.1.zip`. Ambient pedestrians use eight more civilians from
that pack. A pedestrian's selected look is stable for its `(lane key, slot)`
simulation identity.

Run `python3 tools/cook_character_assets.py` from the repository root to pair
the ZIP's exact textures with the matching proven PSX-pack runtime rigs and
`Breathing Idle`, `Walking`, and `Sprint` clips from the original Probable Cause
tree. Cooked meshes, skeletons, animation tracks, and textures stay under the
ignored `models/characters/psx_pack/` private-asset boundary. The renderer
samples the bone tracks continuously; no per-frame mesh swapping is involved.

`models/vehicles/vesper_vx91/body.emesh` is the original fictional 1991 Vesper
VX-91 sports coupe. `tools/make_vesper_vx91_assets.py` builds its single
676-triangle wheel-less body with four open arches, a low wedge nose, long
sloping hood, rear-set faceted fastback cabin, closed pop-up headlight panels,
full-width rear lamp panel, mild ducktail spoiler, and two small exhaust tips.
The script reduces `textures/vehicles/vesper_vx91/body-reference.png`, generated
with OpenAI image generation on 2026-09-02, to the final 256x256 RGBA PSX atlas.
The atlas includes a five-spoke wheel-style reference, but the body mesh never
uses it as wheel geometry. Its traffic-fit authoring points are arch Y `0.55`,
wheel X `1.08`, front Z `2.08`, and rear Z `1.86` in source-model units. It is
one of the deterministic normal-car variants and does not change truck or
emergency-vehicle frequency.

The vehicle source OBJ headers credit **GGBot / ggbot.net**. These four runtime
files were copied from `/Users/andrewbaker/workspace/Games/probablecause` on
2026-08-28 and 2026-08-30. The old repository does not record a licence label
for the vehicle pack, so confirm redistribution terms before a public build.

`textures/vehicles/car5/taxi.png` is a reproducible yellow paint variation of
the original red Car 5 texture. Regenerate it from the repository root with
`python3 tools/make_car5_taxi_texture.py`. The script preserves non-red pixels
and alpha, and writes the result beside `body.png` without changing the source.

## Generated station materials

`textures/world/gas_station/` contains nine 512x512 seamless albedo maps for
painted metal, concrete, brick, stucco, canopy panels, pillar enamel,
fuel-pump steel, fuel-pump cabinet panels, and dumpster paint, plus one
512x512 integrated pump-control face.
They were generated for this project
with OpenAI's built-in image generation on 2026-08-31, then reduced from the
1254x1254 source outputs. The prompts asked
for photorealistic, evenly lit, orthographic PBR-style albedo with seamless
edges, restrained age, no text, no logos, and no baked shadows. The two prop
maps add chipped off-white powder coat and grime to the pumps, plus chipped
green enamel, rust flecks, scrapes, and lower-edge dirt to the dumpster.
The later canopy set adds panel seams and restrained edge wear to the roof and
fascia, smooth chipped enamel to the four support shafts, and larger riveted
sheet-metal panels to the pump cabinets while preserving authored colour tints.
The pump-control face was generated as one straight-on integrated display/POS
panel with `PAY HERE`, card reader, keypad, receipt slot, emergency stop, and
three grade buttons. It replaces 24 small box decorations across four pumps.

## Generated bank art

`textures/world/bank/pinatty-savings-sign.png` is the front face for the
Pinatty Savings & Trust rooftop and lobby signs. It was generated for this
project with OpenAI's built-in image generation on 2026-09-03, then reduced to
the exact runtime texture size. The prompt requested flat late-1980s civic-bank
sign art in navy, ivory, and gold, with a vault-door medallion, the exact bank
name, no mockup, no perspective, and no extra text. The 2026-09-05
`ohaven-savings-sign.png` edit is superseded and unused.

The bank's `terrazzo-floor.png`, `walnut-panel.png`, `acoustic-ceiling.png`,
and `deposit-boxes.png` were generated with the built-in tool on 2026-09-03;
`atm-face.png` is the one the game loads, and the 2026-09-05
`atm-face-ohaven.png` edit is superseded and unused. Tileable diffuse
materials are 512x512; the square ATM control
face is 768x768 and maps once onto each lobby machine's 70 cm square upper
control panel, not the full cabinet. Each cabinet is 86 cm wide and 1.6 m tall,
with a separate plain-metal lower service door. Full
production prompts are saved in `textures/world/bank/GENERATED.md`.

The interactive vault uses `vault-door-face.png` and `vault-office-note.png`
at 768x768 and `vault-keypad.png` at 512x512. The base set was generated with
the built-in tool on 2026-09-03; the 2026-09-05 O'Haven door edit is superseded
and unused. Full prompts are in
`textures/world/bank/VAULT-GENERATED.md`. They are diffuse artwork, not PBR maps.
The note artwork must be regenerated if `kBankVaultCode` changes.

## Generated effect decals

`textures/effects/fluid-spill-mask.png` is a 512x512 transparent neutral spill
mask generated for this project with OpenAI's built-in image generation on
2026-09-01. The prompt requested a directly overhead irregular shallow puddle
with a mottled centre, feathered wet edge, branching trickles, satellite drops,
neutral white/gray coverage, real alpha, and no ground, lighting, text, logo,
border, or shadow. Runtime tint and opacity turn the same coverage into coolant,
oil, or fuel while preserving the organic breakup.

## Billboard art

`textures/world/billboards/pinnaty-taxi.png` is the user-supplied 1850x850
Pinnaty Taxi advertisement added on 2026-09-01. The runtime preserves its
37:17 aspect ratio on a dedicated single-monopole roadside fixture.

`textures/world/airport/` contains two 512x512 seamless albedo maps generated
with OpenAI's built-in image generation on 2026-08-31 and reduced from the
1254x1254 sources. The runway/apron prompt requested cool charcoal airport
paving with fine aggregate, repaired seams, rubber scuffs, and no baked
markings. The terminal prompt requested a repeating blue-gray glass curtain
wall with pale aluminum mullions and off-white cladding. Both prompts required
orthographic, evenly lit, tileable diffuse art with no text, logos, people,
vehicles, perspective, shadows, or watermark. Runway markings stay modeled in
`src/city/airport.h` so their spacing is exact.
