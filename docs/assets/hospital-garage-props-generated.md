# Hospital garage generated prop texture

Generated 2026-09-05 with the built-in `image_gen` tool through the `imagegen`
skill. No CLI, API-key workflow, input image, stock image, external logo, or
third-party artwork was used.

## Final project asset

- Path:
  `assets/textures/world/hospital/garage/entry-pay-station-control-face.png`
- Intended receiver: the named `hospital garage entry pay station control
  face`, a west-facing 0.60 m wide by 0.90 m high rectangle on the entry
  kiosk's upper body.
- Mapping: apply exactly once, full-frame, with the image's left/right edges
  mapped to the face's left/right edges and bottom/top mapped to bottom/top.
  Do not repeat, mirror, crop, atlas with unrelated materials, or use it on the
  kiosk sides, rear, or pedestal.
- File: 1024 x 1536, 8-bit RGB, opaque PNG.
- SHA-256:
  `d352e45db6370cede388324fe49ef81b3505595215533f930f1ed2eed0f51b88`
- Original built-in-tool output:
  `/Users/andrewbaker/.codex/generated_images/01a073cb-2671-7af1-9938-dca9fb5d8ebd/exec-7a16df36-7b5e-435d-9f16-2832b43c2ba3.png`

The original tool output was copied intact to the project path. No crop,
resize, recolor, compression pass, or other postprocessing was applied.

## Exact final prompt

```text
Use case: stylized-concept
Asset type: project-bound, non-seamless albedo texture mapped exactly once onto the named model face "hospital garage entry pay station control face", a 0.60 m wide by 0.90 m high rectangular UV face
Primary request: create one complete flat front control panel for a hospital parking-garage entry pay station, with a dark recessed display showing only a simple white parking-ticket pictogram, one horizontal ticket slot, one small contactless-card pictogram pad, one round amber help button, a compact speaker grille, four sparse corner fasteners, and restrained edge wear
Scene/backdrop: no scene and no backdrop; the rectangular control face itself fills the full image edge to edge
Subject: one single pay-station front face, all controls fully contained inside a continuous painted-steel perimeter bezel
Style/medium: low-resolution PSX-era game texture, chunky pixel clusters, hard-edged shapes, restrained 1990s civic hospital equipment detail, opaque albedo
Composition/framing: exact 2:3 portrait face, perfectly front-on orthographic, no perspective, no side faces, no outer margin; a continuous teal-painted metal boundary clearly defines all four image edges
Lighting/mood: neutral diffuse daylight with minimal baked lighting and no cast shadow
Color palette: muted hospital teal, warm grey, charcoal navy, off-white pictograms, one amber button, slight brown-grey grime
Materials/textures: powder-coated steel, dark glass display, rubberized reader pad, shallow scratches and hand wear kept away from the control symbols
Text (verbatim): ""
Constraints: one unique fitted prop face, non-seamless and non-tileable; exact 2:3 composition; opaque; no letters, numbers, words, logos, trademarks, or watermark; all interface controls must remain clear of the outer edge boundary
Avoid: generic repeated swatch, seamless pattern, multiple panels, product mockup, perspective, 3D box, background scene, photorealism, gradients, glowing bloom, readable text, branding
```

## Built-in tool use and inspection

- Mode: built-in `image_gen` generation, one call, not CLI fallback.
- Input role: no input images; this was a new generated raster.
- The source output was inspected at original detail before selection. It is an
  exact 2:3, front-on, single-face composition. The screen, ticket slot,
  contactless pad, amber button, speaker grille, and fasteners are fully inside
  a continuous teal boundary.
- No letters, numbers, brand marks, watermark, extra panel, perspective side,
  background scene, or tileable/repeated motif are visible.
- The project copy was reopened and visually inspected, then verified as a
  1024 x 1536 opaque RGB PNG and hashed after copying.

The display and button highlights are albedo marks. If the prop needs to read
at night, add a small real light or material emission in the runtime path; do
not treat this image as illumination.
