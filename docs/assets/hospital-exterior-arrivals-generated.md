# Hospital Exterior Arrivals Generated Asset

## Final asset

- Runtime path: `assets/textures/world/hospital/arrivals/emergency-shore-power-cabinet-face-generated.png`
- Generator: built-in `image_gen`
- Final built-in source:
  `/Users/andrewbaker/.codex/generated_images/01a073ee-247d-7ac3-a0b7-695845267e87/exec-ddeccd11-45eb-4dad-b04d-142c20cd9e51.png`
- First-pass built-in source:
  `/Users/andrewbaker/.codex/generated_images/01a073ee-247d-7ac3-a0b7-695845267e87/exec-ea63b77e-cec8-4625-be98-7ff987e22eb6.png`
- Format: PNG, opaque RGB
- Dimensions: 1024 x 1536 pixels, exact 2:3 portrait aspect
- SHA-256:
  `74b9e9b8a7d04dddd31e52d9d9ff495635cc356b5b707c5c3a4b9295c88aeee7`

The selected file was copied unchanged from the final built-in output. Both
passes were visually inspected at original resolution. The first pass had too
much product-render shading. The final pass keeps the control layout while
flattening it into crisp PSX-style albedo with no text or transparency.

## Initial generation prompt

```text
Use case: stylized-concept
Asset type: project-bound game prop texture for one hospital emergency-arrival control cabinet face
Primary request: Create an original front-facing texture for a rugged hospital ambulance-bay shore-power control cabinet, designed to fit exactly once on a 0.60 m wide by 0.90 m high rectangular receiver plane.
Scene/backdrop: the cabinet face fills the entire portrait canvas edge to edge; orthographic straight-on view; no surrounding wall or environment.
Subject: one teal-gray powder-coated metal cabinet front with a dark recessed upper breaker window, two distinct weatherproof circular power sockets with hinged covers, a small amber status lens, a red emergency isolation knob, slim gasket seam, corner fasteners, and light believable edge wear.
Style/medium: high-quality low-poly PSX-era game texture with crisp pixel-cluster detail and restrained material realism; authored prop face, not concept art.
Composition/framing: exact 2:3 portrait aspect ratio, perfectly centered and flat, all controls fully inside safe margins, no perspective distortion.
Lighting/mood: neutral even material reference lighting only.
Color palette: desaturated teal-gray metal, charcoal recesses, muted amber and safety red accents.
Materials/textures: powder-coated steel, rubber socket covers, small glass status lens, restrained chipped paint around physical contact points.
Constraints: no words, no letters, no numbers, no symbols, no logo, no trademark, no watermark; non-seamless; do not tile; no baked cast shadows, directional highlights, glow, reflections, or environmental lighting; no transparent background; opaque RGB output; no surrounding scene.
Avoid: photorealistic product shot, futuristic sci-fi controls, medical cross symbols, generic repeated swatch, duplicated controls, heavy grime.
```

## Corrective edit prompt

```text
Use case: precise-object-edit
Asset type: project-bound game prop texture for one hospital emergency-arrival control cabinet face
Input images: Image 1: edit target
Primary request: Change only the surface rendering into flat, neutral low-poly PSX-era albedo with crisp pixel-cluster detail. Remove all directional light, cast shadows, ambient occlusion, specular highlights, reflection, glow, and product-photography depth cues.
Constraints: preserve the exact portrait 2:3 composition, cabinet proportions, breaker window, six switches, two covered sockets, amber status lens, red isolation knob, gasket, fasteners, teal-gray palette, edge wear, and control placement; keep the cabinet face filling the canvas edge to edge; opaque RGB; no text, letters, numbers, symbols, logos, trademarks, watermarks, transparency, or surrounding scene; non-seamless and intended to map exactly once.
Avoid: baked fake lighting, gradients caused by lighting, dramatic shading, photorealistic product shot, added or removed controls, sci-fi redesign.
```

## Exact mapping contract

- Receiver name: `hospital arrival emergency shore power cabinet fitted face`
- Receiver centre: hospital-site local `(x = 84.5, z = -18.37)`
- Receiver bottom: `y = 0.57 m`
- Receiver size: `0.60 x 0.90 x 0.03 m`
- Orientation: local north-facing `-Z` cabinet face
- UVs: map the complete image exactly once over `[0,1] x [0,1]`
- Sampler: clamp edges
- Tint: white
- Do not repeat, crop, mirror, rotate, atlas-pack, or apply this image to any
  other shore-power cabinet.

The cabinet's amber lens is painted material information, not a baked glow or
an emission mask. Any actual status light and every canopy/wall light must be a
separate runtime light source. Visible emitter geometry in the sidecar is named
exactly `hospital arrival canopy light lens` for that parent-side routing.
