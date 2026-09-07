# PSX Houses 100 textures

Houses 101, 104, and 106 on Sycamore Loop use repeating wall and roof textures
from the local `PSX_Houses_100` pack by **postdev**. The supplied `License.txt`
permits use, modification, and commercial or non-commercial games, but
prohibits reselling, redistributing, or repackaging the asset in whole or in
part.

The vendor source stays outside this repository at `../resources/PSX_Houses_100`.
Copied runtime textures stay in the ignored private-asset tree at
`assets/models/world/psx_house_textures/`.

Run this from the repository root to regenerate the selected runtime set:

```sh
python3 tools/stage_psx_house_textures.py
```

Selection is declared in `src/city/residential_neighborhood.h`. The imported
maps affect only authored exterior wall and pitched-roof pieces. Original
geometry, doors, windows, interiors, collision, paving, props, and trim remain
unchanged. House 102 is deliberately untouched because it is the enterable home.
