# Tidewater Farm generated texture provenance

Created on 2026-09-05 with the built-in `image_gen.imagegen` tool. The
generated square sources were reduced to 128 x 128 production albedo tiles to
keep the texture frequency and texel clusters consistent with Apricot's PSX
presentation. These are original project assets with no external source
attribution.

Validation: every production file is an opaque RGB PNG at 128 x 128. Each was
inspected in a 2 x 2 repeated preview after reduction; the material rhythms
continue cleanly across both axes without a visible hard border at intended
game scale. Opposite-edge PSNR ranged from 19.4 to 37.9 dB, with the lower
scores coming from periodic plank/corrugation bands rather than a broad color
or lighting discontinuity.

## Production assets

- `assets/textures/world/farm/weathered-red-barn-boards-albedo.png`
- `assets/textures/world/farm/farmhouse-clapboard-albedo.png`
- `assets/textures/world/farm/tilled-furrow-soil-albedo.png`
- `assets/textures/world/farm/aged-galvanized-roof-albedo.png`

## Generation prompts

### Weathered red barn boards

```text
Use case: stylized-concept
Asset type: seamless tileable game texture for a low-resolution PSX-style 3D environment
Primary request: weathered red-painted vertical barn boards for Tidewater Farm
Subject: narrow rough-sawn timber planks with thin dark seams, faded oxblood red paint, sparse pale exposed wood, a few tiny nail heads, restrained age and salt-air wear
Style/medium: flat orthographic albedo texture, hand-painted PSX-era pixel clusters, deliberately low-frequency detail, crisp blocky texels
Composition/framing: square surface-only texture viewed straight on; uniform detail density across the full canvas
Color palette: muted oxblood, brick red, umber seams, small desaturated tan wear
Materials/textures: rough timber grain and flaking paint kept broad and readable
Constraints: perfectly seamless on left/right and top/bottom edges; diffuse color only; even neutral illumination; no perspective; no borders; no large unique marks; no text; no logos; no watermark
Avoid: photorealism, glossy highlights, cast shadows, deep relief, knots concentrated in one area, modern clean boards
```

### Farmhouse clapboard

```text
Use case: stylized-concept
Asset type: seamless tileable game texture for a low-resolution PSX-style 3D environment
Primary request: old coastal farmhouse horizontal clapboard siding for Tidewater Farm
Subject: narrow overlapping horizontal wooden clapboards painted warm chalky off-white, thin cool-gray shadow lines under each lap, subtle chipped edges and restrained sea-air grime
Style/medium: flat orthographic albedo texture, hand-painted PSX-era pixel clusters, deliberately low-frequency detail, crisp blocky texels
Composition/framing: square surface-only texture viewed straight on; repeated horizontal courses fill the canvas
Color palette: warm ivory, bone white, pale gray-blue, tiny desaturated beige wear
Materials/textures: aged painted wood with broad readable variation, not dirty or ruined
Constraints: perfectly seamless on left/right and top/bottom edges; diffuse color only; even neutral illumination; no perspective; no borders; no windows or trim; no large unique marks; no text; no logos; no watermark
Avoid: photorealism, glossy highlights, cast shadows, deep relief, vinyl siding, modern pristine finish
```

### Tilled furrow soil

```text
Use case: stylized-concept
Asset type: seamless tileable game texture for a low-resolution PSX-style 3D environment
Primary request: freshly cultivated dark coastal farm soil with parallel shallow planting furrows
Subject: rich brown earth, broad alternating raised rows and narrow grooves, scattered tiny muted straw flecks and occasional small clod shapes
Style/medium: top-down flat albedo texture, hand-painted PSX-era pixel clusters, chunky readable shapes, low-frequency detail suitable for a 1990s open-world game
Composition/framing: square surface-only tile viewed exactly from above; furrows run vertically through the canvas with no horizon
Color palette: dark umber, warm brown, muted clay, very sparse dull straw
Materials/textures: dry-to-damp worked soil, broad clods rather than photographic noise
Constraints: perfectly seamless on left/right and top/bottom edges; diffuse color only; even neutral illumination; no perspective; no borders; no plants; no footprints; no large unique stones; no text; no logos; no watermark
Avoid: photorealism, shiny mud, cast shadows, deep trenches, grass, repeating central emblem
```

### Aged galvanized roof

```text
Use case: stylized-concept
Asset type: seamless tileable game texture for a low-resolution PSX-style 3D environment
Primary request: aged galvanized corrugated metal roofing for Tidewater Farm
Subject: narrow repeating vertical corrugations, muted gray zinc, sparse dull rust freckles and pale oxidation, maintained but old
Style/medium: flat orthographic albedo texture, hand-painted PSX-era pixel clusters, deliberately low-frequency detail, crisp blocky texels
Composition/framing: square surface-only texture viewed straight on; corrugation bands run vertically and repeat evenly
Color palette: charcoal gray, blue-gray, pale zinc, sparse muted rust brown
Materials/textures: galvanized sheet metal, broad banding and restrained wear
Constraints: perfectly seamless on left/right and top/bottom edges; diffuse color only; even neutral illumination; no perspective; no borders; no holes; no large unique stains; no text; no logos; no watermark
Avoid: photorealism, chrome reflections, specular glare, cast shadows, extreme rust, modern pristine roofing
```
