# City atlas

The map uses the full panel width without stretching world coordinates. Roads,
coastlines, contours, buildings and icons are geometry, rather than enlarged
terrain pixels. The existing game font stays consistent with the rest of the UI;
labels use real font measurements, halos or dark plates, and collision checks.

## Research and design choices

- [Rockstar's GTA V manual](https://play.google.com/store/apps/details?id=com.rockstargames.gtavmanual)
  includes an interactive, zoomable map for neighborhoods and activities.
- [GTA Online's marker options](https://www.rockstargames.com/intl/newswire/article/ak32aka841114k/the-overflod-suzume-supercar-and-new-safeguard-deliveries-now-in-gta-o)
  let players manage map blips. Our Explore, Roads and Places modes reduce clutter.
- [Cyberpunk 2077 patch 1.5](https://www.cyberpunk.net/en/news/41435/patch-1-5-next-generation-update-list-of-changes)
  introduced resized map icons and zoom-based/custom filters. Our labels and
  building/terrain detail change with zoom while icons remain screen-sized.
- [Forza Horizon 5 accessibility](https://support.forzamotorsport.net/hc/en-us/articles/46523995129747-Forza-Horizon-5-Accessibility-Support)
  emphasizes text contrast and legibility. Our slate background, light streets,
  gold highways and protected label backgrounds follow that principle.

These are interaction and readability references, not claims about those games'
rendering internals. No game assets were copied.

## Controls

Open with M/controller Back. Pan with WASD/stick or drag; zoom with wheel,
plus/minus or shoulder buttons. Enter/A recentres. C/Y cycles Explore, Roads
and Places. Right-click places a waypoint; R/controller X places one at the
crosshair. Repeating either action on the same spot clears it. M/Escape/B returns. The top layer indicators show the current mode;
they are not mouse buttons. Wheel zoom anchors to the cursor inside the map
panel, in both directions, until the camera reaches the island bounds.

The full map opens at a 200 m view centered on the player. Pan and zoom keep
their current view until you reopen the map.

During play, a lower-left rotating minimap mirrors the atlas's nearby
coastline, contours, district edges, authored roads, lots, buildings, and POI
markers. It is deliberately not GPS: there is no route, road snapping, or turn
instruction. The yellow diamond is the active mission destination and the pink
ring is the player's waypoint. A destination beyond the radar range clamps to
the circular rim at its true bearing. The radar is tighter while stopped or
moving slowly, then opens back out as speed rises; POIs use the same icons as
the full map. The player arrow stays facing up.
The neighborhood business markers use the shipped Material Symbols subset:
Brassline Arms uses `target`, Second Chance Pawn uses `money_range`, Spin Cycle
Laundromat uses `local_laundry_service`, and Rook's Auto Repair uses `build`.
The nearest authored road name appears directly beneath the minimap. It clears
off-road and uses a small junction hysteresis so crossing a road seam does not
make two street names flicker back and forth.

## Rendering and checks

Terrain heights are sampled once on a 1024-square grid. Interpolated triangle
boundaries generate shoreline polygons and 20 m elevation contours. Dry cells
merge into strips; contour chains simplify to 0.15 m locally and 4 m at overview
zoom. The HUD clips geometry to the map panel and feathers line/circle edges.
World-to-map projection uses one uniform scale, including drag and camera bounds.

`apricot_map_lab` renders the production GameUi and Hud, not a screenshot mockup.
From the repository root:

```sh
cmake --build build --target apricot apricot_map_lab ui_flow_tests -j8
ctest --test-dir build --output-on-failure -R '^(minimap_tests|ui_flow_tests|render_batch_contract_tests|render_geometry_tests|glyph_atlas_tests)$'
./build/bin/apricot_map_lab --minimap --x 0 --z 0 --heading 35 --speed 45 --mission-x -2041 --mission-z -600 --waypoint --screenshot build/minimap.png
./build/bin/apricot_map_lab --zoom 1 --screenshot build/map-overview.png
./build/bin/apricot_map_lab --zoom 5 --screenshot build/map-neighborhood.png
./build/bin/apricot_map_lab --zoom 16 --layer 1 --screenshot build/map-roads.png
./build/bin/apricot_map_lab --zoom 8 --x 115 --z 2290 --layer 2 --width 960 --height 720 --screenshot build/map-airport.png
./build/bin/apricot --frames 300
```

The lab warms up six frames, averages 30 frames including GPU completion, and
reports draw calls and GL errors. September 3, 2026 checks on Apple M5 passed all
five focused tests. Overview, neighborhood, maximum zoom and 4:3 airport views
were inspected. Map-only rendering measured roughly 1.5–4.4 ms with one draw
call and no GL errors; the one-time terrain build was roughly 128–175 ms.
These are local map-only measurements, not whole-game performance promises.
The separate 300-frame game smoke also ended with a clean GL error queue.

The September 5 POI-symbol pass rebuilt the shipped font subset with seven
glyphs and rendered `build/qa/poi-material-symbols/map.png` at maximum zoom.
All four requested business symbols were visible; the lab stayed at one HUD
draw with a clean GL queue, and the five focused suites above passed.
