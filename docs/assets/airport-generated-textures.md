# Airport generated textures

The base directory and steel texture were created with the built-in imagegen
tool on 2026-09-04; the superseded O'Haven directory edit on 2026-09-05.
Original generated images are preserved in the local Codex generated-images
folder; project copies are the production inputs. Every production copy was
inspected before integration.

- `assets/textures/world/airport/terminal-directory-generated.png`: portrait 2:3 directory for the single `terminal directory sign face` at local (-48,149.583), facing +Z. Use the whole texture with white tint, no repeat. Its arrows agree with the site: arrivals west, departure hall forward, taxis/buses east. Generated on 2026-09-05 by editing the inspected Pinatty version and changing only the state name to `O'HAVEN`.
- `assets/textures/world/airport/furnishing-steel-generated.png`: neutral ribbed steel for pieces whose name begins `terminal furnishing `, including bench seats/backs, litter bins, luggage corral rails and trolley baskets. Use restrained repeating UVs and a neutral tint.

## Generation prompts

**Directory:** Use case: stylized-concept. Asset type: flat front-face raster texture for a wayfinding information kiosk in an original low-poly PSX-style 1990s airport game. Create ONE finished rectangular vertical airport directory sign graphic, aspect ratio 2:3, filling the entire image edge to edge, absolutely flat orthographic graphic only. Muted deep teal enamel background, warm ivory condensed sans lettering, restrained mustard yellow divider bands, slight age and discrete pixel-cluster wear like high-quality low-resolution baked game art. Exact text top to bottom: 'PINATTY' on top, 'INTERNATIONAL AIRPORT' below. Three generously separated direction rows with simple clear pictograms: 'ARRIVALS' with left arrow, 'DEPARTURES' with up arrow, 'TAXIS & BUSES' with right arrow. Small bottom footer: 'TERMINAL 1'. Professional legible transit wayfinding. Large type and simple symbols that read on a small in-game sign. No photograph, no 3D object, no frame, no stand, no scene, no floor, no perspective, no reflections of surroundings, no decorative text, no real logos. Render only the exact flat graphic that will be mapped onto a rectangular mesh.

**O'Haven directory edit (superseded — the city is Pinatty again, so the game loads the PINATTY original):** Edit the supplied airport directory texture in place. Replace only `PINATTY` with exact text `O'HAVEN`, retaining the apostrophe. Preserve the original layout, typography, arrows, pictograms, colors, weathering, pixel-art finish, 2:3 aspect ratio, and every other word exactly. Flat orthographic game texture only; add no objects or text.

**Steel:** Use case: stylized-concept. Asset type: seamless repeatable albedo texture for low-poly airport outdoor street furniture in a 1990s PSX-style game. Generate a square flat texture tile of muted silver-gray powder-coated ribbed steel. Broad shallow horizontal slats, evenly spaced, slight scuffs and dirt collected only in thin recessed seams, restrained pixel clusters and a small palette. Flat uniform lighting, no cast shadows, no highlights baked from directional studio lights. The texture will wrap onto bench seats, bench backs, luggage trolley corrals and the sides of litter bins. Neutral light gray desaturated steel, mildly warm. Seamless on all four edges. Orthographic surface only, no furniture object, no perspective, no text, no border, no logos, no environment. Aim a convincing authored retro game diffuse material that reads cleanly at 256px.
