Use case: precise-object-edit
Asset type: exact UV/component atlas for a PSX / early-PS2 vehicle model
Input image: the supplied square 4x atlas is the edit target, derived from the final Municipal Cruiser 91-B mesh
Primary request: replace only the flat fills and guide marks inside every existing receiver cell with a finished late-1990s console texture treatment for an original 1991 American police sedan
Style/medium: hard-edged hand-painted game texture; deliberate 4-12 pixel clusters at this 4x working size; limited-value ramps; restrained baked facet shading; no smooth gradients and no photoreal noise
Color palette: warm off-white patrol paint; deep navy lower doors and belt stripe; smoke-blue glass; charcoal rubber and cabin; dull grey metal; pale headlamps; amber signals; red tail lamps; distinct red and blue lightbar lens material
Materials/textures: subtle panel value changes, small chipped edge clusters, light road grime low on the body, restrained glass reflections, ribbed rubber, brushed dull metal, and prismatic lamp clusters
Composition: keep the exact square canvas, exact cell positions, exact cell sizes, exact cell boundaries, gutters, and UV receiver layout from the input; fill each component in place; this is a flat texture atlas only
Constraints: remove every magenta label and every green UV guide line while preserving all boundaries and receivers; keep edge padding clean; make the body paint and navy livery readable at gameplay distance
Avoid: car render, vehicle silhouette, wheels, tires, ground, scenery, shadows outside cells, labels, badges, letters, words, numbers, logos, brands, emblems, watermark, UI, mockup, perspective, rotated or rearranged cells

The final deterministic cook adds exact generic POLICE lettering and locks the critical lamp and lightbar texels after reduction. Do not add any text or insignia in this edit.
