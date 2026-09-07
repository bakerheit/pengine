# Hospital north parking generated texture

## Runtime asset

- File: `assets/textures/world/hospital/parking/north-visitor-wayfinding-generated.png`
- Receiver: `hospital north parking wayfinding fitted face`
- Size: 1536 x 1024 RGB PNG, opaque
- SHA-256: `e4f539d00ad3b5c57b320d0f3d5e0d63bcccb86570ce48aae4ed10663550ec3b`
- Mapping: full image once, white tint, no repeat, crop, mirror, or atlas reuse
- Generator: built-in ImageGen tool

## Final prompt

```text
Use case: stylized-concept
Asset type: fitted game texture for one roadside hospital wayfinding sign face
Primary request: create a single flat, front-facing rectangular hospital wayfinding sign panel for Vellum Regional Hospital, designed as a readable low-resolution PSX-era open-world game texture
Scene/backdrop: the sign panel fills the entire image edge to edge; no environment, no wall, no sky, no mounting posts
Style/medium: flat albedo texture, crisp hand-painted game texture, slightly weathered teal painted metal with an off-white inner border and restrained red emergency accent
Composition/framing: landscape 3:2 panel, perfectly orthographic and head-on, generous margins, three centered text rows with simple directional arrows
Text (verbatim): "VELLUM REGIONAL HOSPITAL" on row 1, "VISITOR PARKING ↑" on row 2, "AMBULANCES →" on row 3
Materials/textures: subtle chips only at outer edges, light grime near lower edge, otherwise clean and legible
Constraints: render every letter exactly once and exactly as provided; large uppercase sans-serif lettering; flat even lighting baked into no part of the image; no perspective; no cast shadows; no reflections; no logos; no medical cross; no extra symbols beyond the two arrows; no watermark
Avoid: photographed sign, sign mockup, background scene, repeated pattern, tiny type, misspelled text, extra words, dramatic lighting
```

The generated output was visually checked before integration. All three text
rows are exact and appear once. The arrow glyphs point visitor parking forward
and ambulances right, with no extra logo, medical cross, watermark, or scene
lighting baked into the albedo.
