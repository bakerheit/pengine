# Hospital emergency generated prop texture

Created on 2026-09-05 with the built-in `image_gen.imagegen` tool through the
`imagegen` skill. This is original fictional hospital artwork. No reference
image, external asset, brand, or real hospital logo was supplied.

## Selected project asset

- Project copy:
  `assets/textures/world/hospital/emergency/ambulance-bay-2-marker-face-generated.png`
- Built-in generated source:
  `/Users/andrewbaker/.codex/generated_images/01a073cb-26f5-72b2-a400-4e0cae7cb717/exec-ba3c2335-aed9-4123-b59c-81a8fe4d77f0.png`
- Native file: 1254 x 1254, opaque RGB PNG.
- SHA-256:
  `0995f9a2dfa56e5ed5f3dbf3482904dfdd8ce0dec09629118eeb1ce83aa3af4a`
- Post-processing: none. The selected built-in output was copied intact into
  the project and the original remains in the Codex generated-images folder.

## Exact receiver fit

Map the whole image once, UV 0-1, with white tint onto the north-facing square
quad named `hospital emergency bay-two marker face`. The face is 1.20 x 1.20 m
and sits on a separate 1.30 x 1.30 x 0.10 m backing at ambulance Bay 2. Do not
tile, crop, atlas, stretch, or route this texture to generic hospital signs.
The printed border is the visible edge boundary of this exact face.

The PNG is albedo only. It was composed with high-contrast white/red regions so
an integration owner can add a separate emissive treatment, but the file has
no alpha or emission channel. A real nearby runtime light is still required to
light the pavement and player.

## Inspection

The built-in result was opened at original detail before selection. It has a
complete dark-red perimeter, thin warm-white inset keyline, centered giant
`ER`, and separate `AMBULANCE` / `BAY 2` lines. All requested copy is present
once and spelled correctly. There are no extra words, arrows, logos, medical
crosses, vehicles, scene elements, watermarks, external margins, or perspective.
The outer wear stays near the border and the main lettering remains readable at
small display size. The copied project PNG was checked again for dimensions,
format, alpha state, and SHA-256.

## Exact final prompt

```text
Use case: stylized-concept
Asset type: non-seamless fitted albedo and emissive-ready front-face texture for exactly one named low-poly game prop, "hospital emergency bay-two marker face", a 1.20 by 1.20 metre square lightbox panel
Primary request: create one complete square hospital emergency ambulance-bay marker face. A dominant deep emergency-red upper field carries huge warm-white letters "ER". A warm-white lower band carries two centered dark-red lines, "AMBULANCE" and "BAY 2". The sign is original and unbranded.
Scene/backdrop: no scene or backdrop; only the flat finished sign face fills the image edge to edge
Subject: the single square sign graphic, with a thick dark-red outer enamel border and a thin warm-white inset keyline that make all four physical face edges unmistakable
Style/medium: original late-1990s PSX-style baked game texture; crisp blocky sans-serif lettering; restrained chunky pixel clusters; tiny enamel chips and light grime only at the outer border
Composition/framing: exact 1:1 square; perfectly straight-on orthographic; full bleed; centered stacked hierarchy; broad safe padding; non-tileable; intended to map exactly once across one square prop face
Lighting/mood: flat neutral diffuse artwork with no cast shadow, no perspective, no baked glow, and no directional highlight; the runtime light will provide illumination
Color palette: deep emergency red, warm hospital white, very dark burgundy, tiny muted gray wear
Materials/textures: screen-printed enamel face behind glass, restrained age at the perimeter, clean readable central field
Text (verbatim): "ER", "AMBULANCE", "BAY 2"
Constraints: render each exact text string once and only once; spell E-R and A-M-B-U-L-A-N-C-E correctly; preserve the numeral 2; clear hard rectangular edge boundaries on all four sides; opaque square image; no external margin; no arrows; no symbols; no logo; no watermark
Avoid: seamless pattern, repeated swatch, generic wall material, whole sign object, side thickness, mounting post, room, street, vehicle, people, perspective, reflections of surroundings, glowing halo, extra letters, extra numbers, extra words, medical cross
```

## Tool-use record

The built-in tool was called in generation mode with the exact prompt above,
without input images, `referenced_image_paths`, or conversation image inputs.
No CLI/API fallback, model override, mask, destination-path argument, or image
editing tool was used. The built-in tool saved the original under
`/Users/andrewbaker/.codex/generated_images/`; the selected PNG was then copied
to the project path listed above, as required for a project-bound asset.
