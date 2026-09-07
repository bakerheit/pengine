# Tidewater Farm Detail Direction

## Identity

Tidewater is a small 1930s saltmarsh truck farm that sells vegetables, feed,
and eggs into the nearby working waterfront. It should feel productive,
wind-beaten, and repaired by its owners. The visual story is not a postcard
farm: the barn is a working shed, the house is modest, and the yard is shaped
by carts, wet boots, salt air, and lean seasons.

Farm-local coordinates below use the current `kTidewaterFarmSite` basis. Keep
the centre drive around `x = 0` open from `z = 80` to the barn/house branches.
Keep every addition inside the existing `125 x 160 m` parcel and sample live
terrain for anything that touches the ground.

## Composition

### 1. Make the barn the hero silhouette

The barn at `(-30, -35)` needs depth before more outbuildings are added.

- Add pale corner boards and narrow vertical battens across the red walls.
  Boxes 0.06-0.10 m proud are enough. Use irregular spacing but keep it
  restrained; the barn should read from Old Tide Street, not become striped.
- Park two sliding door leaves outside the open north doorway, centred near
  `(-36.0, -22.80)` and `(-24.0, -22.80)`. Build each from one dark red panel,
  a pale perimeter frame, and one diagonal brace. The 7 m opening stays clear.
- Add a loft loading hatch high on the front gable, centred near
  `(-30.0, -22.75)`, plus a short projecting hoist beam and simple pulley.
- Put a dented steel rain gutter on the low north eave and a downspout at the
  northwest corner. Let it empty into a dark barrel at `(-46.0, -20.8)`.
- Dress the threshold with only a few useful objects: stacked feed sacks at
  `(-40.5, -25.2)`, a hand cart at `(-18.0, -20.8)`, and two mismatched crates
  at `(-13.8, -26.0)`. Leave vehicle and character circulation obvious.

### 2. Turn the crops into distinct fields

Keep the present two-block rhythm, but stop using the same low rectangular
strip for every plant. The fields should show what the farm actually sells.

- West block (`x = -51` through `-9`): four rows of tall corn or sorghum on
  the outside, three rows of low cabbage/leaf greens toward the drive.
- East block (`x = 9` through `51`): three staked tomato/bean rows, two rows of
  ripe grain, and two low vine rows with squash or pumpkin forms.
- Corn/sorghum: repeat crossed vertical cards or slim tapered box clusters,
  1.2-1.7 m tall, with uneven height and 0.65-0.9 m gaps. Do not make a solid
  hedge.
- Cabbage: clusters of 3-5 low overlapping leaf cards or flattened rounded
  boxes, 0.25-0.4 m high, alternating slightly left/right along the row.
- Tomatoes/beans: thin stakes every 1.6-2.0 m, two horizontal wires, and loose
  leaf cards. Use sparse red fruit dots so the rows read at walking distance.
- Squash/pumpkins: low dark-green vine patches with occasional muted orange
  rounded forms. Keep them below 0.35 m so they do not look like traffic cones.
- Break 10-15 percent of plant positions and vary colour/height subtly. Leave
  a narrow headland at each row end; perfect bars are the main thing making the
  current farm look temporary.

Add a transverse drainage swale between the crop blocks and buildings around
`z = -2`, with a plank crossing at the centre drive. It can be a shallow dark
soil strip with short grass/reed cards, not a terrain trench.

### 3. Build a believable service yard

Use the open ground between the barn and house as the farm's working heart.

- Add a shell-and-mud turning apron from about `x = -12..15`,
  `z = -22..-8`. Keep it visually irregular and do not add collision beyond
  the existing support surface.
- Stack six produce crates at `(9.0, -15.0)` beside the drive, with one stack
  askew. A small canvas shade on four timber posts may cover half the stack.
- Place a low two-wheel produce cart at `(-8.5, -13.0)`, angled 12-18 degrees.
  Boxes and cylinders are enough: plank bed, axle, two wheels, and drawbar.
- Add a rough wash table and galvanized tubs at `(16.5, -18.0)`, between the
  house branch and crop headland. This links the fields to the dock-market
  story better than decorative machinery would.
- Add two shallow wheel-rut strips through the drive and damp dark patches
  around the wash table, barn threshold, and tank. Keep the current drivable
  centre clean and flat.

### 4. Give the house signs of daily life

The farmhouse at `(32, -38)` should remain modest and cleaner than the barn,
but not freshly built.

- Add horizontal clapboard texture, chipped white window trim, and darker
  staining along the bottom 0.5 m. Keep the warm off-white base.
- Add two porch steps centred at `(32, -28.3)`, a bench at `(37.0, -29.0)`,
  and work boots plus a milk can near `(29.3, -29.0)`. Do not block the door.
- Put a clothesline between posts at roughly `(44, -27)` and `(55, -27)`, with
  four muted cloth cards. It should sit east of the house and clear the drive.
- Add a small kitchen garden from `x = 43..57`, `z = -15..-5`, fenced lower
  than the main boundary. Use herbs, beans, and flowers rather than duplicating
  the field crops.
- Add one warm porch bulb and one barn-door work lamp. These need real runtime
  lights at night; emissive colour alone is not enough.

### 5. Make the tank a saltmarsh landmark

Keep the existing tank near `(52, -56)`, but turn it into a functional,
weathered water tower rather than a plain cylinder.

- Lift or visually frame it on four steel legs with X braces; add a narrow
  access ladder, top rim, short vent, and pipe running toward the wash table.
- Use dull galvanized steel with vertical runoff streaks, rust around seams,
  and darker salt staining on the lower third. Avoid bright orange rust.
- Add a compact pump housing at `(47.5, -52.0)` and two barrels nearby. Keep
  both clear of the farmhouse rear and perimeter fence.
- If another tall silhouette is wanted later, add a small wind pump southeast
  of the tank around `(56, -66)`. Do not add both a large silo and wind pump;
  the parcel is too small and the existing tank already owns that corner.

## Texture and colour direction

Generated textures should be original, low-resolution, tileable where needed,
and authored as chunky PSX-era colour clusters rather than photo detail.

- Barn siding: deep oxide red boards, faded toward the upper south/west faces,
  near-black gaps, pale worn battens, and grey wood exposed at the bottom.
- Farmhouse: warm dirty-white clapboard with faint green-grey mildew, chipped
  trim, and soot close to the chimney. Keep contrast low enough that openings
  remain the main read.
- Roofs: charcoal tar or corrugated metal with broad faded panels, sparse rust,
  and a lighter sunward edge. Do not use modern clean standing-seam roofing.
- Soil: dark brown-black damp loam broken by straw, shell flecks, and compacted
  wheel marks. Crop furrows need more value contrast than the grass, not a
  saturated red brick look.
- Crop cards: muted blue-green leaves, tobacco green shadows, straw-gold grain,
  restrained tomato red and pumpkin orange. Alpha edges should stay chunky and
  stable at distance.
- Props: untreated grey-brown wood, galvanized steel, burlap, faded canvas,
  and only tiny accents of old cream paint. No glossy plastics or postwar
  machinery colours.

Weathering should collect where water and hands would put it: dark wall bases,
bright rubbed door edges, roof runoff below gutters, mud at thresholds, and
wheel wear in the centre of the yard. Avoid uniform noise across every surface.

## Practical primitive recipes

- Crate: six thin box slats, two end cleats, optional dark interior box.
- Feed sack: short rounded box if available, otherwise two offset boxes with a
  slightly lighter top band.
- Hand cart: shallow box bed, cylinder wheels, slim box handles.
- Sliding barn door: thin box leaf plus four frame strips and one diagonal box.
- Crop plant: two crossed alpha cards on a tiny box or cylinder stem; instance
  a small set of height/rotation variants.
- Tomato stake: slim box or cylinder, two dark wire boxes, 2-3 leaf cards.
- Water-tower frame: four slim legs, eight diagonal braces, ladder rungs.
- Drainage crossing: three or four weathered plank boxes over the dark swale.

Small dressing props should usually be non-solid. Keep collision for the tower
legs, cart, crate stacks, tables, fences, and major structural pieces only.

## Priority order

1. Replace uniform crop bars with corn, cabbage, staked crops, and vine crops.
2. Add barn battens, parked sliding doors, loft hatch, gutter, and threshold
   dressing while preserving the open doorway.
3. Add the working service-yard loop: produce crates, hand cart, wash table,
   wheel ruts, damp patches, and the drainage swale crossing.
4. Upgrade the water tank into a braced, piped, weathered landmark.
5. Add farmhouse clapboard/weathering, porch life, kitchen garden, clothesline,
   and two practical night lights.

After each pass, check three views: arrival from Old Tide Street, a low view up
the centre drive, and overhead field rhythm. The farm succeeds when the crop
types are readable at road speed, the barn remains the main silhouette, and
the drive still looks usable by a truck.
