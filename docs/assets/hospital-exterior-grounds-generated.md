# Hospital exterior grounds generated asset

## Selected runtime asset

- Workspace file:
  `assets/textures/world/hospital/grounds/healing-garden-botanical-mosaic-face-generated.png`
- Built-in generation source:
  `/Users/andrewbaker/.codex/generated_images/01a073ee-262b-7b71-9100-a1eb891ea2ca/exec-f836b471-6e7b-4da9-8cd4-c57f16773757.png`
- Mode: built-in `image_gen` generate followed by one built-in
  `image_gen` precise-object edit
- Format: PNG, RGB, no alpha
- Dimensions: 1536 x 1024 pixels (3:2)
- SHA-256:
  `af2157bc72d2f86514f21a387cdb42a56bd10cbcd7fa364c8f92f90b618ee4e0`
- Visual inspection: passed. The final is front-on and fully opaque, contains
  no text/logos/watermarks, retains the botanical mosaic composition, and
  removes the first draft's directional highlights, cast shadows, bevel
  shading, and mockup-like depth.

The built-in source remains in Codex generated storage. The selected output was
copied unchanged into the project asset path above.

## Initial generation prompt

```text
Use case: stylized-concept
Asset type: project-bound game environment texture for one unique hospital courtyard receiver plane
Primary request: create one original non-seamless decorative relief panel artwork for the quiet healing garden at Vellum Regional Hospital
Subject: an abstract botanical composition of layered ginkgo-like leaves, gentle water ripples, and rounded river-stone forms; no literal scene and no text
Style/medium: refined low-poly relief and glazed ceramic mosaic translated into chunky PSX-era pixel clusters, restrained high-end civic architecture craft, original design
Composition/framing: exact 3:2 landscape canvas, perfectly front-on orthographic flat panel face, full-bleed intentional composition with a thin dark teal perimeter border; designed to map exactly once onto a 3.6 metre wide by 2.4 metre tall vertical plane
Color palette: muted hospital teal, sage green, warm ivory, dusty terracotta, charcoal grout
Materials/textures: readable ceramic tesserae and shallow relief facets, controlled material breakup, no photographic noise
Lighting/mood: even neutral albedo-reference illumination only
Constraints: no words, letters, numbers, logos, trademarks, symbols, watermark, people, mockup wall, room, frame hardware, perspective, depth background, repeating pattern, seamless tiling, directional shadows, highlights, emissive glow, or baked lighting; keep edges clean and composition legible at low resolution
```

The first draft was inspected at original resolution. Its composition and text
constraints were correct, but the relief had strong baked highlights and cast
shadows. It was rejected as runtime albedo and used only as the edit target.

## Corrective edit prompt

```text
Use case: precise-object-edit
Asset type: project-bound albedo texture for one 3.6 x 2.4 metre hospital courtyard panel
Input images: Image 1 is the selected decorative botanical mosaic design and edit target
Primary request: flatten only the surface illumination so this becomes clean neutral albedo artwork suitable for real-time game lighting
Constraints: preserve the exact 3:2 composition, ginkgo leaves, water ripples, river-stone forms, colors, dark teal perimeter border, tile outlines, and full-bleed layout; remove directional highlights, cast shadows, ambient occlusion, bevel shading, glossy reflections, and apparent depth; render every colored shape as a mostly flat matte color with only tiny hand-made color variation inside tiles; perfectly front-on orthographic; no text, letters, numbers, logos, symbols, watermark, wall mockup, perspective, repeating pattern, or seamless tiling; do not add or remove motifs
```

## Exact receiver mapping

- Receiver name: `hospital grounds healing mosaic face`
- Receiver location: hospital-site local `(128.0, 40.50)`
- Receiver bottom: `0.44 m`
- Receiver dimensions: `3.60 m wide x 2.40 m high x 0.05 m deep`
- Aspect ratio: exact 3:2 image on exact 3:2 face
- UV contract: `(1, 1)`, fitted once, no tiling and no cropping
- Usage count: exactly one receiver plane in
  `bake_hospital_exterior_grounds()`
- Other sides/backing: normal steel/concrete project materials; never sample
  this image for a generic courtyard swatch
- Lighting: albedo only. Do not use the image as emissive output and do not
  treat its painted color changes as scene lighting.
