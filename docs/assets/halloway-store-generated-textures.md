# Halloway Gas convenience-store textures

Generated on 2026-09-04 using the built-in imagegen tool. Original fictional retail packaging; no input images or external source artwork were supplied. Each tool output was copied intact into the game assets. No image postprocessing was applied.

| Asset | Native size | Receiver | Mapping |
| --- | --- | --- | --- |
| `assets/textures/world/gas_station/snacks-shelf-albedo.png` | 1774 × 887, opaque RGB, 2:1 | `store interior snack sign face` | Full image once per 3 × 1.5 m panel, white albedo tint; two panels along each 6 m gondola side. Faces yaw 90/+X and yaw 270/-X. |
| `assets/textures/world/gas_station/chilled-drinks-albedo.png` | 1024 × 1536, opaque RGB, 2:3 | `store interior cooler sign face` | Full image once per 1.2 × 1.8 m cooler bay, white albedo tint; six panels facing yaw 0/+Z. |

Both images were visually inspected for framing, shelf organization, original packaging and useful game-distance readability. Metadata confirms the target aspect ratios and absence of alpha. Tiny generated price-tag text is decorative, not guaranteed readable copy. Cooler artwork depicts the stocked interior; its opaque image does not supply transparent glass, door hardware or lighting. Runtime material wiring and in-game visual checks are handled separately.

Reuse existing Quickbite linoleum and brushed stainless surfaces for the store floor and metal fixtures where appropriate.

## Snack shelves

Original generated output:
`/Users/andrewbaker/.codex/generated_images/01a06c63-aff5-7e21-ba8d-7be4354c7e41/exec-6af0ade8-9dcd-495b-8569-5884dbad51f1.png`

Exact submitted prompt:

```text
Use case: stylized-concept.
Asset type: stocked convenience-store shelving diffuse texture for Halloway Gas, a fictional modest late-20th-century neighborhood gas-station shop in a gritty stylized PS2-era driving game.
Primary request: one front-facing rectangular shelf merchandising texture, landscape 2:1 aspect ratio. Four clean horizontal rows of neatly stocked colorful fictional snacks. Top row: upright red, yellow and teal crisp bags with a simple potato-chip illustration and invented tiny label "CRUNCH". Second row: small cracker boxes and wrapped biscuit packs in warm cream, orange and forest green, invented label "SNAPS". Third row: compact candy bars and bright sealed sweet packets, invented label "ZING". Bottom row: pretzel and nut pouches, mostly tan, deep blue and red with generic snack illustrations. Believable varied packaging sizes and sensible grouping. All brands original; no actual recognizable product logos.
Each row occupies exactly one quarter of the image. Very thin light-grey horizontal steel shelf lips divide the four rows, with restrained small white price-tag rectangles. Align rows evenly and keep merchandise squarely facing the viewer. The top row begins just inside the top edge; the bottom shelf lip reaches the bottom edge. Dark neutral shelf backing visible only in small gaps between packs. Pack fronts occupy most of the texture. No exterior shelving frame or side supports: geometry supplies those.
Style: detailed but readable baked diffuse material, lightly aged packaging and utilitarian retail feel, colorful clean stock, natural small irregularities. Flat orthographic front view with essentially no perspective and no view of shelf sides; controlled shallow product depth only. No shop scene, no people, no countertop, no floor, no room, no external margin. Neutral even light, no directional cast shadows or bright highlights. No big title, no store logo, no watermarks. Original full-bleed raster texture ready to map once across a shelf face.
```

## Chilled drinks

Original generated output:
`/Users/andrewbaker/.codex/generated_images/01a06c63-aff5-7e21-ba8d-7be4354c7e41/exec-50d95c4c-9f73-48f3-be47-0d90d285513a.png`

Exact submitted prompt:

```text
Use case: stylized-concept.
Asset type: chilled-drink stock diffuse texture for one convenience-store cooler door bay at fictional Halloway Gas, a modest late-20th-century neighborhood shop in a gritty stylized PS2-era driving game.
Primary request: a perfectly front-facing rectangular stocked cooler-interior image, portrait 2:3 aspect ratio, 1024 x 1536 composition, to be displayed once behind a 1.2m wide and 1.8m tall glass door. Four even rows of neatly stocked original fictional non-alcoholic bottled and canned drinks. Top row: clear water bottles and green-tinted lemon drink bottles. Second row: red cola cans and orange soda cans. Third row: orange-juice and apple-juice bottles. Bottom row: small white milk cartons and cream-colored iced-coffee cartons. Believable bottle and can shapes, sensible size differences and small irregular spacing; full, maintained neighborhood store stock.
Packaging uses generic readable category words only: "WATER", "LEMON", "COLA", "ORANGE", "APPLE", "MILK", "COFFEE". Original label designs, no actual brand names, logos, or familiar trade dress. White, muted red, forest-green, pale-blue and orange packaging in a practical 1980s/1990s style.
Composition: four slim white metal shelf lips, small plain price tags, pale neutral cooler backing, product fronts fill most of the frame with shallow dark gaps. Camera square to the shelf, orthographic, no view of cabinet sides. One stocked bay only; no outer door frame, handles, glass glare, condensation, neon or light strips, store scene, people, floor, wall, external margin or watermark. The game provides the door, hardware, glass and lighting.
Lighting: even neutral diffuse/albedo with a restrained cool indoor tone, no directional cast shadows, no glossy reflected room, no baked glow or bright specular glare. Detailed but readable at game distance, believable lightly worn packaging with clean stock.
```
