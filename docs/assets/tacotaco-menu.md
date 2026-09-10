# TacoTaco menu atlas

Created with the built-in `image_gen` tool for the TacoTaco restaurant variation.
The food imagery is new generated artwork. No BurgerPiz texture was repainted.

- Runtime source: `assets/models/buildings/tacotaco/menu_atlas.png`.
- Output: 1415 × 1111 PNG, two columns × four rows, no text or logos.
- Consumer: the `menu_burger` material in the TacoTaco cooker variant. Existing
  source UVs map the eight cells onto the menu boards.
- Generated original: `/Users/andrewbaker/.codex/generated_images/01a07eb6-1568-76f0-ba3b-6d5068f06845/exec-c95027b0-535c-43c1-bb54-86a12bd98cc1.png`.
- The output was copied unchanged into the private model folder. Inspection
  confirmed eight separate meals, the intended grid, and no burger imagery.

## Exact generation prompt

Create a production texture atlas for menu boards inside a low-poly taco restaurant game asset. A rectangular landscape image with exact aspect ratio 512:402 (about 1.274:1). The layout MUST be a precise grid of TWO equal columns and FOUR equal rows, eight equal rectangular cells, edge-to-edge, no gutters or outer frame. Each cell has a pure white background and an appetizing studio food photograph of one Mexican fast-food meal, centered with small clear white margins: row 1 left two crunchy beef tacos with salsa, right three soft chicken tacos with lime; row 2 left a grilled burrito cut diagonally and rice, right loaded nachos with guacamole; row 3 left colorful vegetarian tacos with avocado, right quesadilla wedges with salsa; row 4 left a taco combo with a plain unbranded paper drink cup, right churros with chocolate sauce. Small coral and teal paper plate details. Consistent light, realistic food, clean commercial menu photography. Absolutely NO lettering, NO text, NO logos, NO words, NO prices, NO watermarks, NO burgers. Keep all food strictly within its own grid cell. The atlas will map eight meals onto an existing 3D restaurant's menu boards.
