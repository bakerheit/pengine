# Cloggers restaurant generated textures

Current visible brand: **CLOGGERS**. See the Cloggers rebrand record below; Quickbite references document the original version and stable internal paths.

The next menu refresh is governed by
`docs/design/restaurant-menu-board-spec.md`. It intentionally defines separate
4:1 counter-board and 2:3 drive-through canvases before any replacement art is
generated; current shared menu textures are legacy bindings, not a license to
stretch the same art across both receivers.

Created 2026-09-04 using the built-in image_gen tool and the imagegen skill. These are original generated raster materials for the fictional Quickbite Grill restaurant. No reference photographs, real restaurant brands, stock assets, or external logos were supplied. The existing restaurant geometry established the red fascia, yellow trim, and cream wall palette.

## Assets and integration

| File under assets/textures/world/quickbite/ | Native size | Intended surface |
| --- | --- | --- |
| brand-sign-albedo.png | 1881 × 836, 2.25:1, RGB opaque | quickbite brand sign face; quickbite road sign face |
| menu-board-albedo.png | 1024 × 1536, 2:3, RGB opaque | quickbite menu sign face |
| cream-tile-albedo.png | 1254 × 1254, square, RGB opaque | Solid restaurant wall pieces, excluding opening/glass infills and colored trim |

Sign and menu UVs should cover the complete image once, with white tint. Brand generation requested 3:1 but returned 2.25:1; use native-aspect faces 6.75 × 3 m and 4.5 × 2 m, centered on the larger sign backing. Do not stretch the lettering. The menu face is 2 × 3 m and faces the driver's approach (+X local, yaw 90 degrees). Front and pylon faces face local -Z, yaw 180 degrees.

The ceramic material contains four tiles per edge despite the prompt requesting eight. A repeat of approximately 1.2 metres gives 30 cm tiles. Retain the existing red/yellow fascia geometry instead of tinting this cream albedo. The texture was requested as a repeating surface and visually inspected; in-game repetition and directional lighting remain part of the combined renderer QA.

## Inspection

- All three assets were opened and visually inspected from built-in generation results.
- Brand reads "Quickbite" and "GRILL" correctly, with large cream lettering and a gold burger emblem.
- Menu reads CHEESEBURGER MEAL $4.95, CHICKEN MEAL $5.45, DOUBLE BURGER MEAL $5.95, FRIES + DRINK INCLUDED, and ORDER HERE. These are fictional in-game prices.
- Dimensions and absence of alpha were verified with the local image metadata reader.
- Images were copied intact into the project; no cropping, retouching, or resampling was performed.
- These asset checks do not establish in-game mesh orientation, lighting, or readability. Root owns final material hookup and combined runtime QA.

## Source output locations

- Brand: /Users/andrewbaker/.codex/generated_images/01a06c63-aff5-7e21-ba8d-7be4354c7e41/exec-b4aa7f47-54e2-4d76-928e-a84165b49d10.png
- Menu: /Users/andrewbaker/.codex/generated_images/01a06c63-aff5-7e21-ba8d-7be4354c7e41/exec-93ad238f-53b2-4870-968f-4660faa1fd8c.png
- Tile: /Users/andrewbaker/.codex/generated_images/01a06c63-aff5-7e21-ba8d-7be4354c7e41/exec-be6643c8-637b-4b3b-ae19-6a9bb6a84883.png

## Exact final prompts

### Brand sign

Use case: ads-marketing.
Asset type: production diffuse texture for the fictional Quickbite Grill restaurant sign, reused on its storefront and roadside pylon in a gritty stylized late-20th-century driving game.
Primary request: ORIGINAL flat full-bleed restaurant sign artwork, horizontal exactly 3:1 aspect ratio, 3072 by 1024 composition. Bold readable custom chunky friendly italic cream lettering saying exactly "Quickbite", with substantial uppercase "GRILL" beneath it, centered across a deep brick-red enamel field. Restrained warm golden-yellow border accent and a small simple original grilled-burger emblem integrated with the lettering. Restaurant's established colors are red fascia, yellow trim, cream tile. Text exact: "Quickbite" and "GRILL". Spell Q-u-i-c-k-b-i-t-e correctly.
Style: convincing 1980s/1990s neighborhood fast-food brand, screen-printed enamel artwork with mild paint wear and subtle aged surface grain, solid high contrast letterforms readable from a moving car. Keep type very large, occupying most of the panel. No other words, prices or slogans.
Composition/framing: image IS the artwork; rectangular flush full bleed all the way to edges, no surrounding margin, no perspective. Orthographic front-facing 2D diffuse/albedo asset, no frame, no mount, no poles, no facade, no sky, no mockup, no objects around it. Neutral even lighting; no 3D extrusion, no directional shadow, no fake specular highlight. Avoid real brands, trademark lookalikes, modern minimalism, graffiti, heavy damage, tiny text or watermark.

### Drive-through menu

Use case: ads-marketing.
Asset type: production diffuse texture for a drive-through menu board at fictional Quickbite Grill in a gritty stylized 1980s/1990s open-world driving game.
Primary request: original flat printed fast-food menu artwork, PORTRAIT 2:3 aspect ratio, composing a 1024 x 1536 rectangular menu panel. Image fills the whole rectangular panel edge-to-edge. Brand header uses huge warm cream chunky italic lettering "Quickbite", with "GRILL" beneath, on deep brick red; small golden-yellow grilled burger icon. Match a friendly aged red/cream/yellow neighborhood fast-food sign.
Composition: clear readable large type, three appetizing commercial food-photo combo rows with a burger or chicken sandwich, fries in a plain red paper sleeve, and a plain red cup. Warm cream panel background, brick red dividers and headings, golden-yellow price circles. Header occupies top quarter; food pictures and descriptions use the middle two-thirds. Small simple footer.
Text verbatim, no other words:
"Quickbite"
"GRILL"
"CHEESEBURGER MEAL"
"$4.95"
"CHICKEN MEAL"
"$5.45"
"DOUBLE BURGER MEAL"
"$5.95"
"FRIES + DRINK INCLUDED"
"ORDER HERE"
Clearly separate each price with its correct meal. No tiny fine print.
Style: professionally printed late-20th-century menu with subtle aged paper and enamel surface grain, appetizing food photographed like a period fast-food advertisement, restrained slightly faded inks, enough contrast to be legible from inside a car.
Constraints: flat diffuse/albedo artwork only, perfectly front-facing orthographic, no device surround, no bezel, no poles, no installation, no pavement, no restaurant building, no perspective, no exterior scene, no directional shadow, no fake backlight, no modern touch UI, no real-world logos or brands, no watermarks.

### Ceramic facade

Use case: stylized-concept.
Asset type: seamless repeating diffuse/albedo texture for the exterior walls of the fictional Quickbite Grill fast-food restaurant in a gritty, stylized late-20th-century open-world driving game.
Primary request: a clean but lightly aged warm cream ceramic wall-tile surface, true straight-on orthographic flat scan. Square image, 1024 x 1024. Eight by eight substantial square cream tiles, warm pale grey narrow grout, tiny irregular glaze flecks and restrained age at grout edges. Slight variation between tiles, subtle material grain; credible restaurant facade finish and readable at game distance.
Composition: the tile pattern meets perfectly across opposite image edges; no border or framing. Opaque full-bleed flat texture. Surface texture only.
Lighting: neutral evenly lit diffuse albedo, no directional shadows, no highlight, no gradient, no ambient occlusion shading.
Constraints: no text, no branding, no objects, no windows, no building, no perspective, no scene, no modern photo-real restaurant mockup, no extreme grime. Cream is the field color; no red checkerboard. Original material texture with gently gritty PS2-era baked-diffuse character.

## Interior material extension

Created 2026-09-04 with three additional built-in image_gen calls using the same imagegen skill. No external references were supplied. Existing airport furnishing steel was inspected but not reused because its raised ribbed pattern would be wrong for a flat restaurant worktop.

| File under assets/textures/world/quickbite/ | Native size | Intended surface | Suggested full-texture repeat |
| --- | --- | --- | --- |
| red-vinyl-albedo.png | 1254 × 1254, RGB opaque | quickbite interior booth seat cushions and backrests; exclude support legs/frames | 0.8 m |
| linoleum-floor-albedo.png | 1254 × 1254, RGB opaque | quickbite interior floor | 1.2 m, giving 30 cm individual tiles |
| brushed-stainless-albedo.png | 1254 × 1254, RGB opaque | quickbite interior kitchen cabinet/prep/hood/sink metal surfaces; quickbite interior counter top | 1.0 m |

Use white albedo tint. The vinyl has fine pebbled grain and restrained wear without stitching or upholstery geometry baked in. The floor contains four rows by four columns of alternating cream/red tiles, with an even pattern count so checker colors continue across texture repeats. The steel has fine horizontal brushing and faint work scratches, with no ribs or appliance geometry baked in.

Geometry coordination: the dining floor is 29.7 × 15.7 metres; use independent X/Z UV scaling to keep the individual tiles square. Booth upholstery is about 2.4 metres wide; approximately three texture repetitions span that width. Kitchen pieces are typically 1–3 metres wide. Existing cream-tile-albedo.png also covers the interior counter base, while the existing menu texture is reused on three 1.2 × 1.8 metre interior menu sign faces.

All three generated outputs were visually inspected, metadata dimensions and opaque channels were checked, and source PNGs were copied intact without retouching or resampling. In-game UV placement, material lighting, and repeated-edge appearance remain part of root's combined runtime QA.

### Interior source outputs

- Vinyl: /Users/andrewbaker/.codex/generated_images/01a06c63-aff5-7e21-ba8d-7be4354c7e41/exec-853cfb98-f547-4102-bcab-6a768ca1d38e.png
- Floor: /Users/andrewbaker/.codex/generated_images/01a06c63-aff5-7e21-ba8d-7be4354c7e41/exec-6464dcb0-7249-4f16-a602-ad14d764176a.png
- Stainless: /Users/andrewbaker/.codex/generated_images/01a06c63-aff5-7e21-ba8d-7be4354c7e41/exec-c02dec95-377f-4d6f-bbcd-c70b945c3de9.png

### Exact interior prompt: red vinyl

Use case: stylized-concept.
Asset type: seamless repeating opaque diffuse/albedo texture for padded booth seating inside Quickbite Grill, a fictional 1980s/1990s red, yellow and cream fast-food restaurant in a gritty stylized PS2-era driving game.
Primary request: a square full-bleed close-up scan of lightly worn brick-red diner booth vinyl. Uniform deep warm red pigmented vinyl with fine natural pebbled grain, subtle soft creases, and modest rubbed areas. Clean and maintained but used, believable synthetic upholstery, not luxurious leather and not ripped or filthy.
Composition: flat straight-on orthographic material scan only, a continuous surface extending beyond all four edges. Seamless repeat. No stitching, no buttons, no piping, no tufted panels, no upholstery silhouette, no sofa, no booth geometry, no perspective, no text, no logos, no symbols. Square 1024 x 1024 image.
Lighting: strictly neutral evenly lit albedo; no gradient, no directional shading, no large highlight, no shadows. Gentle material grain should survive mipmapping without noisy large blotches. No external border or blank margin.

### Exact interior prompt: linoleum floor

Use case: stylized-concept.
Asset type: seamless repeating opaque diffuse/albedo texture for the dining-room linoleum tile floor of Quickbite Grill, a fictional 1980s/1990s fast-food restaurant in a gritty stylized PS2-era driving game.
Primary request: square full-bleed flat orthographic material scan of a modest cream and muted brick-red linoleum floor grid. Show exactly four by four equal square tiles in a simple alternating red-and-cream checkerboard, with fine flush grey seams. Cream tiles have restrained warm mottled terrazzo-like vinyl flecks; red tiles have fine warm red speckles and faded color. Quiet practical period restaurant floor, mild foot scuffs and age, maintained and clean. No raised ceramic bevels.
Composition: grid aligned to image axes, exactly four rows and four columns so opposite edges continue the checkerboard seamlessly when tiled. Square 1024 x 1024 image. All tiles equal size. No border or inset medallion. The material itself fills the whole image.
Lighting: flat neutral evenly lit diffuse/albedo only, no directional shadows, no highlights, no reflection, no central light spot, no gradients, no baked room lighting.
Avoid: furniture, feet, objects, perspective, room scene, building, text, logos, diagonal pattern, heavy dirt, photoreal puddles, glossy mirror finish, thick grout, external margin.

### Exact interior prompt: brushed stainless steel

Use case: stylized-concept.
Asset type: seamless repeating opaque diffuse/albedo texture for stainless-steel kitchen prep counters, cabinets, sink surrounds and extractor hood inside fictional Quickbite Grill in a gritty stylized late-20th-century driving game.
Primary request: a flat square orthographic close-up material scan of clean, lightly used brushed stainless steel. Soft neutral medium silver-grey base, fine subtle horizontal brushed grain and a few faint hairline work scratches, restrained worn metal surface. No strong directionally lit bands. Useful for both horizontal worktops and vertical cabinet skins.
Composition: one continuous full-bleed material surface extending across all four edges, seamless repeat, square 1024 x 1024 image. No seams, no ribs, no panels, no rivets, no handles, no appliances, no objects, no perspective, no labels, no text, no logos.
Lighting: diffuse albedo only under perfectly even neutral lighting. No bright specular highlights, no reflection of the room, no directional shadow, no vignetting, no gradients, no embossed edges. The game shader supplies the gloss. No rust or heavy dirt, no chrome mirror surface, no extreme scuffs.

## Cloggers rebrand — current sign and menu

### V2 receiver-specific menus — 2026-09-06

| File | Native size | Runtime face |
| --- | ---: | --- |
| `menu-board-interior-a-v2-albedo.png` | 2048 x 512, RGBA opaque | Interior left, combos 1-4 |
| `menu-board-interior-b-v2-albedo.png` | 2048 x 512, RGBA opaque | Interior right, combos 5-7 |
| `menu-board-drive-through-v2-albedo.png` | 1024 x 1536, RGB opaque | Drive-through, three driver-readable featured combos |

These original fictional assets were made with built-in ImageGen. The wide
art was created as two matching 2:1 panels because the generator does not emit
4:1 directly; the panels were joined at native scale and uniformly reduced to
the authored 4:1 canvas with `tools/compose_menu_texture.swift`. The v2 files
keep exact Cloggers menu copy and do not replace the legacy shared texture.

Source outputs are retained under
`/Users/andrewbaker/.codex/generated_images/01a07491-5144-7963-85f9-a2aac292d30e/`.

The current interior menu-board texture is `assets/textures/world/quickbite/menu-board-combos-albedo.png`, a generated 2048 × 512 landscape board with seven combos. The same picture is used on two compact 4.8 × 1.2 m menu faces, mounted high on the kitchen partition behind the two registers. It replaces the three older hanging panels.

On 2026-09-04 the approved visible restaurant name changed to **CLOGGERS**. The brand and menu files listed above now contain Cloggers artwork, replacing the earlier Quickbite images. Internal asset paths and named geometry receivers remain unchanged. The original prompts and source records above describe the superseded first version.

The built-in imagegen tool edited each earlier original image as its only reference. Brand now reads **CLOGGERS** and **YOU'LL FEEL IT LATER.**; menu reads **CLOGGERS** and retains the three meals/prices and footer. Both outputs were visually inspected and copied intact without postprocessing. Brand remains 1881 × 836 (2.25:1), menu remains 1024 × 1536 (2:3); both opaque. No geometry or UV change is required.

Current generated outputs:
- Brand: `/Users/andrewbaker/.codex/generated_images/01a06c63-aff5-7e21-ba8d-7be4354c7e41/exec-ff3b25d4-a92e-43df-a6b9-753ba8a929db.png`
- Menu: `/Users/andrewbaker/.codex/generated_images/01a06c63-aff5-7e21-ba8d-7be4354c7e41/exec-5ed169ee-7a08-4b82-9320-8b4464b6d9de.png`

### Exact Cloggers brand edit prompt

```text
Edit the supplied original fictional restaurant brand texture for an approved rebrand. Replace every instance of Quickbite / GRILL with the new brand "CLOGGERS" in huge bold original chunky cream lettering, and below it the exact smaller slogan "YOU'LL FEEL IT LATER." Keep the deep brick-red enamel field, restrained golden-yellow line border, simple original gold burger icon, lightly worn late-20th-century fast-food print character. Design CLOGGERS to fill the wide composition cleanly and dominate, with the entire name fully inside margins. Preserve the input's exact 2.25:1 landscape aspect ratio, 1881x836 composition. Full-bleed FLAT orthographic diffuse game texture only: no mockup, frame geometry, building, poles, lighting glare, perspective, gradients, scene or real-brand logos. Remove Quickbite and GRILL completely. The only text is "CLOGGERS" and "YOU'LL FEEL IT LATER." Opaque rectangular red background to all corners.
```

### Exact Cloggers menu edit prompt

```text
Edit the supplied original fictional restaurant menu texture for its approved rebrand to CLOGGERS. Replace the entire Quickbite / GRILL top logo with the word "CLOGGERS" in large chunky cream italic lettering, matching the original red/cream/gold branding, and a small simple original gold burger icon. Remove all Quickbite and GRILL lettering completely. Preserve the portrait 2:3 aspect ratio, 1024x1536 composition, existing three food combo rows and exactly these meal names/prices: "CHEESEBURGER MEAL" "$4.95"; "CHICKEN MEAL" "$5.45"; "DOUBLE BURGER MEAL" "$5.95". Preserve footer text "FRIES + DRINK INCLUDED" and "ORDER HERE". Brand header should have a clear uncluttered hierarchy with CLOGGERS at its center and no extra slogan necessary on this menu. Keep appetizing period food photography, plain red cups/fries packaging, lightly aged warm cream paper, brick-red dividers and golden-yellow price circles. Flat full-bleed front-on orthographic diffuse game texture only; no physical board mockup, bezel, frame, pole, scene, building, perspective, directional lighting, backlight, watermark or actual real-world brands. Keep all type fully inside the image and readable.
```
