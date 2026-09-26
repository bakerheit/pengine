# Bellwether asset sources

Created September 26, 2026 for the gritty 1989 town direction.

## Reference art

The user supplied [the approved scene](../design/references/bellwether/approved-town.jpg).
OpenAI image generation used that image to create five separate reference sheets:

- [Water tower](../design/references/bellwether/water-tower.png): front, side, rear, three-quarter.
- [Church](../design/references/bellwether/church.png): front, side, rear, elevated three-quarter.
- [Utility poles](../design/references/bellwether/utility-poles.png): pole views and a cable span.
- [Trench coat character](../design/references/bellwether/detective.png): four views of one outfit.
- [Dusk sky](../design/references/bellwether/dusk-sky.png): west, north, east, south.

The subject prompts are retained in [reference-prompts.json](../design/references/bellwether/reference-prompts.json).
These images guide silhouettes, proportions, materials and color. They are not
runtime textures or automatically reconstructed meshes.

## Town models

`tools/make_bellwether_assets.py` authors original mesh topology, a deterministic
1024-square material atlas, collision boxes, ground support, lights and rain
covers. It uses Python 3, NumPy and Pillow. No downloaded model contributes to
the town geometry. The sign uses a locally available bold font with a fallback.

Tracked source atlas: `assets/textures/world/bellwether/town-atlas.png`.
Cooked outputs: `assets/models/world/bellwether/`. The repository ignores cooked
model folders; recreate these files with the generator before launching a new
checkout. `manifest.json` records counts, and `parts.txt` lists the named meshes.

## Character

`tools/make_trench_detective.py` authors the coat shell, split tails, sleeves,
lapels, tie, belt, storm flap and cloth texture patches. It reuses the existing
private PSX Character 01 head, hands, trousers, shoes and 28-bone skeleton.
The base source is the user's staged `Characters_psx_1.1.zip` pipeline, documented
in `assets/README.md`; its existing asset terms still apply. No broader license
is granted to that supplied content here.

The output keeps the existing skeleton and animation clips. It is skinning,
not cloth simulation. Runtime output and the atlas containing the private base
texture stay inside the ignored `assets/models/characters/psx_pack/` boundary.

```sh
python3 tools/make_bellwether_assets.py
python3 tools/make_trench_detective.py
```

The second command requires an already cooked
`assets/models/characters/psx_pack/player_male_01/`.

## Sky

The sky is original procedural shader code and a lighting preset. No generated
sky image is mapped onto the world. Its continuous direction coordinates make
the cloud pattern wrap as the camera turns. The preset holds 18:36, with navy
clouds, a muted western sunset strip and warm local fixtures.
