# Vellum hospital generated facade asset

Created 2026-09-05 with the built-in `image_gen` tool through the `imagegen`
skill. This was a new generation with no input or reference images. The source
was visually inspected, then copied intact into the project without cropping,
resampling, color changes, or other post-processing.

## Output and intended receiver

- Project asset:
  `assets/textures/world/hospital/facade/vellum-main-entry-mural-face-generated.png`
- Built-in source output:
  `/Users/andrewbaker/.codex/generated_images/01a073cb-2518-78b3-83ba-a17b304e5573/exec-7a553a1b-0fc3-49a0-9a85-e08419887ba2.png`
- File: 1536 x 1024, 8-bit RGB PNG, opaque, 3:2 aspect ratio.
- SHA-256 for both source and project copy:
  `28f41c54e65c3f2cba35346436f3b8e8b712ba377294a297a33e5aa81abfe7ee`
- Receiver: the single proposed `hospital main entry mural face` plane at
  site-local centre `(-13.0, -19.28)`, facing local `-Z`, 9.0 m wide by 6.0 m
  high, bottom at local `y = 4.80 m`.
- Mapping: full image once over the full plane, clamped at the edges with white
  tint. It is not a seamless, repeated, mirrored, or generic facade material.

The output was checked at original resolution. The full rectangular teal edge
is present, the stepped beacon is centred, the copy reads exactly
`VELLUM REGIONAL HOSPITAL` once, and there are no added words, real brands,
watermarks, people, vehicles, facade mockups, or red-cross emblems. The broad
shapes and hard edges remain legible as a PSX-scale game texture. The panel
joints and light wear stay inside this one authored mural; they are not meant
to establish a repeatable wall pattern.

## Exact final prompt

```text
Use case: stylized-concept
Asset type: project-bound fitted facade texture for the single authored plane named "hospital main entry mural face", a 9.0 m wide by 6.0 m high north-facing plane on Vellum Regional Hospital
Primary request: create one straight-on orthographic opaque diffuse/albedo artwork for the hospital's main-entry landmark panel, not a scene or architectural mockup
Subject: one self-contained rectangular civic hospital mural with a broad warm-ivory field, a thick deep-teal perimeter border, an original geometric teal beacon motif built from chunky stepped forms in the upper half, and a strong lower identity band
Style/medium: late-1990s PSX-era environment texture; deliberate large pixel clusters, hard low-resolution edges, flat graphic shapes, restrained baked ceramic wear, readable from driving distance
Composition/framing: exact 3:2 landscape rectangle; show the complete face edge-to-edge; keep every important mark inside a clean 3 percent inset border; centered, balanced, no cropping
Lighting/mood: neutral flat albedo lighting, no cast shadows, no fake shine, civic and calm
Color palette: warm ivory, deep hospital teal, charcoal lettering, one very small muted red accent
Materials/textures: subtly aged glazed ceramic/enamel panel with a few broad panel joints and sparse edge grime; this is one fitted artwork, not a repeating material
Text (verbatim): "VELLUM REGIONAL HOSPITAL"
Constraints: render the exact text once in bold condensed uppercase letters with clean spacing; one original non-trademark geometric emblem only; opaque background; crisp rectangular outer boundary; no perspective; no depth; no facade, doors, windows, canopy, wall, frame, sky, street, people, vehicles, objects, mounting hardware, watermark, or extra words
Avoid: seamless or tileable patterns; repeated logos; red-cross symbols; real hospital branding; tiny details; gradients; photorealism
```

No CLI, API key, fallback model, stock asset, or external source was used.
