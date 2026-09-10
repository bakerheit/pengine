# TacoMaco and Freaky Franks menu art

Created 2026-09-08 using the built-in image generation tool. Each accepted output is copied unchanged into the tracked texture tree and copied into the private cooked model folder by `tools/cook_burgerpiz.py`. No manual painting or post-generation menu edits.

## Saved assets

- TacoMaco source/runtime atlas: `assets/textures/world/tacomaco/menu-atlas.png`
- Freaky Franks source/runtime atlas: `assets/textures/world/freakyfranks/menu-atlas.png`
- Cooked copies: `assets/models/buildings/<variant>/menu_atlas.png`
- Both accepted images: 1415 × 1111 PNG, two columns and four rows, fitted to the existing BurgerPiz menu-board UVs.

Original TacoMaco output: `/Users/andrewbaker/.codex/generated_images/01a07eb6-1568-76f0-ba3b-6d5068f06845/exec-6f65ee58-3161-46e4-be69-fae9edf0ac4d.png`

Original Freaky Franks output: `/Users/andrewbaker/.codex/generated_images/01a07eb6-1568-76f0-ba3b-6d5068f06845/exec-647c73b5-6f33-489f-8c3f-febe16e179de.png`

## Exact TacoMaco prompt

Use case: ads-marketing. Create a production menu texture atlas for TacoMaco, a fictional taco restaurant in a low-poly 3D game. Landscape aspect ratio exactly 512:402, about 1.274:1. Precise grid TWO equal columns and FOUR equal rows, eight rectangular cells, edge-to-edge without gutters or outside frame. Each cell uses a warm cream background, small lime-green and burnt-orange accents, appetizing clean studio food photography, and ONE bold easy-to-read short menu name with price along its bottom edge. Keep all food and text strictly within its cell with generous margins. Row 1 left: three carne asada street tacos with cilantro and lime, exact text "STREET TACOS $8"; right: spicy chicken tacos with orange salsa, exact text "FIRE CHICKEN $8". Row 2 left: sliced grilled beef burrito, exact text "BIG BURRITO $10"; right: loaded tortilla nachos with melted cheese, beans, guacamole and pico, exact text "LOADED NACHOS $9". Row 3 left: colorful avocado black bean tacos, exact text "GREEN MACHINE $8"; right: golden quesadilla wedges with salsa, exact text "QUESADILLA $8". Row 4 left: a crunchy taco combo with rice and a plain unbranded soda cup, exact text "TACO TRIO $11"; right: cinnamon churros with chocolate dip, exact text "CHURRO TIME $5". Consistent photography and bold typography throughout. NO burgers, NO hotdogs, no extra text, logos or watermarks. This is a flat texture atlas, not a photograph of a menu on a wall.

## Exact Freaky Franks prompt

Use case: ads-marketing. Create a production menu texture atlas for Freaky Franks, a fictional restaurant specializing in bizarre fruit-covered HOTDOGS in a 3D game. Landscape aspect ratio exactly 512:402, about 1.274:1. Precise grid TWO equal columns and FOUR equal rows, eight equal wide rectangular cells, edge-to-edge with no gutters or outside frame. Warm ivory background per cell, strawberry-pink and blueberry-blue small paper accents. Clean appetizing studio food photographs, slightly outrageous, colorful and believable. Every meal MUST visibly contain a grilled sausage frankfurter inside a classic split hotdog bun, with the sausage clearly visible under the toppings. Each cell has one bold easy-to-read short name and price along its bottom edge; food and lettering stay strictly inside each cell with margins. Row 1 left: hotdog topped with fresh sliced strawberries, rich chocolate syrup drizzle and tiny white chocolate curls, exact text "STRAWBERRY FREAK $8"; right: hotdog topped with whole fresh blueberries, blueberry glaze and thin cream drizzle, exact text "BLUEBERRY BLAST $8". Row 2 left: hotdog with strawberries, blueberries and chocolate syrup, exact text "BERRY BAD BOY $9"; right: hotdog with grilled pineapple chunks, chili flakes and caramel-colored glaze, exact text "PINEAPPLE PANIC $8". Row 3 left: hotdog with mango cubes, lime zest and chili seasoning, exact text "MANGO MAYHEM $8"; right: hotdog with banana slices, chocolate syrup and chopped peanuts, exact text "BANANA BONKERS $9". Row 4 left: hotdog with cherries, dark chocolate drizzle and cookie crumbs, exact text "CHERRY CHAOS $9"; right: an overloaded hotdog with strawberries, blueberries, pineapple, mango and chocolate syrup, exact text "TOTAL FRUITCAKE $11". NO tacos, NO burgers, no extra lettering, logos or watermarks. This is a flat game texture atlas, not a menu-board scene. Prioritize visible grilled sausages, accurate eight-cell layout, legible exact labels, delicious fruit detail.

## Validation

The accepted atlases were inspected directly and on the rendered in-game hanging and portrait menu boards. Both show eight correctly branded food choices with labels and prices. The Freaky Franks meals visibly contain frankfurters in buns beneath the fruit toppings. Asset-required restaurant tests verify that each variant binds its own menu atlas and replacement sign meshes.

