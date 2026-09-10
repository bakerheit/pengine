# Loom Museum original painting atlas

- Runtime asset: `assets/textures/world/loom_museum/paintings-atlas.png`
- Generated September 8, 2026 with the built-in image-generation tool.
- Actual output: 1254 × 1254 RGB PNG, four equal 627 × 627 square paintings.
- Original retained at
  `/Users/andrewbaker/.codex/generated_images/01a07eb6-1568-76f0-ba3b-6d5068f06845/exec-023c4f53-5a3a-4fc0-b4b9-4e3f95655fd2.png`.

The prompt requested an edge-to-edge 2 × 2 atlas of original oil paintings:
a wooded river valley with a bridge; a fictional woman in a burgundy dress;
pears, grapes and a blue vase; and a harbor at sunset. It specified flat
reproduction scans, no frames, margins, lettering, labels or copied artworks,
with brushwork and silhouettes that remain readable in the game.

The runtime keeps the complete generated image. Four canvas meshes select its
quadrants with inset UVs, accounting for the loader's vertical flip. Geometry
supplies gilt frames and wall labels. The paintings are fictional Loom
collection works, not reproductions of the Cincinnati Art Museum's holdings.
