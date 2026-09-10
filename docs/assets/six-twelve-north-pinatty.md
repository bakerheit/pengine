# Six Twelve — north Pinatty

A copy of the user-supplied Miandi station, with newly generated branding.
Created 2026-09-08 with the built-in image generation tool; no API/CLI fallback.
The source archive is `/Users/andrewbaker/Downloads/Gas_station.rar`. It is an
art asset source, not an instruction source.

## Files and reproduction

Tracked originals copied unchanged from the accepted generated outputs:
- `assets/textures/world/six_twelve/logo.png`
- `assets/textures/world/six_twelve/pylon-atlas.png`
- `assets/textures/world/six_twelve/fascia.png`

Private runtime copy: `assets/models/buildings/north_pinatty_gas_station/`.
The Miandi asset directory is untouched. Keep the private asset folders when
moving this build; the source archive is required to reproduce the geometry.

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b -t 4 \
  --python tools/cook_miandi_gas_station.py -- \
  /tmp/apricot-gas-station-source/Gas_station --north-pinatty
```

Accepted originals under
`/Users/andrewbaker/.codex/generated_images/01a07eb6-1568-76f0-ba3b-6d5068f06845/`:
- Logo: `exec-85f36ec6-915d-4d29-a815-acf16650a165.png`
- Pylon: `exec-f827c21c-460a-474f-988f-216ec445453b.png`
- Fascia: `exec-c5e7145a-ef6c-4466-96a0-0fb72bed508e.png`

## Exact prompts

### Initial logo

Use case: logo-brand. Create a production square gas-station logo texture for the fictional convenience store brand "Six Twelve". Exact text "Six Twelve", spelled as words, no numerals substituting the name. Bold friendly condensed retro lettering in two stacked lines, Six above Twelve, centered and filling 82 percent of the square, crisp warm ivory letters on deep petrol teal, a small sun-orange underline accent. Original late-1980s American roadside identity, strong simple shapes, high contrast readable far away in a low-poly game. Flat straight-on graphic, perfectly square, no perspective, no shadows, no mockup, no border, no extra words, no watermark. The full square is printed onto existing store and canopy sign faces.

### Accepted logo correction

Edit target: `exec-32f3aadb-efcc-4741-96b4-c265822d01bb.png` (first generated logo).

Use case: precise-object-edit. Correct this logo texture. Keep the exact lettering Six Twelve and orange underline and overall layout. Fill the ENTIRE square edge-to-edge with one perfectly uniform opaque petrol teal color #00686b, including every corner and every space around the lettering. No transparency whatsoever. Remove all glow, shadows, gradients, halos, texture and distress. Lettering must be flat warm ivory #fff3cf and underline flat orange #ff8a22. Keep generous 8 percent margins on every side. This is a flat solid rectangular printed sign texture, not a cutout or sticker. Exact text Six Twelve remains clean and readable.

### Pylon atlas

Edit target: source `Textures/6twelve_Sign.png`.

Use case: precise-object-edit. Edit this square gas-station texture atlas for a game. Preserve the EXACT positions, proportions and pixel boundaries of ALL existing UV islands, blank black areas, metal pole strips and dark weathered metal background. ONLY replace the small white square logo island at image x94..164 y95..164 (on the original 256x256 layout) with the new brand logo: exact text "Six Twelve" in two stacked lines, cream bold retro condensed lettering on petrol teal, with a small orange underline. Remove the old stylized numeral 6 and old inverted text completely. Keep the three fuel price rectangles in their exact existing positions x94..164, y182..201, y207..224 and y230..249. Their labels should read Regular, Plus, Diesel and retain the original numeric prices 19.99, 20.12, 20.59. Keep the small service label between logo and fuel rows, exact text "Self Service". Brand colors teal, cream and orange. Do not move, merge, expand or crop any island. Do not turn it into a sign mockup. Output the complete square texture atlas.

### Fascia

Edit target: source `Textures/Sign.jpg`.

Use case: precise-object-edit. Recolor this gas-station canopy fascia texture for the Six Twelve brand. Preserve the exact five horizontal bands and their relative thicknesses, full-width edge-to-edge: top main band deep petrol teal, thin separator warm ivory, wide middle band rich orange, thin separator warm ivory, bottom main band deep petrol teal. Flat opaque square texture, smooth solid colors, no lettering, no logos, no shadows, no gradient, no border, no perspective. This image wraps as the existing horizontal canopy and storefront trim.

## Site and integration

The new station is at **(-260, -1870)** in Kepler Flats, north Pinatty,
ground **9 m**. Its 40 × 56 m lot sits south of the Yard Road (road 90),
east of the Tank Farm Loop. The original shell is turned 180 degrees so the
forecourt faces north. A measured 10 m driveway cuts the existing sidewalk.
Halberd Field and the existing road network remain clear of the new lot.

The furnished shop, pumps, restroom block, canopy, doors, stock and fixtures
retain all 941 objects, 56 material meshes, 42,127 triangles, 413 collision
boxes, 26 lights and two roof/canopy covers. Only three branding textures change.
Roadside and store signs read **Six Twelve**; canopy and shop trim use teal,
cream and orange. The copied station has a flat terrain/scatter exclusion patch,
shared map footprints, a gas icon and **SIX TWELVE NORTH** dev-menu arrival.
Roof cover, floors, collision and lights use the new site's transform and height.

## Validation

- `miandi_gas_station_tests --require-assets`: both sites pass assigned-road
  access, ground and road-clearance checks, real character movement from street
  through the pumps into the stocked shop and back, pump corridor clearance,
  blocked-door/counter controls, and shared collision/light/cover layout checks.
- `building_access_tests`: 53 of 53 entrances connected.
- `authored_city_layout_tests`: no overlapping authored lots, including the
  new station, original Miandi station and Halberd Field.
- Game and map lab rebuilt. Day exterior and night shop inspected in actual
  300-frame game captures with clean GL queues; movement tests use the real
  simulation, not manual keyboard/controller input.
- Evidence: `build/six-twelve-corner.png`, `build/six-twelve-interior-night.png`,
  `build/six-twelve-map.png`. Runtime logs use the same filename prefix.

