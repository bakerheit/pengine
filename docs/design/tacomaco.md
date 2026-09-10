# TacoMaco — Fourth Street

TacoMaco now uses the full furnished BurgerPiz building on its existing Pinatty
parcel: world centre **189.053, 128.367**, Pinatty grid east **136**, south **155**,
ground **12 m**. It faces north toward **Fourth Street (road 33)**. Earlier notes
incorrectly placed this parcel between Sixth and Seventh Street.

The imported shell is rotated 180 degrees to keep the existing street-facing
entrance. Its fixed **60 × 38 m** parking lot has one measured six-metre driveway.
The old Cloggers-derived shell, extra driveway cuts, push-door colliders,
branding materials and ceiling lights are removed. The replacement uses the
original imported open entrance leaves, furnished dining room and kitchen,
816 collision boxes and 37 ceiling lights. Its 59 material groups contain
52,720 triangles, including new extruded TacoMaco lettering.

Lime-green roof and upholstery, burnt-orange accents, cream walls and illuminated
cream signs make this an independent brand. Both menu boards use the new eight-meal
atlas: Street Tacos, Fire Chicken, Big Burrito, Loaded Nachos, Green Machine,
Quesadilla, Taco Trio and Churro Time. Prices range from $5 to $11. Image-generation
prompts and saved textures are in [the asset record](../assets/restaurant-rebrand-menus.md).

The map footprint, restaurant label, safe dev-menu teleport, interior bounds,
terrain/scatter exclusion and rear-wall graffiti follow the replacement shell.
BurgerPiz remains unchanged on Ninth Street. The former TacoTaco on Sixth Street
is now [Freaky Franks](freaky-franks-pinatty.md).

## Reproduce

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b -t 4 \
  --python tools/cook_burgerpiz.py -- \
  assets/models/buildings/burgerpiz/source/BurgerPiz/Models/BurgerPiz.glb \
  --variant=tacomaco
```

The cooker copies the tracked `assets/textures/world/tacomaco/menu-atlas.png`
into the private `assets/models/buildings/tacomaco/` asset set.

## Validation

- `burgerpiz_tests --require-assets`: all three restaurant variants load; actual
  character movement reaches the interior and counter and returns to the street;
  counter and sealed-door controls block movement; collision and lighting stay
  identical between variants.
- `tacomaco_tests`: Fourth Street access, north-facing entrance, fixed imported
  lot, no old shell or obsolete driveway registrations.
- `start_area_tests`, `building_access_tests`, `authored_city_layout_tests`: pass.
- Rebuilt game: 300-frame day exterior and night interior captures, clean GL queues.
- Evidence: `build/tacomaco-final-corner.png`, `build/tacomaco-new-counter.png`.

Parking-lamp update (2026-09-09): both heads now glow and cast real night light,
using the same dusk fade, range and distance cutoff as street lamps. See
[asset and validation notes](../assets/burgerpiz-parking-lamps.md).
