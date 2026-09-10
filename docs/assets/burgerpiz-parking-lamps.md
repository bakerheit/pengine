# BurgerPiz-derived parking lamps

Added 2026-09-09 for BurgerPiz, TacoMaco and Freaky Franks. Both heads on each
imported parking pole now emit real downward light. All six heads use broad, brighter parking-lot beams: warm color, 22 m range,
power 10 at full night, and outer cone cosine 0.65 (about 99 degrees across).
They retain the street lamps' 260 m camera-distance cutoff, dusk fade and
daylight off. Indoor ceiling lights
stay independent and always on.

`tools/cook_burgerpiz.py` extracts downward lens faces from the existing lamppost
UV island, keeping them in `parking_lens.emesh`. Connected faces yield two light
positions immediately below the glass in `parking_lights.txt`. Each asset gains
one material group (59 total), without changing triangle or collision counts.
No new raster images are required. Existing private assets were recooked for
`--variant=burgerpiz`, `--variant=tacomaco` and `--variant=freakyfranks`.

`gfx/street_lamp_light.h` shares the timing and lens-glow policy between road
fixtures and restaurant parking fixtures. The parking preset doubles power and
widens the beam from the road preset's outer cosine 0.94; road lamps keep their
existing beam settings. World transforms the cooked positions
with each site's yaw/elevation, including the reversed TacoMaco site, tracks the
lens mesh nodes, and clears them on teardown. App updates glow before scene
culling and adds the night-only emitters to the existing tiled light pass.

Validation:

- Rebuilt `apricot`, `burgerpiz_tests` and `traffic_signal_damage_tests`.
- `burgerpiz_tests --require-assets`: all three sites pass, including two lens-
  aligned emitters per site, ground-reaching range, dusk/day power, distance
  cutoff, asset loading and the existing character/collision routes.
- `traffic_signal_damage_tests`: street lamp damage, settling and reset pass.
- Actual 300-frame game captures inspected at night for all three sites:
  `build/burger-parking-lit.png`, `build/tacomaco-parking-lit.png`,
  `build/franks-parking-lit.png`. Lit lenses and pools on pavement/planting are
  visible. GL queues are clean.
- Daylight capture: `build/burger-parking-day.png`.
- The earlier `burger-parking-before.png` and `burger-parking-after.png` use
  `--no-traffic-headlights`. That existing debug switch disables the entire
  shared local-light pass, so those files prove lens glow only, not light pools.

Example night view (use an isolated checkpoint):

```sh
./build/bin/apricot --frames 300 --road-start --night --clear \
  --start-at 455 -260 --start-player-at 382.443 -269.981 \
  --start-heading -66 --save-file /tmp/burger-parking-lit.json \
  --screenshot build/burger-parking-lit.png
```

Wider/brighter follow-up: `build/parking-wide-night.png` shows the new spread
from the same BurgerPiz camera used for `build/burger-parking-lit.png`.
