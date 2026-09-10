# Vault assets

The base set was generated with the built-in OpenAI imagegen tool on
2026-09-03; the superseded O'Haven door edit on 2026-09-05. Door and note
are reduced to 768x768; keypad to 512x512. Original outputs remain in the Codex
generated-images folder. These are diffuse artwork, not PBR map sets.
The physical note artwork matches `kBankVaultCode`; the readable game panel
uses that constant directly. Regenerate the artwork if the code changes.

## vault-door-face.png

Use case: stylized-concept. Asset: flat game texture for the face of a thick rectangular bank vault door. Square image, edge-to-edge satin brushed steel panel, late-1980s precision machining, concentric circular reinforcement centered in a rectangular bolted perimeter, restrained brass pinstripes matching a navy-and-gold civic bank. A small upper brass name plate reads exactly 'PINATTY'. Clean realistic micro-scratches and brushed grain, broad planar surface, no central handle or wheel (a real modeled wheel will be mounted there). Orthographic front elevation, no perspective, no surrounding wall, no frame outside canvas, no floor, no cast shadow, no room, no highlights baked from a scene, diffuse albedo only, no watermark.

## vault-door-face-ohaven.png (unused)

Generated with OpenAI built-in imagegen on 2026-09-05 by editing the inspected
vault face. Runtime output is 768x768.

**Superseded.** The city is Pinatty again, so the game loads the PINATTY
original above. This edit is kept only as a record of how it was made.

Use case: stylized-concept. Edit the supplied square bank vault-door face texture in place. Replace only the small brass nameplate text `PINATTY` with exact text `O'HAVEN`, retaining the apostrophe. Preserve the orthographic satin brushed-steel panel, concentric reinforcement, bolts, navy-and-gold pinstripes, restrained wear, and empty central mounting area. Add no handle, perspective, scene, watermark, or extra text.

## vault-office-note.png

Use case: stylized-concept. Asset: flat square paper-note texture for a readable game clue in a bank manager office. Cream paper fills entire square canvas, faint paper fibers and fold, tidy blue pen handwriting. Text verbatim in four lines: 'VAULT ACCESS' / '7491' / 'Close the door when finished.' / '- Manager'. The four digits 7491 are very large clear blue handwriting at the center. Only this text, no desk, no hands, no props, no scene, no perspective, no margins outside the paper, no cast shadows, flat evenly lit orthographic top-down diffuse artwork.

## vault-keypad.png

Use case: stylized-concept. Asset: square flat texture for a late-1980s bank vault code keypad. Brushed stainless steel fills canvas. Small black LCD at top says exactly 'ENTER CODE' in green. Below it a clean 3-column by 4-row numeric keypad with buttons '1 2 3', '4 5 6', '7 8 9', 'CLEAR 0 ENTER'. Tiny red and green indicator lenses near top. Photorealistic restrained finger wear. Perfect orthographic front view, flat evenly lit diffuse artwork, no perspective, no surrounding wall, no scene, no hands, no watermark, no extra text. Outer plate edge touches canvas edges.
