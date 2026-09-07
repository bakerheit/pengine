# Second Chance Pawn merchandise

Created 2026-09-04 with the built-in imagegen tool and imagegen skill. Original fictional, unbranded secondhand merchandise materials; no reference photographs, external assets or actual manufacturer logos supplied. All PNGs were copied intact from the tool. No image editing or postprocessing was applied.

| File under assets/textures/world/neighborhood/ | Native size | Receiver and UV |
| --- | --- | --- |
| pawn-tv-front.png | 1448 × 1086, opaque RGB, 4:3 | `pawn television front face`, .90 × .675 m, front +Z, billboard quad, full image once |
| pawn-radio-front.png | 1697 × 927, opaque RGB, approximately 11:6 | `pawn radio front face`, .66 × .36 m, front +Z, billboard quad, full image once |
| pawn-guitar-soundboard.png | 1086 × 1448, opaque RGB, 3:4 | `pawn guitar body`, .39 × .52 × .10 m, custom extruded silhouette; front +Z uses UV(x+.5,y+.5) |

Use white albedo tint and no repeats on these receivers. TV/radio knobs, perforations, labels and surface wear are texture details on deep cabinets; portable-radio handles and CRT rear housings remain modeled. Guitars use `app/pawn_merchandise_mesh.h::make_pawn_guitar_body()`, an 80-triangle shaped solid with upper/lower bouts and waist. The image supplies only the soundboard material; geometry supplies the outline, thickness, neck, frets, strings, tuning pegs and wall hanger. The complete hanging instrument is approximately 1.06 m tall. Existing room layout and solid collision pieces are preserved.

Images visually checked: CRT shows beveled glass, controls and $49 label; radio shows speaker/cassette/tuner and $18 label; guitar soundboard has wood grain, soundhole rosette, pickguard, six strings and bridge. Tiny knob ticks are decorative, not a precise real product control diagram. Runtime orientation, lighting and distance readability still need the combined game check after root material hookup.

## Generated source outputs

- TV: `/Users/andrewbaker/.codex/generated_images/01a06c63-aff5-7e21-ba8d-7be4354c7e41/exec-2548d926-050d-4030-95b1-d88fbe2c0c04.png`
- Radio: `/Users/andrewbaker/.codex/generated_images/01a06c63-aff5-7e21-ba8d-7be4354c7e41/exec-10601013-cbef-437f-b53d-3815efef895e.png`
- Guitar: `/Users/andrewbaker/.codex/generated_images/01a06c63-aff5-7e21-ba8d-7be4354c7e41/exec-cbc33e5a-8ad9-4081-a243-99e5c8315bbd.png`

## Exact TV prompt

```text
Use case: stylized-concept. Create an ORIGINAL game diffuse texture for the front face of a secondhand late-1980s CRT television, no real brands. Landscape exactly4:3 composition. Full-bleed rectangular TELEVISION FRONT SURFACE ONLY, perfectly front-on orthographic without perspective or room. The 3D game supplies a real deep box cabinet, so show only the bezel/screen/control panel face. Broad worn warm-grey plastic bezel, a large curved dark blue-grey CRT glass screen occupying the left80 percent, with very restrained pale reflection gradient, faint scanlines, a tiny pale channel number "03" at upper-right of screen; screen is clearly bounded by thick rounded rectangular bevels. Narrow right-hand control column with two convincing round channel/volume knobs, little numbered ticks, a small power button, horizontal speaker perforation slots at bottom. A couple subtle handling scratches and a small aged white price label "$49" near lower-right; no other text or brand marks. Detailed readable PS2-era baked-diffuse appearance, muted practical charcoal/grey/ivory palette, no pitch-black featureless areas, no glowing modern widescreen, no view of side/top cabinet, no external border/margin, no showroom, no floor, no stand, no illustration frame, no watermark. The image should read as real molded TV surface detail mapped onto actual geometry, not a photo pasted on a box.
```

## Exact radio prompt

```text
Use case: stylized-concept. An ORIGINAL full-bleed front-surface diffuse texture for a compact secondhand 1980s portable cassette radio in a gritty stylized PS2-era game. Landscape 11:6 aspect ratio. Perfectly straight-on orthographic, no view of top or side, no room, no object mockup; geometry supplies the physical box. Dark warm-grey and aged silver plastic front panel. One large circular finely perforated speaker grille on the left, a cassette window and tape reels in the middle-right with small mechanical rectangular play/stop buttons beneath, a narrow AM/FM analogue tuner scale along the top-right, one small round volume knob. Believable fine molded-plastic seams, subtle scratched silver accents, clearly defined surfaces and readable light/dark contrast. Small white paper price sticker "$18" at bottom-right. Tiny labels "AM" "FM" "TAPE" only, no real brands or other logos. Restrained diffuse baked material detail with natural age, no luminous screen, no harsh specular reflections, no directional room shadow. Outer panel reaches every image edge; no handle sticking beyond the face, no margins, no background, no border around an illustration.
```

## Exact guitar soundboard prompt

```text
Use case: stylized-concept. Original diffuse/albedo TEXTURE MAP for the front wooden soundboard of an unbranded used acoustic guitar in a gritty PS2-era game. Portrait exactly3:4 composition. This is a rectangular MATERIAL PATCH, not a picture of a guitar. Warm honey spruce wood grain runs vertically and fills EVERY corner and edge completely. The game mesh supplies the guitar body outline and thickness, so DO NOT draw an outer guitar silhouette, perimeter border, surrounding background, neck, headstock, strings beyond the patch, guitar stand, room, hands or whole instrument. In the rectangle, put one round dark sound hole centered horizontally at x50%, y34% from top; outer diameter roughly28% of image width, bordered by a thin precise cream/dark concentric rosette. A modest dark tortoiseshell pickguard is just to the right and below the hole. A dark rosewood horizontal bridge at x50%, y73% from top, width38% of image, with thin cream saddle and six tiny light bridge pins. Six extremely thin taut silver strings run vertically near the middle from top edge to bridge, with realistic close spacing. Subtle used varnish grain, minor honest pick scratches around soundhole, lightly warm aged wood, clear surface detail under perfectly even diffuse lighting. No bright glare, no cast shadow, no vignette, no text, no brand, no watermark, no photography of an object against a background. Full-bleed opaque rectangular wood surface map whose corners remain natural spruce grain.
```
