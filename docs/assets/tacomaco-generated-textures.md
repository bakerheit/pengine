# Tacomaco generated textures

Created 2026-09-05 with the built-in imagegen tool using the Apricot imagegen
skill. These are original fictional raster assets for the copied fast-food
building; no real restaurant brand or reference image was supplied.

| File | Native size | Runtime use |
| --- | ---: | --- |
| `assets/textures/world/tacomaco/brand-sign-albedo.png` | 1881 × 836, RGBA | Tacomaco storefront and roadside pylon faces |
| `assets/textures/world/tacomaco/menu-board-combos-albedo.png` | 1024 × 1536, RGB opaque | Tacomaco drive-through and interior menu faces |

The redesigned logo reads **TACOMACO**. The menu contains exactly 15 combos
with approximate 1991 fast-food pricing, from `$1.49` to `$3.79`, plus the
footer **ALL COMBOS INCLUDE CHIPS + SMALL DRINK**. Both images were visually
inspected and copied intact without resampling or post-processing.

## Imagegen source outputs

- Logo: `/Users/andrewbaker/.codex/generated_images/01a0723f-dc75-7213-9c44-b39ea834aa3d/exec-31710684-5d03-49ac-95fc-87c625898706.png`
- Menu: `/Users/andrewbaker/.codex/generated_images/01a0723f-dc75-7213-9c44-b39ea834aa3d/exec-6e3af283-9263-4325-ba74-43de4ecc967a.png`

The copied restaurant keeps the existing Cloggers cream tile, red vinyl,
linoleum, and stainless material set. Only the brand-specific sign/menu
materials are split by site in the runtime path.

## V2 receiver-specific menus — 2026-09-06

| File | Native size | Runtime face |
| --- | ---: | --- |
| `menu-board-interior-a-v2-albedo.png` | 2048 x 512, RGBA opaque | Interior left, combos 01-08 |
| `menu-board-interior-b-v2-albedo.png` | 2048 x 512, RGBA opaque | Interior right, combos 09-15 |
| `menu-board-drive-through-v2-albedo.png` | 1024 x 1536, RGB opaque | Drive-through, combos 01, 07, and 15 |

These original fictional assets were created with built-in ImageGen. The wide
boards are two matching native-scale 2:1 panels joined and uniformly reduced
to the authored 4:1 canvas by `tools/compose_menu_texture.swift`; no lettering
or food art was stretched. Source outputs are retained under
`/Users/andrewbaker/.codex/generated_images/01a07491-5144-7963-85f9-a2aac292d30e/`.
