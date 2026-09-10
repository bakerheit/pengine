# Freaky Franks — Sixth Street

Freaky Franks replaces TacoTaco at the same Pinatty location, world centre
**399.957, -98.832**, ground **12 m**, on **Sixth Street (road 36)** between Wren
and Cinder. It retains the furnished BurgerPiz shell and fixed **60 × 38 m** lot
with a measured six-metre driveway.

The new palette combines a blueberry-blue roof, strawberry-pink trim and walls,
berry upholstery, and warm cream illuminated **Freaky Franks** lettering outside
and over the counter. The new eight-item menu puts fruit and sweet toppings on
visible grilled sausages in split hotdog buns:

| Meal | Toppings | Price |
| --- | --- | --- |
| Strawberry Freak | Strawberries, chocolate syrup, white chocolate curls | $8 |
| Blueberry Blast | Blueberries, blueberry glaze, cream drizzle | $8 |
| Berry Bad Boy | Strawberries, blueberries, chocolate syrup | $9 |
| Pineapple Panic | Grilled pineapple, chili flakes, sweet glaze | $8 |
| Mango Mayhem | Mango, lime zest, chili | $8 |
| Banana Bonkers | Banana, chocolate syrup, peanuts | $9 |
| Cherry Chaos | Cherries, dark chocolate, cookie crumbs | $9 |
| Total Fruitcake | Mixed fruit and chocolate syrup | $11 |

The new atlas is used on the original hanging and portrait menu boards. Exact
prompts and saved assets are in [the asset record](../assets/restaurant-rebrand-menus.md).
The map now shows Freaky Franks with a pink restaurant marker.

## Reproduce

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b -t 4 \
  --python tools/cook_burgerpiz.py -- \
  assets/models/buildings/burgerpiz/source/BurgerPiz/Models/BurgerPiz.glb \
  --variant=freakyfranks
```

The cooker copies the tracked `assets/textures/world/freakyfranks/menu-atlas.png`
into the private `assets/models/buildings/freakyfranks/` asset set. Original
BurgerPiz and historical TacoTaco cooked assets remain untouched. The new brand
has 59,096 triangles in 59 material groups, including replacement lettering;
it retains the source's 816 collision boxes and 37 ceiling lights. The previous
early opaque kitchen rendering and tighter tiled-light bounds remain active.

## Validation

`burgerpiz_tests --require-assets` passes real character entry, dining-room and
counter traversal, street return, blocked-route controls, asset loading and
identical collision/light checks for all three restaurant brands. TacoMaco,
start-area, building-access and authored-city-layout tests also pass.

Day exterior and night counter captures run for 300 frames with clean GL queues.
Evidence: `build/freakyfranks-final-corner.png`, `build/freakyfranks-counter.png`.

Parking-lamp update (2026-09-09): both heads now glow and cast real night light,
using the same dusk fade, range and distance cutoff as street lamps. See
[asset and validation notes](../assets/burgerpiz-parking-lamps.md).
